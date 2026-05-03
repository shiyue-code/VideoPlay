#include "core/ffmpegplayer.h"
#include "core/audioplayer.h"
#include "utils/logger.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>

namespace {
    using Clock = std::chrono::high_resolution_clock;
    inline double elapsedMs(Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
}

extern "C" {
#include <libavutil/channel_layout.h>
}

namespace VideoPlay {

namespace {
    const double CLOCK_SYNC_THRESHOLD = 0.01;
    const double VIDEO_FRAME_TOLERANCE = 0.005;
    const int AUDIO_QUEUE_SIZE = 100;
}

FFmpegPlayer::FFmpegPlayer() {
    initialize();
}

FFmpegPlayer::~FFmpegPlayer() {
    stop();
    cleanup();
}

void FFmpegPlayer::initialize() {
    avformat_network_init();
    Logger::instance().debug("FFmpegPlayer initialized");
}

void FFmpegPlayer::cleanup() {
    closeFile();
}

bool FFmpegPlayer::loadFile(const std::string& filePath) {
    if (!std::filesystem::exists(filePath)) {
        if (m_errorCallback) {
            m_errorCallback("File not found: " + filePath);
        }
        return false;
    }

    Logger::instance().info("Loading file: " + filePath);
    
    closeFile();
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        if (m_errorCallback) {
            m_errorCallback("Cannot allocate format context");
        }
        return false;
    }
    
    if (avformat_open_input(&m_formatContext, filePath.c_str(), nullptr, nullptr) < 0) {
        if (m_errorCallback) {
            m_errorCallback("Cannot open file: " + filePath);
        }
        avformat_free_context(m_formatContext);
        m_formatContext = nullptr;
        return false;
    }
    
    if (avformat_find_stream_info(m_formatContext, nullptr) < 0) {
        if (m_errorCallback) {
            m_errorCallback("Cannot find stream information");
        }
        avformat_close_input(&m_formatContext);
        return false;
    }
    
    m_duration = m_formatContext->duration / (AV_TIME_BASE / 1000);
    if (m_durationCallback && m_duration > 0) {
        m_durationCallback(m_duration);
    }
    
    m_filePath = filePath;
    
    for (unsigned int i = 0; i < m_formatContext->nb_streams; i++) {
        AVStream* stream = m_formatContext->streams[i];
        AVCodecParameters* codecPar = stream->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
        
        if (!codec) continue;
        
        if (codecPar->codec_type == AVMEDIA_TYPE_VIDEO) {
            Logger::instance().info("Found video stream: " + 
                                   std::to_string(codecPar->width) + "x" + 
                                   std::to_string(codecPar->height));
            
            m_videoCtx.stream = stream;
            m_videoCtx.codecContext = avcodec_alloc_context3(codec);
            if (!m_videoCtx.codecContext) continue;
            
            avcodec_parameters_to_context(m_videoCtx.codecContext, codecPar);
            m_videoCtx.codecContext->thread_count = 4;
            m_videoCtx.codecContext->thread_type = FF_THREAD_FRAME;
            if (avcodec_open2(m_videoCtx.codecContext, codec, nullptr) < 0) {
                avcodec_free_context(&m_videoCtx.codecContext);
                continue;
            }
            
            m_videoCtx.startTime = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
            initializeVideoContext();
            
        } else if (codecPar->codec_type == AVMEDIA_TYPE_AUDIO) {
            Logger::instance().info("Found audio stream: " + 
                                   std::to_string(codecPar->ch_layout.nb_channels) + " channels, " +
                                   std::to_string(codecPar->sample_rate) + " Hz");
            
            m_audioCtx.stream = stream;
            m_audioCtx.codecContext = avcodec_alloc_context3(codec);
            if (!m_audioCtx.codecContext) continue;
            
            avcodec_parameters_to_context(m_audioCtx.codecContext, codecPar);
            if (avcodec_open2(m_audioCtx.codecContext, codec, nullptr) < 0) {
                avcodec_free_context(&m_audioCtx.codecContext);
                continue;
            }
            
            m_audioCtx.startTime = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
            initializeAudioContext();
        }
    }
    
    if (!m_videoCtx.codecContext && !m_audioCtx.codecContext) {
        if (m_errorCallback) {
            m_errorCallback("No supported streams found");
        }
        closeFile();
        return false;
    }
    
    return true;
}

void FFmpegPlayer::closeFile() {
    stop();
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_audioPlayer.reset();
    
    if (m_videoCtx.swsContext) {
        sws_freeContext(m_videoCtx.swsContext);
        m_videoCtx.swsContext = nullptr;
    }
    if (m_videoCtx.codecContext) {
        avcodec_free_context(&m_videoCtx.codecContext);
    }
    
    if (m_audioCtx.swrContext) {
        swr_free(&m_audioCtx.swrContext);
    }
    if (m_audioCtx.codecContext) {
        avcodec_free_context(&m_audioCtx.codecContext);
    }
    
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
        m_formatContext = nullptr;
    }
    
    m_filePath.clear();
    m_duration = 0;
    m_position = 0;
    m_state = PlaybackState::Stopped;
    {
        std::lock_guard<std::mutex> vqLock(m_videoQueueMutex);
        m_videoFrameQueue.clear();
    }
    m_decodeCondition.notify_all();
}

std::string FFmpegPlayer::filePath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_filePath;
}

bool FFmpegPlayer::initializeVideoContext() {
    if (!m_videoCtx.codecContext) return false;
    return true;
}

bool FFmpegPlayer::initializeAudioContext() {
    if (!m_audioCtx.codecContext) return false;
    
    m_audioCtx.format.sampleRate = m_audioCtx.codecContext->sample_rate;
    m_audioCtx.format.channels = m_audioCtx.codecContext->ch_layout.nb_channels;
    m_audioCtx.format.bitsPerSample = 32;
    
    m_audioPlayer = std::make_unique<AudioPlayer>();
    if (!m_audioPlayer->initialize(m_audioCtx.format)) {
        Logger::instance().error("Failed to initialize audio player");
        m_audioPlayer.reset();
        return false;
    }
    
    m_audioPlayer->setVolume(m_volume.load());
    m_audioPlayer->setMuted(m_muted.load());
    
    return true;
}

void FFmpegPlayer::play() {
    bool wasPaused = false;
    bool needNewThread = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state == PlaybackState::Playing) return;
        if (m_filePath.empty() || !m_formatContext) return;
        
        wasPaused = (m_state == PlaybackState::Paused);
        m_state = PlaybackState::Playing;
        m_abortRequest = false;
        
        if (!wasPaused && m_decodeThread.joinable()) {
            // 自然 EOF 后旧线程已结束但尚未 join
            needNewThread = true;
        } else if (!m_decodeThread.joinable()) {
            // 手动 stop 后或首次播放
            needNewThread = true;
        }
    }
    
    if (needNewThread) {
        if (m_decodeThread.joinable()) {
            m_decodeThread.join();
        }
        if (m_formatContext) {
            handleSeek(0);
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state == PlaybackState::Playing) {
            m_decodeThread = std::thread(&FFmpegPlayer::decodeLoop, this);
        }
    }
    
    if (wasPaused) {
        // 暂停恢复：直接启动音频
        if (m_audioPlayer) {
            m_audioPlayer->play();
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_condition.notify_all();
        }
        if (m_stateCallback) {
            m_stateCallback(PlaybackState::Playing);
        }
    } else {
        // 首次播放：进入预缓冲状态，由主线程轮询 checkPreloadComplete()
        m_preloading = true;
        m_preloadStartTime = std::chrono::steady_clock::now();
        Logger::instance().debug("Preload started");
    }
}

void FFmpegPlayer::pause() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_state != PlaybackState::Playing) return;
    
    m_state = PlaybackState::Paused;
    
    if (m_audioPlayer) {
        m_audioPlayer->pause();
    }
    
    m_condition.notify_all();
    
    if (m_stateCallback) {
        m_stateCallback(PlaybackState::Paused);
    }
}

void FFmpegPlayer::stop() {
    bool wasAlreadyStopped = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state == PlaybackState::Stopped) {
            wasAlreadyStopped = true;
        } else {
            m_state = PlaybackState::Stopped;
            m_abortRequest = true;
        }
    }

    m_condition.notify_all();
    m_decodeCondition.notify_all();

    if (m_decodeThread.joinable() &&
        m_decodeThread.get_id() != std::this_thread::get_id()) {
        m_decodeThread.join();
    }

    m_preloading = false;

    if (m_audioPlayer) {
        m_audioPlayer->stop();
        m_audioPlayer->reset();
    }

    // Seek back to beginning so play() restarts from the start.
    if (m_formatContext) {
        handleSeek(0);
    }

    m_position = 0;
    m_audioBaseMs = 0;

    {
        std::lock_guard<std::mutex> vqLock(m_videoQueueMutex);
        m_videoFrameQueue.clear();
    }

    if (!wasAlreadyStopped) {
        if (m_stateCallback) {
            m_stateCallback(PlaybackState::Stopped);
        }
        if (m_positionCallback) {
            m_positionCallback(0);
        }
    }
}

void FFmpegPlayer::seek(int64_t positionMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_seekRequested = true;
    m_seekPosition = positionMs;
    m_condition.notify_all();
}

void FFmpegPlayer::handleSeek(int64_t positionMs) {
    if (!m_formatContext) return;
    
    // 使用 AV_TIME_BASE (微秒) 进行全局 seek
    int64_t seekTarget = av_rescale_q(positionMs, {1, 1000}, {1, AV_TIME_BASE});
    if (m_formatContext->start_time != AV_NOPTS_VALUE) {
        seekTarget += m_formatContext->start_time;
    }
    
    int ret = av_seek_frame(m_formatContext, -1, seekTarget, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        Logger::instance().error("Seek failed: " + std::to_string(positionMs));
        return;
    }
    
    if (m_videoCtx.codecContext) {
        avcodec_flush_buffers(m_videoCtx.codecContext);
    }
    if (m_audioCtx.codecContext) {
        avcodec_flush_buffers(m_audioCtx.codecContext);
    }
    
    if (m_audioPlayer) {
        m_audioPlayer->reset();
    }
    
    {
        std::lock_guard<std::mutex> vqLock(m_videoQueueMutex);
        m_videoFrameQueue.clear();
    }
    
    m_position = positionMs;
    m_audioBaseMs = positionMs;
    m_audioClock = positionMs / 1000.0;
    m_frameTimer = av_gettime() / 1000000.0;
    
    if (m_positionCallback) {
        m_positionCallback(positionMs);
    }
}

bool FFmpegPlayer::isPreloading() const {
    return m_preloading.load();
}

bool FFmpegPlayer::checkPreloadComplete() {
    if (!m_preloading.load()) {
        return false;
    }
    
    bool hasVideo = (m_videoCtx.codecContext != nullptr);
    bool hasAudio = (m_audioPlayer != nullptr);
    
    bool videoReady = !hasVideo;
    bool audioReady = !hasAudio;
    int64_t audioQueuedMs = 0;
    size_t videoQueueSize = 0;
    
    if (hasVideo) {
        std::lock_guard<std::mutex> lock(m_videoQueueMutex);
        videoQueueSize = m_videoFrameQueue.size();
        videoReady = videoQueueSize > 0;
    }
    
    if (hasAudio) {
        audioQueuedMs = m_audioPlayer->queuedMs();
        audioReady = audioQueuedMs >= kPreloadAudioMs;
    }
    
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_preloadStartTime).count();
    bool timeout = elapsedMs >= kPreloadTimeoutMs;
    
    if ((videoReady && audioReady) || timeout) {
        m_preloading = false;
        if (m_audioPlayer) {
            m_audioPlayer->play();
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_condition.notify_all();
        }
        if (m_stateCallback) {
            m_stateCallback(PlaybackState::Playing);
        }
        if (timeout) {
            Logger::instance().warning("Preload timeout (" + std::to_string(elapsedMs) + 
                "ms), forcing play. video=" + std::to_string(videoQueueSize) + 
                " audio=" + std::to_string(audioQueuedMs) + "ms");
        } else {
            Logger::instance().debug("Preload complete in " + std::to_string(elapsedMs) + 
                "ms. video=" + std::to_string(videoQueueSize) + 
                " audio=" + std::to_string(audioQueuedMs) + "ms");
        }
        return true;
    }
    
    // 每 500ms 输出一次调试日志
    static auto s_lastLogTime = std::chrono::steady_clock::now();
    if (std::chrono::steady_clock::now() - s_lastLogTime > std::chrono::milliseconds(500)) {
        s_lastLogTime = std::chrono::steady_clock::now();
        Logger::instance().debug("Preloading... elapsed=" + std::to_string(elapsedMs) +
            "ms video=" + std::to_string(videoQueueSize) +
            " audio=" + std::to_string(audioQueuedMs) + "ms");
    }
    
    return false;
}

void FFmpegPlayer::decodeLoop() {
    Logger::instance().debug("Decode thread started");
    
    m_frameTimer = av_gettime() / 1000000.0;
    
    auto reportStart = Clock::now();
    int pktCount = 0;
    int vframeCount = 0;
    int aframeCount = 0;
    
    while (!m_abortRequest) {
        std::unique_lock<std::mutex> lock(m_mutex);
        
        while (m_state == PlaybackState::Paused && !m_abortRequest) {
            m_condition.wait(lock);
        }
        
        if (m_abortRequest) break;
        
        if (m_seekRequested) {
            lock.unlock();
            handleSeek(m_seekPosition.load());
            lock.lock();
            m_seekRequested = false;
            continue;
        }
        
        if (!m_formatContext) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        lock.unlock();
        
        // Pacing: don't decode too far ahead of playback time
        {
            std::unique_lock<std::mutex> vqLock(m_videoQueueMutex);
            m_decodeCondition.wait(vqLock, [this] {
                return m_abortRequest.load() || m_videoFrameQueue.size() < kMaxVideoQueueSize;
            });
            if (m_abortRequest.load()) break;
        }
        
        pktCount++;
        
        AVPacket* packet = av_packet_alloc();
        auto t0 = Clock::now();
        int ret = av_read_frame(m_formatContext, packet);
        double dtRead = elapsedMs(t0);
        
        if (ret < 0) {
            av_packet_free(&packet);
            if (ret == AVERROR_EOF) {
                std::lock_guard<std::mutex> l(m_mutex);
                m_state = PlaybackState::Stopped;
                if (m_stateCallback) {
                    m_stateCallback(PlaybackState::Stopped);
                }
                if (m_audioPlayer) {
                    m_audioPlayer->stop();
                    m_audioPlayer->reset();
                }
                break;
            }
            continue;
        }
        
        if (m_videoCtx.stream && packet->stream_index == m_videoCtx.stream->index && m_videoCtx.codecContext) {
            AVFrame* frame = av_frame_alloc();
            ret = avcodec_send_packet(m_videoCtx.codecContext, packet);
            if (ret == AVERROR(EAGAIN)) {
                while (avcodec_receive_frame(m_videoCtx.codecContext, frame) >= 0) {
                    auto t1 = Clock::now();
                    VideoFrame vframe = convertVideoFrame(frame);
                    double dtConv = elapsedMs(t1);
                    if (!vframe.data.empty()) {
                        int64_t pts = frame->pts;
                        if (pts == AV_NOPTS_VALUE) {
                            pts = frame->best_effort_timestamp;
                        }
                        if (pts != AV_NOPTS_VALUE) {
                            int64_t startTime = m_videoCtx.stream->start_time != AV_NOPTS_VALUE ? m_videoCtx.stream->start_time : 0;
                                vframe.pts = av_rescale_q(pts - startTime, m_videoCtx.stream->time_base, {1, 1000});
                        }
                        auto t2 = Clock::now();
                        pushVideoFrame(std::move(vframe));
                        vframeCount++;
                        double dtPush = elapsedMs(t2);
                        if (dtConv > 5.0 || dtRead > 5.0 || dtPush > 1.0) {
                            Logger::instance().debug("[PERF] video read=" + std::to_string(dtRead) + 
                                "ms convert=" + std::to_string(dtConv) + 
                                "ms push=" + std::to_string(dtPush) + "ms");
                        }
                    }
                    av_frame_unref(frame);
                }
                ret = avcodec_send_packet(m_videoCtx.codecContext, packet);
            }
            
            if (ret >= 0) {
                while (avcodec_receive_frame(m_videoCtx.codecContext, frame) >= 0) {
                    auto t1 = Clock::now();
                    VideoFrame vframe = convertVideoFrame(frame);
                    double dtConv = elapsedMs(t1);
                    if (!vframe.data.empty()) {
                        int64_t pts = frame->pts;
                        if (pts == AV_NOPTS_VALUE) {
                            pts = frame->best_effort_timestamp;
                        }
                        if (pts != AV_NOPTS_VALUE) {
                            int64_t startTime = m_videoCtx.stream->start_time != AV_NOPTS_VALUE ? m_videoCtx.stream->start_time : 0;
                                vframe.pts = av_rescale_q(pts - startTime, m_videoCtx.stream->time_base, {1, 1000});
                        }
                        auto t2 = Clock::now();
                        pushVideoFrame(std::move(vframe));
                        vframeCount++;
                        double dtPush = elapsedMs(t2);
                        if (dtConv > 5.0 || dtRead > 5.0 || dtPush > 1.0) {
                            Logger::instance().debug("[PERF] video read=" + std::to_string(dtRead) + 
                                "ms convert=" + std::to_string(dtConv) + 
                                "ms push=" + std::to_string(dtPush) + "ms");
                        }
                    }
                    av_frame_unref(frame);
                }
            }
            av_frame_free(&frame);
        }
        else if (m_audioCtx.stream && packet->stream_index == m_audioCtx.stream->index && m_audioCtx.codecContext) {
            AVFrame* frame = av_frame_alloc();
            ret = avcodec_send_packet(m_audioCtx.codecContext, packet);
            if (ret == AVERROR(EAGAIN)) {
                while (avcodec_receive_frame(m_audioCtx.codecContext, frame) >= 0) {
                    auto t3 = Clock::now();
                    auto audioData = resampleAudioFrame(frame);
                    double dtResample = elapsedMs(t3);
                    if (!audioData.empty() && m_audioPlayer) {
                        if (frame->pts != AV_NOPTS_VALUE) {
                            int64_t startTime = m_audioCtx.stream->start_time != AV_NOPTS_VALUE ? m_audioCtx.stream->start_time : 0;
                            m_audioClock = av_q2d(m_audioCtx.stream->time_base) * (frame->pts - startTime);
                        }
                        auto t4 = Clock::now();
                        m_audioPlayer->enqueue(audioData);
                        aframeCount++;
                        double dtEnqueue = elapsedMs(t4);
                        if (dtRead > 5.0 || dtResample > 5.0 || dtEnqueue > 1.0) {
                            Logger::instance().debug("[PERF] audio read=" + std::to_string(dtRead) + 
                                "ms resample=" + std::to_string(dtResample) + 
                                "ms enqueue=" + std::to_string(dtEnqueue) + "ms");
                        }
                    }
                    av_frame_unref(frame);
                }
                ret = avcodec_send_packet(m_audioCtx.codecContext, packet);
            }
            
            if (ret >= 0) {
                while (avcodec_receive_frame(m_audioCtx.codecContext, frame) >= 0) {
                    auto t3 = Clock::now();
                    auto audioData = resampleAudioFrame(frame);
                    double dtResample = elapsedMs(t3);
                    if (!audioData.empty() && m_audioPlayer) {
                        if (frame->pts != AV_NOPTS_VALUE) {
                            int64_t startTime = m_audioCtx.stream->start_time != AV_NOPTS_VALUE ? m_audioCtx.stream->start_time : 0;
                            m_audioClock = av_q2d(m_audioCtx.stream->time_base) * (frame->pts - startTime);
                        }
                        auto t4 = Clock::now();
                        m_audioPlayer->enqueue(audioData);
                        aframeCount++;
                        double dtEnqueue = elapsedMs(t4);
                        if (dtRead > 5.0 || dtResample > 5.0 || dtEnqueue > 1.0) {
                            Logger::instance().debug("[PERF] audio read=" + std::to_string(dtRead) + 
                                "ms resample=" + std::to_string(dtResample) + 
                                "ms enqueue=" + std::to_string(dtEnqueue) + "ms");
                        }
                    }
                    av_frame_unref(frame);
                }
            }
            av_frame_free(&frame);
        }
        
        av_packet_free(&packet);
        
        if (elapsedMs(reportStart) >= 1000.0) {
            Logger::instance().debug("[PERF] decodeLoop pkt/s=" + std::to_string(pktCount) +
                " vframe/s=" + std::to_string(vframeCount) +
                " aframe/s=" + std::to_string(aframeCount));
            pktCount = 0;
            vframeCount = 0;
            aframeCount = 0;
            reportStart = Clock::now();
        }
    }
    
    Logger::instance().debug("Decode thread stopped");
}

VideoFrame FFmpegPlayer::convertVideoFrame(AVFrame* frame) {
    VideoFrame result;
    if (!frame || !frame->data[0]) return result;
    
    int width = frame->width;
    int height = frame->height;
    AVPixelFormat srcFormat = static_cast<AVPixelFormat>(frame->format);
    // 使用 BGRA 以匹配 SDL 纹理格式 (Windows 小端字节序)
    AVPixelFormat dstFormat = AV_PIX_FMT_BGRA;
    
    // Determine color space and range from frame metadata
    int colorSpace = SWS_CS_DEFAULT;
    int srcRange = 0;  // 0 = limited (16-235), 1 = full (0-255)
    
    // Check frame-level color range first
    if (frame->color_range == AVCOL_RANGE_MPEG) {
        srcRange = 0;  // Limited range (TV range)
    } else if (frame->color_range == AVCOL_RANGE_JPEG) {
        srcRange = 1;  // Full range (PC range)
    } else {
        // Default: most video files use limited range
        srcRange = 0;
    }
    
    // Determine color space (BT.601 vs BT.709)
    AVCodecContext* codecCtx = m_videoCtx.codecContext;
    if (codecCtx && codecCtx->colorspace != AVCOL_SPC_UNSPECIFIED) {
        if (codecCtx->colorspace == AVCOL_SPC_BT709) {
            colorSpace = SWS_CS_ITU709;
        } else if (codecCtx->colorspace == AVCOL_SPC_BT470BG || 
                   codecCtx->colorspace == AVCOL_SPC_SMPTE170M) {
            colorSpace = SWS_CS_ITU601;
        }
    } else if (frame->colorspace != AVCOL_SPC_UNSPECIFIED) {
        if (frame->colorspace == AVCOL_SPC_BT709) {
            colorSpace = SWS_CS_ITU709;
        } else if (frame->colorspace == AVCOL_SPC_BT470BG || 
                   frame->colorspace == AVCOL_SPC_SMPTE170M) {
            colorSpace = SWS_CS_ITU601;
        }
    }
    
    // HD videos (>= 1280 width) typically use BT.709
    if (colorSpace == SWS_CS_DEFAULT && width >= 1280) {
        colorSpace = SWS_CS_ITU709;
    }
    
    if (!m_videoCtx.swsContext || 
        m_videoCtx.lastWidth != width || 
        m_videoCtx.lastHeight != height || 
        m_videoCtx.lastFormat != srcFormat ||
        m_videoCtx.lastColorSpace != colorSpace ||
        m_videoCtx.lastSrcRange != srcRange) {
        
        if (m_videoCtx.swsContext) {
            sws_freeContext(m_videoCtx.swsContext);
        }
        
        // Fast bilinear only for real-time performance
        int flags = SWS_FAST_BILINEAR;
        
        m_videoCtx.swsContext = sws_getContext(
            width, height, srcFormat,
            width, height, dstFormat,
            flags, nullptr, nullptr, nullptr);
        
        // Try to set color space details - this may not work on all FFmpeg builds
        // If it fails, the video might appear darker than expected
        if (m_videoCtx.swsContext) {
            const int* coefs = sws_getCoefficients(colorSpace);
            int dstRange = 1;  // Full range output (0-255)
            
            // Note: Some FFmpeg builds ignore these settings
            sws_setColorspaceDetails(m_videoCtx.swsContext, 
                                     coefs, srcRange, 
                                     coefs, dstRange, 
                                     0, 1 << 16, 1 << 16);
            
            Logger::instance().debug("SWS context created: colorspace=" + std::to_string(colorSpace) + 
                                     " srcRange=" + std::to_string(srcRange));
        }
        
        m_videoCtx.lastWidth = width;
        m_videoCtx.lastHeight = height;
        m_videoCtx.lastFormat = srcFormat;
        m_videoCtx.lastColorSpace = colorSpace;
        m_videoCtx.lastSrcRange = srcRange;
    }
    
    if (!m_videoCtx.swsContext) return result;
    
    int dstBytesPerRow = width * 4;
    int dstBufferSize = dstBytesPerRow * height;
    
    // Reuse persistent buffer instead of allocating every frame
    if ((int)m_videoCtx.swsBuffer.size() < dstBufferSize) {
        m_videoCtx.swsBuffer.resize(dstBufferSize);
    }
    m_videoCtx.swsStride = dstBytesPerRow;
    
    uint8_t* dstData[4] = { m_videoCtx.swsBuffer.data(), nullptr, nullptr, nullptr };
    int dstStride[4] = { dstBytesPerRow, 0, 0, 0 };
    
    int swsRet = sws_scale(m_videoCtx.swsContext, frame->data, frame->linesize, 0, height, dstData, dstStride);
    (void)swsRet;
    
    result.width = width;
    result.height = height;
    result.data.assign(m_videoCtx.swsBuffer.data(), m_videoCtx.swsBuffer.data() + dstBufferSize);
    
    return result;
}

std::vector<float> FFmpegPlayer::resampleAudioFrame(AVFrame* frame) {
    std::vector<float> result;
    if (!frame || !frame->data[0]) return result;
    
    int srcRate = frame->sample_rate;
    int srcChannels = frame->ch_layout.nb_channels;
    int dstRate = m_audioCtx.format.sampleRate;
    int dstChannels = m_audioCtx.format.channels;
    
    if (!m_audioCtx.swrContext) {
        m_audioCtx.swrContext = swr_alloc();
        if (!m_audioCtx.swrContext) return result;
        
        AVChannelLayout srcLayout, dstLayout;
        av_channel_layout_default(&srcLayout, srcChannels);
        av_channel_layout_default(&dstLayout, dstChannels);
        
        av_opt_set_chlayout(m_audioCtx.swrContext, "in_chlayout", &srcLayout, 0);
        av_opt_set_int(m_audioCtx.swrContext, "in_sample_rate", srcRate, 0);
        av_opt_set_sample_fmt(m_audioCtx.swrContext, "in_sample_fmt", static_cast<AVSampleFormat>(frame->format), 0);
        
        av_opt_set_chlayout(m_audioCtx.swrContext, "out_chlayout", &dstLayout, 0);
        av_opt_set_int(m_audioCtx.swrContext, "out_sample_rate", dstRate, 0);
        av_opt_set_sample_fmt(m_audioCtx.swrContext, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
        
        if (swr_init(m_audioCtx.swrContext) < 0) {
            swr_free(&m_audioCtx.swrContext);
            return result;
        }
    }
    
    int64_t delay = swr_get_delay(m_audioCtx.swrContext, srcRate);
    int outSamples = av_rescale_rnd(delay + frame->nb_samples, dstRate, srcRate, AV_ROUND_UP);
    
    result.resize(outSamples * dstChannels);
    uint8_t* outData = reinterpret_cast<uint8_t*>(result.data());
    
    int converted = swr_convert(m_audioCtx.swrContext, &outData, outSamples, 
                                const_cast<const uint8_t**>(frame->data), frame->nb_samples);
    
    if (converted < 0) {
        result.clear();
        return result;
    }
    
    result.resize(converted * dstChannels);
    return result;
}

void FFmpegPlayer::synchronizeVideo(double pts) {
    double delay = pts - m_videoClock;
    if (delay <= 0 || delay >= 1.0) {
        delay = m_videoClock > 0 ? 0 : 0.04;
    }
    
    m_videoClock = pts;
    
    double diff = m_videoClock - m_audioClock;
    
    if (std::abs(diff) < 10.0) {
        double speed = m_playbackSpeed.load();
        
        if (diff <= -CLOCK_SYNC_THRESHOLD) {
            delay = 0;
        } else if (diff >= CLOCK_SYNC_THRESHOLD) {
            delay *= 2.0;
        }
        
        delay /= speed;
        
        m_frameTimer += delay;
        
        double currentTime = av_gettime() / 1000000.0;
        double actualDelay = m_frameTimer - currentTime;
        
        if (actualDelay > VIDEO_FRAME_TOLERANCE && actualDelay < 0.1) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(static_cast<int>(actualDelay * 1000000)));
        }
    }
}

PlaybackState FFmpegPlayer::state() const {
    return m_state.load();
}

int64_t FFmpegPlayer::position() const {
    return m_position.load();
}

int64_t FFmpegPlayer::duration() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_duration;
}

int64_t FFmpegPlayer::audioPositionMs() const {
    if (m_state.load() == PlaybackState::Stopped) {
        return 0;
    }
    if (m_audioPlayer) {
        return m_audioBaseMs.load() + m_audioPlayer->playedMs();
    }
    return m_audioBaseMs.load();
}

void FFmpegPlayer::setPlaybackSpeed(double speed) {
    speed = std::clamp(speed, 0.25, 4.0);
    m_playbackSpeed = speed;
    if (m_audioPlayer) {
        m_audioPlayer->setPlaybackSpeed(speed);
    }
    if (m_speedCallback) {
        m_speedCallback(speed);
    }
}

double FFmpegPlayer::playbackSpeed() const {
    return m_playbackSpeed.load();
}

void FFmpegPlayer::setVolume(int volume) {
    volume = std::clamp(volume, 0, 100);
    m_volume = volume;
    if (m_audioPlayer) {
        m_audioPlayer->setVolume(volume);
    }
    if (m_volumeCallback) {
        m_volumeCallback(volume);
    }
}

int FFmpegPlayer::volume() const {
    return m_volume.load();
}

void FFmpegPlayer::setMuted(bool muted) {
    m_muted = muted;
    if (m_audioPlayer) {
        m_audioPlayer->setMuted(muted);
    }
    if (m_muteCallback) {
        m_muteCallback(muted);
    }
}

bool FFmpegPlayer::isMuted() const {
    return m_muted.load();
}

void FFmpegPlayer::setStateCallback(StateCallback callback) {
    m_stateCallback = callback;
}

void FFmpegPlayer::setPositionCallback(PositionCallback callback) {
    m_positionCallback = callback;
}

void FFmpegPlayer::setDurationCallback(DurationCallback callback) {
    m_durationCallback = callback;
}

void FFmpegPlayer::setErrorCallback(ErrorCallback callback) {
    m_errorCallback = callback;
}

void FFmpegPlayer::setSpeedCallback(SpeedCallback callback) {
    m_speedCallback = callback;
}

void FFmpegPlayer::setVolumeCallback(VolumeCallback callback) {
    m_volumeCallback = callback;
}

void FFmpegPlayer::setMuteCallback(MuteCallback callback) {
    m_muteCallback = callback;
}

void FFmpegPlayer::setVideoFrameCallback(VideoFrameCallback callback) {
    m_videoFrameCallback = callback;
}

void FFmpegPlayer::pushVideoFrame(VideoFrame&& frame) {
    std::lock_guard<std::mutex> lock(m_videoQueueMutex);
    if (m_videoFrameQueue.size() >= kMaxVideoQueueSize) {
        m_videoFrameQueue.pop_front();
    }
    m_videoFrameQueue.push_back(std::move(frame));
}

bool FFmpegPlayer::getVideoFrame(int64_t targetPtsMs, VideoFrame& frame) {
    std::lock_guard<std::mutex> lock(m_videoQueueMutex);
    if (m_videoFrameQueue.empty()) {
        return false;
    }
    
    // 特殊值 targetPtsMs < 0：启动 fallback，直接取最早的一帧避免黑屏
    if (targetPtsMs < 0) {
        frame = std::move(m_videoFrameQueue.front());
        m_videoFrameQueue.pop_front();
        m_decodeCondition.notify_one();
        return true;
    }
    
    // 严格同步：所有帧都还没到时间，保持当前显示帧（不提前显示未来帧）
    if (m_videoFrameQueue.front().pts > targetPtsMs) {
        return false;
    }
    
    // 找到 PTS <= targetPtsMs 的最后一帧，并丢弃前面的旧帧
    size_t index = 0;
    for (size_t i = 0; i < m_videoFrameQueue.size(); ++i) {
        if (m_videoFrameQueue[i].pts <= targetPtsMs) {
            index = i;
        } else {
            break;
        }
    }
    
    frame = std::move(m_videoFrameQueue[index]);
    m_videoFrameQueue.erase(m_videoFrameQueue.begin(), m_videoFrameQueue.begin() + index + 1);
    m_decodeCondition.notify_one();
    return true;
}

} // namespace VideoPlay
