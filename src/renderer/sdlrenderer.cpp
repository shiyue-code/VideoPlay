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

    // 调试 D3D11 渲染后端（仅在调试构建启用）
#ifndef NDEBUG
    SDL_SetHint(SDL_HINT_RENDER_DIRECT3D11_DEBUG, "1");
#endif

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
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN
    );

    if (!m_window) {
        logger().error("SDL_CreateWindow failed: " + std::string(SDL_GetError()));
        SDL_Quit();
        return false;
    }

    // 创建渲染器：优先尝试 OpenGL（使多重采样、GL 特性生效）
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

    // 开启垂直同步，由渲染后端在 present 时限帧
    m_vsyncEnabled = SDL_SetRenderVSync(m_renderer, 1);
    logger().info(std::string("Render VSync: ") +
                  (m_vsyncEnabled ? "enabled" : "unavailable (" +
                                      std::string(SDL_GetError()) + ")"));

    // 启用 alpha 混合，确保半透明效果正常
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetDefaultTextureScaleMode(m_renderer, SDL_SCALEMODE_LINEAR);

    // 启用文件拖放
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);

    // 初始化管理器
    m_menuManager = std::make_unique<MenuManager>(this);
    m_uiManager = std::make_unique<UIManager>(this);
    m_dialogManager = std::make_unique<DialogManager>(this);

    // 初始化菜单
    m_menuManager->initMenus();
    refreshAudioOutputDevices();

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
    // 关闭时先隐藏窗口，避免标题栏闪现
    if (m_window) {
        SDL_HideWindow(m_window);
    }

    // 必须在销毁窗口之前解除无边框框架（它会访问 SDL_Window / HWND）
    if (m_windowFrame) {
        m_windowFrame->disable();
        m_windowFrame.reset();
    }

    clearTextCache();
    clearIconTextures();
    closeFont();
    
    if (m_videoTexture) {
        SDL_DestroyTexture(m_videoTexture);
        m_videoTexture = nullptr;
    }
    if (m_previewTexture) {
        SDL_DestroyTexture(m_previewTexture);
        m_previewTexture = nullptr;
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

    // 全屏切换后同步窗口框架（隐藏/恢复外侧缩放热区）
    if (m_borderless && m_windowFrame) {
        m_windowFrame->updateFrame();
    }
    
    if (m_callbacks.fullscreen) {
        m_callbacks.fullscreen();
    }
}

void SDLRenderer::toggleBorderless() {
    if (!m_window) return;

    m_borderless = !m_borderless;
    logger().info("toggleBorderless called, new state: " + std::string(m_borderless ? "true" : "false"));

    if (!m_windowFrame) {
        m_windowFrame = WindowFrame::create();
        setupWindowFrameCallbacks();
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

void SDLRenderer::setupWindowFrameCallbacks() {
    if (!m_windowFrame) return;

    // 标题栏命中判定：UI 布局只保存在渲染层，平台层通过回调查询
    m_windowFrame->setCaptionHitTest([this](int x, int y) {
        return captionHitTestAt(x, y);
    });

    // 非客户区鼠标事件：维持系统按钮/菜单栏的 hover 视觉状态并执行按钮动作
    m_windowFrame->setFrameMouseHandler([this](FrameHitTest hit, FrameMouseAction action) {
        handleFrameMouse(hit, action);
    });

    // 原生拖动/缩放期间主循环被系统模态循环阻塞，这里驱动实时重绘
    m_windowFrame->setLiveRenderHandler([this]() {
        renderLiveFrame();
    });
}

void SDLRenderer::renderLiveFrame() {
    if (m_inLiveRender || !m_callbacks.liveRender || !m_window) return;

    m_inLiveRender = true;
    // 模态循环中 SDL 事件未被处理，需要直接同步窗口尺寸
    int w = 0, h = 0;
    SDL_GetWindowSize(m_window, &w, &h);
    if (w > 0 && h > 0) {
        m_windowWidth = w;
        m_windowHeight = h;
    }
    m_callbacks.liveRender();
    m_inLiveRender = false;
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

    const int rotation = m_videoTransform.rotation;
    const bool swapped = (rotation == 90 || rotation == 270);
    const float crop = std::clamp(m_videoTransform.cropPercent, 0, 40) / 100.0f;

    SDL_FRect srcRect;
    srcRect.x = static_cast<float>(frame.width) * crop;
    srcRect.y = static_cast<float>(frame.height) * crop;
    srcRect.w = static_cast<float>(frame.width) * (1.0f - 2.0f * crop);
    srcRect.h = static_cast<float>(frame.height) * (1.0f - 2.0f * crop);
    if (srcRect.w < 1.0f) srcRect.w = 1.0f;
    if (srcRect.h < 1.0f) srcRect.h = 1.0f;

    const float contentW = swapped ? srcRect.h : srcRect.w;
    const float contentH = swapped ? srcRect.w : srcRect.h;

    float targetAspect;
    switch (m_aspectMode) {
        case AspectMode::R16_9:      targetAspect = 16.0f / 9.0f; break;
        case AspectMode::R4_3:       targetAspect = 4.0f / 3.0f; break;
        case AspectMode::FillWindow: targetAspect = static_cast<float>(m_windowWidth) / m_windowHeight; break;
        case AspectMode::Original:
        default:                     targetAspect = contentW / contentH; break;
    }

    float windowAspect = static_cast<float>(m_windowWidth) / m_windowHeight;
    SDL_FRect display;
    if (m_aspectMode == AspectMode::FillWindow) {
        display.x = 0;
        display.y = 0;
        display.w = static_cast<float>(m_windowWidth);
        display.h = static_cast<float>(m_windowHeight);
    } else if (windowAspect > targetAspect) {
        display.h = static_cast<float>(m_windowHeight);
        display.w = display.h * targetAspect;
        display.x = (m_windowWidth - display.w) / 2.0f;
        display.y = 0;
    } else {
        display.w = static_cast<float>(m_windowWidth);
        display.h = display.w / targetAspect;
        display.x = 0;
        display.y = (m_windowHeight - display.h) / 2.0f;
    }

    SDL_FlipMode flip = SDL_FLIP_NONE;
    if (m_videoTransform.flipHorizontal) {
        flip = static_cast<SDL_FlipMode>(flip | SDL_FLIP_HORIZONTAL);
    }
    if (m_videoTransform.flipVertical) {
        flip = static_cast<SDL_FlipMode>(flip | SDL_FLIP_VERTICAL);
    }

    const bool useSrc = crop > 0.001f;
    if (rotation == 0 && flip == SDL_FLIP_NONE) {
        SDL_RenderTexture(m_renderer, m_videoTexture, useSrc ? &srcRect : nullptr, &display);
        return;
    }

    SDL_FRect dstRect;
    if (swapped) {
        dstRect.w = display.h;
        dstRect.h = display.w;
    } else {
        dstRect.w = display.w;
        dstRect.h = display.h;
    }
    dstRect.x = display.x + (display.w - dstRect.w) / 2.0f;
    dstRect.y = display.y + (display.h - dstRect.h) / 2.0f;
    SDL_FPoint center{display.x + display.w / 2.0f, display.y + display.h / 2.0f};
    SDL_RenderTextureRotated(m_renderer, m_videoTexture, useSrc ? &srcRect : nullptr,
                             &dstRect, static_cast<double>(rotation), &center, flip);
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
    if (m_dialogManager) {
        m_dialogManager->processPendingResult();
    }

    return m_initialized;
}

void SDLRenderer::openSubtitleDialog(std::function<void(const std::string&)> callback) {
    m_dialogManager->openSubtitleDialog(std::move(callback));
}

void SDLRenderer::openFolderDialog(std::function<void(const std::string&)> callback) {
    m_dialogManager->openFolderDialog(std::move(callback));
}

void SDLRenderer::openFileDialog(std::function<void(const std::string&)> callback, const std::vector<std::string>& filters) {
    m_dialogManager->openFileDialog(std::move(callback), filters);
}

void SDLRenderer::showMessageBox(const std::string& title, const std::string& message,
                                 bool isError,
                                 std::function<void(int64_t timestampMs)> timestampCallback) {
    m_dialogManager->showMessageBox(title, message, isError, std::move(timestampCallback));
}

void SDLRenderer::showInputDialog(const std::string& title, const std::string& placeholder,
                                  std::function<void(const std::string&)> callback) {
    m_dialogManager->showInputDialog(title, placeholder, std::move(callback));
}

void SDLRenderer::setLoopMode(int mode) {
    if (mode >= 0 && mode <= 2) {
        m_loopMode = mode;
    }
}

void SDLRenderer::setAspectMode(AspectMode mode) {
    m_aspectMode = mode;
}

void SDLRenderer::setVideoTransform(const VideoTransform& transform) {
    m_videoTransform = transform;
}

void SDLRenderer::setSubtitleStyle(const SubtitleStyle& style) {
    m_subtitleFontSize = std::clamp(style.fontSize, 10, 96);

    uint8_t r = 255, g = 255, b = 255;
    if (parseHexColor(style.fontColor, r, g, b)) {
        m_subtitleFontColor[0] = r;
        m_subtitleFontColor[1] = g;
        m_subtitleFontColor[2] = b;
    } else {
        m_subtitleFontColor[0] = m_subtitleFontColor[1] = m_subtitleFontColor[2] = 255;
    }

    m_subtitleHasOutline = style.hasOutline;
    r = 0; g = 0; b = 0;
    if (parseHexColor(style.outlineColor, r, g, b)) {
        m_subtitleOutlineColor[0] = r;
        m_subtitleOutlineColor[1] = g;
        m_subtitleOutlineColor[2] = b;
    } else {
        m_subtitleOutlineColor[0] = m_subtitleOutlineColor[1] = m_subtitleOutlineColor[2] = 0;
    }
    m_subtitleOutlineWidth = std::clamp(style.outlineWidth, 0, 6);
}

TTF_Font* SDLRenderer::ensureSubtitleFont() {
#ifdef HAS_SDL_TTF
    if (m_fontPath.empty()) return nullptr;
    if (!m_subtitleFont || m_subtitleFontOpenedSize != m_subtitleFontSize) {
        if (m_subtitleFont) {
            TTF_CloseFont(m_subtitleFont);
            m_subtitleFont = nullptr;
        }
        m_subtitleFont = TTF_OpenFont(m_fontPath.c_str(), m_subtitleFontSize);
        m_subtitleFontOpenedSize = m_subtitleFont ? m_subtitleFontSize : 0;
    }
    return m_subtitleFont;
#else
    return nullptr;
#endif
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
    m_menuManager->updateRecentFilesMenu();
}



void SDLRenderer::setCallbacks(RendererCallbacks callbacks) {
    auto liveRender = std::move(m_callbacks.liveRender);  // liveRender 由单独 setter 管理
    m_callbacks = std::move(callbacks);
    if (liveRender) m_callbacks.liveRender = std::move(liveRender);
}

void SDLRenderer::setLiveRenderCallback(std::function<void()> callback) {
    m_callbacks.liveRender = std::move(callback);
}

void SDLRenderer::refreshAudioOutputDevices() {
    m_audioOutputDevices = AudioPlayer::listPlaybackDevices();
}

void SDLRenderer::setAudioOutputDeviceName(const std::string& name) {
    m_audioOutputDeviceName = name;
}



void SDLRenderer::applySmallWindowPanelGuard() {
    constexpr int kDualPanelMinWidth = 860;
    if (m_windowWidth < kDualPanelMinWidth && m_showPlaylistPanel && m_showEpisodePanel) {
        m_showEpisodePanel = false;
    }
}

void SDLRenderer::setPreviewFrame(VideoFrame frame) {
    if (!m_renderer || frame.data.empty() || frame.width <= 0 || frame.height <= 0) {
        return;
    }

    if (!m_previewTexture || m_previewTexW != frame.width || m_previewTexH != frame.height) {
        if (m_previewTexture) {
            SDL_DestroyTexture(m_previewTexture);
            m_previewTexture = nullptr;
        }
        m_previewTexture = SDL_CreateTexture(
            m_renderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            frame.width,
            frame.height);
        m_previewTexW = frame.width;
        m_previewTexH = frame.height;
    }
    if (!m_previewTexture) {
        return;
    }
    SDL_UpdateTexture(m_previewTexture, nullptr, frame.data.data(), frame.width * 4);
    m_previewShownPts = frame.pts;
}

void SDLRenderer::clearPreview() {
    m_previewTargetPtsMs = -1;
    m_previewShownPts = -1;
    m_previewHoverStart = 0;
    if (m_previewTexture) {
        SDL_DestroyTexture(m_previewTexture);
        m_previewTexture = nullptr;
        m_previewTexW = 0;
        m_previewTexH = 0;
    }
}

void SDLRenderer::setAudioTracks(const std::vector<TrackInfo>& tracks, int currentIndex) {
    m_audioTracks = tracks;
    m_currentAudioTrack = currentIndex;
    m_menuManager->updateTrackMenus();
}

void SDLRenderer::setSubtitleTracks(const std::vector<TrackInfo>& tracks, int currentIndex) {
    m_subtitleTracks = tracks;
    m_currentSubtitleTrack = currentIndex;
    m_menuManager->updateTrackMenus();
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

    logger().info("[PGS] setSubtitleBitmap " + std::to_string(bitmap.width) + "x" +
                  std::to_string(bitmap.height) + " @(" + std::to_string(bitmap.x) + "," +
                  std::to_string(bitmap.y) + ") t=" + std::to_string(bitmap.startMs) + ".." +
                  std::to_string(bitmap.endMs));

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
    m_menuManager->updateChapterMenuItems();
}

void SDLRenderer::setBookmarks(const std::vector<Bookmark>& bookmarks) {
    m_bookmarks = bookmarks;
}

void SDLRenderer::setSearchHighlights(const std::vector<int64_t>& timestamps) {
    m_searchHighlights = timestamps;
}

void SDLRenderer::updateChapterMenuItems() {
    if (m_menuManager) {
        m_menuManager->updateChapterMenuItems();
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

void SDLRenderer::setDeinterlaceMode(DeinterlaceMode mode) {
    m_deinterlaceMode = mode;
}

void SDLRenderer::setHdrToneMapMode(HdrToneMapMode mode) {
    m_hdrToneMapMode = mode;
}

void SDLRenderer::setHdrToneMapAlgorithm(HdrToneMapAlgorithm algo) {
    m_hdrToneMapAlgorithm = algo;
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

void SDLRenderer::renderUI(int64_t position, int64_t duration, int volume, bool isMuted,
                           bool isPlaying, double speed, const std::string& filename,
                           const std::string& subtitle,
                           const std::vector<std::string>& playlist, size_t currentPlaylistIndex,
                           int64_t audioPts, int64_t videoPts, double avDiff,
                           bool isPreloading) {
    m_uiManager->renderUI(position, duration, volume, isMuted, isPlaying, speed,
                          filename, subtitle, playlist, currentPlaylistIndex,
                          audioPts, videoPts, avDiff, isPreloading);
}



void SDLRenderer::setVideoFilterConfig(const VideoFilterConfig& config) {
    m_videoFilterConfig = config;
    m_deinterlaceMode = config.deinterlace;
    m_hdrToneMapMode = config.hdrToneMap;
    m_hdrToneMapAlgorithm = config.hdrAlgorithm;
}

void SDLRenderer::toggleVideoControlPanel() {
    m_showVideoControlPanel = !m_showVideoControlPanel;
    if (m_showVideoControlPanel) {
        m_showControls = true;
        m_lastMouseMove = SDL_GetTicks();
    }
}

void SDLRenderer::adjustBrightness(float delta) {
    m_videoFilterConfig.brightness = std::clamp(m_videoFilterConfig.brightness + delta, -1.0f, 1.0f);
    m_videoFilterConfig.enabled = true;
    if (m_callbacks.videoFilterConfig) {
        m_callbacks.videoFilterConfig(m_videoFilterConfig);
    }
    int percent = static_cast<int>(std::round(m_videoFilterConfig.brightness * 100.0f));
    std::string sign = percent > 0 ? "+" : "";
    float progress = (m_videoFilterConfig.brightness + 1.0f) / 2.0f;
    showOSD(OSDType::Info, "亮度: " + sign + std::to_string(percent) + "%", progress);
}

void SDLRenderer::adjustContrast(float delta) {
    m_videoFilterConfig.contrast = std::clamp(m_videoFilterConfig.contrast + delta, 0.0f, 2.0f);
    m_videoFilterConfig.enabled = true;
    if (m_callbacks.videoFilterConfig) {
        m_callbacks.videoFilterConfig(m_videoFilterConfig);
    }
    int percent = static_cast<int>(std::round(m_videoFilterConfig.contrast * 100.0f));
    float progress = m_videoFilterConfig.contrast / 2.0f;
    showOSD(OSDType::Info, "对比度: " + std::to_string(percent) + "%", progress);
}

void SDLRenderer::adjustSaturation(float delta) {
    m_videoFilterConfig.saturation = std::clamp(m_videoFilterConfig.saturation + delta, 0.0f, 3.0f);
    m_videoFilterConfig.enabled = true;
    if (m_callbacks.videoFilterConfig) {
        m_callbacks.videoFilterConfig(m_videoFilterConfig);
    }
    int percent = static_cast<int>(std::round(m_videoFilterConfig.saturation * 100.0f));
    float progress = m_videoFilterConfig.saturation / 3.0f;
    showOSD(OSDType::Info, "饱和度: " + std::to_string(percent) + "%", progress);
}

void SDLRenderer::resetVideoFilter() {
    auto deint = m_videoFilterConfig.deinterlace;
    auto hdrMode = m_videoFilterConfig.hdrToneMap;
    auto hdrAlgo = m_videoFilterConfig.hdrAlgorithm;
    m_videoFilterConfig = VideoFilterConfig{};
    m_videoFilterConfig.deinterlace = deint;
    m_videoFilterConfig.hdrToneMap = hdrMode;
    m_videoFilterConfig.hdrAlgorithm = hdrAlgo;
    if (m_callbacks.videoFilterConfig) {
        m_callbacks.videoFilterConfig(m_videoFilterConfig);
    }
    showOSD(OSDType::Info, "画面参数已重置");
}

} // namespace VideoPlay
