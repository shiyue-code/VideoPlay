#pragma once

#include "core/common.h"
#include "core/episodedetector.h"
#include "core/ffmpegplayer.h"
#include "renderer/sdlrenderer.h"

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <optional>

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

    // 菜单处理
    void handleMenu(int menuId);
    void showHelp();
    void showAbout();

    // 播放列表
    void addToPlaylist(const std::string& path);
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

    // 播放列表
    std::vector<std::string> m_playlist;
    size_t m_currentIndex = 0;

    // 状态
    std::atomic<bool> m_running{false};
    std::string m_currentFile;
    std::string m_currentSubtitle;
    int64_t m_duration = 0;
    int64_t m_position = 0;
    int64_t m_seekTargetPosition = -1; // seek 后用于覆盖 UI 位置，防止进度条跳回
    bool m_isPlaying = false;
    int m_volume = 100;
    bool m_isMuted = false;
    double m_speed = 1.0;

    // 剧集
    std::optional<SeriesGroup> m_currentSeries;

    // 手动操作标志（防止 stop/playEpisode 触发自动连播）
    std::atomic<bool> m_isManualOperation{false};
    std::atomic<bool> m_pendingAutoAdvance{false};

    // 视频帧缓冲
    VideoFrame m_displayFrame;
    VideoFrame m_pendingFrame;
};

} // namespace VideoPlay
