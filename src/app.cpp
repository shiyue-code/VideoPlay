#include "app.h"
#include "core/settings.h"
#include "subtitles/subtitleparser.h"
#include "utils/logger.h"

#include <SDL3/SDL.h>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>

namespace {
    using Clock = std::chrono::high_resolution_clock;
    inline double elapsedMs(Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
}

namespace VideoPlay {

namespace {
    const char* APP_TITLE = "VideoPlay - FFmpeg + SDL";
    const int DEFAULT_WIDTH = 1280;
    const int DEFAULT_HEIGHT = 720;
    const int MIN_VOLUME = 0;
    const int MAX_VOLUME = 100;
}

VideoPlayerApp::VideoPlayerApp() = default;

VideoPlayerApp::~VideoPlayerApp() {
    shutdown();
}

bool VideoPlayerApp::initialize() {
    Logger::instance().info("Initializing VideoPlayerApp...");

    // 创建渲染器
    m_renderer = std::make_unique<SDLRenderer>();
    if (!m_renderer->initialize(APP_TITLE, DEFAULT_WIDTH, DEFAULT_HEIGHT)) {
        Logger::instance().error("Failed to initialize SDL renderer");
        return false;
    }

    // 默认启用无边框模式
    m_renderer->toggleBorderless();

    // 创建播放器
    m_player = std::make_unique<FFmpegPlayer>();
    
    // 设置回调
    m_player->setPositionCallback([this](int64_t pos) {
        onPositionChanged(pos);
    });
    m_player->setDurationCallback([this](int64_t dur) {
        onDurationChanged(dur);
    });
    m_player->setStateCallback([this](PlaybackState state) {
        onStateChanged(state);
    });
    m_player->setErrorCallback([this](const std::string& err) {
        onError(err);
    });

    // 设置渲染器回调
    m_renderer->setFileDropCallback([this](const std::string& path) {
        openFile(path);
    });
    m_renderer->setFileOpenCallback([this]() {
        openFileDialog();
    });
    m_renderer->setPlayPauseCallback([this]() {
        togglePlayPause();
    });
    m_renderer->setStopCallback([this]() {
        stop();
    });
    m_renderer->setPrevCallback([this]() {
        playPrevious();
    });
    m_renderer->setNextCallback([this]() {
        playNext();
    });
    m_renderer->setSeekCallback([this](double pos) {
        // pos < 1000 表示相对跳转（秒），否则是绝对位置
        if (pos < 1000) {
            seek(pos * 1000); // 转换为毫秒
        } else {
            seekTo((pos - 1000) / 1000); // 绝对位置
        }
    });
    m_renderer->setVolumeCallback([this](int delta) {
        if (delta >= 1000) {
            m_volume = delta - 1000;
        } else {
            m_volume += delta;
        }
        m_volume = std::max(MIN_VOLUME, std::min(MAX_VOLUME, m_volume));
        if (m_player) {
            m_player->setVolume(m_volume);
        }
        Logger::instance().info("Volume set to: " + std::to_string(m_volume));
    });
    m_renderer->setMuteCallback([this]() {
        toggleMute();
    });
    m_renderer->setSpeedCallback([this](double speed) {
        if (speed == 0) {
            cycleSpeed();
        } else {
            setSpeed(speed);
        }
    });
    m_renderer->setFullscreenCallback([this]() {
        // 全屏切换由渲染器处理
    });
    m_renderer->setPlaylistItemCallback([this](size_t index) {
        playFromPlaylist(static_cast<int>(index));
    });
    m_renderer->setEpisodeItemCallback([this](size_t index) {
        playEpisode(index);
    });
    m_renderer->setEpisodePrevCallback([this]() {
        playPreviousEpisode();
    });
    m_renderer->setEpisodeNextCallback([this]() {
        playNextEpisode();
    });
    m_renderer->setLoopModeCallback([this](int mode) {
        Settings::instance().setLoopMode(static_cast<LoopMode>(mode));
        m_renderer->setLoopMode(mode);
    });
    m_renderer->setMenuCallback([this](int menuId) {
        handleMenu(menuId);
    });

    // 恢复循环模式设置
    auto loopMode = Settings::instance().loopMode();
    m_renderer->setLoopMode(static_cast<int>(loopMode));

    // 恢复画面比例设置
    auto aspectMode = Settings::instance().aspectMode();
    m_renderer->setAspectMode(aspectMode);
    m_renderer->setKeyCallback([this](int key, bool pressed) {
        if (!pressed) return;
        
        switch (key) {
            case SDLK_O:
                if (SDL_GetModState() & SDL_KMOD_CTRL) {
                    openFileDialog();
                }
                break;
            case SDLK_M:
                toggleMute();
                break;
            case SDLK_F1:
                showHelp();
                break;
            case SDLK_E:
                if (SDL_GetModState() & SDL_KMOD_CTRL) {
                    m_renderer->toggleEpisodePanel();
                }
                break;
            case SDLK_L:
                if (SDL_GetModState() & SDL_KMOD_CTRL) {
                    m_renderer->togglePlaylistPanel();
                }
                break;
        }
    });

    // 创建字幕解析器
    m_subtitleParser = std::make_unique<SubtitleParser>();

    Logger::instance().info("VideoPlayerApp initialized successfully");
    return true;
}

void VideoPlayerApp::shutdown() {
    Logger::instance().info("Shutting down VideoPlayerApp...");

    // 正常退出时保存剧集进度、当前进度并清除会话标记
    if (m_currentSeries) {
        saveSeriesProgress();
    }
    if (!m_currentFile.empty() && m_position > 0) {
        Settings::instance().setLastPosition(m_currentFile, m_position);
    }
    Settings::instance().clearLastSession();

    if (m_player) {
        m_player->stop();
    }

    m_renderer.reset();
    m_player.reset();
    m_subtitleParser.reset();

    Logger::instance().info("VideoPlayerApp shutdown complete");
}

int VideoPlayerApp::run(int argc, char* argv[]) {
    if (!initialize()) {
        return 1;
    }

    m_running = true;

    // 如果没有命令行参数，尝试恢复上次意外停止的会话
    if (argc <= 1) {
        auto session = Settings::instance().lastSession();
        if (session.hasValidSession && !session.filePath.empty() &&
            std::filesystem::exists(session.filePath)) {
            Logger::instance().info("Restoring last session: " + session.filePath);
            // 重建单文件播放列表
            m_playlist.push_back(session.filePath);
            m_currentIndex = 0;
            m_progressCacheDirty = true;
            if (m_player->loadFile(session.filePath)) {
                m_currentFile = session.filePath;
                m_renderer->setWindowTitle(
                    std::filesystem::path(session.filePath).filename().string() + " - " + APP_TITLE);
                loadSubtitle(session.filePath);
                detectSeries(session.filePath);
                play();
                // 恢复播放位置（如果记忆位置开启且未接近结尾）
                if (Settings::instance().rememberPosition() &&
                    session.duration > 0 && session.position > 0 &&
                    session.position < session.duration - 5000) {
                    m_player->seek(session.position);
                    m_seekTargetPosition = session.position;
                }
            }
        }
    }

    // 处理命令行参数
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (std::filesystem::exists(arg)) {
            addToPlaylist(arg);
        }
    }

    // 自动播放第一个文件（仅在命令行传入或恢复失败时）
    if (!m_playlist.empty() && m_currentFile.empty()) {
        playFromPlaylist(0);
    }

    // 运行主循环
    runMainLoop();

    shutdown();
    return 0;
}

void VideoPlayerApp::runMainLoop() {
    Logger::instance().info("Entering main loop");

    using Clock = std::chrono::high_resolution_clock;
    auto frameStart = Clock::now();

    while (m_running) {
        // 处理事件
        if (!m_renderer->processEvents()) {
            m_running = false;
            break;
        }

        // 渲染
        render();

        // 控制帧率 (~60 FPS)，仅在提前完成时补延迟
        auto frameEnd = Clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart).count();
        if (elapsedMs < 16) {
            SDL_Delay(static_cast<Uint32>(16 - elapsedMs));
        }
        frameStart = Clock::now();
    }
}

void VideoPlayerApp::render() {
    // 播放开始后重置手动操作标志，确保正常结束能自动连播
    if (m_isPlaying) {
        m_isManualOperation = false;
    }
    if (m_pendingAutoAdvance.exchange(false)) {
        autoAdvanceAfterStop();
    }

    auto t0 = Clock::now();
    // 用音频播放进度作为当前显示时间和 UI 时间（考虑倍速）
    if (m_player) {
        int64_t audioPos = m_player->audioPositionMs();
        if (audioPos >= 0) {
            m_position = audioPos;
        }
    }
    // seek 后异步生效前，用目标位置覆盖 UI，防止进度条跳回原位
    if (m_seekTargetPosition >= 0) {
        if (std::llabs(m_position - m_seekTargetPosition) < 500) {
            m_seekTargetPosition = -1;
        } else {
            m_position = m_seekTargetPosition;
        }
    }
    double dtPos = elapsedMs(t0);

    // 检查预缓冲是否完成
    if (m_player && m_player->isPreloading()) {
        if (m_player->checkPreloadComplete()) {
            m_isPlaying = true;
        }
    }

    // 根据当前播放时间从队列获取对应视频帧
    VideoFrame frame;
    bool gotFrame = false;
    if (m_player) {
        gotFrame = m_player->getVideoFrame(m_position, frame);
    }
    if (gotFrame) {
        m_displayFrame = std::move(frame);
    } else if (m_displayFrame.data.empty() && m_player) {
        // 启动时如果严格同步拿不到帧（音频时间还没到第一帧 pts），
        // fallback 取最早的一帧，避免黑屏
        if (m_player->getVideoFrame(-1, frame)) {
            m_displayFrame = std::move(frame);
        }
    }
    
    // 停止状态清空显示帧，确保画面归零
    if (!m_isPlaying && m_position == 0) {
        m_displayFrame = VideoFrame();
    }
    
    double dtGet = elapsedMs(t0);

    auto t1 = Clock::now();
    // 开始渲染
    m_renderer->clear();
    double dtClear = elapsedMs(t1);

    auto t2 = Clock::now();
    // 渲染视频帧
    if (!m_displayFrame.data.empty()) {
        m_renderer->renderFrame(m_displayFrame);
    }
    double dtRenderFrame = elapsedMs(t2);

    auto t3 = Clock::now();
    // 计算音视频同步调试信息
    int64_t audioPts = m_position;
    int64_t videoPts = m_displayFrame.pts;
    double avDiff = 0.0;
    if (videoPts > 0 && audioPts > 0) {
        avDiff = (audioPts - videoPts) / 1000.0;
    }
    
    // 获取当前字幕
    std::string subtitleText;
    if (m_subtitleParser && m_subtitleParser->isLoaded()) {
        subtitleText = m_subtitleParser->subtitleAt(m_position);
    }

    bool isPreloading = (m_player && m_player->isPreloading());

    // 更新进度缓存（仅在脏时重建，避免每帧 Settings 加锁）
    if (m_progressCacheDirty) {
        m_cachedPlaylistProgress.clear();
        m_cachedPlaylistProgress.reserve(m_playlist.size());
        for (const auto& path : m_playlist) {
            int64_t pos = Settings::instance().lastPosition(path);
            int64_t dur = Settings::instance().lastDuration(path);
            if (dur > 0) {
                m_cachedPlaylistProgress.push_back(std::min(1.0f, static_cast<float>(pos) / static_cast<float>(dur)));
            } else {
                m_cachedPlaylistProgress.push_back(0.0f);
            }
        }

        m_cachedEpisodeProgress.clear();
        if (m_currentSeries) {
            m_cachedEpisodeProgress.reserve(m_currentSeries->episodes.size());
            for (const auto& ep : m_currentSeries->episodes) {
                int64_t pos = Settings::instance().lastPosition(ep.path);
                int64_t dur = Settings::instance().lastDuration(ep.path);
                if (dur > 0) {
                    m_cachedEpisodeProgress.push_back(std::min(1.0f, static_cast<float>(pos) / static_cast<float>(dur)));
                } else {
                    m_cachedEpisodeProgress.push_back(pos > 0 ? 0.01f : 0.0f);
                }
            }
        }
        m_progressCacheDirty = false;
    }
    m_renderer->setPlaylistProgress(m_cachedPlaylistProgress);
    m_renderer->setEpisodeProgress(m_cachedEpisodeProgress);

    // Prev/Next Tooltip 智能语义
    if (m_currentSeries) {
        m_renderer->setPrevNextTooltip("上一集", "下一集");
    } else {
        m_renderer->setPrevNextTooltip("上一个", "下一个");
    }

    // 渲染 UI
    m_renderer->renderUI(
        m_position,
        m_duration,
        m_volume,
        m_isMuted,
        m_isPlaying,
        m_speed,
        m_currentFile,
        subtitleText,
        m_playlist,
        m_currentIndex,
        audioPts,
        videoPts,
        avDiff,
        isPreloading
    );
    double dtRenderUI = elapsedMs(t3);

    auto t4 = Clock::now();
    // 呈现
    m_renderer->present();
    double dtPresent = elapsedMs(t4);

    double total = dtGet + dtClear + dtRenderFrame + dtRenderUI + dtPresent;
    if (total > 10.0) {
        Logger::instance().debug("[PERF] render get=" + std::to_string(dtGet) +
            "(pos=" + std::to_string(dtPos) +
            ",vframe=" + std::to_string(dtGet - dtPos) + ")" +
            " clear=" + std::to_string(dtClear) +
            " frame=" + std::to_string(dtRenderFrame) +
            " ui=" + std::to_string(dtRenderUI) +
            " present=" + std::to_string(dtPresent) +
            " total=" + std::to_string(total) + "ms");
    }
    
    static auto reportStart = Clock::now();
    static int renderCount = 0;
    renderCount++;
    if (elapsedMs(reportStart) >= 1000.0) {
        Logger::instance().debug("[PERF] render fps=" + std::to_string(renderCount));
        renderCount = 0;
        reportStart = Clock::now();
    }

    // 定期保存会话状态和播放位置（每 5 秒），并刷新进度缓存
    if (m_isPlaying && !m_currentFile.empty() && m_duration > 0) {
        uint64_t now = SDL_GetTicks();
        if (now - m_lastSessionSaveTime >= 5000) {
            SessionInfo session;
            session.filePath = m_currentFile;
            session.position = m_position;
            session.duration = m_duration;
            session.playlistIndex = m_currentIndex;
            session.hasValidSession = true;
            Settings::instance().setLastSession(session);
            Settings::instance().setLastPosition(m_currentFile, m_position);
            m_progressCacheDirty = true; // 刷新剧集/播放列表面板的进度显示
            m_lastSessionSaveTime = now;
        }
    }
}

void VideoPlayerApp::openFile(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        Logger::instance().error("File not found: " + path);
        return;
    }

    Logger::instance().info("Opening file: " + path);

    // 停止当前播放
    stop();

    // 添加到播放列表
    addToPlaylist(path);
    m_progressCacheDirty = true;

    // 加载文件
    if (m_player->loadFile(path)) {
        m_currentFile = path;

        // 添加到最近文件
        Settings::instance().addRecentFile(path);

        // 更新窗口标题
        std::string title = std::filesystem::path(path).filename().string() + " - " + APP_TITLE;
        m_renderer->setWindowTitle(title);

        // 尝试加载同名字幕
        loadSubtitle(path);

        // 检测剧集
        detectSeries(path);

        // 开始播放
        play();

        // 恢复上次观看位置（如果记忆位置开启且未接近结尾）
        if (Settings::instance().rememberPosition()) {
            int64_t lastPos = Settings::instance().lastPosition(path);
            int64_t lastDur = Settings::instance().lastDuration(path);
            if (lastPos > 0 && lastDur > 0 && lastPos < lastDur - 5000) {
                m_player->seek(lastPos);
                m_seekTargetPosition = lastPos;
            }
        }

        // 立即保存会话，用于意外停止恢复
        SessionInfo session;
        session.filePath = path;
        session.position = 0;
        session.duration = m_duration;
        session.playlistIndex = m_currentIndex;
        session.hasValidSession = true;
        Settings::instance().setLastSession(session);
        m_lastSessionSaveTime = SDL_GetTicks();

        // 更新菜单中的最近文件列表
        if (m_renderer) {
            m_renderer->updateRecentFilesMenu();
        }
    } else {
        Logger::instance().error("Failed to load file: " + path);
        if (m_renderer) {
            m_renderer->showMessageBox("打开文件失败", "无法加载文件: " + path + "\n请检查文件格式是否受支持。", true);
        }
    }
}

void VideoPlayerApp::openFileDialog() {
    if (!m_renderer) return;

    Logger::instance().info("Opening file dialog...");

    m_renderer->openFileDialog([this](const std::string& filePath) {
        if (!filePath.empty()) {
            Logger::instance().info("Selected file: " + filePath);
            openFile(filePath);
        } else {
            Logger::instance().info("File dialog cancelled");
        }
    });
}

void VideoPlayerApp::openFolderDialog() {
    if (!m_renderer) return;

    Logger::instance().info("Opening folder dialog...");

    m_renderer->openFolderDialog([this](const std::string& folderPath) {
        if (folderPath.empty()) {
            Logger::instance().info("Folder dialog cancelled");
            return;
        }

        Logger::instance().info("Selected folder: " + folderPath);

        std::vector<std::string> mediaFiles;
        const std::vector<std::string> extensions = {
            ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm",
            ".mp3", ".aac", ".wav", ".flac", ".ogg"
        };

        try {
            for (const auto& entry : std::filesystem::directory_iterator(folderPath)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) {
                        mediaFiles.push_back(entry.path().string());
                    }
                }
            }
        } catch (const std::exception& e) {
            Logger::instance().error("Failed to read folder: " + std::string(e.what()));
            return;
        }

        if (mediaFiles.empty()) {
            Logger::instance().info("No media files found in folder");
            return;
        }

        std::sort(mediaFiles.begin(), mediaFiles.end());

        // 添加到播放列表
        for (const auto& file : mediaFiles) {
            addToPlaylist(file);
        }
        m_progressCacheDirty = true;

        Logger::instance().info("Added " + std::to_string(mediaFiles.size()) + " files from folder");

        // 如果当前没有播放文件，自动播放第一个
        if (m_currentFile.empty()) {
            playFromPlaylist(0);
        }
    });
}

void VideoPlayerApp::loadSubtitle(const std::string& videoPath) {
    std::filesystem::path video(videoPath);
    std::filesystem::path srtPath = video.parent_path() / (video.stem().string() + ".srt");
    std::filesystem::path assPath = video.parent_path() / (video.stem().string() + ".ass");
    std::filesystem::path vttPath = video.parent_path() / (video.stem().string() + ".vtt");

    if (std::filesystem::exists(srtPath)) {
        m_currentSubtitle = srtPath.string();
    } else if (std::filesystem::exists(assPath)) {
        m_currentSubtitle = assPath.string();
    } else if (std::filesystem::exists(vttPath)) {
        m_currentSubtitle = vttPath.string();
    } else {
        m_currentSubtitle.clear();
        if (m_subtitleParser) {
            m_subtitleParser->clear();
        }
        return;
    }

    Logger::instance().info("Loading subtitle: " + m_currentSubtitle);
    if (m_subtitleParser) {
        if (!m_subtitleParser->loadFile(m_currentSubtitle)) {
            Logger::instance().warning("Failed to parse subtitle: " + m_currentSubtitle);
            m_currentSubtitle.clear();
        }
    }
}

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

void VideoPlayerApp::addToPlaylist(const std::string& path) {
    // 检查是否已存在
    auto it = std::find(m_playlist.begin(), m_playlist.end(), path);
    if (it == m_playlist.end()) {
        m_playlist.push_back(path);
        m_progressCacheDirty = true;
        Logger::instance().info("Added to playlist: " + path);
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
        Logger::instance().debug("Already playing: " + m_playlist[index]);
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
        Logger::instance().debug("Already playing episode: " + path);
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
        Logger::instance().info("Loop mode: Single, replaying current file");
        if (m_player) {
            m_player->seek(0);
            play();
        }
        return;
    }

    // 不循环：播放完就停止
    if (loopMode == LoopMode::None) {
        Logger::instance().info("Loop mode: None, stopping after current file");
        return;
    }

    // 列表循环（默认行为）
    if (m_currentSeries) {
        size_t nextIndex = m_currentSeries->currentIndex + 1;
        if (nextIndex < m_currentSeries->episodes.size()) {
            Logger::instance().info("Auto-playing next episode: " +
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
                Logger::instance().info("Series finished, continuing playlist");
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

void VideoPlayerApp::onPositionChanged(int64_t position) {
    // 忽略视频解码驱动的位置更新，UI 时间由 render() 中的音频播放时间统一驱动
    (void)position;
}

void VideoPlayerApp::onDurationChanged(int64_t duration) {
    m_duration = duration;
    if (!m_currentFile.empty() && duration > 0) {
        Settings::instance().setLastDuration(m_currentFile, duration);
        m_progressCacheDirty = true;
    }
    Logger::instance().info("Duration: " + std::to_string(duration) + "ms");
}

void VideoPlayerApp::onStateChanged(PlaybackState state) {
    switch (state) {
        case PlaybackState::Playing:
            m_isPlaying = true;
            Logger::instance().info("State changed: Playing");
            break;
        case PlaybackState::Paused:
            m_isPlaying = false;
            Logger::instance().info("State changed: Paused");
            break;
        case PlaybackState::Stopped:
            m_isPlaying = false;
            Logger::instance().info("State changed: Stopped");
            // 自动播放下一个（仅当不是手动停止/切换时）
            if (m_isManualOperation.exchange(false)) {
                break;
            }
            m_pendingAutoAdvance = true;
            break;
    }
}

void VideoPlayerApp::onError(const std::string& error) {
    Logger::instance().error("Player error: " + error);
}

void VideoPlayerApp::handleMenu(int menuId) {
    // 最近文件菜单项 ID 范围 100-109
    if (menuId >= 100 && menuId < 110) {
        auto recent = Settings::instance().recentFiles();
        size_t idx = static_cast<size_t>(menuId - 100);
        if (idx < recent.size()) {
            openFile(recent[idx]);
        }
        return;
    }

    switch (menuId) {
        case 1: // 打开文件
            openFileDialog();
            break;
        case 2: // 打开文件夹
            openFolderDialog();
            break;
        case 4: // 导入字幕
            openSubtitleDialog();
            break;
        case 3: // 退出
            m_running = false;
            break;
        case 10: // 播放/暂停
            togglePlayPause();
            break;
        case 11: // 停止
            stop();
            break;
        case 12: // 上一个
            playPrevious();
            break;
        case 13: // 下一个
            playNext();
            break;
        case 18: // 播放列表
            m_renderer->togglePlaylistPanel();
            break;
        case 19: // 上一集（播放菜单就近入口）
            playPreviousEpisode();
            break;
        case 22: // 下一集（播放菜单就近入口）
            playNextEpisode();
            break;
        case 30: // 上一集
            playPreviousEpisode();
            break;
        case 31: // 下一集
            playNextEpisode();
            break;
        case 32: // 切换选集面板
            m_renderer->toggleEpisodePanel();
            break;
        case 14: // 增加速度
            setSpeed(m_speed * 1.25);
            break;
        case 15: // 降低速度
            setSpeed(m_speed * 0.8);
            break;
        case 16: // 全屏
            if (m_renderer) m_renderer->toggleFullscreen();
            break;
        case 17: // 无边框模式
            if (m_renderer) m_renderer->toggleBorderless();
            break;
        case 50: // 快捷键
            showHelp();
            break;
        case 51: // 关于
            showAbout();
            break;
        case 60: // 不循环
            Settings::instance().setLoopMode(LoopMode::None);
            if (m_renderer) m_renderer->setLoopMode(0);
            break;
        case 61: // 单曲循环
            Settings::instance().setLoopMode(LoopMode::Single);
            if (m_renderer) m_renderer->setLoopMode(1);
            break;
        case 62: // 列表循环
            Settings::instance().setLoopMode(LoopMode::Playlist);
            if (m_renderer) m_renderer->setLoopMode(2);
            break;
        case 70: // 原始比例
            Settings::instance().setAspectMode(AspectMode::Original);
            if (m_renderer) m_renderer->setAspectMode(AspectMode::Original);
            break;
        case 71: // 16:9
            Settings::instance().setAspectMode(AspectMode::R16_9);
            if (m_renderer) m_renderer->setAspectMode(AspectMode::R16_9);
            break;
        case 72: // 4:3
            Settings::instance().setAspectMode(AspectMode::R4_3);
            if (m_renderer) m_renderer->setAspectMode(AspectMode::R4_3);
            break;
        case 73: // 铺满
            Settings::instance().setAspectMode(AspectMode::FillWindow);
            if (m_renderer) m_renderer->setAspectMode(AspectMode::FillWindow);
            break;
    }
}

void VideoPlayerApp::openSubtitleDialog() {
    if (!m_renderer) return;

    Logger::instance().info("Opening subtitle dialog...");

    m_renderer->openSubtitleDialog([this](const std::string& filePath) {
        if (!filePath.empty()) {
            Logger::instance().info("Selected subtitle: " + filePath);
            loadSubtitleFile(filePath);
        } else {
            Logger::instance().info("Subtitle dialog cancelled");
        }
    });
}

void VideoPlayerApp::loadSubtitleFile(const std::string& path) {
    if (!m_subtitleParser) return;

    if (m_subtitleParser->loadFile(path)) {
        m_currentSubtitle = path;
        Logger::instance().info("Loaded subtitle: " + path);
    } else {
        Logger::instance().error("Failed to load subtitle: " + path);
        m_currentSubtitle.clear();
    }
}

void VideoPlayerApp::showHelp() {
    const char* helpText = 
        "快捷键列表：\n\n"
        "空格          - 播放/暂停\n"
        "S             - 停止\n"
        "← / →         - 后退/前进 5秒\n"
"↑ / ↓         - 音量增加/减少\n"
        "M             - 静音切换\n"
        ".             - 切换播放速度\n"
        "F             - 全屏切换\n"
        "N             - 下一个（优先下一集）\n"
        "P             - 上一个（优先上一集）\n"
        "Ctrl+Shift+Right - 下一集\n"
        "Ctrl+Shift+Left  - 上一集\n"
        "Ctrl+E        - 切换选集面板\n"
        "Ctrl+L        - 切换播放列表\n"
        "Ctrl+O        - 打开文件\n"
        "F1            - 显示帮助\n"
        "Esc           - 退出全屏/关闭菜单\n\n"
        "鼠标操作：\n"
        "左键点击控制栏按钮\n"
        "拖动进度条跳转\n"
        "滚轮调节音量\n"
        "拖放文件到窗口播放";
    
    if (m_renderer) {
        m_renderer->showMessageBox("帮助", helpText, false);
    }
}

void VideoPlayerApp::showAbout() {
    const char* aboutText = 
        "VideoPlay v" APP_VERSION "\n\n"
        "基于 FFmpeg + SDL3 的视频播放器\n\n"
        "功能特性：\n"
        "- 支持多种视频格式\n"
        "- 变速播放 (0.25x - 4x)\n"
        "- 字幕支持 (SRT/ASS/VTT)\n"
        "- 播放列表管理\n\n"
        "License: GPLv3";
    
    if (m_renderer) {
        m_renderer->showMessageBox("关于", aboutText, false);
    }
}

} // namespace VideoPlay
