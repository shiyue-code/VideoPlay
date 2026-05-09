#include "app.h"
#include "renderer/sdlrenderer.h"
#include "core/ffmpegplayer.h"
#include "core/settings.h"
#include "utils/logger.h"
#include "subtitles/subtitleparser.h"
#include "core/episodedetector.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <unordered_map>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#endif

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto logger = Logger::get("app");
    return *logger;
}
}


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
    Logger::root().configure(Settings::instance().logConfig());
    logger().info("Initializing VideoPlayerApp...");

    // 创建渲染器
    m_renderer = std::make_unique<SDLRenderer>();
    if (!m_renderer->initialize(APP_TITLE, DEFAULT_WIDTH, DEFAULT_HEIGHT)) {
        logger().error("Failed to initialize SDL renderer");
        return false;
    }

    // 默认启用无边框模式
    m_renderer->toggleBorderless();

    // 确保窗口获得焦点
    if (m_renderer->getWindow()) {
        SDL_RaiseWindow(m_renderer->getWindow());
    }

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
        logger().info("Volume set to: " + std::to_string(m_volume));
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
    m_renderer->setSubtitleSyncCallback([this](int deltaMs) {
        if (m_subtitleParser && m_subtitleParser->isLoaded()) {
            m_subtitleParser->adjustOffset(deltaMs);
            int64_t offset = m_subtitleParser->offset();
            std::string sign = offset >= 0 ? "+" : "";
            logger().info("Subtitle offset: " + sign + std::to_string(offset) + "ms");
        }
    });
    m_renderer->setABLoopCallback([this](char action) {
        switch (action) {
            case 'a': setLoopPointA(); break;
            case 'b': setLoopPointB(); break;
            case 'c': clearLoop(); break;
        }
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

    // 恢复窗口置顶状态
    if (Settings::instance().alwaysOnTop()) {
        m_renderer->toggleAlwaysOnTop();
    }

    // 恢复最大化状态（在无边框模式启用后应用）
    auto windowConfig = Settings::instance().windowConfig();
    if (windowConfig.maximized) {
        m_renderer->maximizeWindow();
    }
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

    // 初始化 AI 功能
    m_aiAnalyzer = std::make_unique<AIAnalyzer>();
    m_searchEngine = std::make_unique<SearchEngine>();
    
    // 配置 AI 分析器（始终加载配置，无论 cacheDir 是否为空）
    AIConfig aiConfig = Settings::instance().aiConfig();
    m_aiAnalyzer->configure(aiConfig);
    logger().info("[AI] Initialized with baseUrl: " + aiConfig.baseUrl + 
                           ", apiKey: " + (aiConfig.apiKey.empty() ? "(empty)" : "(set)") +
                           ", model: " + aiConfig.model);

    logger().info("VideoPlayerApp initialized successfully");
    return true;
}

void VideoPlayerApp::shutdown() {
    logger().info("Shutting down VideoPlayerApp...");

    // 正常退出时保存窗口状态
    if (m_renderer) {
        auto config = Settings::instance().windowConfig();
        config.maximized = m_renderer->isMaximized();
        Settings::instance().setWindowConfig(config);
    }

    // 保存剧集进度、当前进度并清除会话标记
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

    logger().info("VideoPlayerApp shutdown complete");
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
            logger().info("Restoring last session: " + session.filePath);
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
    logger().info("Entering main loop");

    auto frameStart = HRClock::now();

    while (m_running) {
        // 处理事件
        if (!m_renderer->processEvents()) {
            m_running = false;
            break;
        }

        // 渲染
        render();

        // 控制帧率 (~60 FPS)，仅在提前完成时补延迟
        auto frameEnd = HRClock::now();
        auto frameElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart).count();
        if (frameElapsed < 16) {
            SDL_Delay(static_cast<Uint32>(16 - frameElapsed));
        }
        frameStart = HRClock::now();
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

    auto t0 = HRClock::now();
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

    // AB 循环检查（仅在正常播放时触发，防止连续 seek）
    if (m_loopA >= 0 && m_loopB > m_loopA && !m_loopSeeking && m_position >= m_loopB) {
        if (m_player && m_isPlaying) {
            logger().info("AB loop triggered: seeking from " + formatTime(m_position) +
                " to " + formatTime(m_loopA));
            m_player->seek(m_loopA);
            m_seekTargetPosition = m_loopA;
            m_position = m_loopA;
            m_loopSeeking = true;
            m_displayFrame = VideoFrame(); // 清空当前帧，避免 seek 后短暂显示旧画面
        }
    }
    // AB 循环恢复：当播放位置回到 A 点之后至少 500ms，允许下一次触发
    if (m_loopSeeking && m_position >= m_loopA + 500) {
        m_loopSeeking = false;
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

    auto t1 = HRClock::now();
    // 开始渲染
    m_renderer->clear();
    double dtClear = elapsedMs(t1);

    auto t2 = HRClock::now();
    // 渲染视频帧
    if (!m_displayFrame.data.empty()) {
        m_renderer->renderFrame(m_displayFrame);
    }
    double dtRenderFrame = elapsedMs(t2);

    auto t3 = HRClock::now();
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

    auto t4 = HRClock::now();
    // 呈现
    m_renderer->present();
    double dtPresent = elapsedMs(t4);

    double total = dtGet + dtClear + dtRenderFrame + dtRenderUI + dtPresent;
    if (total > 10.0) {
        logger().debug("[PERF] render get=" + std::to_string(dtGet) +
            "(pos=" + std::to_string(dtPos) +
            ",vframe=" + std::to_string(dtGet - dtPos) + ")" +
            " clear=" + std::to_string(dtClear) +
            " frame=" + std::to_string(dtRenderFrame) +
            " ui=" + std::to_string(dtRenderUI) +
            " present=" + std::to_string(dtPresent) +
            " total=" + std::to_string(total) + "ms");
    }
    
    static auto reportStart = HRClock::now();
    static int renderCount = 0;
    renderCount++;
    if (elapsedMs(reportStart) >= 1000.0) {
        logger().debug("[PERF] render fps=" + std::to_string(renderCount));
        renderCount = 0;
        reportStart = HRClock::now();
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
        logger().error("File not found: " + path);
        return;
    }

    logger().info("Opening file: " + path);

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

        // 加载章节信息
        if (m_renderer) {
            auto chapters = m_player->chapters();
            m_renderer->setChapters(chapters);
        }

        // 尝试加载 AI 分析缓存
        if (m_aiAnalyzer && m_aiAnalyzer->hasCache(path)) {
            m_aiResult = m_aiAnalyzer->loadCache(path);
            if (m_aiResult.valid) {
                // 使用 AI 分析的章节覆盖 FFmpeg 解析的章节
                if (!m_aiResult.chapters.empty() && m_renderer) {
                    m_player->setChapters(m_aiResult.chapters);
                    m_renderer->setChapters(m_aiResult.chapters);
                }
                // 构建搜索索引
                if (m_searchEngine) {
                    m_searchEngine->buildIndex(path, m_aiResult.transcript, m_aiResult.chapters);
                }
                logger().info("[AI] Loaded cached analysis for: " + path);
            }
        }

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
        logger().error("Failed to load file: " + path);
        if (m_renderer) {
            m_renderer->showMessageBox("打开文件失败", "无法加载文件: " + path + "\n请检查文件格式是否受支持。", true);
        }
    }
}

void VideoPlayerApp::openFileDialog() {
    if (!m_renderer) return;

    logger().info("Opening file dialog...");

    m_renderer->openFileDialog([this](const std::string& filePath) {
        if (!filePath.empty()) {
            logger().info("Selected file: " + filePath);
            openFile(filePath);
        } else {
            logger().info("File dialog cancelled");
        }
    });
}

void VideoPlayerApp::openFolderDialog() {
    if (!m_renderer) return;

    logger().info("Opening folder dialog...");

    m_renderer->openFolderDialog([this](const std::string& folderPath) {
        if (folderPath.empty()) {
            logger().info("Folder dialog cancelled");
            return;
        }

        logger().info("Selected folder: " + folderPath);

        std::vector<std::string> mediaFiles;
        const std::vector<std::string> extensions = {
            ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm",
            ".mp3", ".aac", ".wav", ".flac", ".ogg"
        };

        try {
            for (const auto& entry : std::filesystem::directory_iterator(folderPath)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
                    if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) {
                        mediaFiles.push_back(entry.path().string());
                    }
                }
            }
        } catch (const std::exception& e) {
            logger().error("Failed to read folder: " + std::string(e.what()));
            return;
        }

        if (mediaFiles.empty()) {
            logger().info("No media files found in folder");
            return;
        }

        std::sort(mediaFiles.begin(), mediaFiles.end());

        // 添加到播放列表
        for (const auto& file : mediaFiles) {
            addToPlaylist(file);
        }
        m_progressCacheDirty = true;

        logger().info("Added " + std::to_string(mediaFiles.size()) + " files from folder");

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

    logger().info("Loading subtitle: " + m_currentSubtitle);
    if (m_subtitleParser) {
        if (!m_subtitleParser->loadFile(m_currentSubtitle)) {
            logger().warning("Failed to parse subtitle: " + m_currentSubtitle);
            m_currentSubtitle.clear();
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
    logger().info("Duration: " + std::to_string(duration) + "ms");
}

void VideoPlayerApp::onStateChanged(PlaybackState state) {
    switch (state) {
        case PlaybackState::Playing:
            m_isPlaying = true;
            logger().info("State changed: Playing");
            break;
        case PlaybackState::Paused:
            m_isPlaying = false;
            logger().info("State changed: Paused");
            break;
        case PlaybackState::Stopped:
            m_isPlaying = false;
            logger().info("State changed: Stopped");
            // 自动播放下一个（仅当不是手动停止/切换时）
            if (m_isManualOperation.exchange(false)) {
                break;
            }
            m_pendingAutoAdvance = true;
            break;
    }
}

void VideoPlayerApp::onError(const std::string& error) {
    logger().error("Player error: " + error);
}

} // namespace VideoPlay
