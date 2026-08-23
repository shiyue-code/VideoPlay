#pragma once

#include "utils/string_utils.h"

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

// 用户书签
struct Bookmark {
    int64_t timeMs = 0;  // 毫秒
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

namespace detail {

inline bool isSimplifiedChinese(std::string_view key) {
    // BCP-47 子标签 / 常见命名
    return key.find("hans") != std::string_view::npos ||
           key.find("cn") != std::string_view::npos ||
           key.find("sg") != std::string_view::npos ||
           key.find("sc") != std::string_view::npos ||
           key.find("simplified") != std::string_view::npos;
}

inline bool isTraditionalChinese(std::string_view key) {
    return key.find("hant") != std::string_view::npos ||
           key.find("tw") != std::string_view::npos ||
           key.find("hk") != std::string_view::npos ||
           key.find("mo") != std::string_view::npos ||
           key.find("tc") != std::string_view::npos ||
           key.find("traditional") != std::string_view::npos;
}

} // namespace detail

// 轨道语言代码转可读名称
// 支持 BCP-47 标签（如 zh-CN、zh-TW、zh-Hant），并优先区分简繁体
inline std::string trackLanguageName(const std::string& code) {
    if (code.empty()) return "未知";

    const std::string lower = toLower(code);
    const bool isChinese = lower == "chi" || lower == "zho" || lower == "zh" ||
                           lower == "chinese" || lower.rfind("zh-", 0) == 0;

    if (isChinese) {
        if (detail::isSimplifiedChinese(lower)) return "简体中文";
        if (detail::isTraditionalChinese(lower)) return "繁体中文";
        return "中文";
    }

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

// 根据流标题中的简繁体关键字进一步区分中文
inline std::string refineChineseName(const std::string& base, const std::string& title) {
    if (base != "中文") return base;
    if (title.empty()) return base;

    const std::string lower = toLower(title);
    // 优先检查明确的繁体关键字，再检查简体关键字
    if (lower.find("繁體") != std::string::npos ||
        lower.find("繁体") != std::string::npos ||
        lower.find("traditional") != std::string::npos) {
        return "繁体中文";
    }
    if (lower.find("簡體") != std::string::npos ||
        lower.find("简体") != std::string::npos ||
        lower.find("simplified") != std::string::npos) {
        return "简体中文";
    }
    return base;
}

// 生成轨道显示标签
inline std::string trackLabel(const TrackInfo& t, int index) {
    std::string lang = trackLanguageName(t.language);
    if (lang == "中文") {
        lang = refineChineseName(lang, t.title);
    }
    std::string label = std::to_string(index) + ". " + lang;
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

struct VideoFilterConfig {
    bool enabled = false;
    float brightness = 0.0f;   // -1.0 ~ 1.0
    float contrast = 1.0f;     //  0.0 ~ 2.0
    float saturation = 1.0f;   //  0.0 ~ 3.0
    float hue = 0.0f;          // -180 ~ 180
    float gamma = 1.0f;        //  0.1 ~ 10.0

    bool isDefault() const {
        return !enabled ||
               (brightness == 0.0f && contrast == 1.0f &&
                saturation == 1.0f && hue == 0.0f && gamma == 1.0f);
    }
};

struct VideoTransform {
    int rotation = 0;          // 0 / 90 / 180 / 270
    bool flipHorizontal = false;
    bool flipVertical = false;
    int cropPercent = 0;       // 0 / 10 / 20，中心裁剪

    bool isDefault() const {
        return rotation == 0 && !flipHorizontal && !flipVertical && cropPercent == 0;
    }
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

// 判断路径/URL 是否为网络串流
inline bool isNetworkUrl(const std::string& path) {
    const std::string lower = toLower(path);
    return startsWith(lower, "http://") || startsWith(lower, "https://") ||
           startsWith(lower, "rtsp://") || startsWith(lower, "rtmp://");
}

struct AIAnalysisResult {
    std::string summary;
    std::vector<ChapterInfo> chapters;
    std::vector<TranscriptSegment> transcript;
    std::string language;
    int64_t analyzedAt = 0;
    bool valid = false;
};

} // namespace VideoPlay
