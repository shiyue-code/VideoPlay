#include "core/audioclock.h"

#include <algorithm>

namespace VideoPlay {

void AudioClock::reset() {
    m_segments.clear();
    m_totalFrames = 0;
}

void AudioClock::push(int64_t frames, double startMs, double msPerFrame) {
    if (frames <= 0) {
        return;
    }
    m_segments.push_back({frames, startMs, msPerFrame});
    m_totalFrames += frames;
}

double AudioClock::positionMs(int64_t queuedFrames) const {
    if (m_segments.empty()) {
        return -1.0;
    }
    int64_t consumed = std::max<int64_t>(
        0, m_totalFrames - std::max<int64_t>(0, queuedFrames));
    for (const Segment& seg : m_segments) {
        if (consumed < seg.frames) {
            return seg.startMs + static_cast<double>(consumed) * seg.msPerFrame;
        }
        consumed -= seg.frames;
    }
    // 全部播完（欠载）：停在最后一段末尾
    const Segment& last = m_segments.back();
    return last.startMs + static_cast<double>(last.frames) * last.msPerFrame;
}

void AudioClock::prune(int64_t queuedFrames) {
    int64_t consumed = m_totalFrames - std::max<int64_t>(0, queuedFrames);
    while (m_segments.size() > 1 && consumed >= m_segments.front().frames) {
        consumed -= m_segments.front().frames;
        m_totalFrames -= m_segments.front().frames;
        m_segments.pop_front();
    }
}

bool AudioClock::empty() const {
    return m_segments.empty();
}

} // namespace VideoPlay
