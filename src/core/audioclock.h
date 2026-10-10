#pragma once

#include <cstdint>
#include <deque>

namespace VideoPlay {

// 按已入队采样推算当前播放到的媒体时间：
// 每段记录起始媒体时间和每帧对应的媒体时长（含倍速）
class AudioClock {
public:
    void reset();
    void push(int64_t frames, double startMs, double msPerFrame); // frames<=0 忽略
    // queuedFrames：尚未被设备取走的帧数。无数据返回 -1；全部播完（欠载）时停在最后一段末尾
    double positionMs(int64_t queuedFrames) const;
    void prune(int64_t queuedFrames);   // 丢弃已完整播完的段（至少保留最后一段）
    bool empty() const;

private:
    struct Segment {
        int64_t frames;
        double startMs;
        double msPerFrame;
    };
    std::deque<Segment> m_segments;
    int64_t m_totalFrames = 0;   // 当前 m_segments 内总帧数
};

} // namespace VideoPlay
