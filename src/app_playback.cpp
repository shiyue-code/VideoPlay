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

    VideoPlay::Logger& logger() {
        static auto logger = VideoPlay::Logger::get("playback");
        return *logger;
    }
}

namespace VideoPlay {


void VideoPlayerApp::play() {
    if (!m_player) return;

    logger().info("Playing");
    m_player->play();
    m_isPlaying = true;
    if (m_renderer) {
        m_renderer->showOSD("播放");
    }
}

void VideoPlayerApp::pause() {
    if (!m_player) return;

    logger().info("Pausing");
    m_player->pause();
    m_isPlaying = false;
    if (m_renderer) {
        m_renderer->showOSD("暂停");
    }
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

    logger().info("Stopping");
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
    
    logger().info("Seeking to: {}ms", targetPos);
    m_player->seek(targetPos);
    m_seekTargetPosition = targetPos;
    m_displayFrame = VideoFrame(); // 清空当前帧，避免 seek 后短暂显示旧画面
    if (m_renderer) {
        m_renderer->showOSD(formatTime(targetPos));
    }
}

void VideoPlayerApp::seekTo(double position) {
    if (!m_player || m_duration <= 0) return;

    position = std::max(0.0, std::min(1.0, position));
    int64_t targetPos = static_cast<int64_t>(position * m_duration);
    
    logger().info("Seeking to position: {}", position);
    m_player->seek(targetPos);
    m_seekTargetPosition = targetPos;
    m_displayFrame = VideoFrame(); // 清空当前帧，避免 seek 后短暂显示旧画面
    if (m_renderer) {
        m_renderer->showOSD(formatTime(targetPos));
    }
}

void VideoPlayerApp::setVolume(int delta) {
    m_volume += delta;
    m_volume = std::max(MIN_VOLUME, std::min(MAX_VOLUME, m_volume));
    
    if (m_player) {
        m_player->setVolume(m_volume);
    }
    
    logger().info("Volume set to: {}", m_volume);
    if (m_renderer) {
        m_renderer->showOSD("音量 " + std::to_string(m_volume) + "%");
    }
}

void VideoPlayerApp::toggleMute() {
    m_isMuted = !m_isMuted;
    
    if (m_player) {
        m_player->setMuted(m_isMuted);
    }
    
    logger().info(m_isMuted ? "Muted" : "Unmuted");
    if (m_renderer) {
        m_renderer->showOSD(m_isMuted ? "静音" : "取消静音");
    }
}

void VideoPlayerApp::setSpeed(double speed) {
    m_speed = std::max(0.25, std::min(4.0, speed));
    
    if (m_player) {
        m_player->setPlaybackSpeed(m_speed);
    }
    
    logger().info("Playback speed set to: {}x", m_speed);
    if (m_renderer) {
        m_renderer->showOSD("速度 " + std::to_string(m_speed).substr(0, 4) + "x");
    }
}

void VideoPlayerApp::cycleSpeed() {
    const double speeds[] = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 4.0 };
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
    logger().info("Loop A set: {}", formatTime(m_loopA));
}

void VideoPlayerApp::setLoopPointB() {
    if (m_loopA < 0) {
        logger().warning("Set loop point A first before setting B");
        return;
    }
    m_loopB = m_position;
    if (m_loopB <= m_loopA) {
        logger().warning("Loop B must be after loop A");
        m_loopB = -1;
        return;
    }
    logger().info("Loop B set: {}, AB loop active", formatTime(m_loopB));
}

void VideoPlayerApp::clearLoop() {
    m_loopA = -1;
    m_loopB = -1;
    m_loopSeeking = false;
    logger().info("AB loop cleared");
}

} // namespace VideoPlay
