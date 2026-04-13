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

#include "miniaudio/miniaudio.h"

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

    using DataCallback = std::function<void(float* output, size_t frameCount)>;
    void setDataCallback(DataCallback callback);

private:
    static void dataCallbackWrapper(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    void processAudio(float* output, size_t frameCount);

    mutable std::mutex m_mutex;
    
    ma_device m_device;
    ma_device_config m_deviceConfig;
    bool m_initialized;
    
    // Flat buffer with read offset to avoid vector::erase in audio callback
    std::vector<float> m_buffer;
    size_t m_readOffset = 0;
    
    std::atomic<bool> m_playing;
    std::atomic<bool> m_paused;
    std::atomic<uint64_t> m_framesPlayed{0};
    std::atomic<double> m_playedMsAtSpeedChange{0.0};
    std::atomic<uint64_t> m_framesPlayedAtSpeedChange{0};
    std::atomic<int> m_volume;
    std::atomic<bool> m_muted;
    std::atomic<double> m_playbackSpeed{1.0};
    
    AudioFormat m_format;
    DataCallback m_dataCallback;
    std::chrono::high_resolution_clock::time_point m_lastProcessTime;
};

} // namespace VideoPlay
