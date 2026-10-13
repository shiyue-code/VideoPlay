#include "app.h"
#include "renderer/sdlrenderer.h"
#include "core/ffmpegplayer.h"
#include "core/settings.h"
#include "core/m3uplaylist.h"
#include "subtitles/subtitleparser.h"
#include "utils/logger.h"
#include "utils/string_utils.h"
#include "core/episodedetector.h"
#ifdef _WIN32
#include "ai/httpclient.h"
#endif
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto logger = Logger::get("app.playlist");
    return *logger;
}

// M3U 展开期置位、作用域结束复位（含异常路径）
struct M3uExpandGuard {
    bool& flag;
    explicit M3uExpandGuard(bool& f) : flag(f) { flag = true; }
    ~M3uExpandGuard() { flag = false; }
};
}



void VideoPlayerApp::addToPlaylist(const std::string& path) {
    // 检查是否已存在
    auto it = std::find(m_playlist.begin(), m_playlist.end(), path);
    if (it == m_playlist.end()) {
        m_playlist.push_back(path);
        m_progressCacheDirty = true;
        logger().info("Added to playlist: " + path);
        persistPlaylist();
    }
}

void VideoPlayerApp::persistPlaylist() {
    Settings::instance().setPlaylist(m_playlist, m_currentIndex);
}

void VideoPlayerApp::removeFromPlaylist(size_t index) {
    if (index >= m_playlist.size()) {
        return;
    }

    const std::string removed = m_playlist[index];
    const bool removingCurrent = (!m_currentFile.empty() && removed == m_currentFile);

    m_playlist.erase(m_playlist.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_currentIndex > index) {
        m_currentIndex--;
    } else if (!m_playlist.empty() && m_currentIndex >= m_playlist.size()) {
        m_currentIndex = m_playlist.size() - 1;
    }

    m_progressCacheDirty = true;
    persistPlaylist();
    logger().info("Removed from playlist: " + removed);

    if (removingCurrent && !m_playlist.empty()) {
        playFromPlaylist(m_currentIndex);
    }

    if (m_renderer) {
        m_renderer->showOSD(OSDType::Info, "已从播放列表删除");
    }
}

void VideoPlayerApp::reorderPlaylist(size_t fromIndex, size_t toIndex) {
    if (fromIndex >= m_playlist.size() || toIndex >= m_playlist.size() || fromIndex == toIndex) {
        return;
    }

    std::string item = m_playlist[fromIndex];
    m_playlist.erase(m_playlist.begin() + static_cast<std::ptrdiff_t>(fromIndex));
    size_t insertIndex = toIndex;
    if (insertIndex > m_playlist.size()) {
        insertIndex = m_playlist.size();
    }
    m_playlist.insert(m_playlist.begin() + static_cast<std::ptrdiff_t>(insertIndex), item);

    auto it = std::find(m_playlist.begin(), m_playlist.end(), m_currentFile);
    if (it != m_playlist.end()) {
        m_currentIndex = static_cast<size_t>(std::distance(m_playlist.begin(), it));
    } else if (m_currentIndex >= m_playlist.size()) {
        m_currentIndex = m_playlist.empty() ? 0 : m_playlist.size() - 1;
    }

    m_progressCacheDirty = true;
    persistPlaylist();
    logger().info("Reordered playlist: " + std::to_string(fromIndex) + " -> " +
                  std::to_string(insertIndex));
}

void VideoPlayerApp::clearPlaylist() {
    m_playlist.clear();
    m_playlistTitles.clear();
    m_currentIndex = 0;
    m_progressCacheDirty = true;
    persistPlaylist();
    logger().info("Playlist cleared");
    if (m_renderer) {
        m_renderer->showOSD(OSDType::Info, "播放列表已清空");
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

bool VideoPlayerApp::openM3uSource(const std::string& source) {
    // 展开期间遇到嵌套清单直接跳过（条目保留在列表里，之后点击再展开），
    // 防止两个清单互相引用时递归爆栈
    if (m_m3uExpanding) {
        return true;
    }

    const bool remote = isNetworkUrl(source);
    // 远程 .m3u8 大概率是 HLS 流：不抓取，直接交 FFmpeg（其 hls 解复用器原生支持）
    if (remote) {
        std::string clean = source;
        size_t suffix = clean.find_first_of("?#");
        if (suffix != std::string::npos) {
            clean = clean.substr(0, suffix);
        }
        if (endsWith(toLower(clean), ".m3u8")) {
            return false;
        }
    }

    std::string content;
    if (remote) {
        // 远程 .m3u 是 IPTV 频道表：抓下来按清单解析
#ifdef _WIN32
        HttpClient http;
        http.setTimeout(15);
        HttpResponse resp = http.get(source);
        if (!resp.success() || resp.body.empty()) {
            logger().warning("Failed to fetch M3U playlist: " + source +
                             " (HTTP " + std::to_string(resp.statusCode) + ")");
            return false;
        }
        content = decodeToUtf8(std::move(resp.body));
#else
        return false;
#endif
    } else {
        std::ifstream file(std::filesystem::u8path(source), std::ios::binary);
        if (!file.is_open()) {
            logger().warning("Cannot open M3U playlist: " + source);
            return false;
        }
        content.assign(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
        content = decodeToUtf8(std::move(content));
    }

    M3uPlaylist list = parseM3uPlaylist(content);
    if (list.isHlsManifest) {
        return false;  // 本地 HLS 清单同样交 FFmpeg
    }

    stop();
    if (m_renderer) {
        m_renderer->clearPreview();
    }
    {
        std::lock_guard<std::mutex> lock(m_embeddedSubtitleMutex);
        m_embeddedSubtitleText.clear();
        m_embeddedSubtitleStartMs = 0;
        m_embeddedSubtitleEndMs = 0;
    }

    M3uExpandGuard guard(m_m3uExpanding);
    std::string firstUri;
    int total = 0;
    for (const auto& entry : list.entries) {
        std::string uri = resolveM3uUri(source, entry.uri);
        if (uri.empty()) continue;
        ++total;
        if (firstUri.empty()) {
            firstUri = uri;
        }
        if (!entry.title.empty()) {
            m_playlistTitles[uri] = entry.title;
        }
        addToPlaylist(uri);
    }
    m_progressCacheDirty = true;

    logger().info("M3U playlist expanded: " + source + ", " +
                  std::to_string(total) + " entries");

    if (firstUri.empty()) {
        if (m_renderer) {
            m_renderer->showOSD(OSDType::Info, "播放列表为空或无法解析");
        }
        return true;
    }

    Settings::instance().addRecentFile(source);
    if (m_renderer) {
        m_renderer->showOSD(OSDType::Info,
                            "已加载 " + std::to_string(total) + " 个条目");
        m_renderer->updateRecentFilesMenu();
    }

    auto it = std::find(m_playlist.begin(), m_playlist.end(), firstUri);
    if (it != m_playlist.end()) {
        m_isManualOperation = true;
        playFromPlaylist(static_cast<size_t>(std::distance(m_playlist.begin(), it)));
    }
    return true;
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
    persistPlaylist();
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

    try {
        logger().info("playEpisode: index=" + std::to_string(index) +
                      ", total=" + std::to_string(m_currentSeries->episodes.size()) +
                      ", path=" + m_currentSeries->episodes[index].path);

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
    } catch (const std::exception& e) {
        logger().error("playEpisode exception: " + std::string(e.what()));
        if (m_renderer) {
            m_renderer->showMessageBox("切换剧集失败", "无法切换到指定剧集:\n" + std::string(e.what()), true);
        }
    }
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

    try {
        std::string seriesKey = m_currentSeries->episodes[0].path;
        seriesKey = std::filesystem::u8path(seriesKey).parent_path().u8string() + "/" + m_currentSeries->seriesName;

        std::unordered_map<std::string, int64_t> positions;
        for (const auto& ep : m_currentSeries->episodes) {
            int64_t pos = Settings::instance().lastPosition(ep.path);
            if (pos > 0) {
                positions[ep.path] = pos;
            }
        }

        Settings::instance().setSeriesProgress(seriesKey, static_cast<int>(m_currentSeries->currentIndex), positions);
    } catch (const std::exception& e) {
        logger().error("Failed to save series progress: " + std::string(e.what()));
    }
}

void VideoPlayerApp::restoreSeriesPosition() {
    if (!m_currentSeries) return;

    try {
        std::string seriesKey = m_currentSeries->episodes[0].path;
        seriesKey = std::filesystem::u8path(seriesKey).parent_path().u8string() + "/" + m_currentSeries->seriesName;

        auto progress = Settings::instance().seriesProgress(seriesKey);
        if (progress.lastEpisodeIndex >= 0 &&
            static_cast<size_t>(progress.lastEpisodeIndex) < m_currentSeries->episodes.size()) {
            m_currentSeries->currentIndex = static_cast<size_t>(progress.lastEpisodeIndex);
        }
    } catch (const std::exception& e) {
        logger().error("Failed to restore series position: " + std::string(e.what()));
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
