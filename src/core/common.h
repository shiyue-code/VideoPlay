#pragma once

#include <string>
#include <cstdint>
#include <functional>
#include <chrono>
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

struct MediaInfo {
    std::string source;
    std::string container;
    int64_t durationMs = 0;
    int64_t bitrate = 0;

    bool hasVideo = false;
    std::string videoCodec;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    int64_t videoBitrate = 0;
    bool hardwareDecoder = false;
    std::string hardwareDevice;

    bool hasAudio = false;
    std::string audioCodec;
    int sampleRate = 0;
    int channels = 0;
    int64_t audioBitrate = 0;
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

inline std::string formatTime(int64_t milliseconds) {
    int totalSeconds = static_cast<int>(milliseconds / 1000);
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;

    char buffer[32];
    if (hours > 0) {
        snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes, seconds);
    } else {
        snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, seconds);
    }
    return std::string(buffer);
}

template<typename... Args>
using Callback = std::function<void(Args...)>;

// High-resolution timing helper
using HRClock = std::chrono::high_resolution_clock;

inline double elapsedMs(HRClock::time_point start) {
    return std::chrono::duration<double, std::milli>(HRClock::now() - start).count();
}

// AI 相关数据结构
struct TranscriptSegment {
    int64_t startTime = 0;  // 毫秒
    int64_t endTime = 0;    // 毫秒
    std::string text;
    float confidence = 0.0f;
};

struct SearchResult {
    int64_t timestamp = 0;  // 毫秒
    std::string text;       // 匹配的文本
    std::string context;    // 上下文
    float relevance = 0.0f;
    int source = 0;         // 0=转录, 1=外挂字幕
};

struct AIAnalysisResult {
    std::string summary;
    std::vector<ChapterInfo> chapters;
    std::vector<TranscriptSegment> transcript;
    std::string language;
    int64_t analyzedAt = 0;
    bool valid = false;
};

} // namespace VideoPlay
