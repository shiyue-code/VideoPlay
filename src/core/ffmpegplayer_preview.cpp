#include "core/ffmpegplayer.h"
#include "core/ffmpegplayer_internal.h"

#include <algorithm>
#include <chrono>

namespace VideoPlay {

void FFmpegPlayer::requestPreview(int64_t ptsMs) {
    if (ptsMs < 0 || m_previewPath.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_previewMutex);
    if (m_previewHasRequest && m_previewRequestPts == ptsMs) {
        return;
    }
    m_previewRequestPts = ptsMs;
    m_previewHasRequest = true;
    m_previewCv.notify_one();
}

bool FFmpegPlayer::takePreviewFrame(VideoFrame& frame) {
    std::lock_guard<std::mutex> lock(m_previewMutex);
    if (!m_previewReady || m_previewFrame.data.empty()) {
        return false;
    }
    frame = std::move(m_previewFrame);
    m_previewReady = false;
    return true;
}

void FFmpegPlayer::startPreviewDecoder() {
    stopPreviewDecoder();
    m_previewPath = m_filePath;
    if (m_previewPath.empty()) {
        return;
    }
    m_previewAbort = false;
    m_previewThread = std::thread(&FFmpegPlayer::previewLoop, this);
}

void FFmpegPlayer::stopPreviewDecoder() {
    m_previewAbort = true;
    m_previewCv.notify_all();
    if (m_previewThread.joinable()) {
        m_previewThread.join();
    }
    closePreviewContext();
    std::lock_guard<std::mutex> lock(m_previewMutex);
    m_previewHasRequest = false;
    m_previewReady = false;
    m_previewRequestPts = -1;
    m_previewFrame = VideoFrame();
    m_previewPath.clear();
}

void FFmpegPlayer::closePreviewContext() {
    if (m_previewCodec) {
        avcodec_free_context(&m_previewCodec);
        m_previewCodec = nullptr;
    }
    if (m_previewFmt) {
        avformat_close_input(&m_previewFmt);
        m_previewFmt = nullptr;
    }
    m_previewStreamIndex = -1;
}

bool FFmpegPlayer::openPreviewContext() {
    closePreviewContext();
    if (m_previewPath.empty()) {
        return false;
    }

    if (avformat_open_input(&m_previewFmt, m_previewPath.c_str(), nullptr, nullptr) < 0) {
        logger().warning("Preview: cannot open " + m_previewPath);
        m_previewFmt = nullptr;
        return false;
    }
    if (avformat_find_stream_info(m_previewFmt, nullptr) < 0) {
        logger().warning("Preview: no stream info");
        closePreviewContext();
        return false;
    }

    m_previewStreamIndex = -1;
    for (unsigned int i = 0; i < m_previewFmt->nb_streams; ++i) {
        AVStream* stream = m_previewFmt->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            !(stream->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            m_previewStreamIndex = static_cast<int>(i);
            break;
        }
    }
    if (m_previewStreamIndex < 0) {
        m_previewStreamIndex = av_find_best_stream(
            m_previewFmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    }
    if (m_previewStreamIndex < 0) {
        closePreviewContext();
        return false;
    }

    AVStream* stream = m_previewFmt->streams[m_previewStreamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        closePreviewContext();
        return false;
    }
    m_previewCodec = avcodec_alloc_context3(codec);
    if (!m_previewCodec) {
        closePreviewContext();
        return false;
    }
    if (avcodec_parameters_to_context(m_previewCodec, stream->codecpar) < 0 ||
        avcodec_open2(m_previewCodec, codec, nullptr) < 0) {
        closePreviewContext();
        return false;
    }
    m_previewCodec->pkt_timebase = stream->time_base;
    return true;
}

bool FFmpegPlayer::decodePreviewFrame(int64_t ptsMs, VideoFrame& out) {
    if (!m_previewFmt || !m_previewCodec || m_previewStreamIndex < 0) {
        return false;
    }

    AVStream* stream = m_previewFmt->streams[m_previewStreamIndex];
    int64_t ts = av_rescale_q(ptsMs, AVRational{1, 1000}, stream->time_base);
    if (av_seek_frame(m_previewFmt, m_previewStreamIndex, ts, AVSEEK_FLAG_BACKWARD) < 0) {
        av_seek_frame(m_previewFmt, -1, ptsMs * (AV_TIME_BASE / 1000), AVSEEK_FLAG_BACKWARD);
    }
    avcodec_flush_buffers(m_previewCodec);

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!packet || !frame) {
        av_packet_free(&packet);
        av_frame_free(&frame);
        return false;
    }

    bool gotFrame = false;
    int packets = 0;
    while (!m_previewAbort && packets < 80 && av_read_frame(m_previewFmt, packet) >= 0) {
        ++packets;
        if (packet->stream_index != m_previewStreamIndex) {
            av_packet_unref(packet);
            continue;
        }
        if (avcodec_send_packet(m_previewCodec, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);

        while (avcodec_receive_frame(m_previewCodec, frame) == 0) {
            int64_t framePts = ptsMs;
            if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                framePts = av_rescale_q(frame->best_effort_timestamp, stream->time_base, AVRational{1, 1000});
            } else if (frame->pts != AV_NOPTS_VALUE) {
                framePts = av_rescale_q(frame->pts, stream->time_base, AVRational{1, 1000});
            }

            if (framePts + 120 < ptsMs && packets < 40) {
                av_frame_unref(frame);
                continue;
            }

            AVFrame* src = frame;
            if (!src->data[0] || src->width <= 0 || src->height <= 0) {
                av_frame_unref(frame);
                continue;
            }

            const int dstW = kPreviewWidth;
            const int dstH = kPreviewHeight;
            float aspect = static_cast<float>(src->width) / static_cast<float>(src->height);
            float box = static_cast<float>(dstW) / static_cast<float>(dstH);
            int drawW = dstW;
            int drawH = dstH;
            if (aspect > box) {
                drawH = std::max(1, static_cast<int>(dstW / aspect));
            } else {
                drawW = std::max(1, static_cast<int>(dstH * aspect));
            }
            int ox = (dstW - drawW) / 2;
            int oy = (dstH - drawH) / 2;

            SwsContext* sws = sws_getContext(
                src->width, src->height, static_cast<AVPixelFormat>(src->format),
                drawW, drawH, AV_PIX_FMT_BGRA, SWS_FAST_BILINEAR,
                nullptr, nullptr, nullptr);
            if (!sws) {
                av_frame_unref(frame);
                continue;
            }

            out.width = dstW;
            out.height = dstH;
            out.pts = framePts;
            out.data.assign(static_cast<size_t>(dstW * dstH * 4), 0);
            for (size_t i = 3; i < out.data.size(); i += 4) {
                out.data[i] = 255;
            }

            uint8_t* dstSlice[4] = {
                out.data.data() + (oy * dstW + ox) * 4, nullptr, nullptr, nullptr
            };
            int dstStride[4] = { dstW * 4, 0, 0, 0 };
            sws_scale(sws, src->data, src->linesize, 0, src->height, dstSlice, dstStride);
            sws_freeContext(sws);
            gotFrame = true;
            av_frame_unref(frame);
            break;
        }
        if (gotFrame) {
            break;
        }
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    return gotFrame;
}

void FFmpegPlayer::previewLoop() {
    if (!openPreviewContext()) {
        logger().warning("Preview decoder failed to start");
        return;
    }
    logger().debug("Preview decoder started");

    while (!m_previewAbort) {
        int64_t ptsMs = -1;
        {
            std::unique_lock<std::mutex> lock(m_previewMutex);
            m_previewCv.wait(lock, [this]() {
                return m_previewAbort.load() || m_previewHasRequest;
            });
            if (m_previewAbort) {
                break;
            }
            ptsMs = m_previewRequestPts;
            m_previewHasRequest = false;
        }

        VideoFrame frame;
        if (decodePreviewFrame(ptsMs, frame) && !m_previewAbort) {
            std::lock_guard<std::mutex> lock(m_previewMutex);
            m_previewFrame = std::move(frame);
            m_previewReady = true;
        }
    }

    closePreviewContext();
    logger().debug("Preview decoder stopped");
}


} // namespace VideoPlay
