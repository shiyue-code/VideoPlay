#pragma once

#include "core/common.h"
#include "core/episodedetector.h"
#include "core/ffmpegplayer.h"
#include "renderer/sdlrenderer.h"
#include "ai/aianalyzer.h"
#include "ai/searchengine.h"
#include "renderer/settingsdialog.h"

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <optional>
#include <mutex>
#include <deque>

namespace VideoPlay {

class SubtitleParser;

class VideoPlayerApp {
public:
    VideoPlayerApp();
    ~VideoPlayerApp();

    // 运行应用
    int run(int argc, char* argv[]);

private:
    // 初始化
    bool initialize();
    void shutdown();

    // 文件操作
    void openFile(const std::string& path);
    void openFileDialog();
    void openFolderDialog();
    void openSubtitleDialog();
    void loadSubtitle(const std::string& videoPath);
    void loadSubtitleFile(const std::string& path);

    // 播放控制
    void play();
    void pause();
    void togglePlayPause();
    void stop();
    void seek(double deltaMs);       // 相对跳转 (毫秒)
    void seekTo(double position);    // 绝对位置 (0-1)
    void setVolume(int delta);
    void toggleMute();
    void setSpeed(double speed);
    void cycleSpeed();
    void toggleAlwaysOnTop();
    void setLoopPointA();
    void setLoopPointB();
    void clearLoop();

    // 书签
    void addBookmark();
    void clearBookmarks();
    void jumpToBookmark(int index);

    // 菜单处理 (int)
    void handleMenu(MenuId menuId);
    void showHelp();
    void showAbout();

    // AI 功能
    void startAIAnalysis();
    void handleSearch(const std::string& query);
    void handleSearchInternal(const std::string& query, bool addUserMessage);
    void processNextPendingSearch();
    void checkAISearchTimeout();
    void handleAIAnalysis();
    void showAISummary();
    void showSearchPanel();
    void clearAICache();
    void showAISettings();
    std::vector<SearchResult> performSearch(const std::string& query);

    // 播放列表
    void addToPlaylist(const std::string& path);
    void removeFromPlaylist(size_t index);
    void clearPlaylist();
    void persistPlaylist();
    void playNext();
    void playPrevious();
    void playFromPlaylist(size_t index);

    // 剧集管理
    void detectSeries(const std::string& path);
    void playEpisode(size_t index);
    void playNextEpisode();
    void playPreviousEpisode();
    void saveSeriesProgress();
    void restoreSeriesPosition();
    void autoAdvanceAfterStop();

    // 回调处理
    void onPositionChanged(int64_t position);
    void onDurationChanged(int64_t duration);
    void onStateChanged(PlaybackState state);
    void onError(const std::string& error);

    // 渲染循环
    void runMainLoop();
    void render();

    // 组件
    std::unique_ptr<SDLRenderer> m_renderer;
    std::unique_ptr<FFmpegPlayer> m_player;
    std::unique_ptr<SubtitleParser> m_subtitleParser;

    // 内封字幕回调状态（由解码线程写入，主线程读取）
    std::mutex m_embeddedSubtitleMutex;
    std::string m_embeddedSubtitleText;
    int64_t m_embeddedSubtitleStartMs = 0;
    int64_t m_embeddedSubtitleEndMs = 0;

    // 播放列表
    std::vector<std::string> m_playlist;
    size_t m_currentIndex = 0;

    // 状态
    std::atomic<bool> m_running{false};
    std::string m_currentFile;
    std::vector<Bookmark> m_bookmarks;
    std::string m_currentSubtitle;
    int64_t m_duration = 0;
    int64_t m_position = 0;
    int64_t m_seekTargetPosition = -1; // seek 后用于覆盖 UI 位置，防止进度条跳回
    bool m_isPlaying = false;
    int m_volume = 100;
    bool m_isMuted = false;
    double m_speed = 1.0;
    std::atomic<NetworkState> m_networkState{NetworkState::Idle};

    // 剧集
    std::optional<SeriesGroup> m_currentSeries;

    // 进度缓存（避免 render() 每帧查询 Settings）
    std::vector<float> m_cachedPlaylistProgress;
    std::vector<float> m_cachedEpisodeProgress;
    bool m_progressCacheDirty = true;

    // 手动操作标志（防止 stop/playEpisode 触发自动连播）
    std::atomic<bool> m_isManualOperation{false};
    std::atomic<bool> m_pendingAutoAdvance{false};

    // 自动恢复：会话状态定期保存（每 5 秒）
    uint64_t m_lastSessionSaveTime = 0;

    // 视频帧缓冲
    VideoFrame m_displayFrame;
    VideoFrame m_pendingFrame;

    // AB 循环
    int64_t m_loopA = -1;
    int64_t m_loopB = -1;
    bool m_loopSeeking = false;

    // AI 功能
    std::unique_ptr<AIAnalyzer> m_aiAnalyzer;
    std::unique_ptr<SearchEngine> m_searchEngine;
    AIAnalysisResult m_aiResult;
    std::mutex m_aiStateMutex;
    bool m_aiAnalyzing = false;
    bool m_aiDirectSearchActive = false;
    float m_aiProgress = 0.0f;
    std::string m_aiStatus;
    uint64_t m_aiRequestStartTimeMs = 0;
    std::atomic<uint64_t> m_aiRequestSeq{0};
    std::deque<std::string> m_pendingSearchQueries;
};

} // namespace VideoPlay
