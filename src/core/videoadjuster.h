#ifndef VIDEOADJUSTER_H
#define VIDEOADJUSTER_H

#include "core/common.h"

#include <string>
#include <vector>

struct AVFrame;
struct AVFilterContext;
struct AVFilterGraph;
struct SwsContext;

namespace VideoPlay {

// 画面调节（eq/hue）并转换为 BGRA。
// 实例非线程安全：解码线程与主线程各持一个，互不共享
class VideoAdjuster
{
public:
    VideoAdjuster() = default;
    ~VideoAdjuster();

    VideoAdjuster(const VideoAdjuster&) = delete;
    VideoAdjuster& operator=(const VideoAdjuster&) = delete;

    // base：软件帧（已完成去隔行/HDR 映射），不修改 base。
    // 失败返回 data 为空的 VideoFrame；pts 不填
    VideoFrame convert(const AVFrame* base, const VideoFilterConfig& config);
    // 释放滤镜图与 sws 上下文
    void reset();

    // "" 表示无需滤镜（!enabled 或 eqIsIdentity()）
    static std::string buildAdjustDescription(const VideoFilterConfig& config);

private:
    bool ensureFilterGraph(const AVFrame* base, const std::string& description);
    void cleanupFilterGraph();
    VideoFrame convertUnfiltered(const AVFrame* base);

    AVFilterGraph* m_filterGraph = nullptr;
    AVFilterContext* m_filterSrc = nullptr;
    AVFilterContext* m_filterSink = nullptr;
    int m_filterWidth = 0;
    int m_filterHeight = 0;
    int m_filterFormat = -1;
    std::string m_filterDescription;

    SwsContext* m_swsContext = nullptr;
    int m_lastWidth = 0;
    int m_lastHeight = 0;
    int m_lastFormat = -1;
    int m_lastColorSpace = -1;
    int m_lastSrcRange = -1;
    std::vector<uint8_t> m_swsBuffer;
    int m_swsStride = 0;
};

} // namespace VideoPlay

#endif // VIDEOADJUSTER_H
