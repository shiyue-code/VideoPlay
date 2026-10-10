#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>

#include <SDL3/SDL.h>

#include "core/audioclock.h"

namespace VideoPlay {

struct AudioFormat {
    int sampleRate = 48000;
    int channels = 2;
    int bitsPerSample = 32;
};

struct AudioOutputDevice {
    SDL_AudioDeviceID id = 0;
    std::string name;
    bool isDefault = false;
};

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    static std::vector<AudioOutputDevice> listPlaybackDevices();

    bool initialize(const AudioFormat& format);
    bool initialize(const AudioFormat& format, SDL_AudioDeviceID deviceId);
    void shutdown();

    void play();
    void pause();
    void stop();
    void reset();

    void setVolume(int volume);
    int volume() const;
    void setMuted(bool muted);
    bool isMuted() const;

    // startMs：这段数据首帧对应的媒体时间（毫秒）
    // msPerFrame：每个输出帧对应的媒体时长（已含倍速）
    void enqueue(const float* data, size_t sampleCount, double startMs, double msPerFrame);

    bool isPlaying() const;
    bool isPaused() const;
    size_t queueSize() const;
    // 当前播放到的媒体时间；时钟为空（尚未入队数据）时返回 -1
    double mediaPositionMs() const;
    int64_t queuedMs() const;

    using DataCallback = std::function<void(float* output, size_t frameCount)>;
    void setDataCallback(DataCallback callback);

private:
    mutable std::mutex m_mutex;

    SDL_AudioStream* m_stream = nullptr;
    SDL_AudioDeviceID m_deviceId = 0;
    SDL_AudioDeviceID m_preferredDeviceId = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    bool m_initialized = false;

    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_paused{false};
    std::atomic<int> m_volume{100};
    std::atomic<bool> m_muted{false};

    AudioFormat m_format;
    DataCallback m_dataCallback;
    AudioClock m_clock;

    // Apply volume/mute to SDL stream gain
    void applyStreamGain();
    // 调用方需持有 m_mutex；返回设备尚未取走的帧数
    int64_t queuedFramesLocked() const;
};

} // namespace VideoPlay
