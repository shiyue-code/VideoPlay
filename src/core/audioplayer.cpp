#define MINIAUDIO_IMPLEMENTATION
#include "core/audioplayer.h"
#include "utils/logger.h"
#include <chrono>
#include <cstring>

namespace {
    using Clock = std::chrono::high_resolution_clock;
    inline double elapsedMs(Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
}

namespace VideoPlay {

AudioPlayer::AudioPlayer()
    : m_initialized(false)
    , m_playing(false)
    , m_paused(false)
    , m_volume(100)
    , m_muted(false) {
    std::memset(&m_device, 0, sizeof(m_device));
    std::memset(&m_deviceConfig, 0, sizeof(m_deviceConfig));
}

AudioPlayer::~AudioPlayer() {
    shutdown();
}

bool AudioPlayer::initialize(const AudioFormat& format) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_initialized) {
        shutdown();
    }

    m_format = format;
    m_buffer.reserve(1024 * 1024); // Reserve ~4MB to avoid reallocations

    m_deviceConfig = ma_device_config_init(ma_device_type_playback);
    m_deviceConfig.playback.format = ma_format_f32;
    m_deviceConfig.playback.channels = format.channels;
    m_deviceConfig.sampleRate = format.sampleRate;
    m_deviceConfig.dataCallback = dataCallbackWrapper;
    m_deviceConfig.pUserData = this;
    // Increase buffer size to avoid underruns under load (~85ms total latency)
    m_deviceConfig.periodSizeInFrames = 2048;
    m_deviceConfig.periods = 2;

    ma_result result = ma_device_init(nullptr, &m_deviceConfig, &m_device);
    if (result != MA_SUCCESS) {
        Logger::instance().error("Failed to initialize audio device: " + std::to_string(result));
        return false;
    }

    m_initialized = true;
    Logger::instance().info("Audio device initialized: " + 
                           std::to_string(format.sampleRate) + "Hz, " +
                           std::to_string(format.channels) + " channels");
    return true;
}

void AudioPlayer::shutdown() {
    stop();
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_initialized) {
        ma_device_uninit(&m_device);
        m_initialized = false;
        Logger::instance().info("Audio device shutdown");
    }
    
    m_buffer.clear();
    m_readOffset = 0;
}

void AudioPlayer::play() {
    if (!m_initialized) return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_playing) {
        ma_result result = ma_device_start(&m_device);
        if (result == MA_SUCCESS) {
            m_playing = true;
            m_paused = false;
            m_lastProcessTime = Clock::now();
            Logger::instance().debug("Audio playback started");
        } else {
            Logger::instance().error("Failed to start audio device");
        }
    } else if (m_paused) {
        ma_result result = ma_device_start(&m_device);
        if (result == MA_SUCCESS) {
            m_paused = false;
            m_lastProcessTime = Clock::now();
            Logger::instance().debug("Audio playback resumed");
        }
    }
}

void AudioPlayer::pause() {
    if (!m_initialized || !m_playing) return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_paused) {
        ma_device_stop(&m_device);
        m_paused = true;
        Logger::instance().debug("Audio playback paused");
    }
}

void AudioPlayer::stop() {
    if (!m_initialized) return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_playing) {
        ma_device_stop(&m_device);
        m_playing = false;
        m_paused = false;
        
        m_buffer.clear();
        m_readOffset = 0;
        m_framesPlayed = 0;
        m_playedMsAtSpeedChange = 0.0;
        m_framesPlayedAtSpeedChange = 0;
        m_lastProcessTime = Clock::now();
        
        Logger::instance().debug("Audio playback stopped");
    }
}

void AudioPlayer::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.clear();
    m_readOffset = 0;
    m_framesPlayed = 0;
    m_playedMsAtSpeedChange = 0.0;
    m_framesPlayedAtSpeedChange = 0;
    m_lastProcessTime = Clock::now();
}

void AudioPlayer::setVolume(int volume) {
    m_volume = std::clamp(volume, 0, 100);
}

int AudioPlayer::volume() const {
    return m_volume.load();
}

void AudioPlayer::setMuted(bool muted) {
    m_muted = muted;
}

bool AudioPlayer::isMuted() const {
    return m_muted.load();
}

void AudioPlayer::setPlaybackSpeed(double speed) {
    double newSpeed = std::clamp(speed, 0.25, 4.0);
    // 固化当前进度，避免 speed 变化后已累积的帧数被错误地按新 speed 计算
    m_playedMsAtSpeedChange.store(playedMs());
    m_framesPlayedAtSpeedChange.store(m_framesPlayed.load());
    m_playbackSpeed = newSpeed;
}

double AudioPlayer::playbackSpeed() const {
    return m_playbackSpeed.load();
}

void AudioPlayer::enqueue(const std::vector<float>& audioData) {
    if (audioData.empty()) return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_buffer.capacity() - m_buffer.size() < audioData.size()) {
        m_buffer.reserve(m_buffer.size() + audioData.size() + 1024 * 1024);
    }
    m_buffer.insert(m_buffer.end(), audioData.begin(), audioData.end());
}

void AudioPlayer::enqueue(const float* data, size_t sampleCount) {
    std::vector<float> buffer(data, data + sampleCount);
    enqueue(buffer);
}

bool AudioPlayer::isPlaying() const {
    return m_playing.load();
}

bool AudioPlayer::isPaused() const {
    return m_paused.load();
}

size_t AudioPlayer::queueSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t samples = (m_buffer.size() > m_readOffset) ? (m_buffer.size() - m_readOffset) : 0;
    return samples / (m_format.channels > 0 ? m_format.channels : 1);
}

int64_t AudioPlayer::playedMs() const {
    if (m_format.sampleRate <= 0) return 0;
    double baseMs = m_playedMsAtSpeedChange.load();
    uint64_t framesSinceChange = m_framesPlayed.load() - m_framesPlayedAtSpeedChange.load();
    double physicalMs = static_cast<double>(framesSinceChange) * 1000.0 / m_format.sampleRate;
    return static_cast<int64_t>(baseMs + physicalMs * m_playbackSpeed.load());
}

void AudioPlayer::setDataCallback(DataCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dataCallback = callback;
}

void AudioPlayer::dataCallbackWrapper(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    AudioPlayer* player = static_cast<AudioPlayer*>(pDevice->pUserData);
    if (player) {
        player->processAudio(static_cast<float*>(pOutput), frameCount);
    }
}

void AudioPlayer::processAudio(float* output, size_t frameCount) {
    auto t0 = Clock::now();
    std::lock_guard<std::mutex> lock(m_mutex);
    double dtLock = elapsedMs(t0);
    
    // 基于实际 callback 时间间隔累加播放进度，避免 buffer 空时卡住，并支持倍速
    if (m_playing && !m_paused) {
        auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(t0 - m_lastProcessTime).count();
        if (elapsedUs > 0 && elapsedUs < 500000) {
            uint64_t expectedFrames = static_cast<uint64_t>(elapsedUs) * m_format.sampleRate / 1000000;
            expectedFrames = static_cast<uint64_t>(expectedFrames * m_playbackSpeed.load());
            m_framesPlayed += expectedFrames;
        }
    }
    m_lastProcessTime = t0;
    
    size_t channels = m_format.channels;
    size_t samplesNeeded = frameCount * channels;
    size_t samplesWritten = 0;
    
    float volumeScale = m_muted ? 0.0f : (m_volume.load() / 100.0f);
    
    if (m_dataCallback) {
        m_dataCallback(output, frameCount);
        for (size_t i = 0; i < samplesNeeded; i++) {
            output[i] *= volumeScale;
        }
        return;
    }
    
    size_t available = (m_buffer.size() > m_readOffset) ? (m_buffer.size() - m_readOffset) : 0;
    size_t samplesToCopy = (std::min)(available, samplesNeeded);
    
    if (samplesToCopy > 0) {
        std::memcpy(output, m_buffer.data() + m_readOffset, samplesToCopy * sizeof(float));
        if (volumeScale != 1.0f) {
            for (size_t i = 0; i < samplesToCopy; i++) {
                output[i] *= volumeScale;
            }
        }
        m_readOffset += samplesToCopy;
        samplesWritten = samplesToCopy;
        
        // Reset offset when buffer fully consumed to prevent unbounded growth
        if (m_readOffset >= m_buffer.size()) {
            m_buffer.clear();
            m_readOffset = 0;
        }
    }
    
    // Fill remaining with silence
    if (samplesWritten < samplesNeeded) {
        std::memset(output + samplesWritten, 0, (samplesNeeded - samplesWritten) * sizeof(float));
    }
    
    double dtTotal = elapsedMs(t0);
    if (dtTotal > 5.0) {
        size_t remaining = (m_buffer.size() > m_readOffset) ? (m_buffer.size() - m_readOffset) : 0;
        Logger::instance().debug("[PERF] audio process lock=" + std::to_string(dtLock) +
            " total=" + std::to_string(dtTotal) + "ms samples=" + std::to_string(remaining));
    }
}

} // namespace VideoPlay
