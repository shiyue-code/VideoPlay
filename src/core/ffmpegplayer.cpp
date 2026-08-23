#include "core/ffmpegplayer.h"
#include "core/audioplayer.h"
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

namespace {
Logger& logger() {
    static auto logger = Logger::get("ffmpeg");
    return *logger;
}
}


namespace {
    const double CLOCK_SYNC_THRESHOLD = 0.01;
    const double VIDEO_FRAME_TOLERANCE = 0.005;
    const int AUDIO_QUEUE_SIZE = 100;


    double rationalToDouble(AVRational value) {
        if (value.num == 0 || value.den == 0) {
            return 0.0;
        }
        return static_cast<double>(value.num) / static_cast<double>(value.den);
    }

    std::string codecDisplayName(const AVCodec* codec, AVCodecID codecId) {
        if (codec && codec->long_name) {
            return codec->long_name;
        }
        const char* name = avcodec_get_name(codecId);
        return name ? std::string(name) : std::string("Unknown");
    }

    std::string hardwareDeviceName(AVHWDeviceType type) {
        const char* name = av_hwdevice_get_type_name(type);
        return name ? std::string(name) : std::string();
    }
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
    if (m_durationCallback && m_duration > 0) {
        m_durationCallback(m_duration);
    }
    
    m_filePath = filePath;
    
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
    
    if (m_videoCtx.swsContext) {
        sws_freeContext(m_videoCtx.swsContext);
        m_videoCtx.swsContext = nullptr;
    }
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
    m_state = PlaybackState::Stopped;
    m_chapters.clear();
    m_audioTracks.clear();
    m_subtitleTracks.clear();
    m_currentAudioTrack = 0;
    m_currentSubtitleTrack = -1;
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
    if (!m_audioPlayer->initialize(m_audioCtx.format)) {
        logger().error("Failed to initialize audio player");
        m_audioPlayer.reset();
        return false;
    }
    
    m_audioPlayer->setVolume(m_volume.load());
    m_audioPlayer->setMuted(m_muted.load());
    
    return true;
}

std::string FFmpegPlayer::buildAudioFilterDescription() const {
    if (!m_audioFilterConfig.enabled) {
        return {};
    }

    std::vector<std::string> filters;
    if (std::abs(m_audioFilterConfig.preampDb) > 0.01) {
        std::ostringstream oss;
        oss << "volume=" << m_audioFilterConfig.preampDb << "dB";
        filters.push_back(oss.str());
    }

    for (const auto& band : m_audioFilterConfig.eqBands) {
        if (band.frequency <= 0.0 || band.width <= 0.0 ||
            std::abs(band.gainDb) <= 0.01) {
            continue;
        }
        std::ostringstream oss;
        oss << "equalizer=f=" << band.frequency
            << ":width_type=o:width=" << band.width
            << ":g=" << band.gainDb;
        filters.push_back(oss.str());
    }

    if (m_audioFilterConfig.dynamicNormalizerEnabled) {
        filters.push_back("dynaudnorm=f=150:g=8");
    }
    if (m_audioFilterConfig.limiterEnabled) {
        filters.push_back("alimiter=limit=0.95");
    }

    if (filters.empty()) {
        return {};
    }

    std::ostringstream desc;
    for (size_t i = 0; i < filters.size(); ++i) {
        if (i > 0) {
            desc << ",";
        }
        desc << filters[i];
    }
    return desc.str();
}

void FFmpegPlayer::cleanupAudioFilterGraph() {
    if (m_audioFilterGraph) {
        avfilter_graph_free(&m_audioFilterGraph);
    }
    m_audioFilterSrc = nullptr;
    m_audioFilterSink = nullptr;
    m_audioFilterSampleRate = 0;
    m_audioFilterChannels = 0;
    m_audioFilterSampleFormat = AV_SAMPLE_FMT_NONE;
    m_audioFilterDescription.clear();
}

bool FFmpegPlayer::ensureAudioFilterGraph(AVFrame* frame) {
    if (!frame || !m_audioFilterConfig.enabled || m_audioFilterRuntimeDisabled) {
        return false;
    }

    std::string description = buildAudioFilterDescription();
    if (description.empty()) {
        return false;
    }

    int channels = frame->ch_layout.nb_channels;
    if (channels <= 0) {
        channels = m_audioCtx.codecContext ? m_audioCtx.codecContext->ch_layout.nb_channels : 0;
    }
    if (channels <= 0 || frame->sample_rate <= 0) {
        return false;
    }

    AVSampleFormat sampleFormat = static_cast<AVSampleFormat>(frame->format);
    bool graphMatches =
        m_audioFilterGraph &&
        m_audioFilterSampleRate == frame->sample_rate &&
        m_audioFilterChannels == channels &&
        m_audioFilterSampleFormat == sampleFormat &&
        m_audioFilterDescription == description;
    if (graphMatches) {
        return true;
    }

    cleanupAudioFilterGraph();

    AVFilterGraph* graph = avfilter_graph_alloc();
    if (!graph) {
        logger().warning("Failed to allocate audio filter graph");
        m_audioFilterRuntimeDisabled = true;
        return false;
    }

    const AVFilter* bufferSrc = avfilter_get_by_name("abuffer");
    const AVFilter* bufferSink = avfilter_get_by_name("abuffersink");
    if (!bufferSrc || !bufferSink) {
        logger().warning("Required audio filter endpoints are unavailable");
        avfilter_graph_free(&graph);
        m_audioFilterRuntimeDisabled = true;
        return false;
    }

    AVFilterContext* src = nullptr;
    AVFilterContext* sink = nullptr;
    int ret = avfilter_graph_create_filter(&src, bufferSrc, "in", nullptr, nullptr, graph);
    if (ret < 0) {
        logger().warning("Failed to create audio filter source");
        avfilter_graph_free(&graph);
        m_audioFilterRuntimeDisabled = true;
        return false;
    }

    AVBufferSrcParameters* params = av_buffersrc_parameters_alloc();
    if (!params) {
        logger().warning("Failed to allocate audio filter source parameters");
        avfilter_graph_free(&graph);
        m_audioFilterRuntimeDisabled = true;
        return false;
    }

    params->format = frame->format;
    params->sample_rate = frame->sample_rate;
    params->time_base = m_audioCtx.stream ? m_audioCtx.stream->time_base : AVRational{1, frame->sample_rate};
    if (frame->ch_layout.nb_channels > 0 &&
        av_channel_layout_check(&frame->ch_layout)) {
        av_channel_layout_copy(&params->ch_layout, &frame->ch_layout);
    } else {
        av_channel_layout_default(&params->ch_layout, channels);
    }

    ret = av_buffersrc_parameters_set(src, params);
    av_channel_layout_uninit(&params->ch_layout);
    av_free(params);
    if (ret < 0) {
        logger().warning("Failed to set audio filter source parameters");
        avfilter_graph_free(&graph);
        m_audioFilterRuntimeDisabled = true;
        return false;
    }

    ret = avfilter_graph_create_filter(&sink, bufferSink, "out", nullptr, nullptr, graph);
    if (ret < 0) {
        logger().warning("Failed to create audio filter sink");
        avfilter_graph_free(&graph);
        m_audioFilterRuntimeDisabled = true;
        return false;
    }

    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    if (!outputs || !inputs) {
        avfilter_inout_free(&outputs);
        avfilter_inout_free(&inputs);
        avfilter_graph_free(&graph);
        m_audioFilterRuntimeDisabled = true;
        return false;
    }

    outputs->name = av_strdup("in");
    outputs->filter_ctx = src;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = sink;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    ret = avfilter_graph_parse_ptr(graph, description.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (ret < 0) {
        logger().warning("Failed to parse audio filter graph: " + description);
        avfilter_graph_free(&graph);
        m_audioFilterRuntimeDisabled = true;
        return false;
    }

    ret = avfilter_graph_config(graph, nullptr);
    if (ret < 0) {
        logger().warning("Failed to configure audio filter graph: " + description);
        avfilter_graph_free(&graph);
        m_audioFilterRuntimeDisabled = true;
        return false;
    }

    m_audioFilterGraph = graph;
    m_audioFilterSrc = src;
    m_audioFilterSink = sink;
    m_audioFilterSampleRate = frame->sample_rate;
    m_audioFilterChannels = channels;
    m_audioFilterSampleFormat = sampleFormat;
    m_audioFilterDescription = description;
    logger().info("Audio filter graph enabled: " + description);
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

    m_frameTimer = av_gettime() / 1000000.0;

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
    int vframeCount = 0;
    int aframeCount = 0;
    int consecutiveReadErrors = 0;

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

        auto t0 = HRClock::now();
        int ret = av_read_frame(m_formatContext, packet);
        double dtRead = elapsedMs(t0);

        if (ret < 0) {
            av_packet_unref(packet);
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
                break;
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
            continue;
        }
        if (m_sourceType == SourceType::NetworkStream) {
            if (consecutiveReadErrors > 0) {
                setNetworkState(NetworkState::Playing);
            }
            consecutiveReadErrors = 0;
        }

        if (m_videoCtx.stream && packet->stream_index == m_videoCtx.stream->index && m_videoCtx.codecContext) {
            // Decode all available video frames from this packet
            ret = avcodec_send_packet(m_videoCtx.codecContext, packet);
            // Process any frames already buffered (EAGAIN case)
            if (ret == AVERROR(EAGAIN)) {
                while (avcodec_receive_frame(m_videoCtx.codecContext, vframe) >= 0) {
                    auto t1 = HRClock::now();
                    VideoFrame frame = convertVideoFrame(vframe);
                    double dtConv = elapsedMs(t1);
                    if (!frame.data.empty()) {
                        int64_t pts = vframe->pts;
                        if (pts == AV_NOPTS_VALUE) pts = vframe->best_effort_timestamp;
                        if (pts != AV_NOPTS_VALUE) {
                            int64_t startTime = m_videoCtx.stream->start_time != AV_NOPTS_VALUE ? m_videoCtx.stream->start_time : 0;
                            frame.pts = av_rescale_q(pts - startTime, m_videoCtx.stream->time_base, {1, 1000});
                        }
                        auto t2 = HRClock::now();
                        pushVideoFrame(std::move(frame));
                        vframeCount++;
                        double dtPush = elapsedMs(t2);
                        if (dtConv > 5.0 || dtRead > 5.0 || dtPush > 1.0) {
                            logger().debug("[PERF] video read=" + std::to_string(dtRead) +
                                "ms convert=" + std::to_string(dtConv) +
                                "ms push=" + std::to_string(dtPush) + "ms");
                        }
                    }
                    av_frame_unref(vframe);
                }
                ret = avcodec_send_packet(m_videoCtx.codecContext, packet);
            }
            if (ret >= 0) {
                while (avcodec_receive_frame(m_videoCtx.codecContext, vframe) >= 0) {
                    auto t1 = HRClock::now();
                    VideoFrame frame = convertVideoFrame(vframe);
                    double dtConv = elapsedMs(t1);
                    if (!frame.data.empty()) {
                        int64_t pts = vframe->pts;
                        if (pts == AV_NOPTS_VALUE) pts = vframe->best_effort_timestamp;
                        if (pts != AV_NOPTS_VALUE) {
                            int64_t startTime = m_videoCtx.stream->start_time != AV_NOPTS_VALUE ? m_videoCtx.stream->start_time : 0;
                            frame.pts = av_rescale_q(pts - startTime, m_videoCtx.stream->time_base, {1, 1000});
                        }
                        auto t2 = HRClock::now();
                        pushVideoFrame(std::move(frame));
                        vframeCount++;
                        double dtPush = elapsedMs(t2);
                        if (dtConv > 5.0 || dtRead > 5.0 || dtPush > 1.0) {
                            logger().debug("[PERF] video read=" + std::to_string(dtRead) +
                                "ms convert=" + std::to_string(dtConv) +
                                "ms push=" + std::to_string(dtPush) + "ms");
                        }
                    }
                    av_frame_unref(vframe);
                }
            }
        }
        else if (m_audioCtx.stream && packet->stream_index == m_audioCtx.stream->index && m_audioCtx.codecContext) {
            // Audio backpressure: skip decoding if audio buffer is already full enough.
            // This prevents unbounded audio buffer growth without blocking video decoding.
            if (m_audioPlayer && m_audioPlayer->queuedMs() > kMaxAudioQueueMs) {
                av_packet_unref(packet);
                continue;
            }
            // Decode all available audio frames from this packet
            ret = avcodec_send_packet(m_audioCtx.codecContext, packet);
            if (ret == AVERROR(EAGAIN)) {
                while (avcodec_receive_frame(m_audioCtx.codecContext, aframe) >= 0) {
                    auto t3 = HRClock::now();
                    auto audioData = processAudioFrame(aframe);
                    double dtResample = elapsedMs(t3);
                    if (!audioData.empty() && m_audioPlayer) {
                        if (aframe->pts != AV_NOPTS_VALUE) {
                            int64_t startTime = m_audioCtx.stream->start_time != AV_NOPTS_VALUE ? m_audioCtx.stream->start_time : 0;
                            m_audioClock = av_q2d(m_audioCtx.stream->time_base) * (aframe->pts - startTime);
                        }
                        auto t4 = HRClock::now();
                        m_audioPlayer->enqueue(audioData);
                        aframeCount++;
                        double dtEnqueue = elapsedMs(t4);
                        if (dtRead > 5.0 || dtResample > 5.0 || dtEnqueue > 1.0) {
                            logger().debug("[PERF] audio read=" + std::to_string(dtRead) +
                                "ms resample=" + std::to_string(dtResample) +
                                "ms enqueue=" + std::to_string(dtEnqueue) + "ms");
                        }
                    }
                    av_frame_unref(aframe);
                }
                ret = avcodec_send_packet(m_audioCtx.codecContext, packet);
            }
            if (ret >= 0) {
                while (avcodec_receive_frame(m_audioCtx.codecContext, aframe) >= 0) {
                    auto t3 = HRClock::now();
                    auto audioData = processAudioFrame(aframe);
                    double dtResample = elapsedMs(t3);
                    if (!audioData.empty() && m_audioPlayer) {
                        if (aframe->pts != AV_NOPTS_VALUE) {
                            int64_t startTime = m_audioCtx.stream->start_time != AV_NOPTS_VALUE ? m_audioCtx.stream->start_time : 0;
                            m_audioClock = av_q2d(m_audioCtx.stream->time_base) * (aframe->pts - startTime);
                        }
                        auto t4 = HRClock::now();
                        m_audioPlayer->enqueue(audioData);
                        aframeCount++;
                        double dtEnqueue = elapsedMs(t4);
                        if (dtRead > 5.0 || dtResample > 5.0 || dtEnqueue > 1.0) {
                            logger().debug("[PERF] audio read=" + std::to_string(dtRead) +
                                "ms resample=" + std::to_string(dtResample) +
                                "ms enqueue=" + std::to_string(dtEnqueue) + "ms");
                        }
                    }
                    av_frame_unref(aframe);
                }
            }
        }
        else if (m_subtitleCtx.stream && packet->stream_index == m_subtitleCtx.stream->index && m_subtitleCtx.codecContext) {
            // 解码内封字幕包（文本/ASS/PGS 等统一用 avcodec_decode_subtitle2）
            AVSubtitle sub{};
            int gotSub = 0;
            if (avcodec_decode_subtitle2(m_subtitleCtx.codecContext, &sub, &gotSub, packet) >= 0 && gotSub) {
                int64_t pktPts = packet->pts != AV_NOPTS_VALUE ? packet->pts : (packet->dts != AV_NOPTS_VALUE ? packet->dts : 0);
                int64_t ptsMs = av_rescale_q(pktPts - m_subtitleCtx.startTime,
                                             m_subtitleCtx.stream->time_base, {1, 1000});
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
                        // ASS/SRT/SSA 等通常以 ass 字符串形式返回
                        m_subtitleTextCallback(ptsMs + sub.start_display_time, std::string(rect->ass));
                    } else if (rect->text && m_subtitleTextCallback) {
                        m_subtitleTextCallback(ptsMs + sub.start_display_time, std::string(rect->text));
                    }
                }
                avsubtitle_free(&sub);
            }
        }

        av_packet_unref(packet);

        if (elapsedMs(reportStart) >= 1000.0) {
            logger().debug("[PERF] decodeLoop pkt/s=" + std::to_string(pktCount) +
                " vframe/s=" + std::to_string(vframeCount) +
                " aframe/s=" + std::to_string(aframeCount));
            pktCount = 0;
            vframeCount = 0;
            aframeCount = 0;
            reportStart = HRClock::now();
        }
    }

    // Free reusable allocations
    av_packet_free(&packet);
    av_frame_free(&vframe);
    av_frame_free(&aframe);

    logger().debug("Decode thread stopped");
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

    // 应用视频基础参数滤镜（eq / hue）
    AVFrame* filterOut = processVideoFilter(sourceFrame);
    if (filterOut != sourceFrame) {
        filteredFrame.reset(filterOut);
        sourceFrame = filterOut;
    }

    if (!sourceFrame->data[0]) return result;
    
    int width = sourceFrame->width;
    int height = sourceFrame->height;
    if (width <= 0 || height <= 0) return result;
    AVPixelFormat srcFormat = static_cast<AVPixelFormat>(sourceFrame->format);
    // 使用 BGRA 以匹配 SDL 纹理格式 (Windows 小端字节序)
    AVPixelFormat dstFormat = AV_PIX_FMT_BGRA;
    
    // Determine color space and range from frame metadata
    int colorSpace = SWS_CS_DEFAULT;
    int srcRange = 0;  // 0 = limited (16-235), 1 = full (0-255)
    
    // Check frame-level color range first
    if (sourceFrame->color_range == AVCOL_RANGE_MPEG) {
        srcRange = 0;  // Limited range (TV range)
    } else if (sourceFrame->color_range == AVCOL_RANGE_JPEG) {
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
    } else if (sourceFrame->colorspace != AVCOL_SPC_UNSPECIFIED) {
        if (sourceFrame->colorspace == AVCOL_SPC_BT709) {
            colorSpace = SWS_CS_ITU709;
        } else if (sourceFrame->colorspace == AVCOL_SPC_BT470BG || 
                   sourceFrame->colorspace == AVCOL_SPC_SMPTE170M) {
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
            
            logger().debug("SWS context created: colorspace=" + std::to_string(colorSpace) + 
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
    
    int swsRet = sws_scale(m_videoCtx.swsContext, sourceFrame->data, sourceFrame->linesize, 0, height, dstData, dstStride);
    if (swsRet <= 0) {
        logger().warning("sws_scale failed or returned 0 lines");
        return result;
    }

    result.width = width;
    result.height = height;
    // Copy buffer data to preserve swsBuffer for reuse.
    // This avoids reallocating ~8MB per frame when move leaves swsBuffer empty.
    result.data.assign(m_videoCtx.swsBuffer.data(), m_videoCtx.swsBuffer.data() + dstBufferSize);
    
    return result;
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
    return result;
}

void FFmpegPlayer::synchronizeVideo(double pts) {
    double delay = pts - m_videoClock;
    if (delay <= 0 || delay >= 1.0) {
        delay = m_videoClock > 0 ? 0 : 0.04;
    }
    
    m_videoClock = pts;
    
    // 音频同步偏移：正值让音频延后（视频提前），负值让音频提前（视频延后）
    double syncClock = m_audioClock + (m_audioSyncOffsetMs.load() / 1000.0);
    double diff = m_videoClock - syncClock;
    
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

void FFmpegPlayer::setHardwareDecodingEnabled(bool enabled) {
    m_hardwareDecodingEnabled = enabled;
}

bool FFmpegPlayer::hardwareDecodingEnabled() const {
    return m_hardwareDecodingEnabled.load();
}

void FFmpegPlayer::setAudioFilterConfig(const AudioFilterConfig& config) {
    std::lock_guard<std::mutex> lock(m_audioFilterMutex);
    m_audioFilterConfig = config;
    m_audioFilterRuntimeDisabled = false;
    cleanupAudioFilterGraph();
    logger().info("Audio filter preset: " +
                  std::string(audioFilterPresetName(m_audioFilterConfig.preset)));
}

AudioFilterConfig FFmpegPlayer::audioFilterConfig() const {
    std::lock_guard<std::mutex> lock(m_audioFilterMutex);
    return m_audioFilterConfig;
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

void FFmpegPlayer::setVideoFilterConfig(const VideoFilterConfig& config) {
    std::lock_guard<std::mutex> lock(m_videoFilterMutex);
    m_videoFilterConfig = config;
    m_videoFilterDescription.clear();  // 下次 convert 时重建 graph
    logger().info("Video filter config updated");
}

VideoFilterConfig FFmpegPlayer::videoFilterConfig() const {
    std::lock_guard<std::mutex> lock(m_videoFilterMutex);
    return m_videoFilterConfig;
}

void FFmpegPlayer::cleanupVideoFilterGraph() {
    if (m_videoFilterGraph) {
        avfilter_graph_free(&m_videoFilterGraph);
    }
    m_videoFilterSrc = nullptr;
    m_videoFilterSink = nullptr;
    m_videoFilterWidth = 0;
    m_videoFilterHeight = 0;
    m_videoFilterFormat = AV_PIX_FMT_NONE;
    m_videoFilterDescription.clear();
}

std::string FFmpegPlayer::buildVideoFilterDescription() const {
    if (!m_videoFilterConfig.enabled || m_videoFilterConfig.isDefault()) {
        return {};
    }

    std::ostringstream desc;
    // 先统一到 yuv420p（eq/hue 的要求）
    desc << "format=pix_fmts=yuv420p,";
    desc << "eq=brightness=" << m_videoFilterConfig.brightness
         << ":contrast=" << m_videoFilterConfig.contrast
         << ":saturation=" << m_videoFilterConfig.saturation
         << ":gamma=" << m_videoFilterConfig.gamma;
    desc << ",hue=h=" << m_videoFilterConfig.hue;
    return desc.str();
}

bool FFmpegPlayer::ensureVideoFilterGraph(AVFrame* frame) {
    if (!frame) return false;

    std::lock_guard<std::mutex> lock(m_videoFilterMutex);
    std::string description = buildVideoFilterDescription();

    int width = frame->width;
    int height = frame->height;
    AVPixelFormat format = static_cast<AVPixelFormat>(frame->format);

    if (description.empty()) {
        if (m_videoFilterGraph) {
            cleanupVideoFilterGraph();
        }
        return false;
    }

    bool graphMatches =
        m_videoFilterGraph &&
        m_videoFilterWidth == width &&
        m_videoFilterHeight == height &&
        m_videoFilterFormat == format &&
        m_videoFilterDescription == description;
    if (graphMatches) {
        return true;
    }

    cleanupVideoFilterGraph();

    AVFilterGraph* graph = avfilter_graph_alloc();
    if (!graph) {
        logger().warning("Failed to allocate video filter graph");
        return false;
    }

    const AVFilter* bufferSrc = avfilter_get_by_name("buffer");
    const AVFilter* bufferSink = avfilter_get_by_name("buffersink");
    if (!bufferSrc || !bufferSink) {
        logger().warning("Required video filter endpoints are unavailable");
        avfilter_graph_free(&graph);
        return false;
    }

    AVFilterContext* src = nullptr;
    AVFilterContext* sink = nullptr;
    int ret = avfilter_graph_create_filter(&src, bufferSrc, "in", nullptr, nullptr, graph);
    if (ret < 0) {
        logger().warning("Failed to create video filter source");
        avfilter_graph_free(&graph);
        return false;
    }

    std::ostringstream args;
    args << "video_size=" << width << "x" << height
         << ":pix_fmt=" << static_cast<int>(format)
         << ":time_base=" << (m_videoCtx.stream ? m_videoCtx.stream->time_base.num : 1)
         << "/" << (m_videoCtx.stream ? m_videoCtx.stream->time_base.den : 1)
         << ":pixel_aspect=" << (frame->sample_aspect_ratio.num ? frame->sample_aspect_ratio.num : 1)
         << "/" << (frame->sample_aspect_ratio.den ? frame->sample_aspect_ratio.den : 1);

    ret = avfilter_init_str(src, args.str().c_str());
    if (ret < 0) {
        logger().warning("Failed to init video filter source: " + args.str());
        avfilter_graph_free(&graph);
        return false;
    }

    ret = avfilter_graph_create_filter(&sink, bufferSink, "out", nullptr, nullptr, graph);
    if (ret < 0) {
        logger().warning("Failed to create video filter sink");
        avfilter_graph_free(&graph);
        return false;
    }

    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    if (!outputs || !inputs) {
        avfilter_inout_free(&outputs);
        avfilter_inout_free(&inputs);
        avfilter_graph_free(&graph);
        return false;
    }

    outputs->name = av_strdup("in");
    outputs->filter_ctx = src;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = sink;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    ret = avfilter_graph_parse_ptr(graph, description.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (ret < 0) {
        logger().warning("Failed to parse video filter graph: " + description);
        avfilter_graph_free(&graph);
        return false;
    }

    ret = avfilter_graph_config(graph, nullptr);
    if (ret < 0) {
        logger().warning("Failed to configure video filter graph: " + description);
        avfilter_graph_free(&graph);
        return false;
    }

    m_videoFilterGraph = graph;
    m_videoFilterSrc = src;
    m_videoFilterSink = sink;
    m_videoFilterWidth = width;
    m_videoFilterHeight = height;
    m_videoFilterFormat = format;
    m_videoFilterDescription = description;
    logger().info("Video filter graph enabled: " + description);
    return true;
}

AVFrame* FFmpegPlayer::processVideoFilter(AVFrame* frame) {
    if (!ensureVideoFilterGraph(frame)) {
        return frame;
    }

    int ret = av_buffersrc_add_frame(m_videoFilterSrc, frame);
    if (ret < 0) {
        logger().warning("Failed to add frame to video filter");
        return frame;
    }

    AVFrame* filtered = av_frame_alloc();
    if (!filtered) return frame;

    ret = av_buffersink_get_frame(m_videoFilterSink, filtered);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        av_frame_free(&filtered);
        return frame;
    }
    if (ret < 0) {
        logger().warning("Failed to get frame from video filter");
        av_frame_free(&filtered);
        return frame;
    }

    // 保留原始 pts 等元数据
    av_frame_copy_props(filtered, frame);
    return filtered;
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

void FFmpegPlayer::pushVideoFrame(VideoFrame&& frame) {
    std::lock_guard<std::mutex> lock(m_videoQueueMutex);
    if (m_videoFrameQueue.size() >= kMaxVideoQueueSize) {
        m_videoFrameQueue.pop_front();
    }
    m_videoFrameQueue.push_back(std::move(frame));
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

std::string FFmpegPlayer::streamTitle(AVStream* stream) const {
    if (!stream) return {};
    AVDictionaryEntry* entry = av_dict_get(stream->metadata, "title", nullptr, 0);
    return entry ? std::string(entry->value) : std::string{};
}

std::string FFmpegPlayer::streamLanguage(AVStream* stream) const {
    if (!stream) return {};
    // 优先读取 BCP-47 / IETF 语言标签（MKV/WebM 常见），信息更丰富
    for (const char* key : {"language_ietf", "BCP47", "LanguageIETF", "Language", "language"}) {
        AVDictionaryEntry* entry = av_dict_get(stream->metadata, key, nullptr, 0);
        if (entry && entry->value[0]) {
            return std::string(entry->value);
        }
    }
    return {};
}

void FFmpegPlayer::scanTracks() {
    m_audioTracks.clear();
    m_subtitleTracks.clear();
    if (!m_formatContext) return;

    for (unsigned int i = 0; i < m_formatContext->nb_streams; i++) {
        AVStream* stream = m_formatContext->streams[i];
        AVCodecParameters* codecPar = stream->codecpar;
        if (!codecPar) continue;

        if (codecPar->codec_type == AVMEDIA_TYPE_AUDIO) {
            TrackInfo t;
            t.streamIndex = static_cast<int>(i);
            t.language = streamLanguage(stream);
            t.title = streamTitle(stream);
            const AVCodecDescriptor* desc = avcodec_descriptor_get(codecPar->codec_id);
            t.codecName = desc ? desc->name : "unknown";
            t.channels = codecPar->ch_layout.nb_channels;
            t.sampleRate = codecPar->sample_rate;
            t.isDefault = (stream->disposition & AV_DISPOSITION_DEFAULT) != 0;
            m_audioTracks.push_back(t);
        } else if (codecPar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            // 跳过附加图片字幕（cover）
            TrackInfo t;
            t.streamIndex = static_cast<int>(i);
            t.language = streamLanguage(stream);
            t.title = streamTitle(stream);
            const AVCodecDescriptor* desc = avcodec_descriptor_get(codecPar->codec_id);
            t.codecName = desc ? desc->name : "unknown";
            t.isDefault = (stream->disposition & AV_DISPOSITION_DEFAULT) != 0;
            t.isForced = (stream->disposition & AV_DISPOSITION_FORCED) != 0;
            // 判断字幕类型
            if (codecPar->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE) {
                t.subtitleType = "pgs";
            } else if (codecPar->codec_id == AV_CODEC_ID_ASS ||
                       codecPar->codec_id == AV_CODEC_ID_SSA) {
                t.subtitleType = "ass";
            } else if (codecPar->codec_id == AV_CODEC_ID_SUBRIP ||
                       codecPar->codec_id == AV_CODEC_ID_SRT) {
                t.subtitleType = "srt";
            } else if (codecPar->codec_id == AV_CODEC_ID_WEBVTT) {
                t.subtitleType = "vtt";
            } else {
                t.subtitleType = "text";
            }
            m_subtitleTracks.push_back(t);
        }
    }

    // 确定当前音轨下标（匹配 m_audioCtx.stream）
    m_currentAudioTrack = 0;
    if (m_audioCtx.stream) {
        for (size_t i = 0; i < m_audioTracks.size(); i++) {
            if (m_audioTracks[i].streamIndex == static_cast<int>(m_audioCtx.stream->index)) {
                m_currentAudioTrack = static_cast<int>(i);
                break;
            }
        }
    }
    m_currentSubtitleTrack = -1;

    logger().info("Scanned tracks: " + std::to_string(m_audioTracks.size()) + " audio, " +
                  std::to_string(m_subtitleTracks.size()) + " subtitle");
    for (size_t i = 0; i < m_audioTracks.size(); i++) {
        const auto& t = m_audioTracks[i];
        logger().info("  Audio " + std::to_string(i) + ": stream=" + std::to_string(t.streamIndex) +
                      " lang=" + t.language + " title=" + t.title +
                      " " + std::to_string(t.channels) + "ch/" + std::to_string(t.sampleRate) + "Hz");
    }
    for (size_t i = 0; i < m_subtitleTracks.size(); i++) {
        const auto& t = m_subtitleTracks[i];
        logger().info("  Subtitle " + std::to_string(i) + ": stream=" + std::to_string(t.streamIndex) +
                      " lang=" + t.language + " title=" + t.title +
                      " type=" + t.subtitleType + (t.isForced ? " [forced]" : ""));
    }
}

bool FFmpegPlayer::openAudioStream(int streamIndex) {
    if (!m_formatContext || streamIndex < 0 ||
        streamIndex >= static_cast<int>(m_formatContext->nb_streams)) {
        return false;
    }
    AVStream* stream = m_formatContext->streams[streamIndex];
    if (!stream || stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) return false;

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return false;

    // 关闭旧音频上下文
    if (m_audioCtx.swrContext) {
        swr_free(&m_audioCtx.swrContext);
        m_audioCtx.lastSrcRate = 0;
        m_audioCtx.lastSrcChannels = 0;
        m_audioCtx.lastSrcFormat = AV_SAMPLE_FMT_NONE;
    }
    {
        std::lock_guard<std::mutex> filterLock(m_audioFilterMutex);
        cleanupAudioFilterGraph();
    }
    if (m_audioCtx.codecContext) {
        avcodec_free_context(&m_audioCtx.codecContext);
    }

    m_audioCtx.stream = stream;
    m_audioCtx.codecContext = avcodec_alloc_context3(codec);
    if (!m_audioCtx.codecContext) return false;
    avcodec_parameters_to_context(m_audioCtx.codecContext, stream->codecpar);
    if (avcodec_open2(m_audioCtx.codecContext, codec, nullptr) < 0) {
        avcodec_free_context(&m_audioCtx.codecContext);
        m_audioCtx.stream = nullptr;
        return false;
    }
    m_audioCtx.startTime = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    initializeAudioContext();
    return true;
}

bool FFmpegPlayer::openSubtitleStream(int streamIndex) {
    if (!m_formatContext || streamIndex < 0 ||
        streamIndex >= static_cast<int>(m_formatContext->nb_streams)) {
        return false;
    }
    AVStream* stream = m_formatContext->streams[streamIndex];
    if (!stream || stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) return false;

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return false;

    closeSubtitleStream();
    m_subtitleCtx.stream = stream;
    m_subtitleCtx.codecContext = avcodec_alloc_context3(codec);
    if (!m_subtitleCtx.codecContext) return false;
    avcodec_parameters_to_context(m_subtitleCtx.codecContext, stream->codecpar);
    if (avcodec_open2(m_subtitleCtx.codecContext, codec, nullptr) < 0) {
        avcodec_free_context(&m_subtitleCtx.codecContext);
        m_subtitleCtx.stream = nullptr;
        return false;
    }
    m_subtitleCtx.startTime = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    logger().info("Opened subtitle stream " + std::to_string(streamIndex));
    return true;
}

void FFmpegPlayer::closeSubtitleStream() {
    if (m_subtitleCtx.codecContext) {
        avcodec_free_context(&m_subtitleCtx.codecContext);
    }
    m_subtitleCtx.stream = nullptr;
    m_subtitleCtx.startTime = 0;
    clearSubtitleBitmaps();
}

std::optional<SubtitleBitmap> FFmpegPlayer::popSubtitleBitmap() {
    std::lock_guard<std::mutex> lock(m_subtitleBitmapMutex);
    if (m_subtitleBitmapQueue.empty()) {
        return std::nullopt;
    }
    SubtitleBitmap bm = std::move(m_subtitleBitmapQueue.front());
    m_subtitleBitmapQueue.pop_front();
    return bm;
}

void FFmpegPlayer::clearSubtitleBitmaps() {
    std::lock_guard<std::mutex> lock(m_subtitleBitmapMutex);
    m_subtitleBitmapQueue.clear();
}

bool FFmpegPlayer::setAudioTrack(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_audioTracks.size())) {
        return false;
    }
    if (trackIndex == m_currentAudioTrack && m_audioCtx.stream) {
        return true; // 已是当前音轨
    }
    const TrackInfo& t = m_audioTracks[trackIndex];
    logger().info("Switching audio track to " + std::to_string(trackIndex) +
                  " (stream " + std::to_string(t.streamIndex) + ")");

    // 记住当前播放位置
    int64_t curPos = m_position.load();

    bool wasPlaying = (m_state == PlaybackState::Playing);
    if (wasPlaying) {
        pause();
    }

    bool ok = openAudioStream(t.streamIndex);
    if (!ok) {
        logger().error("Failed to open audio stream " + std::to_string(t.streamIndex));
        if (wasPlaying) play();
        return false;
    }
    m_currentAudioTrack = trackIndex;

    // 重置音频播放器
    if (m_audioPlayer) {
        m_audioPlayer->stop();
        m_audioPlayer->reset();
    }

    // seek 回原位置以重新填充缓冲
    if (wasPlaying) {
        play();
        seek(curPos);
    } else {
        seek(curPos);
    }
    return true;
}

bool FFmpegPlayer::setSubtitleTrack(int trackIndex) {
    if (trackIndex < -1 || trackIndex >= static_cast<int>(m_subtitleTracks.size())) {
        return false;
    }
    if (trackIndex == m_currentSubtitleTrack) return true;

    if (trackIndex == -1) {
        // 关闭内封字幕
        closeSubtitleStream();
        m_currentSubtitleTrack = -1;
        logger().info("Closed embedded subtitle stream");
        return true;
    }

    const TrackInfo& t = m_subtitleTracks[trackIndex];
    bool ok = openSubtitleStream(t.streamIndex);
    if (!ok) return false;
    m_currentSubtitleTrack = trackIndex;
    return true;
}

void FFmpegPlayer::setSubtitleTextCallback(SubtitleTextCallback callback) {
    m_subtitleTextCallback = std::move(callback);
}

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
