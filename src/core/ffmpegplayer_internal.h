#pragma once

// FFmpegPlayer 拆分翻译单元共享的内部助手。
// 名字与原 ffmpegplayer.cpp 匿名命名空间保持一致，拆分时不改调用点。
// 只放实现细节，不作为公共接口（公共接口仍在 ffmpegplayer.h）。

#include "utils/logger.h"

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/rational.h>
}

namespace VideoPlay {

inline Logger& logger() {
    static auto logger = Logger::get("ffmpeg");
    return *logger;
}

inline constexpr int AUDIO_QUEUE_SIZE = 100;

inline double rationalToDouble(AVRational value) {
    if (value.num == 0 || value.den == 0) {
        return 0.0;
    }
    return static_cast<double>(value.num) / static_cast<double>(value.den);
}

inline std::string codecDisplayName(const AVCodec* codec, AVCodecID codecId) {
    if (codec && codec->long_name) {
        return codec->long_name;
    }
    const char* name = avcodec_get_name(codecId);
    return name ? std::string(name) : std::string("Unknown");
}

inline std::string hardwareDeviceName(AVHWDeviceType type) {
    const char* name = av_hwdevice_get_type_name(type);
    return name ? std::string(name) : std::string();
}

} // namespace VideoPlay
