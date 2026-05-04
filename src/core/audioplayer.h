#pragma once

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

namespace VideoPlay {

struct AudioFormat {
    int sampleRate = 48000;
    int channels = 2;
    int bitsPerSample = 32;
};

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    bool initialize(const AudioFormat& format);
    void shutdown();

    void play();
    void pause();
    void stop();
    void reset();

    void setVolume(int volume);
    int volume() const;
    void setMuted(bool muted);
    bool isMuted() const;
    void setPlaybackSpeed(double speed);
    double playbackSpeed() const;

    void enqueue(const std::vector<float>& audioData);
    void enqueue(const float* data, size_t sampleCount);

    bool isPlaying() const;
    bool isPaused() const;
    size_t queueSize() const;
    int64_t playedMs() const;
    int64_t queuedMs() const;

    using DataCallback = std::function<void(float* output, size_t frameCount)>;
    void setDataCallback(DataCallback callback);

private:
    using Clock = std::chrono::high_resolution_clock;

    mutable std::mutex m_mutex;

    SDL_AudioStream* m_stream = nullptr;
    SDL_AudioDeviceID m_deviceId = 0;
    bool m_initialized = false;

    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_paused{false};
    std::atomic<int> m_volume{100};
    std::atomic<bool> m_muted{false};
    std::atomic<double> m_playbackSpeed{1.0};

    AudioFormat m_format;
    DataCallback m_dataCallback;

    // Apply volume/mute to SDL stream gain
    void applyStreamGain();

    // Timing state for playedMs()
    double m_basePlayedMs = 0.0;
    Clock::time_point m_timerStart;
    bool m_timerRunning = false;
};

} // namespace VideoPlay
