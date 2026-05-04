#include "core/audioplayer.h"
#include "utils/logger.h"
#include <algorithm>
#include <cstring>

namespace VideoPlay {

AudioPlayer::AudioPlayer() = default;

AudioPlayer::~AudioPlayer() {
    shutdown();
}

bool AudioPlayer::initialize(const AudioFormat& format) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_initialized) {
        shutdown();
    }

    m_format = format;

    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_F32LE;
    spec.channels = format.channels;
    spec.freq = format.sampleRate;

    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!m_stream) {
        Logger::instance().error("Failed to initialize audio stream: " + std::string(SDL_GetError()));
        return false;
    }

    m_deviceId = SDL_GetAudioStreamDevice(m_stream);
    m_initialized = true;
    m_basePlayedMs = 0.0;
    m_timerRunning = false;

    // Apply initial volume/mute state to stream gain
    applyStreamGain();

    Logger::instance().info("Audio stream initialized: " +
                           std::to_string(format.sampleRate) + "Hz, " +
                           std::to_string(format.channels) + " channels");
    return true;
}

void AudioPlayer::shutdown() {
    stop();

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_initialized) {
        if (m_stream) {
            SDL_DestroyAudioStream(m_stream);
            m_stream = nullptr;
            m_deviceId = 0;
        }
        m_initialized = false;
        Logger::instance().info("Audio stream shutdown");
    }
}

void AudioPlayer::play() {
    if (!m_stream) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_playing) {
        if (SDL_ResumeAudioStreamDevice(m_stream)) {
            m_playing = true;
            m_paused = false;
            m_timerStart = Clock::now();
            m_timerRunning = true;
            Logger::instance().debug("Audio playback started");
        } else {
            Logger::instance().error("Failed to start audio stream: " + std::string(SDL_GetError()));
        }
    } else if (m_paused) {
        if (SDL_ResumeAudioStreamDevice(m_stream)) {
            m_paused = false;
            m_timerStart = Clock::now();
            m_timerRunning = true;
            Logger::instance().debug("Audio playback resumed");
        }
    }
}

void AudioPlayer::pause() {
    if (!m_stream || !m_playing) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_paused) {
        SDL_PauseAudioStreamDevice(m_stream);
        m_paused = true;
        if (m_timerRunning) {
            auto physicalElapsed = std::chrono::duration<double, std::milli>(Clock::now() - m_timerStart).count();
            m_basePlayedMs += physicalElapsed * m_playbackSpeed.load();
            m_timerRunning = false;
        }
        Logger::instance().debug("Audio playback paused");
    }
}

void AudioPlayer::stop() {
    if (!m_stream) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_playing) {
        SDL_PauseAudioStreamDevice(m_stream);
        SDL_ClearAudioStream(m_stream);
        m_playing = false;
        m_paused = false;
        Logger::instance().debug("Audio playback stopped");
    }
    m_basePlayedMs = 0.0;
    m_timerRunning = false;
}

void AudioPlayer::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stream) {
        SDL_ClearAudioStream(m_stream);
    }
    m_basePlayedMs = 0.0;
    if (m_timerRunning) {
        m_timerStart = Clock::now();
    }
}

void AudioPlayer::setVolume(int volume) {
    m_volume = std::clamp(volume, 0, 100);
    applyStreamGain();
}

int AudioPlayer::volume() const {
    return m_volume.load();
}

void AudioPlayer::setMuted(bool muted) {
    m_muted = muted;
    applyStreamGain();
}

bool AudioPlayer::isMuted() const {
    return m_muted.load();
}

void AudioPlayer::applyStreamGain() {
    if (!m_stream) return;
    float gain = m_muted ? 0.0f : (m_volume.load() / 100.0f);
    SDL_SetAudioStreamGain(m_stream, gain);
}

void AudioPlayer::setPlaybackSpeed(double speed) {
    double newSpeed = std::clamp(speed, 0.25, 4.0);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_timerRunning) {
        auto physicalElapsed = std::chrono::duration<double, std::milli>(Clock::now() - m_timerStart).count();
        m_basePlayedMs += physicalElapsed * m_playbackSpeed.load();
        m_timerStart = Clock::now();
    }
    m_playbackSpeed = newSpeed;
}

double AudioPlayer::playbackSpeed() const {
    return m_playbackSpeed.load();
}

void AudioPlayer::enqueue(const std::vector<float>& audioData) {
    if (audioData.empty() || !m_stream) return;

    // Volume is handled by SDL_SetAudioStreamGain, no need to copy/scale here
    if (!SDL_PutAudioStreamData(m_stream, audioData.data(), static_cast<int>(audioData.size() * sizeof(float)))) {
        Logger::instance().error("SDL_PutAudioStreamData failed: " + std::string(SDL_GetError()));
    }
}

void AudioPlayer::enqueue(const float* data, size_t sampleCount) {
    if (!data || sampleCount == 0 || !m_stream) return;

    // Volume is handled by SDL_SetAudioStreamGain, no need to copy/scale here
    if (!SDL_PutAudioStreamData(m_stream, data, static_cast<int>(sampleCount * sizeof(float)))) {
        Logger::instance().error("SDL_PutAudioStreamData failed: " + std::string(SDL_GetError()));
    }
}

bool AudioPlayer::isPlaying() const {
    return m_playing.load();
}

bool AudioPlayer::isPaused() const {
    return m_paused.load();
}

size_t AudioPlayer::queueSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_stream) return 0;
    int queuedBytes = SDL_GetAudioStreamQueued(m_stream);
    if (queuedBytes <= 0) return 0;
    int samples = queuedBytes / static_cast<int>(sizeof(float));
    int channels = m_format.channels > 0 ? m_format.channels : 1;
    return static_cast<size_t>(samples / channels);
}

int64_t AudioPlayer::queuedMs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_stream) return 0;
    int queuedBytes = SDL_GetAudioStreamQueued(m_stream);
    if (queuedBytes <= 0) return 0;
    int totalFloats = queuedBytes / static_cast<int>(sizeof(float));
    int channels = m_format.channels > 0 ? m_format.channels : 1;
    int sampleRate = m_format.sampleRate > 0 ? m_format.sampleRate : 48000;
    int frames = totalFloats / channels;
    if (frames <= 0 || sampleRate <= 0) return 0;
    return static_cast<int64_t>(frames * 1000LL / sampleRate);
}

int64_t AudioPlayer::playedMs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    double result = m_basePlayedMs;
    if (m_timerRunning) {
        auto physicalElapsed = std::chrono::duration<double, std::milli>(Clock::now() - m_timerStart).count();
        result += physicalElapsed * m_playbackSpeed.load();
    }
    return static_cast<int64_t>(result);
}

void AudioPlayer::setDataCallback(DataCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dataCallback = callback;
}

} // namespace VideoPlay
