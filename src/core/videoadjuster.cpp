#include "core/videoadjuster.h"
#include "utils/logger.h"

#include <locale>
#include <memory>
#include <sstream>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto logger = Logger::get("videoadjuster");
    return *logger;
}
}

VideoAdjuster::~VideoAdjuster()
{
    reset();
}

void VideoAdjuster::reset()
{
    cleanupFilterGraph();
    if (m_swsContext)
    {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }
    m_lastFormat = -1;
}

std::string VideoAdjuster::buildAdjustDescription(const VideoFilterConfig& config)
{
    if (!config.enabled || config.eqIsIdentity())
    {
        return {};
    }

    std::ostringstream desc;
    desc.imbue(std::locale::classic());
    desc << "format=pix_fmts=yuv420p"
         << ",eq=brightness=" << config.brightness
         << ":contrast=" << config.contrast
         << ":saturation=" << config.saturation
         << ":gamma=" << config.gamma
         << ",hue=h=" << config.hue;
    return desc.str();
}

void VideoAdjuster::cleanupFilterGraph()
{
    if (m_filterGraph)
    {
        avfilter_graph_free(&m_filterGraph);
    }
    m_filterSrc = nullptr;
    m_filterSink = nullptr;
    m_filterWidth = 0;
    m_filterHeight = 0;
    m_filterFormat = -1;
    m_filterDescription.clear();
}

bool VideoAdjuster::ensureFilterGraph(const AVFrame* base,
                                      const std::string& description)
{
    const int width = base->width;
    const int height = base->height;
    const int format = base->format;

    if (m_filterGraph &&
        m_filterWidth == width &&
        m_filterHeight == height &&
        m_filterFormat == format &&
        m_filterDescription == description)
    {
        return true;
    }

    cleanupFilterGraph();

    AVFilterGraph* graph = avfilter_graph_alloc();
    if (!graph)
    {
        logger().warning("Failed to allocate adjust filter graph");
        return false;
    }

    const AVFilter* bufferSrc = avfilter_get_by_name("buffer");
    const AVFilter* bufferSink = avfilter_get_by_name("buffersink");
    if (!bufferSrc || !bufferSink)
    {
        logger().warning("Required video filter endpoints are unavailable");
        avfilter_graph_free(&graph);
        return false;
    }

    std::ostringstream args;
    args.imbue(std::locale::classic());
    args << "video_size=" << width << "x" << height
         << ":pix_fmt=" << format
         << ":time_base=1/1000"
         << ":pixel_aspect="
         << (base->sample_aspect_ratio.num ? base->sample_aspect_ratio.num : 1)
         << "/"
         << (base->sample_aspect_ratio.den ? base->sample_aspect_ratio.den : 1);

    AVFilterContext* src = nullptr;
    int ret = avfilter_graph_create_filter(&src, bufferSrc, "in",
                                           args.str().c_str(), nullptr, graph);
    if (ret < 0)
    {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_make_error_string(errBuf, sizeof(errBuf), ret);
        logger().warning(std::string("Failed to create adjust filter source: ") +
                         errBuf + " args=" + args.str());
        avfilter_graph_free(&graph);
        return false;
    }

    AVFilterContext* sink = nullptr;
    ret = avfilter_graph_create_filter(&sink, bufferSink, "out",
                                       nullptr, nullptr, graph);
    if (ret < 0)
    {
        logger().warning("Failed to create adjust filter sink");
        avfilter_graph_free(&graph);
        return false;
    }

    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    if (!outputs || !inputs)
    {
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

    ret = avfilter_graph_parse_ptr(graph, description.c_str(),
                                   &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (ret < 0)
    {
        logger().warning("Failed to parse adjust filter graph: " + description);
        avfilter_graph_free(&graph);
        return false;
    }

    ret = avfilter_graph_config(graph, nullptr);
    if (ret < 0)
    {
        logger().warning("Failed to configure adjust filter graph: " + description);
        avfilter_graph_free(&graph);
        return false;
    }

    m_filterGraph = graph;
    m_filterSrc = src;
    m_filterSink = sink;
    m_filterWidth = width;
    m_filterHeight = height;
    m_filterFormat = format;
    m_filterDescription = description;
    return true;
}

VideoFrame VideoAdjuster::convert(const AVFrame* base,
                                  const VideoFilterConfig& config)
{
    VideoFrame result;
    if (!base || !base->data[0] || base->width <= 0 || base->height <= 0)
    {
        return result;
    }

    const AVFrame* filteredBase = base;
    auto frameDeleter = [](AVFrame* value) {
        if (value)
        {
            av_frame_free(&value);
        }
    };
    std::unique_ptr<AVFrame, decltype(frameDeleter)> filteredFrame(nullptr, frameDeleter);

    const std::string description = buildAdjustDescription(config);
    if (!description.empty() && ensureFilterGraph(base, description))
    {
        int ret = av_buffersrc_add_frame_flags(m_filterSrc, const_cast<AVFrame*>(base),
                                               AV_BUFFERSRC_FLAG_KEEP_REF);
        if (ret < 0)
        {
            logger().warning("Failed to feed adjust filter graph; using unfiltered frame");
        }
        else
        {
            AVFrame* filtered = av_frame_alloc();
            if (filtered)
            {
                ret = av_buffersink_get_frame(m_filterSink, filtered);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    av_frame_free(&filtered);
                }
                else if (ret < 0)
                {
                    logger().warning(
                        "Failed to read adjust filter graph output; using unfiltered frame");
                    av_frame_free(&filtered);
                }
                else
                {
                    // 保留原帧的时钟与色彩元数据（eq/hue 不改变色彩属性）
                    av_frame_copy_props(filtered, base);
                    filteredFrame.reset(filtered);
                    filteredBase = filtered;
                }
            }
        }
    }

    return convertUnfiltered(filteredBase);
}

VideoFrame VideoAdjuster::convertUnfiltered(const AVFrame* base)
{
    VideoFrame result;

    const int width = base->width;
    const int height = base->height;
    const AVPixelFormat srcFormat = static_cast<AVPixelFormat>(base->format);
    // 使用 BGRA 以匹配 SDL 纹理格式 (Windows 小端字节序)
    const AVPixelFormat dstFormat = AV_PIX_FMT_BGRA;

    // Determine color space and range from frame metadata
    int colorSpace = SWS_CS_DEFAULT;
    int srcRange = 0;  // 0 = limited (16-235), 1 = full (0-255)

    // Check frame-level color range first
    if (base->color_range == AVCOL_RANGE_MPEG)
    {
        srcRange = 0;  // Limited range (TV range)
    }
    else if (base->color_range == AVCOL_RANGE_JPEG)
    {
        srcRange = 1;  // Full range (PC range)
    }
    else
    {
        // Default: most video files use limited range
        srcRange = 0;
    }

    // Determine color space (BT.601 vs BT.709)；帧未指定时由调用方在生成
    // base 前回填 codec 的 colorspace，这里不再访问 codecContext
    if (base->colorspace == AVCOL_SPC_BT709)
    {
        colorSpace = SWS_CS_ITU709;
    }
    else if (base->colorspace == AVCOL_SPC_BT470BG ||
             base->colorspace == AVCOL_SPC_SMPTE170M)
    {
        colorSpace = SWS_CS_ITU601;
    }

    // HD videos (>= 1280 width) typically use BT.709
    if (colorSpace == SWS_CS_DEFAULT && width >= 1280)
    {
        colorSpace = SWS_CS_ITU709;
    }

    if (!m_swsContext ||
        m_lastWidth != width ||
        m_lastHeight != height ||
        m_lastFormat != srcFormat ||
        m_lastColorSpace != colorSpace ||
        m_lastSrcRange != srcRange)
    {
        if (m_swsContext)
        {
            sws_freeContext(m_swsContext);
        }

        // Fast bilinear only for real-time performance
        int flags = SWS_FAST_BILINEAR;

        m_swsContext = sws_getContext(
            width, height, srcFormat,
            width, height, dstFormat,
            flags, nullptr, nullptr, nullptr);

        // Try to set color space details - this may not work on all FFmpeg builds
        // If it fails, the video might appear darker than expected
        if (m_swsContext)
        {
            const int* coefs = sws_getCoefficients(colorSpace);
            int dstRange = 1;  // Full range output (0-255)

            // Note: Some FFmpeg builds ignore these settings
            sws_setColorspaceDetails(m_swsContext,
                                     coefs, srcRange,
                                     coefs, dstRange,
                                     0, 1 << 16, 1 << 16);

            logger().debug("SWS context created: colorspace=" +
                           std::to_string(colorSpace) +
                           " srcRange=" + std::to_string(srcRange));
        }

        m_lastWidth = width;
        m_lastHeight = height;
        m_lastFormat = srcFormat;
        m_lastColorSpace = colorSpace;
        m_lastSrcRange = srcRange;
    }

    if (!m_swsContext)
    {
        return result;
    }

    const int dstBytesPerRow = width * 4;
    const int dstBufferSize = dstBytesPerRow * height;

    // Reuse persistent buffer instead of allocating every frame
    if (static_cast<int>(m_swsBuffer.size()) < dstBufferSize)
    {
        m_swsBuffer.resize(dstBufferSize);
    }
    m_swsStride = dstBytesPerRow;

    uint8_t* dstData[4] = { m_swsBuffer.data(), nullptr, nullptr, nullptr };
    int dstStride[4] = { dstBytesPerRow, 0, 0, 0 };

    int swsRet = sws_scale(m_swsContext, base->data, base->linesize,
                           0, height, dstData, dstStride);
    if (swsRet <= 0)
    {
        logger().warning("sws_scale failed or returned 0 lines");
        return result;
    }

    result.width = width;
    result.height = height;
    // Copy buffer data to preserve swsBuffer for reuse.
    // This avoids reallocating ~8MB per frame when move leaves swsBuffer empty.
    result.data.assign(m_swsBuffer.data(), m_swsBuffer.data() + dstBufferSize);

    return result;
}

} // namespace VideoPlay
