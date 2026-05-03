#include "app.h"
#include "renderer/sdlrenderer.h"
#include "core/ffmpegplayer.h"
#include "core/settings.h"
#include "utils/logger.h"
#include <SDL3/SDL.h>
#include <iostream>

namespace {
    const int MIN_VOLUME = 0;
    const int MAX_VOLUME = 100;
}

namespace VideoPlay {


void VideoPlayerApp::play() {
    if (!m_player) return;

    Logger::instance().info("Playing");
    m_player->play();
    m_isPlaying = true;
}

void VideoPlayerApp::pause() {
    if (!m_player) return;

    Logger::instance().info("Pausing");
    m_player->pause();
    m_isPlaying = false;
}

void VideoPlayerApp::togglePlayPause() {
    if (m_isPlaying) {
        pause();
    } else {
        play();
    }
}

void VideoPlayerApp::stop() {
    if (!m_player) return;

    Logger::instance().info("Stopping");
    m_isManualOperation = true;
    m_player->stop();
    m_isPlaying = false;
    m_position = 0;

    // 清空当前帧
    m_displayFrame = VideoFrame();
}

void VideoPlayerApp::seek(double deltaMs) {
    if (!m_player || m_duration <= 0) return;

    int64_t targetPos = m_position + static_cast<int64_t>(deltaMs);
    targetPos = std::max(0LL, std::min(targetPos, m_duration));
    
    Logger::instance().info("Seeking to: " + std::to_string(targetPos) + "ms");
    m_player->seek(targetPos);
    m_seekTargetPosition = targetPos;
    m_displayFrame = VideoFrame(); // 清空当前帧，避免 seek 后短暂显示旧画面
}

void VideoPlayerApp::seekTo(double position) {
    if (!m_player || m_duration <= 0) return;

    position = std::max(0.0, std::min(1.0, position));
    int64_t targetPos = static_cast<int64_t>(position * m_duration);
    
    Logger::instance().info("Seeking to position: " + std::to_string(position));
    m_player->seek(targetPos);
    m_seekTargetPosition = targetPos;
    m_displayFrame = VideoFrame(); // 清空当前帧，避免 seek 后短暂显示旧画面
}

void VideoPlayerApp::setVolume(int delta) {
    m_volume += delta;
    m_volume = std::max(MIN_VOLUME, std::min(MAX_VOLUME, m_volume));
    
    if (m_player) {
        m_player->setVolume(m_volume);
    }
    
    Logger::instance().info("Volume set to: " + std::to_string(m_volume));
}

void VideoPlayerApp::toggleMute() {
    m_isMuted = !m_isMuted;
    
    if (m_player) {
        m_player->setMuted(m_isMuted);
    }
    
    Logger::instance().info(m_isMuted ? "Muted" : "Unmuted");
}

void VideoPlayerApp::setSpeed(double speed) {
    m_speed = std::max(0.25, std::min(4.0, speed));
    
    if (m_player) {
        m_player->setPlaybackSpeed(m_speed);
    }
    
    Logger::instance().info("Playback speed set to: " + std::to_string(m_speed) + "x");
}

void VideoPlayerApp::cycleSpeed() {
    const double speeds[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 2.0 };
    const int count = sizeof(speeds) / sizeof(speeds[0]);
    
    // 找到下一个速度
    for (int i = 0; i < count; i++) {
        if (speeds[i] > m_speed) {
            setSpeed(speeds[i]);
            return;
        }
    }
    
    // 回到第一个
    setSpeed(speeds[0]);
}

void VideoPlayerApp::setLoopPointA() {
    m_loopA = m_position;
    Logger::instance().info("Loop A set: " + formatTime(m_loopA));
}

void VideoPlayerApp::setLoopPointB() {
    if (m_loopA < 0) {
        Logger::instance().warning("Set loop point A first before setting B");
        return;
    }
    m_loopB = m_position;
    if (m_loopB <= m_loopA) {
        Logger::instance().warning("Loop B must be after loop A");
        m_loopB = -1;
        return;
    }
    Logger::instance().info("Loop B set: " + formatTime(m_loopB) + ", AB loop active");
}

void VideoPlayerApp::clearLoop() {
    m_loopA = -1;
    m_loopB = -1;
    m_loopSeeking = false;
    Logger::instance().info("AB loop cleared");
}

} // namespace VideoPlay
