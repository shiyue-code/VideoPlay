#pragma once

#include <string>
#include <cstdint>
#include <functional>
#include <vector>

namespace VideoPlay {

// VideoFrame definition (shared across the project)
struct VideoFrame {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    int64_t pts = 0;
};

enum class PlaybackState {
    Stopped = 0,
    Playing = 1,
    Paused = 2
};

enum class AspectMode {
    Original = 0,   // 原始比例
    R16_9 = 1,      // 16:9
    R4_3 = 2,       // 4:3
    FillWindow = 3  // 铺满窗口
};

struct ChapterInfo {
    int64_t startTime = 0;  // 毫秒
    int64_t endTime = 0;    // 毫秒
    std::string title;
};

enum class PlaybackSpeed {
    Speed_0_25 = 0,
    Speed_0_5,
    Speed_0_75,
    Speed_1_0,
    Speed_1_25,
    Speed_1_5,
    Speed_2_0,
    Speed_4_0
};

inline double playbackSpeedToDouble(PlaybackSpeed speed) {
    switch (speed) {
        case PlaybackSpeed::Speed_0_25: return 0.25;
        case PlaybackSpeed::Speed_0_5:  return 0.5;
        case PlaybackSpeed::Speed_0_75: return 0.75;
        case PlaybackSpeed::Speed_1_0:  return 1.0;
        case PlaybackSpeed::Speed_1_25: return 1.25;
        case PlaybackSpeed::Speed_1_5:  return 1.5;
        case PlaybackSpeed::Speed_2_0:  return 2.0;
        case PlaybackSpeed::Speed_4_0:  return 4.0;
    }
    return 1.0;
}

inline PlaybackSpeed doubleToPlaybackSpeed(double rate) {
    if (rate <= 0.3) return PlaybackSpeed::Speed_0_25;
    if (rate <= 0.6) return PlaybackSpeed::Speed_0_5;
    if (rate <= 0.8) return PlaybackSpeed::Speed_0_75;
    if (rate <= 1.1) return PlaybackSpeed::Speed_1_0;
    if (rate <= 1.35) return PlaybackSpeed::Speed_1_25;
    if (rate <= 1.75) return PlaybackSpeed::Speed_1_5;
    if (rate <= 3.0) return PlaybackSpeed::Speed_2_0;
    return PlaybackSpeed::Speed_4_0;
}

inline std::string formatTime(int64_t ms) {
    int64_t seconds = ms / 1000;
    int64_t minutes = seconds / 60;
    int64_t hours = minutes / 60;
    seconds %= 60;
    minutes %= 60;

    char buffer[16];
    if (hours > 0) {
        snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld", 
                 static_cast<long long>(hours),
                 static_cast<long long>(minutes), 
                 static_cast<long long>(seconds));
    } else {
        snprintf(buffer, sizeof(buffer), "%lld:%02lld",
                 static_cast<long long>(minutes),
                 static_cast<long long>(seconds));
    }
    return std::string(buffer);
}

template<typename... Args>
using Callback = std::function<void(Args...)>;

} // namespace VideoPlay
