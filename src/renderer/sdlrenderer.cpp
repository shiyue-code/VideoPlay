#include "renderer/sdlrenderer.h"
#include "renderer/sdlrenderer_internal.h"
#include "core/episodedetector.h"
#include "core/settings.h"
#include "utils/logger.h"
#include "renderer/windowframe.h"
#include "renderer/custommessagebox.h"

#include <SDL3/SDL.h>
#ifdef HAS_SDL_TTF
#include <SDL3_ttf/SDL_ttf.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <shlobj.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "utils/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "utils/stb_image_write.h"

namespace {
    std::string getExecutableDir() {
#ifdef _WIN32
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        return std::filesystem::path(path).parent_path().u8string();
#else
        return std::filesystem::current_path().u8string();
#endif
    }
}

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto logger = Logger::get("renderer");
    return *logger;
}
}


SDLRenderer::SDLRenderer() = default;

SDLRenderer::~SDLRenderer() {
    shutdown();
}

bool SDLRenderer::initialize(const std::string& title, int width, int height) {
    if (m_initialized) {
        return true;
    }

    m_windowWidth = width;
    m_windowHeight = height;

    // 初始化 SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        logger().error("SDL_Init failed: " + std::string(SDL_GetError()));
        return false;
    }

    // 开启 OpenGL 多重采样（若后端是 OpenGL 则生效）
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 16);

    // 调试 D3D11 渲染后端（仅在调试时有用）
    SDL_SetHint(SDL_HINT_RENDER_DIRECT3D11_DEBUG, "1");

#ifdef HAS_SDL_TTF
    // 初始化 SDL_ttf
    if (!TTF_Init()) {
        logger().error("TTF_Init failed: " + std::string(SDL_GetError()));
        // 继续运行，只是没有文字
        } else {
        // 加载字体
        std::vector<std::string> fontPaths = {
            "fonts/NotoSansCJKsc-Regular.otf",
            "C:/Windows/Fonts/msyh.ttc",  // 微软雅黑
            "C:/Windows/Fonts/simsun.ttc", // 宋体
            "C:/Windows/Fonts/arial.ttf"   // Arial
        };
        
        for (const auto& path : fontPaths) {
            if (std::filesystem::exists(path)) {
                if (loadFont(path, 14)) {
                    logger().info("Font loaded: " + path);
                    break;
                }
            }
        }
        
        if (!m_font) {
            logger().warning("No font loaded, text rendering disabled");
        }
    }
#endif

    // 创建窗口
    m_window = SDL_CreateWindow(
        title.c_str(),
        width,
        height,
        SDL_WINDOW_RESIZABLE
    );

    if (!m_window) {
        logger().error("SDL_CreateWindow failed: " + std::string(SDL_GetError()));
        SDL_Quit();
        return false;
    }

    // 创建渲染器：优先尝试 OpenGL（使多重采样�?GL 特性生效）
    m_renderer = SDL_CreateRenderer(m_window, "opengl");
    if (m_renderer) {
        logger().info("Renderer backend: opengl");
    } else {
        logger().warning("OpenGL renderer unavailable, falling back to default: " + std::string(SDL_GetError()));
        m_renderer = SDL_CreateRenderer(m_window, NULL);
        if (!m_renderer) {
            logger().error("SDL_CreateRenderer failed: " + std::string(SDL_GetError()));
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
            SDL_Quit();
            return false;
        }
    }

    // 启用 alpha 混合，确保半透明效果正常
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetDefaultTextureScaleMode(m_renderer, SDL_SCALEMODE_LINEAR);

    // 启用文件拖放
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);

    // 初始化菜单
    initMenus();

    // 加载 PNG 图标
    loadIconTextures();

    // 预创建系统光标（无边�?resize 用）
    m_cursorDefault = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    m_cursorSizeWE  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    m_cursorSizeNS  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    m_cursorSizeNWSE = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NWSE_RESIZE);
    m_cursorSizeNESW = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NESW_RESIZE);

    m_initialized = true;
    m_lastMouseMove = SDL_GetTicks(); // 防止启动时控制栏立即自动隐藏
    logger().info("SDLRenderer initialized: " + std::to_string(width) + "x" + std::to_string(height));
    
    return true;
}

void SDLRenderer::shutdown() {
    clearTextCache();
    clearIconTextures();
    closeFont();
    
    if (m_videoTexture) {
        SDL_DestroyTexture(m_videoTexture);
        m_videoTexture = nullptr;
    }

    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }

    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }

#ifdef HAS_SDL_TTF
    TTF_Quit();
#endif

    if (m_initialized) {
        SDL_Quit();
        m_initialized = false;
    }

    SDL_DestroyCursor(m_cursorDefault);
    SDL_DestroyCursor(m_cursorSizeWE);
    SDL_DestroyCursor(m_cursorSizeNS);
    SDL_DestroyCursor(m_cursorSizeNWSE);
    SDL_DestroyCursor(m_cursorSizeNESW);
    m_cursorDefault = nullptr;
    m_cursorSizeWE = nullptr;
    m_cursorSizeNS = nullptr;
    m_cursorSizeNWSE = nullptr;
    m_cursorSizeNESW = nullptr;

    if (m_windowFrame) {
        m_windowFrame->disable();
        m_windowFrame.reset();
    }

    logger().info("SDLRenderer shutdown");
}

void SDLRenderer::setWindowTitle(const std::string& title) {
    if (m_window) {
        SDL_SetWindowTitle(m_window, title.c_str());
    }
}

void SDLRenderer::setWindowSize(int width, int height) {
    if (m_window) {
        SDL_SetWindowSize(m_window, width, height);
        m_windowWidth = width;
        m_windowHeight = height;
    }
}

void SDLRenderer::toggleFullscreen() {
    if (!m_window) return;

    m_fullscreen = !m_fullscreen;
    SDL_SetWindowFullscreen(m_window, m_fullscreen);
    
    if (m_fullscreenCallback) {
        m_fullscreenCallback();
    }
}

void SDLRenderer::toggleBorderless() {
    if (!m_window) return;

    m_borderless = !m_borderless;
    logger().info("toggleBorderless called, new state: " + std::string(m_borderless ? "true" : "false"));

    if (!m_windowFrame) {
        m_windowFrame = WindowFrame::create();
    }

    if (m_borderless) {
        if (!m_windowFrame->enable(m_window)) {
            // 错误恢复：enable 失败时回退到边框模式
            logger().warning("Failed to enable borderless mode, falling back to bordered");
            m_borderless = false;
        }
    } else {
        m_windowFrame->disable();
    }
}

bool SDLRenderer::isBorderless() const {
    return m_borderless;
}

void SDLRenderer::ensureTexture(int width, int height) {
    if (m_videoTexture && m_videoWidth == width && m_videoHeight == height) {
        return;
    }

    if (m_videoTexture) {
        SDL_DestroyTexture(m_videoTexture);
    }

    // 使用 ARGB8888 格式：在小端系统(Windows)上，其内存布局�?B,G,R,A
    // 这与 FFmpeg �?AV_PIX_FMT_BGRA 输出完全匹配
    m_videoTexture = SDL_CreateTexture(
        m_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height
    );

    m_videoWidth = width;
    m_videoHeight = height;

    if (!m_videoTexture) {
        logger().error("Failed to create video texture: " + std::string(SDL_GetError()));
    } else {
        logger().info("Video texture created: " + std::to_string(width) + "x" + std::to_string(height));
    }
}

void SDLRenderer::renderFrame(const VideoFrame& frame) {
    if (frame.data.empty() || frame.width <= 0 || frame.height <= 0) {
        return;
    }

    ensureTexture(frame.width, frame.height);

    if (!m_videoTexture) {
        return;
    }

    // 更新纹理数据 - 现在格式匹配 (BGRA)
    SDL_UpdateTexture(
        m_videoTexture,
        nullptr,
        frame.data.data(),
        frame.width * 4  // BGRA stride
    );

    // 计算目标宽高比
    float targetAspect;
    switch (m_aspectMode) {
        case AspectMode::R16_9:      targetAspect = 16.0f / 9.0f; break;
        case AspectMode::R4_3:       targetAspect = 4.0f / 3.0f; break;
        case AspectMode::FillWindow: targetAspect = static_cast<float>(m_windowWidth) / m_windowHeight; break;
        case AspectMode::Original:
        default:                     targetAspect = static_cast<float>(frame.width) / frame.height; break;
    }

    // 计算视频显示区域（保持目标宽高比，UI 悬浮在上层）
    float windowAspect = static_cast<float>(m_windowWidth) / m_windowHeight;

    SDL_FRect dstRect;
    if (m_aspectMode == AspectMode::FillWindow) {
        dstRect.x = 0;
        dstRect.y = 0;
        dstRect.w = static_cast<float>(m_windowWidth);
        dstRect.h = static_cast<float>(m_windowHeight);
    } else if (windowAspect > targetAspect) {
        // 窗口更宽，以高度为基准
        dstRect.h = static_cast<float>(m_windowHeight);
        dstRect.w = dstRect.h * targetAspect;
        dstRect.x = (m_windowWidth - dstRect.w) / 2.0f;
        dstRect.y = 0;
    } else {
        // 窗口更高，以宽度为基准
        dstRect.w = static_cast<float>(m_windowWidth);
        dstRect.h = dstRect.w / targetAspect;
        dstRect.x = 0;
        dstRect.y = (m_windowHeight - dstRect.h) / 2.0f;
    }

    // 渲染视频
    SDL_RenderTexture(m_renderer, m_videoTexture, nullptr, &dstRect);
}

void SDLRenderer::clear() {
    if (m_renderer) {
        SDL_SetRenderDrawColor(m_renderer, COLOR_BG[0], COLOR_BG[1], COLOR_BG[2], COLOR_BG[3]);
        SDL_RenderClear(m_renderer);
    }
}

void SDLRenderer::present() {
    if (m_renderer) {
        SDL_RenderPresent(m_renderer);
    }
}

bool SDLRenderer::processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // 先让 WindowFrame 处理平台特定事件
        bool handled = false;
        if (m_borderless && m_windowFrame) {
            handled = m_windowFrame->processEvent(event);
        }
        if (!handled) {
            handleEvent(event);
        }
    }

    //  处理异步对话框结果（确保在主线程回调�?
    {
        std::lock_guard<std::mutex> lock(m_dialogMutex);
        if (m_dialogResultReady) {
            if (m_dialogCallback) {
                m_dialogCallback(m_pendingDialogResult);
            }
            m_dialogResultReady = false;
            m_pendingDialogResult.clear();
            m_dialogCallback = nullptr;
        }
    }

    return m_initialized;
}

void SDLRenderer::openSubtitleDialog(std::function<void(const std::string&)> callback) {
    {
        std::lock_guard<std::mutex> lock(m_dialogMutex);
        m_dialogResultReady = false;
        m_pendingDialogResult.clear();
        m_dialogCallback = std::move(callback);
    }

    auto sdlCallback = [](void* userdata, const char* const* filelist, int /*filter*/) {
        auto* renderer = static_cast<SDLRenderer*>(userdata);
        std::lock_guard<std::mutex> lock(renderer->m_dialogMutex);
        if (filelist && filelist[0]) {
            renderer->m_pendingDialogResult = filelist[0];
        }
        renderer->m_dialogResultReady = true;
    };

    SDL_DialogFileFilter sdlFilters[] = {
        { "字幕文件", "srt;ass;ssa;vtt" }
    };

    // parent 传 nullptr 避免无边框窗口遮挡对话框
    SDL_ShowOpenFileDialog(sdlCallback, this, nullptr, sdlFilters, 1, nullptr, false);
}

void SDLRenderer::openFolderDialog(std::function<void(const std::string&)> callback) {
    {
        std::lock_guard<std::mutex> lock(m_dialogMutex);
        m_dialogResultReady = false;
        m_pendingDialogResult.clear();
        m_dialogCallback = std::move(callback);
    }

    auto sdlCallback = [](void* userdata, const char* const* filelist, int /*filter*/) {
        auto* renderer = static_cast<SDLRenderer*>(userdata);
        std::lock_guard<std::mutex> lock(renderer->m_dialogMutex);
        if (filelist && filelist[0]) {
            renderer->m_pendingDialogResult = filelist[0];
        }
        renderer->m_dialogResultReady = true;
    };

    // parent 传 nullptr 避免无边框窗口遮挡对话框
    SDL_ShowOpenFolderDialog(sdlCallback, this, nullptr, nullptr, false);
}

void SDLRenderer::openFileDialog(std::function<void(const std::string&)> callback, const std::vector<std::string>& /*filters*/) {
    {
        std::lock_guard<std::mutex> lock(m_dialogMutex);
        m_dialogResultReady = false;
        m_pendingDialogResult.clear();
        m_dialogCallback = std::move(callback);
    }

    auto sdlCallback = [](void* userdata, const char* const* filelist, int /*filter*/) {
        auto* renderer = static_cast<SDLRenderer*>(userdata);
        std::lock_guard<std::mutex> lock(renderer->m_dialogMutex);
        if (filelist && filelist[0]) {
            renderer->m_pendingDialogResult = filelist[0];
        }
        renderer->m_dialogResultReady = true;
    };

    SDL_DialogFileFilter sdlFilters[] = {
        { "媒体文件", "mp4;mkv;avi;mov;wmv;flv;webm;m4v;ts;m2ts;mpeg;mpg;vob;3gp;ogv;asf;rm;rmvb;mp3;aac;wav;flac;ogg;m4a;wma;opus;ape;ac3;dts;eac3;wv;weba;srt;ass;ssa;vtt" }
    };

    // parent 传 nullptr 避免无边框窗口遮挡对话框
    SDL_ShowOpenFileDialog(sdlCallback, this, nullptr, sdlFilters, 1, nullptr, false);
}

void SDLRenderer::showMessageBox(const std::string& title, const std::string& message,
                                 bool isError,
                                 std::function<void(int64_t timestampMs)> timestampCallback) {
    if (!m_messageBox) {
        m_messageBox = std::make_unique<CustomMessageBox>(m_window, m_font);
    }
    m_messageBox->show(title, message, isError, std::move(timestampCallback));
}

void SDLRenderer::setLoopMode(int mode) {
    if (mode >= 0 && mode <= 2) {
        m_loopMode = mode;
    }
}

void SDLRenderer::setAspectMode(AspectMode mode) {
    m_aspectMode = mode;
}

AspectMode SDLRenderer::aspectMode() const {
    return m_aspectMode;
}

void SDLRenderer::toggleAlwaysOnTop() {
    if (!m_window) return;
    m_alwaysOnTop = !m_alwaysOnTop;
    SDL_SetWindowAlwaysOnTop(m_window, m_alwaysOnTop);
}

bool SDLRenderer::isAlwaysOnTop() const {
    return m_alwaysOnTop;
}

bool SDLRenderer::isMaximized() const {
    if (!m_window) return false;
    return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) != 0;
}

void SDLRenderer::maximizeWindow() {
    if (m_window) SDL_MaximizeWindow(m_window);
}

void SDLRenderer::restoreWindow() {
    if (m_window) SDL_RestoreWindow(m_window);
}

void SDLRenderer::updateRecentFilesMenu() {
    if (m_menus.empty()) return;
    Menu& fileMenu = m_menus[0];

    // 保留固定项（ID < 100），过滤掉空的分隔线
    std::vector<MenuItem> fixedItems;
    for (const auto& item : fileMenu.items) {
        if (item.id < 100) {
            fixedItems.push_back(item);
        }
    }

    auto recent = Settings::instance().recentFiles();
    if (!recent.empty()) {
        // 在退出前添加分隔线和最近文件
        fixedItems.insert(fixedItems.end() - 1, {0, "", "", true}); // 分隔线
        for (size_t i = 0; i < recent.size() && i < 10; ++i) {
            std::string label = std::filesystem::u8path(recent[i]).filename().u8string();
            if (label.length() > 40) {
                label = label.substr(0, 37) + "...";
            }
            fixedItems.insert(fixedItems.end() - 1,
                {static_cast<int>(100 + i), label, "", false, true});
        }
    }

    // 清理连续的空分隔线
    std::vector<MenuItem> cleanedItems;
    bool lastWasSeparator = false;
    for (const auto& item : fixedItems) {
        if (item.separator) {
            if (!lastWasSeparator) {
                cleanedItems.push_back(item);
                lastWasSeparator = true;
            }
        } else {
            cleanedItems.push_back(item);
            lastWasSeparator = false;
        }
    }
    // 移除末尾的分隔线
    while (!cleanedItems.empty() && cleanedItems.back().separator) {
        cleanedItems.pop_back();
    }

    fileMenu.items = std::move(cleanedItems);
}

void SDLRenderer::takeScreenshot() {
    if (!m_renderer || !m_window) return;

    SDL_Surface* surface = SDL_RenderReadPixels(m_renderer, nullptr);
    if (!surface) {
        logger().error("Failed to read pixels for screenshot");
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

    int result = stbi_write_png(savePath.c_str(), surface->w, surface->h,
                                surface->format == SDL_PIXELFORMAT_BGRA32 ? 4 : 4,
                                surface->pixels, surface->pitch);
    SDL_DestroySurface(surface);

    if (result) {
        logger().info("Screenshot saved: " + savePath);
        showMessageBox("截图已保存", "截图已保存到:\n" + savePath, false);
    } else {
        logger().error("Failed to save screenshot");
        showMessageBox("截图失败", "无法保存截图", true);
    }
}

void SDLRenderer::setFileDropCallback(FileDropCallback callback) {
    m_fileDropCallback = callback;
}

void SDLRenderer::setFileOpenCallback(FileOpenCallback callback) {
    m_fileOpenCallback = callback;
}

void SDLRenderer::setKeyCallback(KeyCallback callback) {
    m_keyCallback = callback;
}

void SDLRenderer::setMouseCallback(MouseCallback callback) {
    m_mouseCallback = callback;
}

void SDLRenderer::setSeekCallback(SeekCallback callback) {
    m_seekCallback = callback;
}

void SDLRenderer::setVolumeCallback(VolumeCallback callback) {
    m_volumeCallback = callback;
}

void SDLRenderer::setMuteCallback(UIMuteCallback callback) {
    m_muteCallback = callback;
}

void SDLRenderer::setPlayPauseCallback(PlayPauseCallback callback) {
    m_playPauseCallback = callback;
}

void SDLRenderer::setStopCallback(StopCallback callback) {
    m_stopCallback = callback;
}

void SDLRenderer::setSpeedCallback(SpeedCallback callback) {
    m_speedCallback = callback;
}

void SDLRenderer::setPrevCallback(PrevCallback callback) {
    m_prevCallback = callback;
}

void SDLRenderer::setNextCallback(NextCallback callback) {
    m_nextCallback = callback;
}

void SDLRenderer::setFullscreenCallback(FullscreenCallback callback) {
    m_fullscreenCallback = callback;
}

void SDLRenderer::setMenuCallback(MenuCallback callback) {
    m_menuCallback = callback;
}

void SDLRenderer::setPlaylistItemCallback(PlaylistItemCallback callback) {
    m_playlistItemCallback = callback;
}

void SDLRenderer::setEpisodeItemCallback(EpisodeItemCallback callback) {
    m_episodeItemCallback = callback;
}

void SDLRenderer::setEpisodePrevCallback(EpisodePrevCallback callback) {
    m_episodePrevCallback = callback;
}

void SDLRenderer::setEpisodeNextCallback(EpisodeNextCallback callback) {
    m_episodeNextCallback = callback;
}

void SDLRenderer::setLoopModeCallback(LoopModeCallback callback) {
    m_loopModeCallback = std::move(callback);
}

void SDLRenderer::setSearchCallback(SearchCallback callback) {
    m_searchCallback = std::move(callback);
}

void SDLRenderer::setSubtitleSyncCallback(SubtitleSyncCallback callback) {
    m_subtitleSyncCallback = callback;
}

void SDLRenderer::setABLoopCallback(ABLoopCallback callback) {
    m_abLoopCallback = callback;
}

void SDLRenderer::setChapterSeekCallback(ChapterSeekCallback callback) {
    m_chapterSeekCallback = callback;
}

void SDLRenderer::setAudioTrackCallback(AudioTrackCallback callback) {
    m_audioTrackCallback = std::move(callback);
}

void SDLRenderer::setSubtitleTrackCallback(SubtitleTrackCallback callback) {
    m_subtitleTrackCallback = std::move(callback);
}

void SDLRenderer::setAudioTracks(const std::vector<TrackInfo>& tracks, int currentIndex) {
    m_audioTracks = tracks;
    m_currentAudioTrack = currentIndex;
    updateTrackMenus();
}

void SDLRenderer::setSubtitleTracks(const std::vector<TrackInfo>& tracks, int currentIndex) {
    m_subtitleTracks = tracks;
    m_currentSubtitleTrack = currentIndex;
    updateTrackMenus();
}

void SDLRenderer::setSubtitleBitmap(const SubtitleBitmap& bitmap) {
    if (!m_renderer || bitmap.width <= 0 || bitmap.height <= 0 || bitmap.pixels.empty()) {
        clearSubtitleBitmap();
        return;
    }

    std::lock_guard<std::mutex> lock(m_subtitleBitmapMutex);
    if (m_subtitleTexture) {
        SDL_DestroyTexture(m_subtitleTexture);
    }

    m_subtitleTexture = SDL_CreateTexture(m_renderer,
        SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
        bitmap.width, bitmap.height);
    if (!m_subtitleTexture) {
        m_currentBitmap = {};
        return;
    }

    SDL_SetTextureBlendMode(m_subtitleTexture, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(m_subtitleTexture, nullptr, bitmap.pixels.data(), bitmap.width * 4);
    m_currentBitmap = bitmap;
}

void SDLRenderer::clearSubtitleBitmap() {
    std::lock_guard<std::mutex> lock(m_subtitleBitmapMutex);
    if (m_subtitleTexture) {
        SDL_DestroyTexture(m_subtitleTexture);
        m_subtitleTexture = nullptr;
    }
    m_currentBitmap = {};
}

void SDLRenderer::setChapters(const std::vector<ChapterInfo>& chapters) {
    m_chapters = chapters;
    m_hasChapters = !chapters.empty();
    updateChapterMenuItems();
}

void SDLRenderer::setSearchHighlights(const std::vector<int64_t>& timestamps) {
    m_searchHighlights = timestamps;
}

void SDLRenderer::updateChapterMenuItems() {
    if (m_menus.size() < 5) return; // Chapter menu is index 4 (after 文件/播放/音轨/字幕)
    Menu& chapterMenu = m_menus[4];
    
    chapterMenu.items.clear();
    
    if (m_chapters.empty()) {
        chapterMenu.items.push_back({200, "无可用章节", "", false, false});
    } else {
        for (size_t i = 0; i < m_chapters.size() && i < 50; ++i) {
            std::string label = m_chapters[i].title;
            if (label.empty()) {
                label = "Chapter " + std::to_string(i + 1);
            }
            if (label.length() > 30) {
                label = label.substr(0, 27) + "...";
            }
            chapterMenu.items.push_back({static_cast<int>(200 + i), label, "", false, true});
        }
    }
}

void SDLRenderer::setEpisodeData(const std::vector<EpisodeInfo>* episodes, size_t currentIndex,
                                  const std::string& seriesName, int seasonNumber) {
    m_episodeData = episodes;
    m_currentEpisodeIndex = currentIndex;
    m_episodeSeriesName = seriesName;
    m_episodeSeasonNumber = seasonNumber;
    if (m_episodeData) {
        logger().info("[Episode] setEpisodeData: count=" + std::to_string(m_episodeData->size()) +
                      ", current=" + std::to_string(currentIndex));
    } else {
        logger().info("[Episode] setEpisodeData: null");
    }
}

void SDLRenderer::toggleEpisodePanel() {
    m_showEpisodePanel = !m_showEpisodePanel;
}

void SDLRenderer::togglePlaylistPanel() {
    m_showPlaylistPanel = !m_showPlaylistPanel;
    if (m_showPlaylistPanel) {
        m_showEpisodePanel = false;
        m_showSearchPanel = false;
        m_isSearchInputFocused = false;
        SDL_StopTextInput(m_window);
    }
}

void SDLRenderer::showSearchPanel() {
    m_showSearchPanel = true;
    m_showPlaylistPanel = false;
    m_showEpisodePanel = false;
    m_showControls = true;
    m_lastMouseMove = SDL_GetTicks();
    m_isSearchInputFocused = true;
    SDL_StartTextInput(m_window);
}

void SDLRenderer::toggleSearchPanel() {
    if (m_showSearchPanel) {
        m_showSearchPanel = false;
        m_isSearchInputFocused = false;
        SDL_StopTextInput(m_window);
    } else {
        showSearchPanel();
    }
}

void SDLRenderer::addChatMessage(bool isUser, const std::string& text) {
    std::lock_guard<std::mutex> lock(m_chatMutex);
    m_chatHistory.push_back({isUser, text});
    // Scroll to bottom
    m_searchScrollOffset = 999999;
}

void SDLRenderer::toggleMediaInfoPanel() {
    m_showMediaInfoPanel = !m_showMediaInfoPanel;
    showOSD(m_showMediaInfoPanel ? "媒体信息" : "关闭媒体信息");
}

void SDLRenderer::setMediaInfo(const MediaInfo& info) {
    m_mediaInfo = info;
}

void SDLRenderer::showOSD(const std::string& text) {
    showOSD(OSDType::Message, text, -1.0f);
}

void SDLRenderer::showOSD(OSDType type, const std::string& text, float progress) {
    m_osdText = text;
    m_osdType = type;
    m_osdProgress = progress;
    m_osdStartTime = SDL_GetTicks();
}

void SDLRenderer::setHardwareDecodingEnabled(bool enabled) {
    m_hardwareDecodingEnabled = enabled;
}

void SDLRenderer::setAudioFilterPreset(AudioFilterPreset preset) {
    m_audioFilterPreset = preset;
}

void SDLRenderer::setNetworkState(NetworkState state) {
    m_networkState = state;
}

void SDLRenderer::setAIAnalysisState(bool active, float progress, const std::string& status) {
    uint64_t now = SDL_GetTicks();
    bool wasActive = m_aiAnalysisActive;
    float safeProgress = std::isfinite(progress) ? progress : 0.0f;

    m_aiAnalysisProgress = std::clamp(safeProgress, 0.0f, 1.0f);
    m_aiAnalysisStatus = status;
    m_aiAnalysisActive = active;
    bool terminalStatus = status.find("分析完成") == 0 || status.find("分析失败") == 0;

    if (active && !wasActive) {
        m_aiAnalysisStartTime = now;
        m_aiAnalysisNoticeText.clear();
        m_aiAnalysisNoticeProgress = -1.0f;
        m_aiAnalysisNoticeStartTime = 0;
    } else if (!active && wasActive && !status.empty()) {
        m_aiAnalysisNoticeText = status;
        m_aiAnalysisNoticeProgress = m_aiAnalysisProgress;
        m_aiAnalysisNoticeStartTime = now;
    } else if (!active && terminalStatus && status != m_aiAnalysisNoticeText) {
        m_aiAnalysisNoticeText = status;
        m_aiAnalysisNoticeProgress = m_aiAnalysisProgress;
        m_aiAnalysisNoticeStartTime = now;
    }
}

void SDLRenderer::setEpisodeProgress(const std::vector<float>& progress) {
    m_episodeProgress = progress;
}

void SDLRenderer::setPlaylistProgress(const std::vector<float>& progress) {
    m_playlistProgress = progress;
}

void SDLRenderer::setPrevNextTooltip(const std::string& prevTooltip, const std::string& nextTooltip) {
    m_tooltipPrev = prevTooltip;
    m_tooltipNext = nextTooltip;
}

} // namespace VideoPlay
