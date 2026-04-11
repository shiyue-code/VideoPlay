#include "core/ffmpegplayer.h"
#include "utils/logger.h"

#include <QDebug>
#include <QAudioFormat>
#include <QTimer>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/pixdesc.h>
}

namespace VideoPlay {

namespace {
    const int AUDIO_BUFFER_SIZE = 4096;
    const int VIDEO_BUFFER_SIZE = 1024;
    const double CLOCK_SYNC_THRESHOLD = 0.01; // 10ms
    const double VIDEO_FRAME_TOLERANCE = 0.005; // 5ms tolerance for frame timing

    static inline uint64_t getDefaultChannelLayout(int channels) {
        // Simple channel mask: bit 0 = front left, bit 1 = front right, etc.
        // Works for common layouts: 1 (mono), 2 (stereo), 4 (quad), 6 (5.1), 8 (7.1)
        return (1ULL << channels) - 1;
    }
}

FFmpegPlayer::FFmpegPlayer(QObject* parent)
    : QObject(parent)
    , m_formatContext(nullptr)
    , m_decodeThread(nullptr)
    , m_duration(0)
    , m_position(0)
    , m_playbackSpeed(1.0)
    , m_volume(100)
    , m_muted(false)
    , m_state(PlaybackState::Stopped)
    , m_autoPlayAfterLoad(false)
    , m_seekRequested(false)
    , m_seekPosition(0)
    , m_abortRequest(false)
    , m_audioThread(nullptr)
    , m_baseTime(0.0)
    , m_clock(0.0)
    , m_audioClock(0.0)
    , m_lastVideoPts(0)
    , m_startTime(0)
    , m_videoStartTime(0.0)
    , m_frameTimer(0.0)
{
    initialize();
}

FFmpegPlayer::~FFmpegPlayer()
{
    stop();
    cleanup();
}

void FFmpegPlayer::cleanup()
{
    {
        QMutexLocker locker(&m_mutex);
        m_abortRequest = true;
    }
    m_condition.wakeAll();

    if (m_decodeThread && m_decodeThread->isRunning()) {
        m_decodeThread->wait(1000);
        delete m_decodeThread;
        m_decodeThread = nullptr;
    }

    if (m_audioThread) {
        m_audioThread->stop();
        m_audioThread->wait(2000);
        delete m_audioThread;
        m_audioThread = nullptr;
    }

    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
        m_formatContext = nullptr;
    }

    m_filePath.clear();
    m_duration = 0;
    m_position = 0;
    m_playbackSpeed = 1.0;
    m_volume = 100;
    m_muted = false;
    {
        QMutexLocker locker(&m_mutex);
        m_state = PlaybackState::Stopped;
        m_clock = 0.0;
        m_baseTime = 0.0;
    }
}

void FFmpegPlayer::initialize()
{
    // av_register_all() is deprecated in newer FFmpeg versions
    // av_register_all(); // Removed for FFmpeg 4.0+
    avformat_network_init();

    Logger::instance().debug("FFmpegPlayer initialized");
}

bool FFmpegPlayer::loadFile(const QString& filePath)
{
    if (!QFile::exists(filePath)) {
        emit errorOccurred(QString("File not found: %1").arg(filePath));
        return false;
    }

    Logger::instance().info(QString("Loading file: %1").arg(filePath));
    qDebug() << "FFmpegPlayer::loadFile:" << filePath;

    closeFile();
    m_filePath = filePath;
    m_autoPlayAfterLoad = true;

    try {
        openFile(filePath);
        qDebug() << "File opened, audioStream:" << (m_audioStream.stream ? "yes" : "no");
        emit fileLoaded(filePath);
        return true;
    } catch (const QString& error) {
        emit errorOccurred(error);
        return false;
    }
}

void FFmpegPlayer::openFile(const QString& filePath)
{
    QByteArray filePathUtf8 = filePath.toUtf8();
    const char* filename = filePathUtf8.constData();

    // Open format context
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        emit errorOccurred("Cannot allocate format context");
        return;
    }

    // Open input
    if (avformat_open_input(&m_formatContext, filename, nullptr, nullptr) < 0) {
        emit errorOccurred(QString("Cannot open file: %1").arg(filePath));
        avformat_free_context(m_formatContext);
        m_formatContext = nullptr;
        return;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(m_formatContext, nullptr) < 0) {
        emit errorOccurred("Cannot find stream information");
        avformat_close_input(&m_formatContext);
        m_formatContext = nullptr;
        return;
    }

    m_duration = m_formatContext->duration / (AV_TIME_BASE / 1000); // Convert to ms
    if (m_duration > 0) {
        emit durationChanged(m_duration);
    }

    Logger::instance().info(QString("Duration: %1 ms, %2 streams")
                                .arg(m_duration)
                                .arg(m_formatContext->nb_streams));

    // Find and initialize video stream
    for (unsigned int i = 0; i < m_formatContext->nb_streams; i++) {
        AVStream* stream = m_formatContext->streams[i];
        AVCodecParameters* codecPar = stream->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);

        if (!codec) {
            Logger::instance().warning(QString("Stream %1: Unsupported codec %2")
                                       .arg(i)
                                       .arg(avcodec_get_name(codecPar->codec_id)));
            continue;
        }

        if (codecPar->codec_type == AVMEDIA_TYPE_VIDEO) {
            Logger::instance().info(QString("Found video stream %1: %2x%3")
                                        .arg(i)
                                        .arg(codecPar->width)
                                        .arg(codecPar->height));

            m_videoStream.stream = stream;
            m_videoStream.codecContext = avcodec_alloc_context3(codec);
            if (!m_videoStream.codecContext) {
                Logger::instance().error("Cannot allocate video codec context");
                continue;
            }
            avcodec_parameters_to_context(m_videoStream.codecContext, codecPar);

            if (avcodec_open2(m_videoStream.codecContext, codec, nullptr) < 0) {
                Logger::instance().error("Cannot open video codec");
                m_videoStream.codecContext = nullptr;
                continue;
            }

            m_videoStream.startTime = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
            m_videoStream.swsContext = nullptr;

        } else if (codecPar->codec_type == AVMEDIA_TYPE_AUDIO) {
            Logger::instance().info(QString("Found audio stream %1: %2 channels, %3 Hz")
                                        .arg(i)
                                        .arg(codecPar->ch_layout.nb_channels)
                                        .arg(codecPar->sample_rate));

            m_audioStream.stream = stream;
            m_audioStream.codecContext = avcodec_alloc_context3(codec);
            if (!m_audioStream.codecContext) {
                Logger::instance().error("Cannot allocate audio codec context");
                continue;
            }
            avcodec_parameters_to_context(m_audioStream.codecContext, codecPar);

            // Prepare audio format for QAudioSink
            m_audioStream.audioFormat = getAudioFormatFromCodec(m_audioStream.codecContext);
            m_audioStream.resampleContext = nullptr;

            if (avcodec_open2(m_audioStream.codecContext, codec, nullptr) < 0) {
                Logger::instance().error("Cannot open audio codec");
                m_audioStream.codecContext = nullptr;
                continue;
            }

            // Prepare audio format - use actual stream parameters for better quality
            QAudioFormat audioFormat = m_audioStream.audioFormat;
            // Ensure we have valid format settings
            if (audioFormat.sampleRate() <= 0) {
                audioFormat.setSampleRate(48000);
            }
            if (audioFormat.channelCount() <= 0) {
                audioFormat.setChannelCount(2);
            }
            // Use Float format for best compatibility
            audioFormat.setSampleFormat(QAudioFormat::Float);
            m_audioStream.audioFormat = audioFormat;

            // Create audio playback thread (will be started when playback begins)
            if (m_audioThread) {
                delete m_audioThread;
            }
            m_audioThread = new AudioPlaybackThread(audioFormat, this);

            m_audioStream.startTime = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
        }
    }

    if (!m_videoStream.codecContext && !m_audioStream.codecContext) {
        emit errorOccurred("No supported streams found");
        avformat_close_input(&m_formatContext);
        m_formatContext = nullptr;
        return;
    }
}

void FFmpegPlayer::closeFile()
{
    {
        QMutexLocker locker(&m_mutex);
        m_abortRequest = true;
    }
    m_condition.wakeAll();

    if (m_decodeThread && m_decodeThread->isRunning()) {
        m_decodeThread->wait(1000);
        delete m_decodeThread;
        m_decodeThread = nullptr;
    }

    if (m_videoStream.codecContext) {
        avcodec_free_context(&m_videoStream.codecContext);
        if (m_videoStream.swsContext) {
            sws_freeContext(m_videoStream.swsContext);
            m_videoStream.swsContext = nullptr;
        }
    }

    if (m_audioStream.codecContext) {
        avcodec_free_context(&m_audioStream.codecContext);
        if (m_audioStream.resampleContext) {
            swr_free(&m_audioStream.resampleContext);
            m_audioStream.resampleContext = nullptr;
        }
    }

    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
        m_formatContext = nullptr;
    }

    // Clean up audio thread
    if (m_audioThread) {
        m_audioThread->stop();
        m_audioThread->wait(2000);
        delete m_audioThread;
        m_audioThread = nullptr;
    }

    m_filePath.clear();
    m_duration = 0;
    m_position = 0;
    m_playbackSpeed = 1.0;
    m_volume = 100;
    m_muted = false;
    {
        QMutexLocker locker(&m_mutex);
        m_state = PlaybackState::Stopped;
        m_clock = 0.0;
        m_baseTime = 0.0;
    }
}

void FFmpegPlayer::play()
{
    QMutexLocker locker(&m_mutex);
    if (m_state == PlaybackState::Playing) return;

    if (!m_filePath.isEmpty()) {
        if (m_state == PlaybackState::Stopped) {
            if (!m_decodeThread) {
                m_decodeThread = new DecodeThread(this);
                m_abortRequest = false;
                locker.unlock();
                m_decodeThread->start();
                locker.relock();
            }
        }

        // Start audio playback thread
        if (m_audioThread && !m_audioThread->isRunning()) {
            m_audioThread->start();
        }
        
        m_state = PlaybackState::Playing;
        m_condition.wakeAll();
        locker.unlock();
        emit stateChanged(m_state);
    }
}

void FFmpegPlayer::pause()
{
    QMutexLocker locker(&m_mutex);
    if (m_state != PlaybackState::Playing) return;

    m_state = PlaybackState::Paused;
    locker.unlock();
    emit stateChanged(m_state);

    m_condition.wakeAll(); // Wake decode thread to check state
}

void FFmpegPlayer::stop()
{
    QMutexLocker locker(&m_mutex);
    if (m_state == PlaybackState::Stopped) return;

    m_state = PlaybackState::Stopped;
    locker.unlock();
    emit stateChanged(m_state);

    handleSeek(0); // Seek to beginning

    // Stop audio playback thread
    if (m_audioThread) {
        m_audioThread->stop();
    }
}

void FFmpegPlayer::seek(qint64 position)
{
    QMutexLocker locker(&m_mutex);
    m_seekRequested = true;
    m_seekPosition = position;
    m_condition.wakeAll();
}

void FFmpegPlayer::handleSeek(qint64 position)
{
    if (!m_formatContext) return;

    QMutexLocker locker(&m_mutex);

    // Convert position to AV_TIME_BASE units - use video stream's time_base if available
    AVStream* seekStream = m_videoStream.stream ? m_videoStream.stream : m_formatContext->streams[0];
    int64_t seekTarget = av_rescale_q(position, {1, 1000}, seekStream->time_base);

    // Seek in format context
    int ret = av_seek_frame(m_formatContext, -1, seekTarget, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        emit errorOccurred(QString("Seek failed: %1 ms").arg(position));
        return;
    }

    // Flush buffers
    if (m_videoStream.codecContext) {
        avcodec_flush_buffers(m_videoStream.codecContext);
    }
    if (m_audioStream.codecContext) {
        avcodec_flush_buffers(m_audioStream.codecContext);
    }

    m_position = position;
    m_clock = position / 1000.0;
    m_baseTime = av_gettime() / 1000000.0 - m_clock;

    // Don't stop audio sink; just clear any pending audio we have (we don't buffer)
    // if (m_audioSink) {
    //     m_audioSink->stop();
    //     m_audioBuffer.clear();
    // }

    m_seekRequested = false;
    emit positionChanged(m_position);
}

void FFmpegPlayer::setPlaybackSpeed(double speed)
{
    if (speed < 0.25) speed = 0.25;
    if (speed > 4.0) speed = 4.0;

    QMutexLocker locker(&m_mutex);
    if (qFuzzyCompare(m_playbackSpeed, speed)) return;

    m_playbackSpeed = speed;
    locker.unlock();
    emit playbackSpeedChanged(speed);
}

void FFmpegPlayer::managePlaybackSpeed(double speed)
{
    Q_UNUSED(speed);
    // Playback speed is controlled in decode loop by adjusting the delay
    // For now, speed affects the QAudioSink indirectly through timestamp synchronization
    // More advanced: we could filter frames based on PTS
}

void FFmpegPlayer::setVolume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;

    QMutexLocker locker(&m_mutex);
    if (m_volume == volume) return;

    m_volume = volume;
    locker.unlock();
    // Volume is now handled by audio thread
    emit volumeChanged(m_volume);
}

void FFmpegPlayer::setMuted(bool muted)
{
    QMutexLocker locker(&m_mutex);
    if (m_muted == muted) return;

    m_muted = muted;
    locker.unlock();
    // Volume is now handled by audio thread
    emit muteChanged(m_muted);
}

void FFmpegPlayer::onAudioSinkStateChanged(QAudio::State state)
{
    qDebug() << "AudioSink state (main thread):" << state;
    // Audio playback is now handled in the separate thread
}

void FFmpegPlayer::decodeLoop()
{
    Logger::instance().debug("Decode thread started");
    
    // Initialize frame timer for video sync
    m_frameTimer = av_gettime() / 1000000.0;
    m_videoStartTime = m_frameTimer;

    try {
        while (true) {
            try {
                QMutexLocker locker(&m_mutex);
                while (!m_abortRequest && m_state != PlaybackState::Playing) {
                    m_condition.wait(&m_mutex);
                }
                if (m_abortRequest) break;
            } catch (...) {
                Logger::instance().error("Exception in decode loop wait");
                break;
            }

            if (m_state != PlaybackState::Playing) continue;

            // Handle seek request
            {
                QMutexLocker locker(&m_mutex);
                if (m_seekRequested) {
                    m_mutex.unlock();
                    try {
                        handleSeek(m_seekPosition);
                        // Reset frame timer after seek
                        m_frameTimer = av_gettime() / 1000000.0;
                        m_videoStartTime = m_frameTimer;
                    } catch (...) {
                        Logger::instance().error("Exception during seek");
                    }
                    continue;
                }
            }

            // Decode packets
            AVPacket* pkt = av_packet_alloc();
            if (!pkt) {
                Logger::instance().error("Failed to allocate packet");
                break;
            }

            int ret = av_read_frame(m_formatContext, pkt);
            if (ret < 0) {
                av_packet_free(&pkt);
                if (ret == AVERROR_EOF) {
                    {
                        QMutexLocker locker(&m_mutex);
                        m_state = PlaybackState::Stopped;
                    }
                    emit stateChanged(m_state);
                    break;
                }
                continue; // Try next packet
            }

            // Process based on stream type
            if (m_videoStream.stream && pkt->stream_index == m_videoStream.stream->index && m_videoStream.codecContext) {
                // Video packet
                AVFrame* frame = av_frame_alloc();
                if (!frame) {
                    av_packet_free(&pkt);
                    continue;
                }

                ret = avcodec_send_packet(m_videoStream.codecContext, pkt);
                if (ret < 0) {
                    av_frame_free(&frame);
                    av_packet_free(&pkt);
                    continue;
                }

                while (ret >= 0) {
                    ret = avcodec_receive_frame(m_videoStream.codecContext, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        break;
                    }

                    // Convert to QImage with frame timing control
                    try {
                        QImage image = convertVideoFrame(frame, m_videoStream);
                        if (!image.isNull()) {
                            // Calculate PTS
                            int64_t pts = frame->pts;
                            if (pts == AV_NOPTS_VALUE && frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                                pts = frame->best_effort_timestamp;
                            }
                            
                            if (pts != AV_NOPTS_VALUE) {
                                qint64 pts_ms = av_rescale_q(pts, m_videoStream.stream->time_base, {1, 1000});
                                {
                                    QMutexLocker locker(&m_mutex);
                                    m_position = pts_ms;
                                    m_lastVideoPts = pts_ms;
                                }
                                emit positionChanged(pts_ms);
                                
                                // Calculate when this frame should be displayed
                                double pts_seconds = pts_ms / 1000.0;
                                double frameDelay = pts_seconds - (m_videoStartTime > 0 ? 0 : 0);
                                
                                // Get current time
                                double currentTime = av_gettime() / 1000000.0;
                                
                                // Calculate time to wait before displaying this frame
                                // If using audio clock for sync, compare with audio clock
                                double targetTime = m_frameTimer;
                                double actualDelay = targetTime - currentTime;
                                
                                // Only sleep if we need to wait (avoid negative delays)
                                if (actualDelay > VIDEO_FRAME_TOLERANCE) {
                                    // Cap maximum delay to avoid long freezes
                                    if (actualDelay > 0.1) {
                                        actualDelay = 0.1; // Max 100ms wait
                                    }
                                    QThread::usleep(static_cast<unsigned long>(actualDelay * 1000000));
                                }
                                
                                // Update frame timer for next frame
                                // Estimate next frame delay based on frame rate or previous interval
                                double nextFrameDuration = 0.04; // Default 25fps
                                if (m_videoStream.stream->avg_frame_rate.den > 0 && 
                                    m_videoStream.stream->avg_frame_rate.num > 0) {
                                    nextFrameDuration = av_q2d(m_videoStream.stream->avg_frame_rate);
                                    if (nextFrameDuration > 0) {
                                        nextFrameDuration = 1.0 / nextFrameDuration;
                                    }
                                }
                                m_frameTimer = av_gettime() / 1000000.0 + nextFrameDuration / m_playbackSpeed;
                            }
                            
                            emit videoFrameReady(image, m_position);
                        }
                    } catch (...) {
                        Logger::instance().error("Exception processing video frame");
                    }

                    av_frame_unref(frame);
                }

                av_frame_free(&frame);

            } else if (m_audioStream.stream && pkt->stream_index == m_audioStream.stream->index && m_audioStream.codecContext) {
                // Audio packet
                AVFrame* frame = av_frame_alloc();
                if (!frame) {
                    av_packet_free(&pkt);
                    continue;
                }

                ret = avcodec_send_packet(m_audioStream.codecContext, pkt);
                if (ret < 0) {
                    av_frame_free(&frame);
                    av_packet_free(&pkt);
                    continue;
                }

                while (ret >= 0) {
                    ret = avcodec_receive_frame(m_audioStream.codecContext, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        break;
                    }

                    // Resample audio and send to audio thread
                    try {
                        int numSamples = 0;
                        QVector<char> audioData = resampleAudio(frame, m_audioStream, numSamples);
                        if (!audioData.isEmpty()) {
                            // Update audio clock
                            double frame_pts = 0;
                            if (frame->pts != AV_NOPTS_VALUE) {
                                frame_pts = av_q2d(m_audioStream.stream->time_base) * frame->pts;
                            }
                            double frame_duration = static_cast<double>(numSamples) / m_audioStream.codecContext->sample_rate;
                            m_audioClock = frame_pts + frame_duration;
                            m_clock = m_audioClock;

                            // Send to audio playback thread
                            QByteArray data(audioData.constData(), audioData.size());
                            if (m_audioThread) {
                                m_audioThread->enqueue(data);
                            }
                        }
                    } catch (...) {
                        Logger::instance().error("Exception processing audio frame");
                    }

                    av_frame_unref(frame);
                }

                av_frame_free(&frame);
            }

            av_packet_free(&pkt);
        }
    } catch (const std::exception& e) {
        Logger::instance().error(QString("Decode thread exception: %1").arg(e.what()));
    } catch (...) {
        Logger::instance().error("Unknown exception in decode thread");
    }

    Logger::instance().debug("Decode thread stopped");
}

QImage FFmpegPlayer::convertVideoFrame(AVFrame* frame, VideoStreamData& vsd)
{
    if (!frame || !frame->data[0]) return QImage();

    // Get frame dimensions
    int width = frame->width;
    int height = frame->height;
    AVPixelFormat srcFormat = (AVPixelFormat)frame->format;

    // If the frame is already in RGB format, we can use it directly
    const AVPixelFormat targetFormat = AV_PIX_FMT_RGB32;

    // Create or update conversion context only if format or dimensions changed
    if (!vsd.swsContext || vsd.lastWidth != width || vsd.lastHeight != height || vsd.lastFormat != srcFormat) {
        if (vsd.swsContext) {
            sws_freeContext(vsd.swsContext);
            vsd.swsContext = nullptr;
        }
        vsd.swsContext = sws_getContext(width, height, srcFormat,
                                         width, height, targetFormat,
                                         SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        if (!vsd.swsContext) return QImage();
        
        // Cache the format parameters
        vsd.lastWidth = width;
        vsd.lastHeight = height;
        vsd.lastFormat = srcFormat;
    }

    // Allocate buffer for RGB32 image
    QImage img(width, height, QImage::Format_ARGB32);
    if (img.isNull()) return QImage();

    uint8_t* dstData[4] = { reinterpret_cast<uint8_t*>(img.bits()), nullptr, nullptr, nullptr };
    int dstLinesize[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };

    // Convert
    int h = sws_scale(vsd.swsContext,
                      frame->data, frame->linesize,
                      0, height,
                      dstData, dstLinesize);

    if (h != height) {
        return QImage();
    }

    return img;
}

QAudioFormat FFmpegPlayer::getAudioFormatFromCodec(AVCodecContext* codecCtx)
{
    QAudioFormat format;

    // Determine sample format
    AVSampleFormat sampleFmt = codecCtx->sample_fmt;
    if (sampleFmt == AV_SAMPLE_FMT_NONE) {
        sampleFmt = AV_SAMPLE_FMT_FLTP; // Default for AAC
    }

    // Map channel layout
    uint64_t channelLayout = codecCtx->ch_layout.order == AV_CHANNEL_ORDER_NATIVE
                             ? codecCtx->ch_layout.u.mask
                             : 0;

    // Set Qt audio format
    format.setSampleRate(codecCtx->sample_rate);
    format.setChannelCount(codecCtx->ch_layout.nb_channels);

    // Map sample format
    switch (sampleFmt) {
    case AV_SAMPLE_FMT_U8:
        format.setSampleFormat(QAudioFormat::UInt8);
        break;
    case AV_SAMPLE_FMT_S16:
        format.setSampleFormat(QAudioFormat::Int16);
        break;
    case AV_SAMPLE_FMT_S32:
        format.setSampleFormat(QAudioFormat::Int32);
        break;
    case AV_SAMPLE_FMT_FLT:
        format.setSampleFormat(QAudioFormat::Float);
        break;
    case AV_SAMPLE_FMT_DBL:
        format.setSampleFormat(QAudioFormat::Float); // Map double to float
        break;
    case AV_SAMPLE_FMT_FLTP:
        format.setSampleFormat(QAudioFormat::Float);
        break;
    default:
        format.setSampleFormat(QAudioFormat::Int16);
        break;
    }

    return format;
}

QVector<char> FFmpegPlayer::resampleAudio(AVFrame* frame, AudioStreamData& asd, int& numSamples)
{
    QVector<char> result;
    if (!frame || !frame->data[0]) return result;

    // Map Qt SampleFormat to AVSampleFormat
    AVSampleFormat outFormat = AV_SAMPLE_FMT_NONE;
    switch (asd.audioFormat.sampleFormat()) {
        case QAudioFormat::UInt8: outFormat = AV_SAMPLE_FMT_U8; break;
        case QAudioFormat::Int16: outFormat = AV_SAMPLE_FMT_S16; break;
        case QAudioFormat::Int32: outFormat = AV_SAMPLE_FMT_S32; break;
        case QAudioFormat::Float: outFormat = AV_SAMPLE_FMT_FLT; break; // Interleaved Float
        default: outFormat = AV_SAMPLE_FMT_S16; break;
    }

    // Check if we need conversion
    // QAudioSink expects Interleaved samples.
    // If input is Planar (e.g. FLTP), we MUST convert to Interleaved.
    bool needsConversion = (frame->format != outFormat || 
                            frame->ch_layout.nb_channels != asd.audioFormat.channelCount() ||
                            frame->sample_rate != asd.audioFormat.sampleRate());
    
    if (av_sample_fmt_is_planar((AVSampleFormat)frame->format)) {
        needsConversion = true;
    }

    if (!needsConversion) {
        // Direct copy (Interleaved input matching output)
        int dataSize = av_samples_get_buffer_size(nullptr, frame->ch_layout.nb_channels, 
                                                  frame->nb_samples, (AVSampleFormat)frame->format, 1);
        if (dataSize > 0) {
            result.resize(dataSize);
            memcpy(result.data(), frame->data[0], dataSize);
            numSamples = frame->nb_samples;
        }
        return result;
    }

    // Resampling/Conversion needed
    if (!asd.resampleContext) {
        asd.resampleContext = swr_alloc();
        if (!asd.resampleContext) {
            Logger::instance().error("Failed to allocate resample context");
            return result;
        }

        // Input settings
        av_opt_set_chlayout(asd.resampleContext, "in_chlayout", &frame->ch_layout, 0);
        av_opt_set_int(asd.resampleContext, "in_sample_rate", frame->sample_rate, 0);
        av_opt_set_sample_fmt(asd.resampleContext, "in_sample_fmt", (AVSampleFormat)frame->format, 0);

        // Output settings
        AVChannelLayout outLayout;
        av_channel_layout_default(&outLayout, asd.audioFormat.channelCount());
        av_opt_set_chlayout(asd.resampleContext, "out_chlayout", &outLayout, 0);
        av_opt_set_int(asd.resampleContext, "out_sample_rate", asd.audioFormat.sampleRate(), 0);
        av_opt_set_sample_fmt(asd.resampleContext, "out_sample_fmt", outFormat, 0);

        if (swr_init(asd.resampleContext) < 0) {
            Logger::instance().error("Failed to initialize resample context");
            swr_free(&asd.resampleContext);
            return result;
        }
        Logger::instance().debug("Resample context initialized");
    }

    // Allocate output buffer
    int64_t delay = swr_get_delay(asd.resampleContext, frame->sample_rate);
    int outSamples = av_rescale_rnd(delay + frame->nb_samples, asd.audioFormat.sampleRate(), frame->sample_rate, AV_ROUND_UP);
    outSamples = qMax(outSamples, frame->nb_samples); // Safety margin

    int outBytes = av_samples_get_buffer_size(nullptr, asd.audioFormat.channelCount(), outSamples, outFormat, 0);
    if (outBytes <= 0) return result;

    result.resize(outBytes);
    uint8_t* outData[1];
    outData[0] = reinterpret_cast<uint8_t*>(result.data());

    // Convert
    int converted = swr_convert(asd.resampleContext, outData, outSamples, 
                                (const uint8_t**)frame->data, frame->nb_samples);
    if (converted < 0) {
        Logger::instance().error(QString("swr_convert failed"));
        result.clear();
        return result;
    }

    // Resize to actual size
    int actualBytes = av_samples_get_buffer_size(nullptr, asd.audioFormat.channelCount(), converted, outFormat, 0);
    if (actualBytes > 0 && actualBytes < result.size()) {
        result.resize(actualBytes);
    }
    numSamples = converted;

    return result;
}

void FFmpegPlayer::DecodeThread::run()
{
    player->decodeLoop();
}

qint64 FFmpegPlayer::position() const
{
    return m_position;
}

qint64 FFmpegPlayer::duration() const
{
    return m_duration;
}

QString FFmpegPlayer::filePath() const
{
    return m_filePath;
}

double FFmpegPlayer::playbackSpeed() const
{
    return m_playbackSpeed;
}

int FFmpegPlayer::volume() const
{
    return m_volume;
}

bool FFmpegPlayer::isMuted() const
{
    return m_muted;
}

PlaybackState FFmpegPlayer::state() const
{
    return m_state;
}

void FFmpegPlayer::synchronizeClocks()
{
    // Placeholder for future clock synchronization implementation
    // Currently not used
}

} // namespace VideoPlay
