#pragma once

#include <string>
#include <cstdint>
#include <functional>
#include <chrono>
#include <cstdio>
#include <vector>

namespace VideoPlay {

// VideoFrame definition (shared across the project)
struct VideoFrame {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    int64_t pts = 0;
};

// 图形字幕（PGS/DVB）位图
struct SubtitleBitmap {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int64_t startMs = 0;
    int64_t endMs = 0;
    std::vector<uint32_t> pixels; // RGBA，每像素 4 字节
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

// 音轨/字幕轨信息
struct TrackInfo {
    int streamIndex = -1;       // FFmpeg 流索引
    std::string language;       // 语言代码（如 "chi", "eng"）
    std::string title;          // 流标题（来自 metadata）
    std::string codecName;      // 编码器名称
    bool isDefault = false;     // 是否为默认流
    bool isForced = false;      // 字幕：是否为强制字幕
    // 音轨特有
    int channels = 0;
    int sampleRate = 0;
    // 字幕特有
    std::string subtitleType;   // "text" / "ass" / "pgs" 等
};

// 轨道语言代码转可读名称
inline std::string trackLanguageName(const std::string& code) {
    if (code.empty()) return "未知";
    if (code == "chi" || code == "zho" || code == "zh" || code == "chinese") return "中文";
    if (code == "eng" || code == "en" || code == "english") return "英语";
    if (code == "jpn" || code == "ja" || code == "japanese") return "日语";
    if (code == "kor" || code == "ko" || code == "korean") return "韩语";
    if (code == "fra" || code == "fre" || code == "fr" || code == "french") return "法语";
    if (code == "deu" || code == "ger" || code == "de" || code == "german") return "德语";
    if (code == "spa" || code == "es" || code == "spanish") return "西班牙语";
    if (code == "rus" || code == "ru" || code == "russian") return "俄语";
    if (code == "und") return "未指定";
    return code;
}

// 生成轨道显示标签
inline std::string trackLabel(const TrackInfo& t, int index) {
    std::string label = std::to_string(index) + ". " + trackLanguageName(t.language);
    if (!t.title.empty()) {
        label += " (" + t.title + ")";
    }
    if (t.isForced) {
        label += " [强制]";
    }
    if (t.channels > 0) {
        label += " [" + std::to_string(t.channels) + "ch]";
    }
    return label;
}

enum class SourceType {
    LocalFile = 0,
    NetworkStream = 1
};

enum class NetworkState {
    Idle = 0,
    Connecting,
    Buffering,
    Playing,
    Reconnecting,
    Failed
};

inline const char* networkStateText(NetworkState state) {
    switch (state) {
        case NetworkState::Idle:         return "";
        case NetworkState::Connecting:   return "正在连接";
        case NetworkState::Buffering:    return "正在缓冲";
        case NetworkState::Playing:      return "";
        case NetworkState::Reconnecting: return "正在重连";
        case NetworkState::Failed:       return "网络错误";
    }
    return "";
}

struct MediaInfo {
    std::string source;
    SourceType sourceType = SourceType::LocalFile;
    std::string container;
    int64_t durationMs = 0;
    int64_t bitrate = 0;

    bool hasVideo = false;
    std::string videoCodec;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    int64_t videoBitrate = 0;
    bool hardwareDecoderEnabled = true;
    bool hardwareDecoder = false;
    std::string hardwareDevice;

    bool hasAudio = false;
    std::string audioCodec;
    int sampleRate = 0;
    int channels = 0;
    int64_t audioBitrate = 0;
};

enum class AudioFilterPreset {
    Off = 0,
    Voice = 1,
    Bass = 2,
    Night = 3
};

struct EQBand {
    double frequency = 1000.0;
    double width = 1.0;
    double gainDb = 0.0;
};

struct AudioFilterConfig {
    bool enabled = false;
    AudioFilterPreset preset = AudioFilterPreset::Off;
    double preampDb = 0.0;
    bool limiterEnabled = false;
    bool dynamicNormalizerEnabled = false;
    std::vector<EQBand> eqBands;
};

inline const char* audioFilterPresetName(AudioFilterPreset preset) {
    switch (preset) {
        case AudioFilterPreset::Off:   return "关闭";
        case AudioFilterPreset::Voice: return "语音增强";
        case AudioFilterPreset::Bass:  return "低音增强";
        case AudioFilterPreset::Night: return "夜间模式";
    }
    return "关闭";
}

inline AudioFilterConfig audioFilterConfigForPreset(AudioFilterPreset preset) {
    AudioFilterConfig config;
    config.preset = preset;
    config.enabled = preset != AudioFilterPreset::Off;

    switch (preset) {
        case AudioFilterPreset::Voice:
            config.eqBands = {
                {120.0, 0.8, -4.0},
                {2500.0, 1.0, 4.0},
                {6000.0, 1.0, 2.0}
            };
            config.limiterEnabled = true;
            break;
        case AudioFilterPreset::Bass:
            config.eqBands = {
                {80.0, 1.0, 5.0},
                {160.0, 1.0, 3.0}
            };
            config.limiterEnabled = true;
            break;
        case AudioFilterPreset::Night:
            config.preampDb = -3.0;
            config.dynamicNormalizerEnabled = true;
            config.limiterEnabled = true;
            break;
        case AudioFilterPreset::Off:
        default:
            break;
    }

    return config;
}

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
