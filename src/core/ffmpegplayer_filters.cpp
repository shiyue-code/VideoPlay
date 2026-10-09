#include "core/ffmpegplayer.h"
#include "core/ffmpegplayer_internal.h"

#include <algorithm>
#include <sstream>

namespace VideoPlay {

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

bool FFmpegPlayer::isHdrFrame(const AVFrame* frame) const {
    if (!frame) return false;

    AVColorTransferCharacteristic trc = frame->color_trc;
    if (trc == AVCOL_TRC_UNSPECIFIED && m_videoCtx.codecContext) {
        trc = m_videoCtx.codecContext->color_trc;
    }
    if (trc == AVCOL_TRC_UNSPECIFIED && m_videoCtx.stream && m_videoCtx.stream->codecpar) {
        trc = m_videoCtx.stream->codecpar->color_trc;
    }

    if (trc == AVCOL_TRC_SMPTE2084 || trc == AVCOL_TRC_ARIB_STD_B67) {
        return true;
    }

    AVColorPrimaries pri = frame->color_primaries;
    if (pri == AVCOL_PRI_UNSPECIFIED && m_videoCtx.codecContext) {
        pri = m_videoCtx.codecContext->color_primaries;
    }
    if (pri == AVCOL_PRI_UNSPECIFIED && m_videoCtx.stream && m_videoCtx.stream->codecpar) {
        pri = m_videoCtx.stream->codecpar->color_primaries;
    }

    if (av_frame_get_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA) ||
        av_frame_get_side_data(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL)) {
        return true;
    }

    if (pri == AVCOL_PRI_BT2020) {
        AVPixelFormat fmt = static_cast<AVPixelFormat>(frame->format);
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(fmt);
        if (desc && desc->comp[0].depth >= 10) {
            return true;
        }
    }

    return false;
}

std::string FFmpegPlayer::buildVideoFilterDescription(const AVFrame* frame) const {
    const bool needDeint = m_videoFilterConfig.deinterlace != DeinterlaceMode::Off;
    const bool needEq = m_videoFilterConfig.enabled && !m_videoFilterConfig.eqIsIdentity();

    bool needToneMap = false;
    if (m_videoFilterConfig.hdrToneMap == HdrToneMapMode::On) {
        needToneMap = true;
    } else if (m_videoFilterConfig.hdrToneMap == HdrToneMapMode::Auto) {
        needToneMap = isHdrFrame(frame);
    }

    if (!needDeint && !needEq && !needToneMap) {
        return {};
    }

    std::ostringstream desc;
    if (needToneMap) {
        AVColorTransferCharacteristic trc = frame ? frame->color_trc : AVCOL_TRC_UNSPECIFIED;
        if (trc == AVCOL_TRC_UNSPECIFIED && m_videoCtx.codecContext) {
            trc = m_videoCtx.codecContext->color_trc;
        }
        if (trc == AVCOL_TRC_UNSPECIFIED && m_videoCtx.stream && m_videoCtx.stream->codecpar) {
            trc = m_videoCtx.stream->codecpar->color_trc;
        }

        std::string trcStr = (trc == AVCOL_TRC_ARIB_STD_B67) ? "arib-std-b67" : "smpte2084";
        std::string primariesStr = "bt2020";
        std::string colorspaceStr = "bt2020nc";

        std::string algoStr = "hable";
        switch (m_videoFilterConfig.hdrAlgorithm) {
            case HdrToneMapAlgorithm::Mobius:   algoStr = "mobius"; break;
            case HdrToneMapAlgorithm::Reinhard: algoStr = "reinhard"; break;
            case HdrToneMapAlgorithm::Hable:
            default:                            algoStr = "hable"; break;
        }

        desc << "setparams=color_primaries=" << primariesStr
             << ":color_trc=" << trcStr
             << ":colorspace=" << colorspaceStr;
        desc << ",zscale=transfer=linear:npl=100,format=gbrpf32le";
        desc << ",tonemap=tonemap=" << algoStr << ":desat=0";
        desc << ",zscale=transfer=bt709:matrix=bt709:primaries=bt709:range=limited,format=yuv420p";
    } else {
        desc << "format=pix_fmts=yuv420p";
    }

    if (needDeint) {
        const bool hasBwdif = avfilter_get_by_name("bwdif") != nullptr;
        switch (m_videoFilterConfig.deinterlace) {
            case DeinterlaceMode::Auto:
                if (hasBwdif) {
                    desc << ",bwdif=mode=send_frame:parity=auto:deint=interlaced";
                } else {
                    desc << ",yadif=mode=0:parity=-1:deint=1";
                }
                break;
            case DeinterlaceMode::Yadif:
                desc << ",yadif=mode=0:parity=-1:deint=0";
                break;
            case DeinterlaceMode::Bwdif:
                if (hasBwdif) {
                    desc << ",bwdif=mode=send_frame:parity=auto:deint=all";
                } else {
                    desc << ",yadif=mode=0:parity=-1:deint=0";
                }
                break;
            case DeinterlaceMode::Off:
            default:
                break;
        }
    }

    if (needEq) {
        desc << ",eq=brightness=" << m_videoFilterConfig.brightness
             << ":contrast=" << m_videoFilterConfig.contrast
             << ":saturation=" << m_videoFilterConfig.saturation
             << ":gamma=" << m_videoFilterConfig.gamma;
        desc << ",hue=h=" << m_videoFilterConfig.hue;
    }
    return desc.str();
}

bool FFmpegPlayer::ensureVideoFilterGraph(AVFrame* frame) {
    if (!frame) return false;

    std::lock_guard<std::mutex> lock(m_videoFilterMutex);
    std::string description = buildVideoFilterDescription(frame);

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

    std::ostringstream args;
    args << "video_size=" << width << "x" << height
         << ":pix_fmt=" << static_cast<int>(format)
         << ":time_base=" << (m_videoCtx.stream ? m_videoCtx.stream->time_base.num : 1)
         << "/" << (m_videoCtx.stream ? m_videoCtx.stream->time_base.den : 1)
         << ":pixel_aspect=" << (frame->sample_aspect_ratio.num ? frame->sample_aspect_ratio.num : 1)
         << "/" << (frame->sample_aspect_ratio.den ? frame->sample_aspect_ratio.den : 1);

    AVFilterContext* src = nullptr;
    AVFilterContext* sink = nullptr;
    int ret = avfilter_graph_create_filter(&src, bufferSrc, "in", args.str().c_str(), nullptr, graph);
    if (ret < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_make_error_string(errBuf, sizeof(errBuf), ret);
        logger().warning(std::string("Failed to create video filter source: ") + errBuf + " args=" + args.str());
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

    int ret = av_buffersrc_add_frame_flags(m_videoFilterSrc, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
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

    // 保留原始 pts 等时钟与元数据，但保留滤镜产物的色彩属性
    auto outTrc = filtered->color_trc;
    auto outPri = filtered->color_primaries;
    auto outSpc = filtered->colorspace;
    auto outRange = filtered->color_range;
    av_frame_copy_props(filtered, frame);
    if (outTrc != AVCOL_TRC_UNSPECIFIED) filtered->color_trc = outTrc;
    if (outPri != AVCOL_PRI_UNSPECIFIED) filtered->color_primaries = outPri;
    if (outSpc != AVCOL_SPC_UNSPECIFIED) filtered->colorspace = outSpc;
    if (outRange != AVCOL_RANGE_UNSPECIFIED) filtered->color_range = outRange;

    // HDR 色调映射后的产物已转换为 BT.709 Limited Range，显式标记以确保 sws_scale 正确扩展黑位（16->0），避免画面发白
    bool isToneMapped = (m_videoFilterConfig.hdrToneMap == HdrToneMapMode::On) ||
                        (m_videoFilterConfig.hdrToneMap == HdrToneMapMode::Auto && isHdrFrame(frame));
    if (isToneMapped) {
        filtered->color_trc = AVCOL_TRC_BT709;
        filtered->color_primaries = AVCOL_PRI_BT709;
        filtered->colorspace = AVCOL_SPC_BT709;
        filtered->color_range = AVCOL_RANGE_MPEG;
    }
    return filtered;
}


} // namespace VideoPlay
