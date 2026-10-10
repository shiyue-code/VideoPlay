#include "app.h"
#include "renderer/sdlrenderer.h"
#include "core/ffmpegplayer.h"
#include "core/settings.h"
#include "utils/frame_image.h"
#include "utils/logger.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>

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
        m_renderer->showOSD(OSDType::Play, "播放");
    }
}

void VideoPlayerApp::pause() {
    if (!m_player) return;

    logger().info("Pausing");
    m_player->pause();
    m_isPlaying = false;
    if (m_renderer) {
        m_renderer->showOSD(OSDType::Pause, "暂停");
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
        m_renderer->showOSD(deltaMs >= 0 ? OSDType::SeekForward : OSDType::SeekBackward,
                            formatTime(targetPos));
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
        m_renderer->showOSD(OSDType::Info, formatTime(targetPos),
                            m_duration > 0 ? static_cast<float>(targetPos) / static_cast<float>(m_duration) : -1.0f);
    }
}

void VideoPlayerApp::setVolume(int delta) {
    setVolumeTo(m_volume + delta);
}

void VideoPlayerApp::setVolumeTo(int value) {
    m_volume = std::max(MIN_VOLUME, std::min(MAX_VOLUME, value));
    Settings::instance().setVolume(m_volume);

    if (m_player) {
        m_player->setVolume(m_volume);
    }

    logger().info("Volume set to: {}", m_volume);
    if (m_renderer) {
        m_renderer->showOSD(OSDType::Volume, "音量 " + std::to_string(m_volume) + "%",
                            m_volume / 100.0f);
    }
}

void VideoPlayerApp::toggleMute() {
    m_isMuted = !m_isMuted;
    Settings::instance().setMuted(m_isMuted);

    if (m_player) {
        m_player->setMuted(m_isMuted);
    }
    
    logger().info(m_isMuted ? "Muted" : "Unmuted");
    if (m_renderer) {
        m_renderer->showOSD(m_isMuted ? OSDType::Mute : OSDType::Volume,
                            m_isMuted ? "静音" : "取消静音",
                            m_isMuted ? 0.0f : m_volume / 100.0f);
    }
}

void VideoPlayerApp::setSpeed(double speed) {
    m_speed = std::max(0.25, std::min(4.0, speed));
    Settings::instance().setPlaybackSpeed(m_speed);

    if (m_player) {
        m_player->setPlaybackSpeed(m_speed);
    }
    
    logger().info("Playback speed set to: {}x", m_speed);
    if (m_renderer) {
        m_renderer->showOSD(OSDType::Speed, "速度 " + std::to_string(m_speed).substr(0, 4) + "x");
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

void VideoPlayerApp::takeScreenshot() {
    if (!m_renderer) return;

    // 截取当前显示的视频帧（不含 UI/OSD），套用与渲染一致的画面变换
    if (m_displayFrame.data.empty() ||
        m_displayFrame.width <= 0 || m_displayFrame.height <= 0) {
        m_renderer->showMessageBox("截图失败", "当前没有可截取的视频画面", true);
        return;
    }

    const VideoTransform transform = m_renderer->videoTransform();
    VideoFrame shot = transformFrame(m_displayFrame, transform.rotation,
                                     transform.flipHorizontal, transform.flipVertical,
                                     transform.cropPercent);
    if (shot.data.empty()) {
        logger().error("Failed to transform frame for screenshot");
        m_renderer->showMessageBox("截图失败", "无法保存截图", true);
        return;
    }

    // 构造保存路径：桌面 / VideoPlay_Screenshot_YYYYMMDD_HHMMSS.png
    std::string savePath;
#ifdef _WIN32
    const char* userProfile = getenv("USERPROFILE");
    if (userProfile) {
        savePath = std::string(userProfile) + "/Desktop/";
    } else {
        savePath = "./";
    }
#else
    const char* home = getenv("HOME");
    savePath = home ? std::string(home) + "/Desktop/" : "./";
#endif

    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << "VideoPlay_Screenshot_"
        << std::put_time(&tm, "%Y%m%d_%H%M%S")
        << ".png";
    savePath += oss.str();

    if (saveFramePng(shot, savePath)) {
        logger().info("Screenshot saved: " + savePath);
        m_renderer->showMessageBox("截图已保存", "截图已保存到:\n" + savePath, false);
    } else {
        logger().error("Failed to save screenshot");
        m_renderer->showMessageBox("截图失败", "无法保存截图", true);
    }
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

void VideoPlayerApp::addBookmark() {
    if (m_currentFile.empty() || !m_player) return;
    int64_t pos = m_player->position();
    Bookmark b;
    b.timeMs = pos;
    b.title = "书签 " + formatTime(pos);
    Settings::instance().addBookmark(m_currentFile, b);
    Settings::instance().save();
    m_bookmarks = Settings::instance().bookmarksForFile(m_currentFile);
    if (m_renderer) {
        m_renderer->setBookmarks(m_bookmarks);
        m_renderer->showOSD(OSDType::Info, "添加书签 " + formatTime(pos));
    }
}

void VideoPlayerApp::clearBookmarks() {
    if (m_currentFile.empty()) return;
    Settings::instance().clearBookmarksForFile(m_currentFile);
    Settings::instance().save();
    m_bookmarks.clear();
    if (m_renderer) {
        m_renderer->setBookmarks(m_bookmarks);
        m_renderer->showOSD(OSDType::Info, "清空书签");
    }
}

void VideoPlayerApp::jumpToBookmark(int index) {
    if (!m_player || index < 0 || index >= (int)m_bookmarks.size()) return;
    int64_t pos = m_bookmarks[index].timeMs;
    m_player->seek(pos);
    if (m_renderer) {
        m_renderer->showOSD(OSDType::Info, "跳转至 " + m_bookmarks[index].title);
    }
}

} // namespace VideoPlay
