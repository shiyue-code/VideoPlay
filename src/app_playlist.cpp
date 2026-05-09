#include "app.h"
#include "renderer/sdlrenderer.h"
#include "core/ffmpegplayer.h"
#include "core/settings.h"
#include "utils/logger.h"
#include "core/episodedetector.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <filesystem>

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto logger = Logger::get("app.playlist");
    return *logger;
}
}



void VideoPlayerApp::addToPlaylist(const std::string& path) {
    // 检查是否已存在
    auto it = std::find(m_playlist.begin(), m_playlist.end(), path);
    if (it == m_playlist.end()) {
        m_playlist.push_back(path);
        m_progressCacheDirty = true;
        logger().info("Added to playlist: " + path);
    }
}

void VideoPlayerApp::playNext() {
    // 优先剧集维度；若当前文件属于某剧集，则播放下一集
    if (m_currentSeries) {
        m_isManualOperation = true;
        playNextEpisode();
        return;
    }

    if (m_playlist.empty()) return;

    m_currentIndex++;
    if (m_currentIndex >= m_playlist.size()) {
        m_currentIndex = 0;  // 循环
    }

    m_isManualOperation = true;
    playFromPlaylist(m_currentIndex);
}

void VideoPlayerApp::playPrevious() {
    // 优先剧集维度；若当前文件属于某剧集，则播放上一集
    if (m_currentSeries) {
        m_isManualOperation = true;
        playPreviousEpisode();
        return;
    }

    if (m_playlist.empty()) return;

    if (m_currentIndex == 0) {
        m_currentIndex = m_playlist.size() - 1;
    } else {
        m_currentIndex--;
    }

    m_isManualOperation = true;
    playFromPlaylist(m_currentIndex);
}

void VideoPlayerApp::playFromPlaylist(size_t index) {
    if (index >= m_playlist.size()) return;
    if (!m_currentFile.empty() && m_currentFile == m_playlist[index]) {
        logger().debug("Already playing: " + m_playlist[index]);
        return;
    }

    m_currentIndex = index;
    m_progressCacheDirty = true;
    m_isManualOperation = true;
    openFile(m_playlist[index]);
}

void VideoPlayerApp::detectSeries(const std::string& path) {
    m_currentSeries = EpisodeDetector::detectFromFile(path);
    m_progressCacheDirty = true;
    if (m_currentSeries) {
        m_renderer->setEpisodeData(&m_currentSeries->episodes, m_currentSeries->currentIndex,
                                   m_currentSeries->seriesName, m_currentSeries->seasonNumber);
    } else {
        m_renderer->setEpisodeData(nullptr, 0);
    }
}

void VideoPlayerApp::playEpisode(size_t index) {
    if (!m_currentSeries || index >= m_currentSeries->episodes.size()) return;

    m_progressCacheDirty = true;
    // 保存当前集的进度
    saveSeriesProgress();

    std::string path = m_currentSeries->episodes[index].path;
    if (!m_currentFile.empty() && m_currentFile == path) {
        logger().debug("Already playing episode: " + path);
        return;
    }

    // 同步播放列表索引（若该集已在列表中）
    auto it = std::find(m_playlist.begin(), m_playlist.end(), path);
    if (it != m_playlist.end()) {
        m_currentIndex = static_cast<size_t>(std::distance(m_playlist.begin(), it));
    }

    // 临时清空剧集数据，防止 openFile -> stop() -> onStateChanged(Stopped)
    // 触发自动连播，导致递归跳过多集
    m_currentSeries = std::nullopt;
    m_renderer->setEpisodeData(nullptr, 0);
    m_isManualOperation = true;
    openFile(path);
}

void VideoPlayerApp::playNextEpisode() {
    if (!m_currentSeries) return;

    size_t nextIndex = m_currentSeries->currentIndex + 1;
    if (nextIndex >= m_currentSeries->episodes.size()) {
        return; // 不循环
    }
    playEpisode(nextIndex);
}

void VideoPlayerApp::playPreviousEpisode() {
    if (!m_currentSeries) return;

    if (m_currentSeries->currentIndex == 0) {
        return; // 不循环
    }
    playEpisode(m_currentSeries->currentIndex - 1);
}

void VideoPlayerApp::saveSeriesProgress() {
    if (!m_currentSeries) return;

    std::string seriesKey = m_currentSeries->episodes[0].path;
    seriesKey = std::filesystem::path(seriesKey).parent_path().string() + "/" + m_currentSeries->seriesName;

    std::unordered_map<std::string, int64_t> positions;
    for (const auto& ep : m_currentSeries->episodes) {
        int64_t pos = Settings::instance().lastPosition(ep.path);
        if (pos > 0) {
            positions[ep.path] = pos;
        }
    }

    Settings::instance().setSeriesProgress(seriesKey, static_cast<int>(m_currentSeries->currentIndex), positions);
}

void VideoPlayerApp::restoreSeriesPosition() {
    if (!m_currentSeries) return;

    std::string seriesKey = m_currentSeries->episodes[0].path;
    seriesKey = std::filesystem::path(seriesKey).parent_path().string() + "/" + m_currentSeries->seriesName;

    auto progress = Settings::instance().seriesProgress(seriesKey);
    if (progress.lastEpisodeIndex >= 0 &&
        static_cast<size_t>(progress.lastEpisodeIndex) < m_currentSeries->episodes.size()) {
        m_currentSeries->currentIndex = static_cast<size_t>(progress.lastEpisodeIndex);
    }
}

void VideoPlayerApp::autoAdvanceAfterStop() {
    // 将刚播完的文件标记为已完成
    if (!m_currentFile.empty() && m_duration > 0) {
        Settings::instance().setLastPosition(m_currentFile, m_duration);
    }

    auto loopMode = Settings::instance().loopMode();

    // 单曲循环：重新播放当前文件
    if (loopMode == LoopMode::Single && !m_currentFile.empty()) {
        logger().info("Loop mode: Single, replaying current file");
        if (m_player) {
            // Clear display frame to avoid showing last frame briefly
            m_displayFrame = VideoFrame();
            m_player->seek(0);
            play();
        }
        return;
    }

    // 不循环：播放完就停止
    if (loopMode == LoopMode::None) {
        logger().info("Loop mode: None, stopping after current file");
        return;
    }

    // 列表循环（默认行为）
    if (m_currentSeries) {
        size_t nextIndex = m_currentSeries->currentIndex + 1;
        if (nextIndex < m_currentSeries->episodes.size()) {
            logger().info("Auto-playing next episode: " +
                                    std::to_string(nextIndex + 1));
            playEpisode(nextIndex);
            return;
        }
        if (!m_playlist.empty()) {
            size_t nextPlaylistIndex = m_currentIndex + 1;
            if (nextPlaylistIndex >= m_playlist.size()) {
                nextPlaylistIndex = 0;
            }
            if (m_playlist.size() > 1 || m_playlist[nextPlaylistIndex] != m_currentFile) {
                logger().info("Series finished, continuing playlist");
                playFromPlaylist(nextPlaylistIndex);
            }
        }
        return;
    }

    if (!m_playlist.empty()) {
        size_t nextIndex = m_currentIndex + 1;
        if (nextIndex >= m_playlist.size()) {
            nextIndex = 0;
        }
        if (m_playlist.size() > 1 || m_playlist[nextIndex] != m_currentFile) {
            playFromPlaylist(nextIndex);
        }
    }
}

} // namespace VideoPlay
