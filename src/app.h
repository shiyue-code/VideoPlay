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
#include <unordered_map>

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
    void openUrlDialog();
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
    void seekToMs(int64_t targetPos, OSDType osdType, float osdRatio = -1.0f); // 绝对定位统一出口
    void setVolume(int delta);
    void setVolumeTo(int value);
    void toggleMute();
    void setSpeed(double speed);
    void cycleSpeed();
    void toggleAlwaysOnTop();
    void takeScreenshot();
    void copyScreenshotToClipboard();
    void exportGifClip();   // AB 区间已设则直接用，否则弹时长输入框
    void startGifExport(int64_t startMs, int64_t durationMs);
    VideoFrame captureCurrentFrame(); // 当前显示帧套用画面变换；失败返回空帧
    void stepFrame(int direction);    // 逐帧步进：-1 上一帧 / +1 下一帧
    void showGotoTimeDialog();        // Ctrl+G 跳转到时间
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

    // 文件关联（Windows 默认播放器注册）
    void registerAsDefaultPlayer();
    void removeFileAssociations();

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
    void showSubtitleStyleDialog();
    std::vector<SearchResult> performSearch(const std::string& query);

    // 播放列表
    void addToPlaylist(const std::string& path);
    void removeFromPlaylist(size_t index);
    void clearPlaylist();
    void persistPlaylist();
    void reorderPlaylist(size_t fromIndex, size_t toIndex);
    void playNext();
    void playPrevious();
    void playFromPlaylist(size_t index);
    // M3U/M3U8 清单：本地文件读取、远程 .m3u 抓取后展开入列；
    // 已处理（含空清单）返回 true，HLS 或读取失败返回 false 让 FFmpeg 接手
    bool openM3uSource(const std::string& source);

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
    // M3U #EXTINF 标题（URI → 显示名），仅会话内有效，随清空播放列表清除
    std::unordered_map<std::string, std::string> m_playlistTitles;
    // 清单展开防递归：展开期间点击到嵌套清单直接跳过
    bool m_m3uExpanding = false;

    // 状态
    std::atomic<bool> m_running{false};
    std::string m_currentFile;
    std::vector<Bookmark> m_bookmarks;
    std::string m_currentSubtitle;
    int64_t m_duration = 0;
    int64_t m_position = 0;
    // loadFile 进行中的目标路径：duration 回调早于 m_currentFile 更新，
    // 用它给 onDurationChanged 定位文件（回调持锁，不能回调 player）
    std::string m_loadingFile;
    int64_t m_seekTargetPosition = -1; // seek 后用于覆盖 UI 位置，防止进度条跳回
    // 逐帧步进（帧驱动，不经时间估算）：
    //   +1 → m_stepping：暂停态下恢复播放，render() 采到 pts 更新的帧立即再暂停
    //   -1 → m_waitingStepFrame：seekStepBack 后由 render() 经
    //        getStepLandingFrame 取回落点帧（pts < 锚点的最大帧）
    bool m_stepping = false;
    int64_t m_stepPrevPts = -1;
    bool m_waitingStepFrame = false;
    std::chrono::steady_clock::time_point m_stepDeadline; // 步进超时（+1:1s，-1:8s）
    int m_pendingStepDir = 0;   // 步进飞行期累计的方向键数，落点到达后逐帧执行
    // 右上角实时统计：上屏帧率按滚动窗口计数（显示帧 pts 变化即 +1）
    double m_displayFps = 0.0;
    int m_fpsFrameCount = 0;
    int64_t m_lastDisplayPts = -1;
    std::chrono::steady_clock::time_point m_fpsWindowStart =
        std::chrono::steady_clock::now();
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
    // 播放列表显示名缓存（#EXTINF 标题优先，与 m_playlist 等长对齐）
    std::vector<std::string> m_cachedPlaylistDisplay;
    bool m_progressCacheDirty = true;

    // 手动操作标志（防止 stop/playEpisode 触发自动连播）
    std::atomic<bool> m_isManualOperation{false};
    std::atomic<bool> m_pendingAutoAdvance{false};

    // 解码线程传来的待显示错误，主循环消费后弹窗
    std::mutex m_pendingErrorMutex;
    std::string m_pendingError;

    // 自动恢复：会话状态定期保存（每 5 秒）
    uint64_t m_lastSessionSaveTime = 0;

    // 视频帧缓冲
    VideoFrame m_displayFrame;
    VideoFrame m_pendingFrame;

    // 画面调节配置延迟落盘（拖动滑杆连续触发回调，500ms 防抖）
    bool m_videoFilterSavePending = false;
    std::chrono::steady_clock::time_point m_videoFilterSaveDeadline;

    // AB 循环
    int64_t m_loopA = -1;
    int64_t m_loopB = -1;
    bool m_loopSeeking = false;

    // GIF 导出：后台 ffmpeg 子进程写状态，主线程在 render() 消费后置空
    struct GifExportState {
        std::mutex mutex;
        bool done = false;
        bool ok = false;
        std::string outputPath;
        std::string errorLog;
    };
    std::shared_ptr<GifExportState> m_gifExport;

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
