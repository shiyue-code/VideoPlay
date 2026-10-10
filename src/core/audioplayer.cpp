#include "core/audioplayer.h"
#include "utils/logger.h"
#include <algorithm>
#include <cstring>
#include <utility>

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto logger = Logger::get("audio");
    return *logger;
}
}


AudioPlayer::AudioPlayer() = default;

AudioPlayer::~AudioPlayer() {
    shutdown();
}

bool AudioPlayer::initialize(const AudioFormat& format) {
    return initialize(format, m_preferredDeviceId);
}

std::vector<AudioOutputDevice> AudioPlayer::listPlaybackDevices() {
    std::vector<AudioOutputDevice> devices;
    devices.push_back({SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, "系统默认", true});

    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
    if (!ids || count <= 0) {
        return devices;
    }

    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetAudioDeviceName(ids[i]);
        AudioOutputDevice device;
        device.id = ids[i];
        device.name = (name && name[0]) ? name : ("设备 " + std::to_string(i + 1));
        device.isDefault = false;
        devices.push_back(std::move(device));
    }
    SDL_free(ids);
    return devices;
}

bool AudioPlayer::initialize(const AudioFormat& format, SDL_AudioDeviceID deviceId) {
    // 先销毁旧的 audio stream，避免在持有 m_mutex 时调用 shutdown() 造成递归上锁
    shutdown();

    std::lock_guard<std::mutex> lock(m_mutex);

    m_format = format;
    m_preferredDeviceId = deviceId != 0 ? deviceId : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;

    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_F32LE;
    spec.channels = format.channels;
    spec.freq = format.sampleRate;

    auto openStream = [&](SDL_AudioDeviceID id) -> SDL_AudioStream* {
        return SDL_OpenAudioDeviceStream(id, &spec, nullptr, nullptr);
    };

    m_stream = openStream(m_preferredDeviceId);
    if (!m_stream && m_preferredDeviceId != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK) {
        logger().warning("Failed to open audio device, falling back to default: " +
                         std::string(SDL_GetError()));
        m_preferredDeviceId = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
        m_stream = openStream(m_preferredDeviceId);
    }
    if (!m_stream) {
        logger().error("Failed to initialize audio stream: " + std::string(SDL_GetError()));
        return false;
    }

    m_deviceId = SDL_GetAudioStreamDevice(m_stream);
    m_initialized = true;
    m_clock.reset();

    applyStreamGain();

    logger().info("Audio stream initialized: " +
                  std::to_string(format.sampleRate) + "Hz, " +
                  std::to_string(format.channels) + " channels, device=" +
                  std::to_string(m_deviceId));
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
        m_clock.reset();
        logger().info("Audio stream shutdown");
    }
}

void AudioPlayer::play() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_stream || !m_initialized) { return; }

    if (!m_playing) {
        // 尝试同时用 stream 和 device ID 恢复，确保设备真正开始播放
        bool streamResumed = SDL_ResumeAudioStreamDevice(m_stream);
        bool deviceResumed = (m_deviceId != 0) ? SDL_ResumeAudioDevice(m_deviceId) : true;
        if (streamResumed || deviceResumed) {
            m_playing = true;
            m_paused = false;
            bool streamPaused = SDL_AudioStreamDevicePaused(m_stream);
            logger().debug("Audio playback started, streamPaused=" +
                           std::string(streamPaused ? "true" : "false"));
        } else {
            logger().error("Failed to start audio stream: " + std::string(SDL_GetError()));
        }
    } else if (m_paused) {
        if (SDL_ResumeAudioStreamDevice(m_stream) ||
            (m_deviceId != 0 && SDL_ResumeAudioDevice(m_deviceId))) {
            m_paused = false;
            logger().debug("Audio playback resumed");
        }
    }
}

void AudioPlayer::pause() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_stream || !m_playing) { return; }

    if (!m_paused) {
        SDL_PauseAudioStreamDevice(m_stream);
        if (m_deviceId != 0) {
            SDL_PauseAudioDevice(m_deviceId);
        }
        m_paused = true;
        logger().debug("Audio playback paused");
    }
}

void AudioPlayer::stop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_stream) { return; }

    if (m_playing) {
        SDL_PauseAudioStreamDevice(m_stream);
        if (m_deviceId != 0) {
            SDL_PauseAudioDevice(m_deviceId);
        }
        SDL_ClearAudioStream(m_stream);
        m_playing = false;
        m_paused = false;
        logger().debug("Audio playback stopped");
    }
    m_clock.reset();
}

void AudioPlayer::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stream) {
        SDL_ClearAudioStream(m_stream);
    }
    m_clock.reset();
}

void AudioPlayer::setVolume(int volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_volume = std::clamp(volume, 0, 100);
    applyStreamGain();
}

int AudioPlayer::volume() const {
    return m_volume.load();
}

void AudioPlayer::setMuted(bool muted) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_muted = muted;
    applyStreamGain();
}

bool AudioPlayer::isMuted() const {
    return m_muted.load();
}

void AudioPlayer::applyStreamGain() {
    // 调用方需要持有 m_mutex
    if (!m_stream) { return; }
    float gain = m_muted ? 0.0f : (m_volume.load() / 100.0f);
    SDL_SetAudioStreamGain(m_stream, gain);
}

void AudioPlayer::enqueue(const float* data, size_t sampleCount, double startMs, double msPerFrame) {
    if (!data || sampleCount == 0) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_stream) { return; }

    // Volume is handled by SDL_SetAudioStreamGain, no need to copy/scale here
    if (!SDL_PutAudioStreamData(m_stream, data, static_cast<int>(sampleCount * sizeof(float)))) {
        logger().error("SDL_PutAudioStreamData failed: " + std::string(SDL_GetError()));
        return;
    }
    int channels = m_format.channels > 0 ? m_format.channels : 1;
    m_clock.push(static_cast<int64_t>(sampleCount) / channels, startMs, msPerFrame);
    m_clock.prune(queuedFramesLocked());
}

bool AudioPlayer::isPlaying() const {
    return m_playing.load();
}

bool AudioPlayer::isPaused() const {
    return m_paused.load();
}

int64_t AudioPlayer::queuedFramesLocked() const {
    if (!m_stream) return 0;
    int queuedBytes = SDL_GetAudioStreamQueued(m_stream);
    if (queuedBytes <= 0) return 0;
    int channels = m_format.channels > 0 ? m_format.channels : 1;
    return queuedBytes / static_cast<int>(sizeof(float) * channels);
}

size_t AudioPlayer::queueSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<size_t>(queuedFramesLocked());
}

int64_t AudioPlayer::queuedMs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    int64_t frames = queuedFramesLocked();
    int sampleRate = m_format.sampleRate > 0 ? m_format.sampleRate : 48000;
    if (frames <= 0 || sampleRate <= 0) return 0;
    return frames * 1000LL / sampleRate;
}

double AudioPlayer::mediaPositionMs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_clock.positionMs(queuedFramesLocked());
}

void AudioPlayer::setDataCallback(DataCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dataCallback = callback;
}

} // namespace VideoPlay
