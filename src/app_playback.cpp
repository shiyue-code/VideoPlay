#include "app.h"
#include "renderer/sdlrenderer.h"
#include "core/ffmpegplayer.h"
#include "core/settings.h"
#include "utils/frame_image.h"
#include "utils/logger.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
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
    m_stepping = false;
    m_waitingStepFrame = false;
    m_pendingStepDir = 0;

    // 清空当前帧
    m_displayFrame = VideoFrame();
}

void VideoPlayerApp::seek(double deltaMs) {
    if (!m_player || m_duration <= 0) return;

    seekToMs(m_position + static_cast<int64_t>(deltaMs),
             deltaMs >= 0 ? OSDType::SeekForward : OSDType::SeekBackward);
}

void VideoPlayerApp::seekTo(double position) {
    if (!m_player || m_duration <= 0) return;

    position = std::max(0.0, std::min(1.0, position));
    seekToMs(static_cast<int64_t>(position * m_duration), OSDType::Info,
             static_cast<float>(position));
}

// 所有绝对定位的统一出口：发 seek、更新目标位置、清显示帧、报 OSD
void VideoPlayerApp::seekToMs(int64_t targetPos, OSDType osdType, float osdRatio) {
    if (!m_player || m_duration <= 0) return;

    targetPos = std::max<int64_t>(0, std::min(targetPos, m_duration));
    logger().info("Seeking to: {}ms", targetPos);
    m_player->seek(targetPos);
    m_seekTargetPosition = targetPos;
    // 常规 seek 取消进行中的逐帧步进，否则 -1 等待通道会误吞本次 seek 的落点帧
    m_stepping = false;
    m_waitingStepFrame = false;
    m_pendingStepDir = 0;
    m_displayFrame = VideoFrame(); // 清空当前帧，避免 seek 后短暂显示旧画面
    if (m_renderer) {
        m_renderer->showOSD(osdType, formatTime(targetPos), osdRatio);
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

VideoFrame VideoPlayerApp::captureCurrentFrame() {
    VideoFrame shot;
    if (!m_renderer) return shot;

    // 截取当前显示的视频帧（不含 UI/OSD），套用与渲染一致的画面变换
    if (m_displayFrame.data.empty() ||
        m_displayFrame.width <= 0 || m_displayFrame.height <= 0) {
        return shot;
    }

    const VideoTransform transform = m_renderer->videoTransform();
    shot = transformFrame(m_displayFrame, transform.rotation,
                          transform.flipHorizontal, transform.flipVertical,
                          transform.cropPercent);
    if (shot.data.empty()) {
        logger().error("Failed to transform frame for screenshot");
    }
    return shot;
}

void VideoPlayerApp::takeScreenshot() {
    if (!m_renderer) return;

    VideoFrame shot = captureCurrentFrame();
    if (shot.data.empty()) {
        m_renderer->showMessageBox("截图失败",
            m_displayFrame.data.empty() ? "当前没有可截取的视频画面" : "无法保存截图", true);
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
        // 轻提示走 OSD；日志仍记录完整路径
        std::string filename = std::filesystem::path(savePath).filename().u8string();
        m_renderer->showOSD(OSDType::Info, "截图已保存：" + filename);
    } else {
        logger().error("Failed to save screenshot");
        m_renderer->showMessageBox("截图失败", "无法保存截图", true);
    }
}

void VideoPlayerApp::copyScreenshotToClipboard() {
    if (!m_renderer) return;

    VideoFrame shot = captureCurrentFrame();
    if (shot.data.empty()) {
        m_renderer->showMessageBox("截图失败",
            m_displayFrame.data.empty() ? "当前没有可截取的视频画面" : "无法保存截图", true);
        return;
    }

    auto png = std::make_unique<std::vector<uint8_t>>();
    if (!encodeFramePng(shot, *png)) {
        logger().error("Failed to encode screenshot to PNG");
        m_renderer->showMessageBox("截图失败", "无法保存截图", true);
        return;
    }

    // SDL_SetClipboardData：剪贴板被请求时由回调提供 PNG 字节，
    // 数据所有权移交 SDL（cleanup 回调在剪贴板被替换/退出时释放）。
    // 失败路径中缓冲区可能已被 SDL 回收或继续持有，release 后不重复释放。
    static const char* kMimeTypes[] = { "image/png", "PNG" };
    auto dataCallback = [](void* userdata, const char* /*mime_type*/, size_t* size) -> const void* {
        auto* buffer = static_cast<std::vector<uint8_t>*>(userdata);
        *size = buffer->size();
        return buffer->data();
    };
    auto cleanupCallback = [](void* userdata) {
        delete static_cast<std::vector<uint8_t>*>(userdata);
    };

    if (SDL_SetClipboardData(dataCallback, cleanupCallback, png.release(),
                             kMimeTypes, sizeof(kMimeTypes) / sizeof(kMimeTypes[0]))) {
        logger().info("Screenshot copied to clipboard");
        m_renderer->showOSD(OSDType::Info, "截图已复制到剪贴板");
    } else {
        logger().error("Failed to set clipboard data: " + std::string(SDL_GetError()));
        m_renderer->showMessageBox("截图失败", "无法复制到剪贴板", true);
    }
}

void VideoPlayerApp::stepFrame(int direction) {
    if (!m_player || direction == 0 || m_currentFile.empty()) return;

    // 纯音频文件没有可步进的视频帧：退化为方向键语义的 5 秒 seek。
    // 否则 -1 静默无效、+1 会恢复播放空等落点 1 秒，看起来像随机跳进度
    if (!m_player->mediaInfo().hasVideo) {
        seek(direction * 5000.0);
        return;
    }

    // 定位 seek 未落定（含启动时恢复上次观看位置）时禁止步进：
    // UI 位置已是 seek 目标，但显示帧可能仍是 seek 前的旧帧——
    // 用旧帧 pts 做锚点会步到毫不相干的位置，看起来像乱跳进度
    if (m_seekTargetPosition >= 0 &&
        (m_displayFrame.data.empty() ||
         std::llabs(m_displayFrame.pts - m_seekTargetPosition) >= 500)) {
        return;
    }

    // 步进飞行中（+1 等新帧 / -1 等落点）：按键先记账，落点到达后由
    // render() 逐帧执行。立即执行会锚到尚未更新的显示帧，方向相反时
    // 就会少走一帧（先右再左回不到原位）
    if (m_stepping || m_waitingStepFrame) {
        m_pendingStepDir = std::clamp(m_pendingStepDir + direction, -8, 8);
        return;
    }

    // 播放中第一下只暂停；之后一次按键走一帧
    if (m_isPlaying) {
        pause();
        return;
    }

    // -1 要给长 GOP 留时间：关键帧可能数秒之外，解码量远超 +1
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(direction > 0 ? 1 : 8);

    if (direction > 0) {
        // +1 下一帧：恢复播放，render() 采到 pts 更新的帧立即再暂停。
        // 帧驱动而非时间换算：音频时钟与帧 pts 的容器精度差异会让按时间算的
        // 落点丢掉目标帧或停在原帧。
        m_stepPrevPts = m_displayFrame.data.empty() ? -1 : m_displayFrame.pts;
        m_stepDeadline = deadline;
        m_stepping = true;
        play();
        return;
    }

    // -1 上一帧：以显示帧 pts 为上界做回退落点 seek——解码到
    // anchor + 保护窗后取 pts < anchor 的最大帧，即解码序真正的
    // 上一帧，与 fps/容器精度/B 帧重排深度全部无关
    if (m_displayFrame.data.empty() || m_displayFrame.pts < 0) {
        return;
    }
    m_player->seekStepBack(m_displayFrame.pts);
    // 保留 m_displayFrame：落点帧到达前继续显示旧画面，避免黑屏闪烁
    m_waitingStepFrame = true;
    m_stepDeadline = deadline;
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
