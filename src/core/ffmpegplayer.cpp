#include "core/ffmpegplayer.h"
#include "core/ffmpegplayer_internal.h"
#include "core/audioplayer.h"
#include "subtitles/subtitleparser.h"
#include "utils/logger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <sstream>

extern "C" {
#include <libavutil/channel_layout.h>
}

namespace VideoPlay {

// 共享助手（logger/rationalToDouble/codecDisplayName/hardwareDeviceName/
// AUDIO_QUEUE_SIZE）已挪到 ffmpegplayer_internal.h

FFmpegPlayer::FFmpegPlayer() {
    initialize();
}

FFmpegPlayer::~FFmpegPlayer() {
    stop();
    cleanup();
}

void FFmpegPlayer::initialize() {
    avformat_network_init();
    logger().debug("FFmpegPlayer initialized");
}

void FFmpegPlayer::cleanup() {
    closeFile();
}

bool FFmpegPlayer::loadFile(const std::string& filePath) {
    const bool networkSource = isNetworkUrl(filePath);
    m_sourceType = networkSource ? SourceType::NetworkStream : SourceType::LocalFile;
    if (networkSource) {
        setNetworkState(NetworkState::Connecting);
    }
    if (!networkSource && !std::filesystem::exists(std::filesystem::u8path(filePath))) {
        if (m_errorCallback) {
            m_errorCallback("File not found: " + filePath);
        }
        return false;
    }

    logger().info("Loading file: " + filePath);
    
    closeFile();
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        if (m_errorCallback) {
            m_errorCallback("Cannot allocate format context");
        }
        return false;
    }
    
    AVDictionary* inputOptions = nullptr;
    if (networkSource) {
        av_dict_set(&inputOptions, "rw_timeout", "10000000", 0);
        av_dict_set(&inputOptions, "reconnect", "1", 0);
        av_dict_set(&inputOptions, "reconnect_streamed", "1", 0);
        av_dict_set(&inputOptions, "reconnect_delay_max", "5", 0);
    }

    int openRet = avformat_open_input(&m_formatContext, filePath.c_str(), nullptr, &inputOptions);
    av_dict_free(&inputOptions);
    if (openRet < 0) {
        if (networkSource) {
            setNetworkState(NetworkState::Failed);
        }
        if (m_errorCallback) {
            m_errorCallback("Cannot open file: " + filePath);
        }
        avformat_free_context(m_formatContext);
        m_formatContext = nullptr;
        return false;
    }
    
    if (avformat_find_stream_info(m_formatContext, nullptr) < 0) {
        if (networkSource) {
            setNetworkState(NetworkState::Failed);
        }
        if (m_errorCallback) {
            m_errorCallback("Cannot find stream information");
        }
        avformat_close_input(&m_formatContext);
        return false;
    }
    
    m_duration = m_formatContext->duration > 0 ?
        m_formatContext->duration / (AV_TIME_BASE / 1000) : 0;
    m_mediaInfo = MediaInfo();
    m_mediaInfo.source = filePath;
    m_mediaInfo.sourceType = m_sourceType;
    m_mediaInfo.durationMs = m_duration;
    m_mediaInfo.bitrate = m_formatContext->bit_rate;
    m_mediaInfo.hardwareDecoderEnabled = m_hardwareDecodingEnabled.load();
    if (m_formatContext->iformat) {
        if (m_formatContext->iformat->long_name) {
            m_mediaInfo.container = m_formatContext->iformat->long_name;
        } else if (m_formatContext->iformat->name) {
            m_mediaInfo.container = m_formatContext->iformat->name;
        }
    }
    // 先记录 filePath 再触发回调：上层按回调定位文件时用 filePath() 作键，
    // 时序颠倒会把新文件的时长写到旧文件名下（污染 resume 判断）
    m_filePath = filePath;
    if (m_durationCallback && m_duration > 0) {
        m_durationCallback(m_duration);
    }
    
    // 读取章节信息
    m_chapters.clear();
    if (m_formatContext->nb_chapters > 0) {
        for (unsigned int i = 0; i < m_formatContext->nb_chapters; i++) {
            AVChapter* ch = m_formatContext->chapters[i];
            VideoPlay::ChapterInfo info;
            info.startTime = av_rescale_q(ch->start, ch->time_base, {1, 1000});
            info.endTime = av_rescale_q(ch->end, ch->time_base, {1, 1000});
            AVDictionaryEntry* entry = av_dict_get(ch->metadata, "title", nullptr, 0);
            if (entry) {
                info.title = entry->value;
            } else {
                info.title = "Chapter " + std::to_string(i + 1);
            }
            m_chapters.push_back(info);
        }
        logger().info("Found " + std::to_string(m_chapters.size()) + " chapters");
    }
    
    // 使用 av_find_best_stream 选择最佳视频/音频流，避免选到封面图等附加流
    int bestVideoIdx = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    int bestAudioIdx = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_AUDIO, -1, bestVideoIdx, nullptr, 0);

    // 如果 av_find_best_stream 仍选到了 attached_pic，手动找第一个非 attached_pic 的视频流
    if (bestVideoIdx >= 0) {
        AVStream* vs = m_formatContext->streams[bestVideoIdx];
        if (vs->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            (vs->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            logger().warning("Best video stream is attached_pic, searching for real video stream");
            bestVideoIdx = -1;
            for (unsigned int i = 0; i < m_formatContext->nb_streams; i++) {
                AVStream* s = m_formatContext->streams[i];
                if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                    !(s->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
                    bestVideoIdx = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    for (unsigned int i = 0; i < m_formatContext->nb_streams; i++) {
        AVStream* stream = m_formatContext->streams[i];
        AVCodecParameters* codecPar = stream->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);

        if (!codec) continue;

        bool isBestVideo = (static_cast<int>(i) == bestVideoIdx);
        bool isBestAudio = (static_cast<int>(i) == bestAudioIdx);

        if (codecPar->codec_type == AVMEDIA_TYPE_VIDEO && isBestVideo) {
            logger().info("Selected video stream " + std::to_string(i) + ": " +
                                   std::to_string(codecPar->width) + "x" +
                                   std::to_string(codecPar->height) +
                                   " (disposition=0x" + std::to_string(stream->disposition) + ")");

            m_mediaInfo.hasVideo = true;
            m_mediaInfo.videoCodec = codecDisplayName(codec, codecPar->codec_id);
            m_mediaInfo.width = codecPar->width;
            m_mediaInfo.height = codecPar->height;
            m_mediaInfo.fps = rationalToDouble(stream->avg_frame_rate);
            if (m_mediaInfo.fps <= 0.0) {
                m_mediaInfo.fps = rationalToDouble(stream->r_frame_rate);
            }
            m_mediaInfo.videoBitrate = codecPar->bit_rate;

            AVColorTransferCharacteristic trc = codecPar->color_trc;
            AVColorPrimaries pri = codecPar->color_primaries;
            AVColorSpace spc = codecPar->color_space;
            const char* trcName = av_color_transfer_name(trc);
            const char* priName = av_color_primaries_name(pri);
            const char* spcName = av_color_space_name(spc);

            m_mediaInfo.colorTransfer = (trcName && trc != AVCOL_TRC_UNSPECIFIED) ? trcName : "BT.709";
            m_mediaInfo.colorPrimaries = (priName && pri != AVCOL_PRI_UNSPECIFIED) ? priName : "BT.709";
            m_mediaInfo.colorSpace = (spcName && spc != AVCOL_SPC_UNSPECIFIED) ? spcName : "BT.709";

            bool isHdr = (trc == AVCOL_TRC_SMPTE2084 || trc == AVCOL_TRC_ARIB_STD_B67);
            if (!isHdr && pri == AVCOL_PRI_BT2020 && (spc == AVCOL_SPC_BT2020_NCL || spc == AVCOL_SPC_BT2020_CL)) {
                isHdr = true;
            }
            m_mediaInfo.isHdr = isHdr;

            m_videoCtx.stream = stream;
            m_videoCtx.codecContext = avcodec_alloc_context3(codec);
            if (!m_videoCtx.codecContext) continue;

            avcodec_parameters_to_context(m_videoCtx.codecContext, codecPar);
            m_videoCtx.codecContext->thread_count = 4;
            m_videoCtx.codecContext->thread_type = FF_THREAD_FRAME;

            bool hardwareConfigured = setupHardwareDecoder(codec);
            int openVideoRet = avcodec_open2(m_videoCtx.codecContext, codec, nullptr);
            if (openVideoRet < 0 && hardwareConfigured) {
                logger().warning("Hardware decoder failed to open, falling back to software decoder");
                releaseHardwareDecoder();
                m_videoCtx.codecContext->get_format = nullptr;
                m_videoCtx.codecContext->opaque = nullptr;
                openVideoRet = avcodec_open2(m_videoCtx.codecContext, codec, nullptr);
            }

            if (openVideoRet < 0) {
                releaseHardwareDecoder();
                avcodec_free_context(&m_videoCtx.codecContext);
                continue;
            }

            if (m_hwDeviceCtx) {
                m_mediaInfo.hardwareDecoder = true;
                m_mediaInfo.hardwareDevice = hardwareDeviceName(m_hwDeviceType);
            }

            m_videoCtx.startTime = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
            initializeVideoContext();

        } else if (codecPar->codec_type == AVMEDIA_TYPE_AUDIO && isBestAudio) {
            logger().info("Selected audio stream " + std::to_string(i) + ": " +
                                   std::to_string(codecPar->ch_layout.nb_channels) + " channels, " +
                                   std::to_string(codecPar->sample_rate) + " Hz");

            m_mediaInfo.hasAudio = true;
            m_mediaInfo.audioCodec = codecDisplayName(codec, codecPar->codec_id);
            m_mediaInfo.sampleRate = codecPar->sample_rate;
            m_mediaInfo.channels = codecPar->ch_layout.nb_channels;
            m_mediaInfo.audioBitrate = codecPar->bit_rate;

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

    // 扫描所有音轨和字幕轨
    scanTracks();

    if (m_sourceType == SourceType::LocalFile && m_videoCtx.codecContext) {
        startPreviewDecoder();
    }

    return true;
}

void FFmpegPlayer::closeFile() {
    stop();
    stopPreviewDecoder();

    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_audioPlayer.reset();

    // stop() 已 join 解码线程；closeFile 只在主线程调用，两个调节器都可安全重置
    m_decodeAdjuster.reset();
    m_refreshAdjuster.reset();
    releaseHardwareDecoder();
    cleanupVideoFilterGraph();
    if (m_videoCtx.codecContext) {
        avcodec_free_context(&m_videoCtx.codecContext);
    }
    
    if (m_audioCtx.swrContext) {
        swr_free(&m_audioCtx.swrContext);
    }
    {
        std::lock_guard<std::mutex> filterLock(m_audioFilterMutex);
        cleanupAudioFilterGraph();
    }
    if (m_audioCtx.codecContext) {
        avcodec_free_context(&m_audioCtx.codecContext);
    }

    // 清理内封字幕流
    closeSubtitleStream();
    
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
        m_formatContext = nullptr;
    }
    
    m_filePath.clear();
    m_duration = 0;
    m_mediaInfo = MediaInfo();
    m_sourceType = SourceType::LocalFile;
    setNetworkState(NetworkState::Idle);
    m_position = 0;
    m_audioSyncOffsetMs = 0;
    // 裁剪与补解码状态随文件复位（视频裁剪现已持续生效，不会自动解除）
    m_videoTrimBeforeMs = -1;
    m_audioTrimBeforeMs = -1;
    m_seekBurstActive = false;
    m_seekLandingReady = -1;
    m_landingIsStepBack = false;
    m_stepBackAnchorMs = -1;
    m_state = PlaybackState::Stopped;
    m_chapters.clear();
    m_audioTracks.clear();
    m_subtitleTracks.clear();
    m_currentAudioTrack = 0;
    m_currentSubtitleTrack = -1;
    {
        std::lock_guard<std::mutex> vqLock(m_videoQueueMutex);
        m_videoFrameQueue.clear();
        m_stepHistory.clear();
        m_stepHistBytes = 0;
        m_stepDropWatermark = -1;
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

bool FFmpegPlayer::setupHardwareDecoder(const AVCodec* codec) {
    if (!codec || !m_videoCtx.codecContext) {
        return false;
    }
    if (!m_hardwareDecodingEnabled.load()) {
        logger().debug("Hardware decoder disabled by settings");
        return false;
    }

    releaseHardwareDecoder();

    std::vector<AVHWDeviceType> preferredTypes;
#if defined(_WIN32)
    preferredTypes = {AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2};
#elif defined(__linux__)
    preferredTypes = {AV_HWDEVICE_TYPE_VAAPI, AV_HWDEVICE_TYPE_VDPAU, AV_HWDEVICE_TYPE_CUDA};
#elif defined(__APPLE__)
    preferredTypes = {AV_HWDEVICE_TYPE_VIDEOTOOLBOX};
#else
    preferredTypes = {AV_HWDEVICE_TYPE_CUDA};
#endif

    for (AVHWDeviceType deviceType : preferredTypes) {
        for (int i = 0;; ++i) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
            if (!config) {
                break;
            }
            if (config->device_type != deviceType ||
                !(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
                continue;
            }

            AVBufferRef* deviceCtx = nullptr;
            int ret = av_hwdevice_ctx_create(&deviceCtx, deviceType, nullptr, nullptr, 0);
            if (ret < 0) {
                logger().debug("Hardware device unavailable: " + hardwareDeviceName(deviceType));
                continue;
            }

            m_hwDeviceCtx = deviceCtx;
            m_hwPixelFormat = config->pix_fmt;
            m_hwDeviceType = deviceType;
            m_videoCtx.codecContext->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
            if (!m_videoCtx.codecContext->hw_device_ctx) {
                releaseHardwareDecoder();
                continue;
            }
            m_videoCtx.codecContext->opaque = this;
            m_videoCtx.codecContext->get_format = FFmpegPlayer::selectHardwareFormat;

            logger().info("Hardware decoder configured: " + hardwareDeviceName(deviceType));
            return true;
        }
    }

    logger().debug("No compatible hardware decoder found for codec: " +
                   std::string(codec->name ? codec->name : "unknown"));
    return false;
}

void FFmpegPlayer::releaseHardwareDecoder() {
    if (m_videoCtx.codecContext && m_videoCtx.codecContext->hw_device_ctx) {
        av_buffer_unref(&m_videoCtx.codecContext->hw_device_ctx);
    }
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
    }
    m_hwPixelFormat = AV_PIX_FMT_NONE;
    m_hwDeviceType = AV_HWDEVICE_TYPE_NONE;
}

AVPixelFormat FFmpegPlayer::selectHardwareFormat(AVCodecContext* ctx, const AVPixelFormat* pixFmts) {
    if (!pixFmts) {
        return AV_PIX_FMT_NONE;
    }
    auto* player = static_cast<FFmpegPlayer*>(ctx ? ctx->opaque : nullptr);
    if (player && player->m_hwPixelFormat != AV_PIX_FMT_NONE) {
        for (const AVPixelFormat* fmt = pixFmts; *fmt != AV_PIX_FMT_NONE; ++fmt) {
            if (*fmt == player->m_hwPixelFormat) {
                return *fmt;
            }
        }
        logger().warning("Decoder did not offer requested hardware pixel format");
    }
    return pixFmts[0];
}

bool FFmpegPlayer::initializeAudioContext() {
    if (!m_audioCtx.codecContext) return false;
    
    m_audioCtx.format.sampleRate = m_audioCtx.codecContext->sample_rate;
    m_audioCtx.format.channels = m_audioCtx.codecContext->ch_layout.nb_channels;
    m_audioCtx.format.bitsPerSample = 32;
    
    m_audioPlayer = std::make_unique<AudioPlayer>();
    if (!m_audioPlayer->initialize(m_audioCtx.format, m_audioOutputDeviceId)) {
        logger().error("Failed to initialize audio player");
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
        // 丢弃 Stopped 期间堆积的 seek 请求（进度条点击/步进），
        // 否则新解码线程启动后先消费旧 seek 跳到原位置，EOF 场景会立刻再次播完
        m_seekRequested = false;
        if (m_formatContext) {
            handleSeek(0);
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state == PlaybackState::Playing) {
            m_decodeThread = std::thread(&FFmpegPlayer::decodeLoop, this);
        }
    }
    
    if (wasPaused) {
        // 暂停恢复：直接启动音频；无音轨时启动回退时钟
        if (m_audioPlayer) {
            m_audioPlayer->play();
        } else {
            startFallbackClock();
        }
        if (m_sourceType == SourceType::NetworkStream) {
            setNetworkState(NetworkState::Playing);
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
        if (m_sourceType == SourceType::NetworkStream) {
            setNetworkState(NetworkState::Buffering);
        }
        logger().debug("Preload started");
    }
}

void FFmpegPlayer::pause() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_state != PlaybackState::Playing) return;

    // 预缓冲期间用户主动暂停 = 放弃自动开播：否则 checkPreloadComplete
    // 会在缓冲满/超时后强制起音频并回调 Playing，app 的 m_isPlaying
    // 被置回 true，画面却冻着——暂停后逐帧步进全部失效
    m_preloading = false;
    m_state = PlaybackState::Paused;
    
    if (m_audioPlayer) {
        m_audioPlayer->pause();
    } else {
        stopFallbackClock();
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
    m_fallbackClockRunning = false;
    m_seekBurstActive = false;
    m_seekLandingReady = -1;
    m_landingIsStepBack = false;
    m_stepBackAnchorMs = -1;
    m_videoTrimBeforeMs = -1;

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
        m_stepHistory.clear();
        m_stepHistBytes = 0;
        m_stepDropWatermark = -1;
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
    m_stepBackAnchorMs = -1;  // 普通 seek 取消挂起的逐帧回退模式
    m_seekRequested = true;
    m_seekPosition = positionMs;
    m_condition.notify_all();
}

void FFmpegPlayer::seekStepBack(int64_t anchorPtsMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 窗口命中：队列/历史里已有可信的 pts < anchor 前帧时免 seek、
    // 免解码，直接标就绪——连续 -1（含暂停后首击）零解码开销
    if (!m_seekRequested.load() && !m_seekBurstActive.load()) {
        std::lock_guard<std::mutex> vqLock(m_videoQueueMutex);
        int64_t predPts = -1;
        for (auto it = m_videoFrameQueue.rbegin();
             it != m_videoFrameQueue.rend(); ++it) {
            if (it->pts < anchorPtsMs) {
                predPts = it->pts;
                break;
            }
        }
        auto hit = m_stepHistory.lower_bound(anchorPtsMs);
        if (hit != m_stepHistory.begin()) {
            const int64_t histPrev = std::prev(hit)->first;
            predPts = std::max(predPts, histPrev);
        }
        if (predPts > m_stepDropWatermark) {
            // 落点标志最后置位，作为对消费端的"发布"
            m_landingIsStepBack = true;
            m_seekLandingReady = anchorPtsMs;
            return;
        }
    }
    // 锚点先置位再发请求：解码线程处理 handleSeek 时按回退模式裁剪，
    // 队列只留 pts < anchor 的帧，落点取队尾（真正的上一帧）
    m_stepBackAnchorMs = anchorPtsMs;
    m_seekRequested = true;
    m_seekPosition = anchorPtsMs;
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
        logger().error("Seek failed: " + std::to_string(positionMs));
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
        m_audioPlayer->play(); // 确保音频设备在清空后恢复播放
    }

    {
        std::lock_guard<std::mutex> filterLock(m_audioFilterMutex);
        cleanupAudioFilterGraph();
    }
    
    {
        std::lock_guard<std::mutex> vqLock(m_videoQueueMutex);
        m_videoFrameQueue.clear();
        // seek 后旧窗口不再连续：历史清空、水位归零，新的解码窗口重建
        m_stepHistory.clear();
        m_stepHistBytes = 0;
        m_stepDropWatermark = -1;
    }
    // 新 seek 使未消费的落点就绪标志失效
    m_seekLandingReady = -1;
    m_landingIsStepBack = false;

    // 音频媒体时间跟踪复位：目标之前的整帧音频被丢弃
    m_audioNextPtsMs = -1.0;
    m_audioInputExpectedMs = -1.0;
    m_audioTrimBeforeMs = positionMs;
    if (m_stepBackAnchorMs.load() >= 0) {
        // 逐帧回退：正向裁剪关闭，handleDecodedVideoFrame 改按锚点统计进度
        m_videoTrimBeforeMs = -1;
        m_stepBackDecodedMaxMs = -1;
        m_stepBackFwdCount = 0;
    } else {
        // 视频同样裁剪掉 seek 目标之前的帧（持续生效，见 handleDecodedVideoFrame）
        m_videoTrimBeforeMs = positionMs;
    }

    m_position = positionMs;
    m_audioBaseMs = positionMs;
    // 回退时钟重新从 seek 点起算（无音轨文件）
    if (m_fallbackClockRunning.load()) {
        m_fallbackClockStartNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
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
        // 已不在 Playing（暂停时已清掉预缓冲标志，这里兜底）：
        // 只收尾不起播，也不要回报完成——true 会让 app 误置 m_isPlaying
        if (m_state != PlaybackState::Playing) {
            return false;
        }
        if (m_audioPlayer) {
            m_audioPlayer->play();
        } else {
            startFallbackClock();
        }
        if (m_sourceType == SourceType::NetworkStream) {
            setNetworkState(NetworkState::Playing);
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_condition.notify_all();
        }
        if (m_stateCallback) {
            m_stateCallback(PlaybackState::Playing);
        }
        if (timeout) {
            logger().warning("Preload timeout (" + std::to_string(elapsedMs) + 
                "ms), forcing play. video=" + std::to_string(videoQueueSize) + 
                " audio=" + std::to_string(audioQueuedMs) + "ms");
        } else {
            logger().debug("Preload complete in " + std::to_string(elapsedMs) + 
                "ms. video=" + std::to_string(videoQueueSize) + 
                " audio=" + std::to_string(audioQueuedMs) + "ms");
        }
        return true;
    }
    
    // 每 500ms 输出一次调试日志
    static auto s_lastLogTime = std::chrono::steady_clock::now();
    if (std::chrono::steady_clock::now() - s_lastLogTime > std::chrono::milliseconds(500)) {
        s_lastLogTime = std::chrono::steady_clock::now();
        logger().debug("Preloading... elapsed=" + std::to_string(elapsedMs) +
            "ms video=" + std::to_string(videoQueueSize) +
            " audio=" + std::to_string(audioQueuedMs) + "ms");
    }
    
    return false;
}

void FFmpegPlayer::decodeLoop() {
    logger().debug("Decode thread started");

    // Allocate packet and frames once, reuse throughout the loop
    AVPacket* packet = av_packet_alloc();
    AVFrame* vframe = av_frame_alloc();
    AVFrame* aframe = av_frame_alloc();

    if (!packet || !vframe || !aframe) {
        logger().error("Failed to allocate FFmpeg packet/frame");
        av_packet_free(&packet);
        av_frame_free(&vframe);
        av_frame_free(&aframe);
        return;
    }

    auto reportStart = HRClock::now();
    int pktCount = 0;
    m_vframeCount = 0;
    m_aframeCount = 0;
    int consecutiveReadErrors = 0;
    // 暂停态下允许补解码的包数：seek 后让新位置的帧入队，随后回到暂停等待
    int pausedBurstPackets = 0;
    int64_t pausedSeekTargetMs = 0;

    while (!m_abortRequest) {
        std::unique_lock<std::mutex> lock(m_mutex);

        while (m_state == PlaybackState::Paused && !m_abortRequest &&
               !m_seekRequested && pausedBurstPackets <= 0) {
            m_condition.wait(lock);
        }

        if (m_abortRequest) break;

        if (m_seekRequested) {
            lock.unlock();
            handleSeek(m_seekPosition.load());
            lock.lock();
            m_seekRequested = false;
            if (m_state == PlaybackState::Paused) {
                // handleSeek 里会重启音频流，暂停态需要压回去；
                // 并允许补解码若干包，让 seek 目标位置的帧进入显示队列
                if (m_audioPlayer) {
                    m_audioPlayer->pause();
                }
                // 上限要覆盖长 GOP：关键帧间隔长的文件可能要解几百个包才到目标帧
                pausedBurstPackets = 4096;
                pausedSeekTargetMs = m_seekPosition.load();
                m_seekBurstActive = true;
            }
            continue;
        }

        // 补解码到目标后还要再补约 800ms：B 帧重排时目标附近的 B 帧在
        // 下一个 P/I 帧之后才解出，过早停止会让落点帧根本不在队列里
        if (pausedBurstPackets > 0) {
            if (m_stepBackAnchorMs.load() >= 0) {
                // 回退落点收齐：锚点及之后已入队 32 帧（远超重排深度，
                // 且为 +1 步进留缓冲）；或解码输出越过锚点+保护窗兜底（EOF 等）
                if (m_stepBackFwdCount >= kStepBackFwdKeep ||
                    m_stepBackDecodedMaxMs >=
                        m_stepBackAnchorMs.load() + kSeekGuardMs) {
                    pausedBurstPackets = 0;
                }
            } else {
                std::lock_guard<std::mutex> vqLock(m_videoQueueMutex);
                if (!m_videoFrameQueue.empty() &&
                    m_videoFrameQueue.back().pts >=
                        pausedSeekTargetMs + kSeekGuardMs) {
                    pausedBurstPackets = 0;
                }
            }
        }
        if (pausedBurstPackets == 0 && m_seekBurstActive.exchange(false)) {
            // 重排窗口收齐：普通 seek 取队首（最小 pts）；
            // 回退落点取队尾（pts < anchor 的最大值）
            m_landingIsStepBack = m_stepBackAnchorMs.load() >= 0;
            m_seekLandingReady = pausedSeekTargetMs;
            // 锚点使命完成：恢复解码后锚点及之后的帧正常入队（+1 需要）
            m_stepBackAnchorMs = -1;
        }

        if (!m_formatContext) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        lock.unlock();

        // Pacing: don't decode too far ahead of playback time
        if (!waitForDecodeSlot()) break;
        if (m_seekRequested.load()) continue;   // seek 优先，交给循环顶部处理

        pktCount++;

        auto t0 = HRClock::now();
        int ret = av_read_frame(m_formatContext, packet);
        double dtRead = elapsedMs(t0);

        if (ret < 0) {
            av_packet_unref(packet);
            if (ret == AVERROR_EOF) {
                // 排空解码器与播放队列，再进入 Stopped；
                // 排空期间有 seek 请求则交回主循环正常处理 seek 并继续读包
                drainEndOfStream(vframe, aframe);
                if (m_abortRequest.load()) {
                    break;
                }
                if (m_seekRequested.load()) {
                    continue;
                }
            }
            if (handleReadFailure(ret, consecutiveReadErrors)) break;
            continue;
        }
        if (m_sourceType == SourceType::NetworkStream) {
            if (consecutiveReadErrors > 0) {
                setNetworkState(NetworkState::Playing);
            }
            consecutiveReadErrors = 0;
        }

        if (m_videoCtx.stream && packet->stream_index == m_videoCtx.stream->index && m_videoCtx.codecContext) {
            decodeVideoPacket(packet, vframe, dtRead);
        }
        else if (m_audioCtx.stream && packet->stream_index == m_audioCtx.stream->index && m_audioCtx.codecContext) {
            // 暂停态补解码期间音频不会消耗，满了只能丢包，否则补解码卡死；
            // 正常播放时由 waitForDecodeSlot 阻塞式背压，不丢包
            if (pausedBurstPackets > 0 && m_audioPlayer &&
                m_audioPlayer->queuedMs() > kMaxAudioQueueMs) {
                av_packet_unref(packet);
                continue;
            }
            decodeAudioPacket(packet, aframe, dtRead);
        }
        else if (m_subtitleCtx.stream && packet->stream_index == m_subtitleCtx.stream->index && m_subtitleCtx.codecContext) {
            decodeSubtitlePacket(packet);
        }

        av_packet_unref(packet);

        if (pausedBurstPackets > 0) {
            --pausedBurstPackets;
        }

        if (elapsedMs(reportStart) >= 1000.0) {
            logger().debug("[PERF] decodeLoop pkt/s=" + std::to_string(pktCount) +
                " vframe/s=" + std::to_string(m_vframeCount) +
                " aframe/s=" + std::to_string(m_aframeCount));
            pktCount = 0;
            m_vframeCount = 0;
            m_aframeCount = 0;
            reportStart = HRClock::now();
        }
    }

    // Free reusable allocations
    av_packet_free(&packet);
    av_frame_free(&vframe);
    av_frame_free(&aframe);

    logger().debug("Decode thread stopped");
}

bool FFmpegPlayer::waitForDecodeSlot() {
    // 视频队列有空位且音频队列不超上限才可继续解码；
    // 音频没有通知机制，5ms 轮询等待并响应 abort/seek。
    // 音频设备暂停时不等音频（暂停态补解码由丢包防溢出）。
    for (;;) {
        if (m_abortRequest.load()) return false;
        if (m_seekRequested.load()) return true;
        // 补解码窗口内用更大的上限，否则暂停态无人消费、队列到 10
        // 就背压死锁，落点帧永远收不齐
        const size_t videoLimit = m_seekBurstActive.load()
                                      ? kSeekBurstQueueSize : kMaxVideoQueueSize;
        bool videoHasSpace;
        if (m_stepBackAnchorMs.load() >= 0) {
            // 逐帧回退补解码：队列只起到上限内的环形缓冲作用，
            // 满时 pushVideoFrame 丢队首保尾部落点，不能背压停解码
            videoHasSpace = true;
        } else {
            std::lock_guard<std::mutex> vqLock(m_videoQueueMutex);
            videoHasSpace = m_videoFrameQueue.size() < videoLimit;
        }
        bool audioOk = !m_audioPlayer || m_audioPlayer->isPaused() ||
                       m_audioPlayer->queuedMs() <= kMaxAudioQueueMs;
        if (videoHasSpace && audioOk) return true;
        std::unique_lock<std::mutex> vqLock(m_videoQueueMutex);
        m_decodeCondition.wait_for(vqLock, std::chrono::milliseconds(5),
                                   [this, videoLimit] {
            return m_abortRequest.load() || m_seekRequested.load() ||
                   m_videoFrameQueue.size() < videoLimit;
        });
    }
}

// EOF 时排空解码器与播放队列：先送 NULL 收完剩余帧（B 帧尾帧），
// 再等视频队列与音频缓冲播完，最后才允许进入 Stopped
void FFmpegPlayer::drainEndOfStream(AVFrame* vframe, AVFrame* aframe) {
    if (m_videoCtx.codecContext) {
        avcodec_send_packet(m_videoCtx.codecContext, nullptr);
        while (avcodec_receive_frame(m_videoCtx.codecContext, vframe) >= 0) {
            // pushVideoFrame 满了会丢队首，推入前先等空位
            {
                std::unique_lock<std::mutex> vqLock(m_videoQueueMutex);
                m_decodeCondition.wait_for(vqLock, std::chrono::milliseconds(5), [this] {
                    return m_abortRequest.load() || m_seekRequested.load() ||
                           m_videoFrameQueue.size() < kMaxVideoQueueSize;
                });
            }
            if (m_abortRequest.load() || m_seekRequested.load()) {
                av_frame_unref(vframe);
                return;
            }
            handleDecodedVideoFrame(vframe, 0.0);
            av_frame_unref(vframe);
        }
    }
    if (m_audioCtx.codecContext) {
        avcodec_send_packet(m_audioCtx.codecContext, nullptr);
        while (avcodec_receive_frame(m_audioCtx.codecContext, aframe) >= 0) {
            handleDecodedAudioFrame(aframe, 0.0);
            av_frame_unref(aframe);
        }
    }

    // 等队列播完：有 seek 请求则交回主循环处理；暂停态下队列永远不会
    // 排空（消费停止、音频安全阀也不生效），直接放弃排空进入 Stopped
    int64_t lastAudioQueuedMs = -1;
    int audioStallMs = 0;
    while (!m_abortRequest && !m_seekRequested) {
        if (m_state == PlaybackState::Paused) break;
        size_t videoQueued;
        {
            std::lock_guard<std::mutex> vqLock(m_videoQueueMutex);
            videoQueued = m_videoFrameQueue.size();
        }
        int64_t audioQueuedMs = m_audioPlayer ? m_audioPlayer->queuedMs() : 0;
        bool audioDone = !m_audioPlayer || !m_audioPlayer->isPlaying() || audioQueuedMs <= 0;
        if (videoQueued == 0 && audioDone) break;
        // 安全阀：播放态音频队列连续 2 秒不下降就放弃等待
        if (m_state == PlaybackState::Playing && m_audioPlayer &&
            m_audioPlayer->isPlaying()) {
            if (lastAudioQueuedMs >= 0 && audioQueuedMs < lastAudioQueuedMs) {
                audioStallMs = 0;
            } else {
                audioStallMs += 10;
                if (audioStallMs >= 2000) break;
            }
            lastAudioQueuedMs = audioQueuedMs;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// 读包失败/EOF/网络重连处理；返回 true 表示需要退出解码循环
bool FFmpegPlayer::handleReadFailure(int ret, int& consecutiveReadErrors) {
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
        if (m_sourceType == SourceType::NetworkStream) {
            setNetworkState(NetworkState::Idle);
        }
        return true;
    }
    if (m_sourceType == SourceType::NetworkStream) {
        consecutiveReadErrors++;
        if (consecutiveReadErrors == 1) {
            setNetworkState(NetworkState::Reconnecting);
        } else if (consecutiveReadErrors > 200) {
            setNetworkState(NetworkState::Failed);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

// 流时间戳（pts/dts）转媒体毫秒，基准为容器 start_time，与 seek 基准一致
int64_t FFmpegPlayer::streamPtsToMs(const AVStream* stream, int64_t pts) const {
    if (!stream || pts == AV_NOPTS_VALUE) {
        return 0;
    }
    int64_t startTime = 0;
    if (m_formatContext && m_formatContext->start_time != AV_NOPTS_VALUE) {
        startTime = av_rescale_q(m_formatContext->start_time,
                                 {1, AV_TIME_BASE}, stream->time_base);
    }
    return av_rescale_q(pts - startTime, stream->time_base, {1, 1000});
}

void FFmpegPlayer::handleDecodedVideoFrame(AVFrame* frame, double dtRead) {
    // seek 裁剪：seek 落点前的关键帧会被解出，目标之前的帧不入队，
    // 否则消费者会先显示一大段早于 seek 位置的画面
    int64_t rawPts = frame->pts;
    if (rawPts == AV_NOPTS_VALUE) rawPts = frame->best_effort_timestamp;
    const int64_t rawPtsMs = streamPtsToMs(m_videoCtx.stream, rawPts);
    const int64_t stepBackAnchor = m_stepBackAnchorMs.load();
    if (stepBackAnchor >= 0) {
        // 逐帧回退：锚点及之后的帧同样入队——落点取 pts < anchor 的最大帧，
        // 而 +1 步进靠队列里锚点起的缓冲窗口精确回位，不能丢。
        // 只跟踪解码进度：>=anchor 的帧数够覆盖重排深度即收齐
        if (rawPts != AV_NOPTS_VALUE && rawPtsMs >= stepBackAnchor) {
            m_stepBackFwdCount++;
        }
        if (rawPts != AV_NOPTS_VALUE && rawPtsMs > m_stepBackDecodedMaxMs) {
            m_stepBackDecodedMaxMs = rawPtsMs;
        }
    } else if (m_videoTrimBeforeMs >= 0 &&
        // 持续裁剪：B 帧重排时 pts < target 的帧可能在首个 >= target 的
        // P/I 帧之后才解出，只裁到首个帧会把它漏进队列；裁剪保持生效，
        // 由下一次 seek 覆盖新值（loadFile/stop 时复位）
        rawPts != AV_NOPTS_VALUE && rawPtsMs < m_videoTrimBeforeMs) {
        return;
    }

    auto t1 = HRClock::now();
    VideoFrame videoFrame = convertVideoFrame(frame);
    double dtConv = elapsedMs(t1);
    if (!videoFrame.data.empty()) {
        videoFrame.pts = rawPtsMs;
        auto t2 = HRClock::now();
        pushVideoFrame(std::move(videoFrame));
        m_vframeCount++;
        double dtPush = elapsedMs(t2);
        if (dtConv > 5.0 || dtRead > 5.0 || dtPush > 1.0) {
            logger().debug("[PERF] video read=" + std::to_string(dtRead) +
                "ms convert=" + std::to_string(dtConv) +
                "ms push=" + std::to_string(dtPush) + "ms");
        }
    }
}

void FFmpegPlayer::receiveVideoFrames(AVFrame* frame, double dtRead) {
    while (avcodec_receive_frame(m_videoCtx.codecContext, frame) >= 0) {
        handleDecodedVideoFrame(frame, dtRead);
        av_frame_unref(frame);
    }
}

void FFmpegPlayer::decodeVideoPacket(AVPacket* packet, AVFrame* frame, double dtRead) {
    // Decode all available video frames from this packet
    int ret = avcodec_send_packet(m_videoCtx.codecContext, packet);
    // Process any frames already buffered (EAGAIN case)
    if (ret == AVERROR(EAGAIN)) {
        receiveVideoFrames(frame, dtRead);
        ret = avcodec_send_packet(m_videoCtx.codecContext, packet);
    }
    if (ret >= 0) {
        receiveVideoFrames(frame, dtRead);
    }
}

void FFmpegPlayer::handleDecodedAudioFrame(AVFrame* frame, double dtRead) {
    // 媒体时间跟踪：seek 后整帧早于目标的丢弃；时间戳不连续时重新对齐
    int64_t rawPts = frame->best_effort_timestamp != AV_NOPTS_VALUE ?
        frame->best_effort_timestamp : frame->pts;
    const bool hasPts = rawPts != AV_NOPTS_VALUE;
    double framePtsMs = hasPts ?
        static_cast<double>(streamPtsToMs(m_audioCtx.stream, rawPts)) :
        static_cast<double>(m_audioBaseMs.load());
    double durMs = frame->sample_rate > 0 ?
        frame->nb_samples * 1000.0 / frame->sample_rate : 0.0;
    if (m_audioTrimBeforeMs >= 0) {
        if (framePtsMs + durMs <= m_audioTrimBeforeMs) {
            return;   // 整帧都在 seek 目标之前
        }
        m_audioTrimBeforeMs = -1;
    }
    if (m_audioNextPtsMs < 0) {
        m_audioNextPtsMs = framePtsMs;
    } else if (m_audioInputExpectedMs >= 0 &&
               std::abs(framePtsMs - m_audioInputExpectedMs) > 1000.0) {
        // 断流/时间戳跳变：重新对齐媒体时间
        m_audioNextPtsMs = framePtsMs;
    }
    m_audioInputExpectedMs = framePtsMs + durMs;

    auto t3 = HRClock::now();
    auto audioData = processAudioFrame(frame);
    double dtResample = elapsedMs(t3);
    if (!audioData.empty() && m_audioPlayer) {
        // 取产出这段数据的滤镜图实际生效的 atempo（无图为 1.0）
        double tempo = 1.0;
        {
            std::lock_guard<std::mutex> filterLock(m_audioFilterMutex);
            if (m_audioFilterGraph) {
                tempo = m_audioFilterTempo;
            }
        }
        int channels = m_audioCtx.format.channels > 0 ? m_audioCtx.format.channels : 1;
        int64_t outFrames = static_cast<int64_t>(audioData.size()) / channels;
        int dstRate = m_audioCtx.format.sampleRate > 0 ? m_audioCtx.format.sampleRate : 48000;
        double msPerFrame = tempo * 1000.0 / dstRate;
        auto t4 = HRClock::now();
        m_audioPlayer->enqueue(audioData.data(), audioData.size(),
                               m_audioNextPtsMs, msPerFrame);
        m_audioNextPtsMs += static_cast<double>(outFrames) * msPerFrame;
        m_aframeCount++;
        double dtEnqueue = elapsedMs(t4);
        if (dtRead > 5.0 || dtResample > 5.0 || dtEnqueue > 1.0) {
            logger().debug("[PERF] audio read=" + std::to_string(dtRead) +
                "ms resample=" + std::to_string(dtResample) +
                "ms enqueue=" + std::to_string(dtEnqueue) + "ms");
        }
    }
}

void FFmpegPlayer::receiveAudioFrames(AVFrame* frame, double dtRead) {
    while (avcodec_receive_frame(m_audioCtx.codecContext, frame) >= 0) {
        handleDecodedAudioFrame(frame, dtRead);
        av_frame_unref(frame);
    }
}

void FFmpegPlayer::decodeAudioPacket(AVPacket* packet, AVFrame* frame, double dtRead) {
    // Decode all available audio frames from this packet
    int ret = avcodec_send_packet(m_audioCtx.codecContext, packet);
    if (ret == AVERROR(EAGAIN)) {
        receiveAudioFrames(frame, dtRead);
        ret = avcodec_send_packet(m_audioCtx.codecContext, packet);
    }
    if (ret >= 0) {
        receiveAudioFrames(frame, dtRead);
    }
}

void FFmpegPlayer::decodeSubtitlePacket(AVPacket* packet) {
    // 解码内封字幕包（文本/ASS/PGS 等统一用 avcodec_decode_subtitle2）
    AVSubtitle sub{};
    int gotSub = 0;
    if (avcodec_decode_subtitle2(m_subtitleCtx.codecContext, &sub, &gotSub, packet) >= 0 && gotSub) {
        int64_t pktPts = packet->pts != AV_NOPTS_VALUE ? packet->pts : (packet->dts != AV_NOPTS_VALUE ? packet->dts : 0);
        int64_t ptsMs = streamPtsToMs(m_subtitleCtx.stream, pktPts);
        // 文本字幕的显示区间：优先取解码器给出的 end_display_time，
        // 其次用包 duration，都没有才退到 5 秒兜底
        int64_t startMs = ptsMs + sub.start_display_time;
        int64_t endMs;
        if (sub.end_display_time > 0) {
            endMs = startMs + sub.end_display_time;
        } else if (packet->duration > 0) {
            endMs = startMs + av_rescale_q(packet->duration,
                                           m_subtitleCtx.stream->time_base, {1, 1000});
        } else {
            endMs = startMs + 5000;
        }
        for (unsigned i = 0; i < sub.num_rects; i++) {
            AVSubtitleRect* rect = sub.rects[i];
            if (!rect) continue;

            if (rect->type == SUBTITLE_BITMAP) {
                SubtitleBitmap bm;
                bm.x = rect->x;
                bm.y = rect->y;
                bm.width = rect->w;
                bm.height = rect->h;
                bm.startMs = ptsMs + sub.start_display_time;
                bm.endMs = bm.startMs + (sub.end_display_time > 0 ? sub.end_display_time : 5000);

                const uint8_t* src = rect->data[0];
                const int linesize = rect->linesize[0];
                const uint32_t* palette = reinterpret_cast<const uint32_t*>(rect->data[1]);

                if (src && bm.width > 0 && bm.height > 0) {
                    bm.pixels.resize(static_cast<size_t>(bm.width) * bm.height);
                    for (int y = 0; y < bm.height; y++) {
                        for (int x = 0; x < bm.width; x++) {
                            uint8_t idx = src[y * linesize + x];
                            bm.pixels[y * bm.width + x] = palette ? palette[idx] : 0xFF000000;
                        }
                    }
                    std::lock_guard<std::mutex> lock(m_subtitleBitmapMutex);
                    m_subtitleBitmapQueue.push_back(std::move(bm));
                    if (m_subtitleBitmapQueue.size() > 10) {
                        m_subtitleBitmapQueue.pop_front();
                    }
                }
            } else if (rect->ass && m_subtitleTextCallback) {
                // ASS/SSA：rect->ass 是整行 Dialogue，需要提取 Text 并剥标签
                m_subtitleTextCallback(startMs, endMs,
                    SubtitleParser::embeddedTextToPlain(rect->ass));
            } else if (rect->text && m_subtitleTextCallback) {
                m_subtitleTextCallback(startMs, endMs,
                    SubtitleParser::removeFormatting(rect->text));
            }
        }
        avsubtitle_free(&sub);
    }
}

VideoFrame FFmpegPlayer::convertVideoFrame(AVFrame* frame) {
    VideoFrame result;
    if (!frame) return result;

    auto frameDeleter = [](AVFrame* value) {
        if (value) {
            av_frame_free(&value);
        }
    };
    std::unique_ptr<AVFrame, decltype(frameDeleter)> transferredFrame(nullptr, frameDeleter);
    std::unique_ptr<AVFrame, decltype(frameDeleter)> filteredFrame(nullptr, frameDeleter);

    AVFrame* sourceFrame = frame;
    if (m_hwPixelFormat != AV_PIX_FMT_NONE &&
        static_cast<AVPixelFormat>(frame->format) == m_hwPixelFormat) {
        AVFrame* swFrame = av_frame_alloc();
        if (!swFrame) {
            logger().warning("Failed to allocate software frame for hardware transfer");
            return result;
        }
        transferredFrame.reset(swFrame);

        int transferRet = av_hwframe_transfer_data(transferredFrame.get(), frame, 0);
        if (transferRet < 0) {
            logger().warning("Hardware frame transfer failed");
            return result;
        }
        av_frame_copy_props(transferredFrame.get(), frame);
        sourceFrame = transferredFrame.get();
    }

    // 阶段 1：HDR 色调映射 / 去隔行（仅解码线程内构建滤镜图）
    AVFrame* filterOut = processVideoFilter(sourceFrame);
    if (filterOut != sourceFrame) {
        filteredFrame.reset(filterOut);
        sourceFrame = filterOut;
    }

    if (!sourceFrame->data[0]) return result;

    int width = sourceFrame->width;
    int height = sourceFrame->height;
    if (width <= 0 || height <= 0) return result;

    // 保留阶段 1 输出作为基础帧：解码器自产帧克隆引用（调用方随后 unref），
    // 转存/过滤产物直接转移所有权，均不额外复制像素
    std::shared_ptr<AVFrame> base;
    if (sourceFrame == frame) {
        base.reset(av_frame_clone(frame), frameDeleter);
        if (!base) {
            logger().warning("Failed to clone frame for adjust base");
            return result;
        }
    } else if (sourceFrame == filteredFrame.get()) {
        base = std::shared_ptr<AVFrame>(filteredFrame.release(), frameDeleter);
    } else {
        base = std::shared_ptr<AVFrame>(transferredFrame.release(), frameDeleter);
    }

    // 帧未携带色彩空间时回填 codec 的（clone/产物都是独立 AVFrame，改字段安全），
    // 否则 VideoAdjuster 的 BGRA 转换会按错误矩阵走
    if (base->colorspace == AVCOL_SPC_UNSPECIFIED &&
        m_videoCtx.codecContext &&
        m_videoCtx.codecContext->colorspace != AVCOL_SPC_UNSPECIFIED) {
        base->colorspace = m_videoCtx.codecContext->colorspace;
    }

    // 转换前快照配置与代次，保证 data 与 adjustGeneration 一致
    VideoFilterConfig config;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_videoFilterMutex);
        config = m_videoFilterConfig;
        generation = m_videoAdjustGeneration.load();
    }

    // 阶段 2：画面调节（eq/hue）+ sws 转 BGRA
    result = m_decodeAdjuster.convert(base.get(), config);
    if (result.data.empty()) {
        return result;
    }
    result.base = std::move(base);
    result.adjustGeneration = generation;
    return result;
}

bool FFmpegPlayer::refreshVideoFrame(VideoFrame& frame) {
    if (!frame.base) {
        return false;
    }

    VideoFilterConfig config;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_videoFilterMutex);
        generation = m_videoAdjustGeneration.load();
        if (frame.adjustGeneration == generation) {
            return false;
        }
        config = m_videoFilterConfig;
    }

    VideoFrame refreshed = m_refreshAdjuster.convert(frame.base.get(), config);
    if (refreshed.data.empty()) {
        return false;
    }
    frame.data = std::move(refreshed.data);
    frame.width = refreshed.width;
    frame.height = refreshed.height;
    frame.adjustGeneration = generation;
    return true;
}

std::vector<float> FFmpegPlayer::processAudioFrame(AVFrame* frame) {
    if (!frame) {
        return {};
    }

    {
        std::lock_guard<std::mutex> filterLock(m_audioFilterMutex);
        if (ensureAudioFilterGraph(frame)) {
            int ret = av_buffersrc_add_frame_flags(m_audioFilterSrc, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
            if (ret < 0) {
                logger().warning("Failed to feed audio filter graph; using unfiltered audio");
                m_audioFilterRuntimeDisabled = true;
                cleanupAudioFilterGraph();
                return resampleAudioFrame(frame);
            }

            std::vector<float> result;
            AVFrame* filteredFrame = av_frame_alloc();
            if (!filteredFrame) {
                return result;
            }

            while (true) {
                ret = av_buffersink_get_frame(m_audioFilterSink, filteredFrame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                if (ret < 0) {
                    logger().warning("Failed to read audio filter graph output");
                    break;
                }

                auto chunk = resampleAudioFrame(filteredFrame);
                result.insert(result.end(), chunk.begin(), chunk.end());
                av_frame_unref(filteredFrame);
            }

            av_frame_free(&filteredFrame);
            return result;
        }
    }

    return resampleAudioFrame(frame);
}

std::vector<float> FFmpegPlayer::resampleAudioFrame(AVFrame* frame) {
    std::vector<float> result;
    if (!frame || !frame->data[0]) return result;
    
    int srcRate = frame->sample_rate;
    int srcChannels = frame->ch_layout.nb_channels;
    int dstRate = m_audioCtx.format.sampleRate;
    int dstChannels = m_audioCtx.format.channels;
    if (srcRate <= 0) {
        srcRate = dstRate;
    }
    if (srcChannels <= 0) {
        srcChannels = dstChannels > 0 ? dstChannels : 2;
    }
    
    AVSampleFormat srcFormat = static_cast<AVSampleFormat>(frame->format);
    bool needsNewSwr =
        !m_audioCtx.swrContext ||
        m_audioCtx.lastSrcRate != srcRate ||
        m_audioCtx.lastSrcChannels != srcChannels ||
        m_audioCtx.lastSrcFormat != srcFormat;

    if (needsNewSwr) {
        if (m_audioCtx.swrContext) {
            swr_free(&m_audioCtx.swrContext);
        }
        m_audioCtx.swrContext = swr_alloc();
        if (!m_audioCtx.swrContext) return result;
        
        AVChannelLayout srcLayout, dstLayout;
        av_channel_layout_default(&srcLayout, srcChannels);
        av_channel_layout_default(&dstLayout, dstChannels);
        
        av_opt_set_chlayout(m_audioCtx.swrContext, "in_chlayout", &srcLayout, 0);
        av_opt_set_int(m_audioCtx.swrContext, "in_sample_rate", srcRate, 0);
        av_opt_set_sample_fmt(m_audioCtx.swrContext, "in_sample_fmt", srcFormat, 0);
        
        av_opt_set_chlayout(m_audioCtx.swrContext, "out_chlayout", &dstLayout, 0);
        av_opt_set_int(m_audioCtx.swrContext, "out_sample_rate", dstRate, 0);
        av_opt_set_sample_fmt(m_audioCtx.swrContext, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
        
        if (swr_init(m_audioCtx.swrContext) < 0) {
            swr_free(&m_audioCtx.swrContext);
            return result;
        }

        m_audioCtx.lastSrcRate = srcRate;
        m_audioCtx.lastSrcChannels = srcChannels;
        m_audioCtx.lastSrcFormat = srcFormat;
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
    // 声道映射（单声道/左右交换）：交错 F32 就地处理，滤镜与直通两条路径都会经过这里
    mapAudioChannels(result, dstChannels, m_channelMapMode.load());
    return result;
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
    // 音频同步偏移（Shift+G/H）：加到显示位置上，等效于音频相对视频偏移
    const int64_t syncOffset = m_audioSyncOffsetMs.load();
    if (m_audioPlayer) {
        // 媒体时钟按已入队采样推算；时钟为空（如刚 seek）时用 seek 基准
        double pos = m_audioPlayer->mediaPositionMs();
        if (pos < 0) {
            pos = static_cast<double>(m_audioBaseMs.load());
        }
        return static_cast<int64_t>(pos) + syncOffset;
    }
    // 无音轨文件：挂钟回退，保证进度条与视频帧正常前进
    int64_t base = m_audioBaseMs.load();
    if (m_fallbackClockRunning.load()) {
        int64_t startNs = m_fallbackClockStartNs.load();
        double elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now().time_since_epoch() -
            std::chrono::nanoseconds(startNs)).count();
        return base + static_cast<int64_t>(elapsed * m_playbackSpeed.load()) + syncOffset;
    }
    return base + syncOffset;
}

void FFmpegPlayer::startFallbackClock() {
    if (m_fallbackClockRunning.load()) return;
    m_fallbackClockStartNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_fallbackClockRunning = true;
}

void FFmpegPlayer::stopFallbackClock() {
    if (!m_fallbackClockRunning.load()) return;
    // 把已流逝时间并入基准，冻结时钟
    int64_t startNs = m_fallbackClockStartNs.load();
    double elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch() -
        std::chrono::nanoseconds(startNs)).count();
    m_audioBaseMs = m_audioBaseMs.load() +
        static_cast<int64_t>(elapsed * m_playbackSpeed.load());
    m_fallbackClockRunning = false;
}

void FFmpegPlayer::setPlaybackSpeed(double speed) {
    speed = std::clamp(speed, 0.25, 4.0);
    m_playbackSpeed = speed;
    // 倍速经 atempo 滤镜实现：滤镜描述串变化后由解码线程自动重建滤镜图
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

void FFmpegPlayer::setHardwareDecodingEnabled(bool enabled) {
    m_hardwareDecodingEnabled = enabled;
}

bool FFmpegPlayer::hardwareDecodingEnabled() const {
    return m_hardwareDecodingEnabled.load();
}

void FFmpegPlayer::setAudioOutputDevice(SDL_AudioDeviceID deviceId) {
    m_audioOutputDeviceId = deviceId != 0 ? deviceId : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    if (!m_audioPlayer) {
        return;
    }

    const bool wasPlaying = m_audioPlayer->isPlaying() && !m_audioPlayer->isPaused();
    const bool wasPaused = m_audioPlayer->isPaused();
    const int volume = m_audioPlayer->volume();
    const bool muted = m_audioPlayer->isMuted();
    const AudioFormat format = m_audioCtx.format;

    // 重建 stream 会丢队列与媒体时钟，记录当前位置做基准，避免时钟跳回旧 seek 点
    const double pos = m_audioPlayer->mediaPositionMs();
    if (pos >= 0) {
        m_audioBaseMs = static_cast<int64_t>(pos);
    }

    if (!m_audioPlayer->initialize(format, m_audioOutputDeviceId)) {
        logger().error("Failed to switch audio output device");
        return;
    }
    m_audioPlayer->setVolume(volume);
    m_audioPlayer->setMuted(muted);
    if (wasPlaying) {
        m_audioPlayer->play();
    } else if (wasPaused) {
        m_audioPlayer->play();
        m_audioPlayer->pause();
    }
}

SDL_AudioDeviceID FFmpegPlayer::audioOutputDevice() const {
    return m_audioOutputDeviceId;
}

void FFmpegPlayer::setAudioSyncOffsetMs(int64_t offsetMs) {
    m_audioSyncOffsetMs = offsetMs;
    logger().info("Audio sync offset: " + std::to_string(offsetMs) + "ms");
}

void FFmpegPlayer::adjustAudioSync(int64_t deltaMs) {
    setAudioSyncOffsetMs(m_audioSyncOffsetMs.load() + deltaMs);
}

int64_t FFmpegPlayer::audioSyncOffsetMs() const {
    return m_audioSyncOffsetMs.load();
}

void FFmpegPlayer::setNetworkState(NetworkState state) {
    NetworkState previous = m_networkState.exchange(state);
    if (previous != state && m_networkStateCallback) {
        m_networkStateCallback(state);
    }
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

void FFmpegPlayer::setNetworkStateCallback(NetworkStateCallback callback) {
    m_networkStateCallback = callback;
}

void FFmpegPlayer::pushStepHistory(VideoFrame frame) {
    // 调用方必须持 m_videoQueueMutex；按字节容量逐出最旧帧
    m_stepHistBytes += frame.data.size();
    m_stepHistory.emplace(frame.pts, std::move(frame));
    while (m_stepHistBytes > kStepHistByteCap && m_stepHistory.size() > 1) {
        auto it = m_stepHistory.begin();
        m_stepHistBytes -= it->second.data.size();
        // 历史逐出等同于未捕获的丢帧：抬水位
        m_stepDropWatermark = std::max(m_stepDropWatermark, it->first);
        m_stepHistory.erase(it);
    }
}

void FFmpegPlayer::pushVideoFrame(VideoFrame&& frame) {
    std::lock_guard<std::mutex> lock(m_videoQueueMutex);
    // 满时丢 pts 最小（最旧）的一帧；补解码窗口内放宽上限，
    // 防止 seek 落点帧（最小 pts）在 B 重排收齐前被挤掉
    const size_t limit = m_seekBurstActive.load() ? kSeekBurstQueueSize
                                                  : kMaxVideoQueueSize;
    if (m_videoFrameQueue.size() >= limit) {
        // 未消费即被挤掉的帧抬水位：队列/历史里 pts 更小的旧帧
        // 与锚点间可能隔着这个空洞，不能再当 -1 的落点
        m_stepDropWatermark = std::max(m_stepDropWatermark,
                                       m_videoFrameQueue.front().pts);
        m_videoFrameQueue.pop_front();
    }
    // 按 pts 有序插入：B 帧重排的解码顺序（如 P320 早于 B200 入队）
    // 会让队首不是最小 pts，逐帧步进与严格同步都依赖单调 pts
    auto it = std::lower_bound(m_videoFrameQueue.begin(), m_videoFrameQueue.end(),
                               frame.pts,
                               [](const VideoFrame& f, int64_t pts) {
                                   return f.pts < pts;
                               });
    m_videoFrameQueue.insert(it, std::move(frame));
    m_decodeCondition.notify_one();
}

std::vector<ChapterInfo> FFmpegPlayer::chapters() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_chapters;
}

void FFmpegPlayer::setChapters(const std::vector<ChapterInfo>& chapters) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_chapters = chapters;
    logger().info("[FFmpeg] Chapters set: " + std::to_string(chapters.size()));
}

MediaInfo FFmpegPlayer::mediaInfo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_mediaInfo;
}

int FFmpegPlayer::videoQueueSize() const {
    std::lock_guard<std::mutex> lock(m_videoQueueMutex);
    return static_cast<int>(m_videoFrameQueue.size());
}

int64_t FFmpegPlayer::audioQueuedMs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_audioPlayer ? m_audioPlayer->queuedMs() : 0;
}

std::string FFmpegPlayer::ffmpegVersion() {
    return std::string(av_version_info());
}

bool FFmpegPlayer::getVideoFrame(int64_t targetPtsMs, VideoFrame& frame) {
    std::lock_guard<std::mutex> lock(m_videoQueueMutex);
    // seek 请求未处理前，队列里仍是旧位置帧，不能取
    if (m_seekRequested.load()) {
        return false;
    }
    if (m_videoFrameQueue.empty()) {
        return false;
    }
    
    // 特殊值 targetPtsMs < 0：启动 fallback，直接取最早的一帧避免黑屏
    if (targetPtsMs < 0) {
        pushStepHistory(m_videoFrameQueue.front());
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

    // 所有被消费的帧（含返回帧）进步进历史：历史 ∪ 队列保持连续的
    // 已解码窗口，暂停后 -1 落点可以直接命中前帧，不用重新 seek
    for (size_t i = 0; i < index; ++i) {
        pushStepHistory(std::move(m_videoFrameQueue[i]));
    }
    pushStepHistory(m_videoFrameQueue[index]);
    frame = std::move(m_videoFrameQueue[index]);
    m_videoFrameQueue.erase(m_videoFrameQueue.begin(), m_videoFrameQueue.begin() + index + 1);
    m_decodeCondition.notify_one();
    return true;
}

bool FFmpegPlayer::getStepLandingFrame(VideoFrame& frame) {
    // 新 seek 请求未处理前不能取：上一次 seek 残留的就绪标志会让
    // 消费端弹出旧队列的帧
    if (m_seekRequested.load()) {
        return false;
    }
    // paused seek 补解码窗口未收齐前不取帧：
    // 队列按 pts 排序，收齐后队首即 seek 目标帧
    if (m_seekLandingReady.load() < 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_videoQueueMutex);
    if (m_landingIsStepBack.load()) {
        // 回退落点：pts < anchor 的最大帧（队列按 pts 有序，
        // 锚点即 m_seekLandingReady 存的锚点位置）。候选从队列与
        // 步进历史的并集里取——消费过的帧（如 +1 走过的）在历史里，
        // 它们才保证解码序相邻；低于丢弃水位的候选不可信
        const int64_t anchor = m_seekLandingReady.load();
        const VideoFrame* cand = nullptr;
        for (auto it = m_videoFrameQueue.rbegin();
             it != m_videoFrameQueue.rend(); ++it) {
            if (it->pts < anchor) {
                if (it->pts > m_stepDropWatermark) cand = &*it;
                break;
            }
        }
        auto hit = m_stepHistory.lower_bound(anchor);
        if (hit != m_stepHistory.begin()) {
            auto prev = std::prev(hit);
            if (prev->first > m_stepDropWatermark &&
                (!cand || prev->first > cand->pts)) {
                cand = &prev->second;
            }
        }
        if (!cand) {
            // 已是第一帧（或窗口内无可信前帧）：返回空帧让调用方取消步进
            m_seekLandingReady = -1;
            m_landingIsStepBack = false;
            return true;
        }
        // 只取不删：落点与前驱都留在窗口里，连续 -1/+1 继续命中
        frame = *cand;
    } else {
        if (m_videoFrameQueue.empty()) {
            return false;
        }
        pushStepHistory(m_videoFrameQueue.front());
        frame = std::move(m_videoFrameQueue.front());
        m_videoFrameQueue.pop_front();
    }
    m_seekLandingReady = -1;
    m_landingIsStepBack = false;
    m_decodeCondition.notify_one();
    return true;
}

bool FFmpegPlayer::getStepForwardFrame(int64_t prevPtsMs, VideoFrame& frame) {
    std::lock_guard<std::mutex> lock(m_videoQueueMutex);
    // seek 请求未处理前，队列里仍是旧位置帧，不能取
    if (m_seekRequested.load()) {
        return false;
    }
    // 取 pts > prevPtsMs 的第一帧：已消费的帧在历史里（比如
    // +1 走过后又 -1 回去），未消费的在队列里，两边都要看
    auto qit = std::upper_bound(m_videoFrameQueue.begin(),
                                m_videoFrameQueue.end(), prevPtsMs,
                                [](int64_t pts, const VideoFrame& f) {
                                    return pts < f.pts;
                                });
    auto hit = m_stepHistory.upper_bound(prevPtsMs);
    if (hit != m_stepHistory.end() &&
        (qit == m_videoFrameQueue.end() || hit->first < qit->pts)) {
        // 历史命中：只取不删，帧留在窗口里供 -1 继续命中
        frame = hit->second;
        return true;
    }
    if (qit == m_videoFrameQueue.end()) {
        // 还没有更晚的帧：把已消费的最旧帧迁入历史腾队位，
        // 解除暂停态背压让解码线程继续产出后续帧
        for (int moved = 0; moved < 4 && !m_videoFrameQueue.empty() &&
             m_videoFrameQueue.front().pts <= prevPtsMs; ++moved) {
            pushStepHistory(std::move(m_videoFrameQueue.front()));
            m_videoFrameQueue.pop_front();
        }
        m_decodeCondition.notify_one();
        return false;
    }
    pushStepHistory(*qit);
    frame = std::move(*qit);
    m_videoFrameQueue.erase(qit);
    m_decodeCondition.notify_one();
    return true;
}


} // namespace VideoPlay
