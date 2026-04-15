#include "renderer/sdlrenderer.h"
#include "utils/logger.h"

#include <SDL3/SDL.h>
#ifdef HAS_SDL_TTF
#include <SDL3_ttf/SDL_ttf.h>
#endif
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include "utils/stb_image.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
    std::string getExecutableDir() {
#ifdef _WIN32
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        return std::filesystem::path(path).parent_path().string();
#else
        return std::filesystem::current_path().string();
#endif
    }
}

namespace VideoPlay {

namespace {
    // 颜色定义 (RGBA)
    const uint8_t COLOR_BG[] = { 20, 20, 20, 255 };
    const uint8_t COLOR_CONTROL_BG[] = { 32, 34, 38, 175 };
    const uint8_t COLOR_BUTTON_BG[] = { 60, 60, 60, 0 };
    const uint8_t COLOR_BUTTON_BG_HOVER[] = { 255, 255, 255, 45 };
    const uint8_t COLOR_BUTTON_BG_PRESSED[] = { 255, 255, 255, 75 };
    const uint8_t COLOR_PROGRESS_BG[] = { 255, 255, 255, 60 };
    const uint8_t COLOR_PROGRESS_FILL[] = { 0, 170, 255, 255 };
    const uint8_t COLOR_PROGRESS_HOVER[] = { 80, 210, 255, 255 };
    const uint8_t COLOR_BUTTON[] = { 220, 220, 220, 255 };
    const uint8_t COLOR_BUTTON_HOVER[] = { 255, 255, 255, 255 };
    const uint8_t COLOR_TEXT[] = { 245, 245, 245, 255 };
    const uint8_t COLOR_MENU_BG[] = { 45, 45, 45, 255 };
    const uint8_t COLOR_MENU_HOVER[] = { 60, 60, 60, 255 };
    const uint8_t COLOR_MENU_ACTIVE[] = { 0, 120, 200, 255 };

    // 速度选项
    const double SPEED_OPTIONS[] = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 4.0 };
    const int SPEED_COUNT = sizeof(SPEED_OPTIONS) / sizeof(SPEED_OPTIONS[0]);

    std::string formatTime(int64_t milliseconds) {
        int totalSeconds = static_cast<int>(milliseconds / 1000);
        int hours = totalSeconds / 3600;
        int minutes = (totalSeconds % 3600) / 60;
        int seconds = totalSeconds % 60;

        std::ostringstream oss;
        if (hours > 0) {
            oss << hours << ":" << std::setfill('0') << std::setw(2) << minutes << ":"
                << std::setfill('0') << std::setw(2) << seconds;
        } else {
            oss << minutes << ":" << std::setfill('0') << std::setw(2) << seconds;
        }
        return oss.str();
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
        Logger::instance().error("SDL_Init failed: " + std::string(SDL_GetError()));
        return false;
    }

#ifdef HAS_SDL_TTF
    // 初始化 SDL_ttf
    if (!TTF_Init()) {
        Logger::instance().error("TTF_Init failed: " + std::string(SDL_GetError()));
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
                    Logger::instance().info("Font loaded: " + path);
                    break;
                }
            }
        }
        
        if (!m_font) {
            Logger::instance().warning("No font loaded, text rendering disabled");
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
        Logger::instance().error("SDL_CreateWindow failed: " + std::string(SDL_GetError()));
        SDL_Quit();
        return false;
    }

    // 创建渲染器
    m_renderer = SDL_CreateRenderer(
        m_window,
        NULL
    );

    if (!m_renderer) {
        Logger::instance().error("SDL_CreateRenderer failed: " + std::string(SDL_GetError()));
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        SDL_Quit();
        return false;
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

    m_initialized = true;
    Logger::instance().info("SDLRenderer initialized: " + std::to_string(width) + "x" + std::to_string(height));
    
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

    Logger::instance().info("SDLRenderer shutdown");
}

void SDLRenderer::initMenus() {
    // 文件菜单
    Menu fileMenu;
    fileMenu.label = "文件";
    fileMenu.items = {
        {1, "打开文件...", "Ctrl+O", false, true},
        {2, "打开文件夹...", "", false, true},
        {0, "", "", true}, // 分隔线
        {3, "退出", "Alt+F4", false, true}
    };
    m_menus.push_back(fileMenu);

    // 播放菜单
    Menu playMenu;
    playMenu.label = "播放";
    playMenu.items = {
        {10, "播放/暂停", "Space", false, true},
        {11, "停止", "S", false, true},
        {0, "", "", true},
        {12, "上一个", "P", false, true},
        {13, "下一个", "N", false, true},
        {0, "", "", true},
        {14, "增加速度", "]", false, true},
        {15, "降低速度", "[", false, true},
        {0, "", "", true},
        {16, "全屏", "F", false, true}
    };
    m_menus.push_back(playMenu);

    // 帮助菜单
    Menu helpMenu;
    helpMenu.label = "帮助";
    helpMenu.items = {
        {20, "快捷键", "F1", false, true},
        {21, "关于", "", false, true}
    };
    m_menus.push_back(helpMenu);
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

void SDLRenderer::ensureTexture(int width, int height) {
    if (m_videoTexture && m_videoWidth == width && m_videoHeight == height) {
        return;
    }

    if (m_videoTexture) {
        SDL_DestroyTexture(m_videoTexture);
    }

    // 使用 ARGB8888 格式：在小端系统(Windows)上，其内存布局为 B,G,R,A
    // 这与 FFmpeg 的 AV_PIX_FMT_BGRA 输出完全匹配
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
        Logger::instance().error("Failed to create video texture: " + std::string(SDL_GetError()));
    } else {
        Logger::instance().info("Video texture created: " + std::to_string(width) + "x" + std::to_string(height));
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

    // 计算视频显示区域 (保持宽高比)
    int availableHeight = m_windowHeight - m_menuBarHeight - m_controlHeight;
    float windowAspect = static_cast<float>(m_windowWidth) / availableHeight;
    float videoAspect = static_cast<float>(frame.width) / frame.height;

    SDL_FRect dstRect;
    if (windowAspect > videoAspect) {
        // 窗口更宽，以高度为基准
        dstRect.h = availableHeight;
        dstRect.w = static_cast<int>(availableHeight * videoAspect);
        dstRect.x = (m_windowWidth - dstRect.w) / 2;
        dstRect.y = m_menuBarHeight;
    } else {
        // 窗口更高，以宽度为基准
        dstRect.w = m_windowWidth;
        dstRect.h = static_cast<int>(m_windowWidth / videoAspect);
        dstRect.x = 0;
        dstRect.y = m_menuBarHeight + (availableHeight - dstRect.h) / 2;
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
        handleEvent(event);
    }

    // 处理异步对话框结果（确保在主线程回调）
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

void SDLRenderer::handleEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_QUIT:
            m_initialized = false;
            break;

        case SDL_EVENT_DROP_FILE: {
            const char* droppedFile = event.drop.data;
            if (droppedFile && m_fileDropCallback) {
                m_fileDropCallback(droppedFile);
            }
            break;
        }

        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            m_windowWidth = event.window.data1;
            m_windowHeight = event.window.data2;
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
            
            // 菜单快捷键处理
            if (pressed) {
                // Ctrl+O 打开文件
                if (event.key.key == SDLK_O && (event.key.mod & SDL_KMOD_CTRL)) {
                    if (m_fileOpenCallback) m_fileOpenCallback();
                    break;
                }
            }
            
            if (m_keyCallback) {
                m_keyCallback(event.key.key, pressed);
            }

            // 内置快捷键
            if (pressed) {
                switch (event.key.key) {
                    case SDLK_SPACE:
                        if (m_playPauseCallback) m_playPauseCallback();
                        break;
                    case SDLK_ESCAPE:
                        if (m_fullscreen) toggleFullscreen();
                        closeAllMenus();
                        break;
                    case SDLK_F:
                        toggleFullscreen();
                        break;
                    case SDLK_S:
                        if (m_stopCallback) m_stopCallback();
                        break;
                    case SDLK_LEFT:
                        if (m_seekCallback) m_seekCallback(-5.0); // 后退5秒
                        break;
                    case SDLK_RIGHT:
                        if (m_seekCallback) m_seekCallback(5.0);  // 前进5秒
                        break;
                    case SDLK_UP:
                        if (m_volumeCallback) m_volumeCallback(5);
                        break;
                    case SDLK_DOWN:
                        if (m_volumeCallback) m_volumeCallback(-5);
                        break;
                    case SDLK_M:
                        // 静音切换
                        break;
                    case SDLK_N:
                        if (m_nextCallback) m_nextCallback();
                        break;
                    case SDLK_P:
                        if (m_prevCallback) m_prevCallback();
                        break;
                    case SDLK_PERIOD:
                        if (m_speedCallback) m_speedCallback(0); // 循环速度
                        break;
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_MOTION:
            m_mouseX = static_cast<int>(event.motion.x);
            m_mouseY = static_cast<int>(event.motion.y);
            m_lastMouseMove = SDL_GetTicks();
            m_showControls = true;
            handleMouseMotion(m_mouseX, m_mouseY);
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            m_mouseX = static_cast<int>(event.button.x);
            m_mouseY = static_cast<int>(event.button.y);
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_mouseDown = true;
                handleMouseButtonDown(m_mouseX, m_mouseY);
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                // 右键菜单
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            m_mouseX = static_cast<int>(event.button.x);
            m_mouseY = static_cast<int>(event.button.y);
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_mouseDown = false;
                handleMouseButtonUp(m_mouseX, m_mouseY);
            }
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            if (m_volumeCallback) {
                m_volumeCallback(event.wheel.y > 0.0f ? 5 : -5);
            }
            break;
    }
}

void SDLRenderer::handleMouseMotion(int x, int y) {
    // 检测悬浮的控件
    m_hoveredControl = getControlAt(x, y);
    
    // 检测菜单栏悬浮
    m_menuBarHovered = (y < m_menuBarHeight);
    
    // 处理进度条拖动
    if (m_draggingProgress && m_seekCallback) {
        for (const auto& rect : m_controlRects) {
            if (rect.type == ControlType::ProgressBar) {
                float ratio = static_cast<float>(x - rect.x) / rect.w;
                ratio = std::max(0.0f, std::min(1.0f, ratio));
                m_seekCallback(ratio * 1000 + 1000); // 传回绝对位置 (1000~2000)
                break;
            }
        }
    }
    
    // 处理音量拖动
    if (m_draggingVolume && m_volumeCallback) {
        for (const auto& rect : m_controlRects) {
            if (rect.type == ControlType::VolumeBar) {
                float ratio = static_cast<float>(x - rect.x) / rect.w;
                ratio = std::max(0.0f, std::min(1.0f, ratio));
                m_volumeCallback(static_cast<int>(ratio * 100) + 1000);
                break;
            }
        }
    }
}

void SDLRenderer::handleMouseButtonDown(int x, int y) {
    m_pressedControl = getControlAt(x, y, &m_pressedControlValue);
    
    // 处理菜单点击
    if (y < m_menuBarHeight) {
        handleMenuClick(x, y);
        return;
    }
    
    // 如果点击了菜单区域之外，关闭菜单
    if (m_activeMenu >= 0 && y >= m_menuBarHeight) {
        bool inMenu = false;
        // 检查是否在打开的菜单内
        int menuX = 0;
        for (int i = 0; i <= m_activeMenu && i < (int)m_menus.size(); i++) {
            if (i == m_activeMenu) {
                int menuWidth = 180;
                int menuHeight = (int)m_menus[i].items.size() * 24 + 8;
                if (x >= menuX && x <= menuX + menuWidth && y >= m_menuBarHeight && y <= m_menuBarHeight + menuHeight) {
                    inMenu = true;
                    // 处理菜单项点击
                    int itemIndex = (y - m_menuBarHeight - 4) / 24;
                    if (itemIndex >= 0 && itemIndex < (int)m_menus[i].items.size()) {
                        const auto& item = m_menus[i].items[itemIndex];
                        if (!item.separator && item.enabled && m_menuCallback) {
                            m_menuCallback(item.id);
                        }
                    }
                }
                break;
            }
            menuX += 60;
        }
        if (!inMenu) {
            closeAllMenus();
        }
        return;
    }
    
    // 处理控件点击
    switch (m_pressedControl) {
        case ControlType::PlayButton:
            if (m_playPauseCallback) m_playPauseCallback();
            break;
        case ControlType::StopButton:
            if (m_stopCallback) m_stopCallback();
            break;
        case ControlType::PrevButton:
            if (m_prevCallback) m_prevCallback();
            break;
        case ControlType::NextButton:
            if (m_nextCallback) m_nextCallback();
            break;
        case ControlType::ProgressBar:
            m_draggingProgress = true;
            // 立即更新位置
            for (const auto& rect : m_controlRects) {
                if (rect.type == ControlType::ProgressBar) {
                    float ratio = static_cast<float>(x - rect.x) / rect.w;
                    ratio = std::max(0.0f, std::min(1.0f, ratio));
                    if (m_seekCallback) m_seekCallback(ratio * 1000 + 1000); // 传回绝对位置 (1000~2000)
                    break;
                }
            }
            break;
        case ControlType::VolumeButton:
            if (m_muteCallback) m_muteCallback();
            break;
        case ControlType::VolumeBar:
            m_draggingVolume = true;
            // 立即设置音量
            for (const auto& rect : m_controlRects) {
                if (rect.type == ControlType::VolumeBar) {
                    float ratio = static_cast<float>(x - rect.x) / rect.w;
                    ratio = std::max(0.0f, std::min(1.0f, ratio));
                    if (m_volumeCallback) m_volumeCallback(static_cast<int>(ratio * 100) + 1000);
                    break;
                }
            }
            break;
        case ControlType::SpeedButton:
            if (m_speedCallback) m_speedCallback(0);
            break;
        case ControlType::PlaylistButton:
            m_showPlaylistPanel = !m_showPlaylistPanel;
            break;
        case ControlType::PlaylistItem:
            if (m_playlistItemCallback) m_playlistItemCallback(static_cast<size_t>(m_pressedControlValue));
            break;
        default:
            break;
    }
}

void SDLRenderer::handleMouseButtonUp(int x, int y) {
    m_draggingProgress = false;
    m_draggingVolume = false;
    m_pressedControl = ControlType::None;
}

ControlType SDLRenderer::getControlAt(int x, int y, int* outValue) {
    for (const auto& rect : m_controlRects) {
        if (x >= rect.x && x <= rect.x + rect.w &&
            y >= rect.y && y <= rect.y + rect.h) {
            if (outValue) *outValue = rect.value;
            return rect.type;
        }
    }
    return ControlType::None;
}

void SDLRenderer::handleMenuClick(int x, int y) {
    int menuX = 10;
    for (int i = 0; i < (int)m_menus.size(); i++) {
        int menuWidth = 50; // 估算宽度
        if (x >= menuX && x <= menuX + menuWidth) {
            if (m_activeMenu == i) {
                m_activeMenu = -1; // 切换关闭
            } else {
                m_activeMenu = i; // 打开菜单
            }
            return;
        }
        menuX += 60;
    }
    closeAllMenus();
}

void SDLRenderer::closeAllMenus() {
    m_activeMenu = -1;
}

bool SDLRenderer::isMenuOpen() const {
    return m_activeMenu >= 0;
}

void SDLRenderer::renderUI(int64_t position, int64_t duration, int volume, bool isMuted,
                           bool isPlaying, double speed, const std::string& filename,
                           const std::vector<std::string>& playlist, size_t currentPlaylistIndex,
                           int64_t audioPts, int64_t videoPts, double avDiff) {
    // 清空控件区域
    m_controlRects.clear();

    // 自动隐藏控制栏（3秒无鼠标操作）
    if (m_showControls && SDL_GetTicks() - m_lastMouseMove > 3000) {
        m_showControls = false;
    }

    if (m_showControls) {
        // 底部渐变遮罩，让控制栏自然融入视频
        drawGradientVignette();
        // 渲染菜单栏
        renderMenuBar();
        renderControls(position, duration, volume, isMuted, isPlaying, speed);
        // 渲染播放列表
        if (m_showPlaylistPanel && !playlist.empty()) {
            renderPlaylistPanel(playlist, currentPlaylistIndex);
        }
    } else {
        // 仅渲染菜单栏（始终可见）
        renderMenuBar();
    }

    // 渲染文件名
    if (!filename.empty()) {
        renderFilename(filename);
    }
    
    // 渲染音视频同步调试信息
    renderSyncInfo(audioPts, videoPts, avDiff);
    
    // 渲染打开的菜单
    if (m_activeMenu >= 0 && m_activeMenu < (int)m_menus.size()) {
        int menuX = 10;
        for (int i = 0; i < m_activeMenu; i++) {
            menuX += 60;
        }
        renderMenu(m_menus[m_activeMenu], menuX, m_menuBarHeight);
    }
}

void SDLRenderer::renderMenuBar() {
    // 菜单栏背景
    fillRect(0, 0, m_windowWidth, m_menuBarHeight, 
             COLOR_MENU_BG[0], COLOR_MENU_BG[1], COLOR_MENU_BG[2], COLOR_MENU_BG[3]);
    
    // 菜单项
    int x = 10;
    for (int i = 0; i < (int)m_menus.size(); i++) {
        int textW = getTextWidth(m_menus[i].label);
        int itemWidth = textW + 20;
        
        bool isActive = (i == m_activeMenu);
        bool isHovered = m_menuBarHovered && m_mouseY < m_menuBarHeight && 
                         m_mouseX >= x && m_mouseX <= x + itemWidth;
        
        if (isActive) {
            fillRect(x, 0, itemWidth, m_menuBarHeight,
                     COLOR_MENU_ACTIVE[0], COLOR_MENU_ACTIVE[1], COLOR_MENU_ACTIVE[2], COLOR_MENU_ACTIVE[3]);
        } else if (isHovered) {
            fillRect(x, 0, itemWidth, m_menuBarHeight,
                     COLOR_MENU_HOVER[0], COLOR_MENU_HOVER[1], COLOR_MENU_HOVER[2], COLOR_MENU_HOVER[3]);
        }
        
        // 渲染文字
        int textY = (m_menuBarHeight - getFontHeight()) / 2;
        drawText(m_menus[i].label, x + 10, textY, 
                 COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2]);
        
        x += itemWidth + 10;
    }
}

void SDLRenderer::renderMenu(const Menu& menu, int x, int y) {
    int itemHeight = 24;
    int menuWidth = 180;
    int menuHeight = (int)menu.items.size() * itemHeight + 8;
    
    // 菜单背景
    fillRect(x, y, menuWidth, menuHeight,
             COLOR_MENU_BG[0], COLOR_MENU_BG[1], COLOR_MENU_BG[2], 240);
    
    // 菜单项
    int itemY = y + 4;
    for (const auto& item : menu.items) {
        if (item.separator) {
            // 分隔线
            fillRect(x + 5, itemY + itemHeight/2 - 1, menuWidth - 10, 2, 100, 100, 100, 255);
        } else {
            // 检测悬浮
            bool hovered = (m_mouseX >= x && m_mouseX <= x + menuWidth &&
                           m_mouseY >= itemY && m_mouseY <= itemY + itemHeight);
            
            if (hovered && item.enabled) {
                fillRect(x + 2, itemY, menuWidth - 4, itemHeight,
                         COLOR_MENU_ACTIVE[0], COLOR_MENU_ACTIVE[1], COLOR_MENU_ACTIVE[2], COLOR_MENU_ACTIVE[3]);
            }
            
            // 渲染菜单项文字
            if (item.enabled) {
                drawText(item.label, x + 10, itemY + 4,
                        COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 12);
                
                // 渲染快捷键
                if (!item.shortcut.empty()) {
                    drawText(item.shortcut, x + menuWidth - 60, itemY + 4,
                            150, 150, 150, 11);
                }
            }
        }
        itemY += itemHeight;
    }
}

void SDLRenderer::renderControls(int64_t position, int64_t duration, int volume, bool isMuted,
                                 bool isPlaying, double speed) {
    int marginX = 24;
    int marginBottom = 24;
    int controlY = m_windowHeight - m_controlHeight - marginBottom;
    int controlW = m_windowWidth - marginX * 2;
    int radius = 20;
    
    // 底部投影（增加悬浮感）
    renderSmoothRoundRect(marginX + 2, controlY + 4, controlW, m_controlHeight, radius,
                          0, 0, 0, 80);

    // 外层细白边框（玻璃拟态边框效果）
    renderSmoothRoundRect(marginX - 1, controlY - 1, controlW + 2, m_controlHeight + 2, radius + 1,
                          255, 255, 255, 55);

    // 内层主背景
    renderSmoothRoundRect(marginX, controlY, controlW, m_controlHeight, radius,
                          COLOR_CONTROL_BG[0], COLOR_CONTROL_BG[1], COLOR_CONTROL_BG[2], COLOR_CONTROL_BG[3]);
    
    // 进度条（在最上面）
    renderProgressBar(position, duration, controlY);
    
    // 播放控制按钮
    renderPlaybackControls(isPlaying, controlY);
    
    // 速度按钮
    renderSpeedButton(speed, controlY);
    
    // 时间显示
    renderTimeDisplay(position, duration, controlY);

    // 播放列表切换按钮
    {
        int btnX = m_windowWidth - 44 - m_volumeWidth - 40 - 42;
        int btnY = controlY + 32;
        bool hovered = (m_hoveredControl == ControlType::PlaylistButton);
        bool pressed = (m_pressedControl == ControlType::PlaylistButton);
        drawButton(btnX, btnY, m_buttonSize, m_buttonSize, "playlist", hovered, pressed);
        m_controlRects.push_back({btnX, btnY, m_buttonSize, m_buttonSize, ControlType::PlaylistButton, 0});
    }
    
    // 音量控制
    renderVolumeControl(volume, isMuted, controlY);
}

void SDLRenderer::renderProgressBar(int64_t position, int64_t duration, int controlY) {
    int barY = controlY + 14;
    int margin = 44;
    int barWidth = m_windowWidth - margin * 2;
    int barHeight = 6;
    
    // 检测悬浮
    bool hovered = (m_mouseY >= barY && m_mouseY <= barY + barHeight &&
                   m_mouseX >= margin && m_mouseX <= margin + barWidth);
    bool pressed = m_draggingProgress;
    
    // 背景（圆角）
    renderSmoothRoundRect(margin, barY, barWidth, barHeight, barHeight / 2,
                          COLOR_PROGRESS_BG[0], COLOR_PROGRESS_BG[1], COLOR_PROGRESS_BG[2], COLOR_PROGRESS_BG[3]);

    // 进度
    if (duration > 0) {
        float progress = static_cast<float>(position) / duration;
        progress = std::max(0.0f, std::min(1.0f, progress));

        const uint8_t* fillColor = pressed ? COLOR_PROGRESS_HOVER :
                                   (hovered ? COLOR_PROGRESS_HOVER : COLOR_PROGRESS_FILL);

        int fillW = static_cast<int>(barWidth * progress);
        if (fillW < barHeight) fillW = barHeight;
        renderSmoothRoundRect(margin, barY, fillW, barHeight, barHeight / 2,
                              fillColor[0], fillColor[1], fillColor[2], fillColor[3]);

        // 圆形进度 thumb
        int knobX = margin + static_cast<int>(barWidth * progress);
        int knobY = barY + barHeight / 2;
        int thumbRadius = (hovered || pressed) ? 7 : 5;
        renderSmoothCircle(knobX, knobY, thumbRadius, 255, 255, 255, 255);
    }
    
    // 记录控件位置
    m_controlRects.push_back({margin, barY, barWidth, barHeight,
                              ControlType::ProgressBar, 0});
}

void SDLRenderer::renderPlaybackControls(bool isPlaying, int controlY) {
    int buttonY = controlY + 26;
    int x = 44;
    
    // 上一首按钮
    drawButton(x, buttonY, m_buttonSize, m_buttonSize, "prev",
               m_hoveredControl == ControlType::PrevButton, false);
    m_controlRects.push_back({x, buttonY, m_buttonSize, m_buttonSize, ControlType::PrevButton, 0});
    x += m_buttonSize + 10;
    
    // 播放/暂停按钮
    drawButton(x, buttonY, m_buttonSize, m_buttonSize, isPlaying ? "pause" : "play",
               m_hoveredControl == ControlType::PlayButton, m_pressedControl == ControlType::PlayButton);
    m_controlRects.push_back({x, buttonY, m_buttonSize, m_buttonSize, ControlType::PlayButton, 0});
    x += m_buttonSize + 10;
    
    // 停止按钮
    drawButton(x, buttonY, m_buttonSize, m_buttonSize, "stop",
               m_hoveredControl == ControlType::StopButton, m_pressedControl == ControlType::StopButton);
    m_controlRects.push_back({x, buttonY, m_buttonSize, m_buttonSize, ControlType::StopButton, 0});
    x += m_buttonSize + 10;
    
    // 下一首按钮
    drawButton(x, buttonY, m_buttonSize, m_buttonSize, "next",
               m_hoveredControl == ControlType::NextButton, false);
    m_controlRects.push_back({x, buttonY, m_buttonSize, m_buttonSize, ControlType::NextButton, 0});
}

void SDLRenderer::renderSpeedButton(double speed, int controlY) {
    int buttonY = controlY + 32;
    int x = 240;
    
    bool hovered = (m_hoveredControl == ControlType::SpeedButton);
    
    // 圆角按钮背景
    const uint8_t* bgColor = hovered ? COLOR_BUTTON_BG_HOVER : COLOR_BUTTON_BG;
    renderSmoothRoundRect(x, buttonY, 50, m_buttonSize, 8, bgColor[0], bgColor[1], bgColor[2], bgColor[3]);
    
    // 速度文字
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << speed << "x";
    std::string speedText = oss.str();
    int textW = getTextWidth(speedText, 11);
    int textX = x + (50 - textW) / 2;
    int textY = buttonY + (m_buttonSize - getFontHeight(11)) / 2;
    drawText(speedText, textX, textY, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 11);
    
    m_controlRects.push_back({x, buttonY, 50, m_buttonSize, ControlType::SpeedButton, 0});
}

void SDLRenderer::renderVolumeControl(int volume, bool isMuted, int controlY) {
    int buttonY = controlY + 32;
    int x = m_windowWidth - 44 - m_volumeWidth - 40;
    
    // 音量图标按钮
    bool volHovered = (m_hoveredControl == ControlType::VolumeButton);
    drawButton(x, buttonY, m_buttonSize, m_buttonSize, isMuted ? "mute" : "volume", volHovered, false);
    m_controlRects.push_back({x, buttonY, m_buttonSize, m_buttonSize, ControlType::VolumeButton, 0});
    x += m_buttonSize + 5;
    
    // 音量条
    int volBarWidth = m_volumeWidth;
    int volBarHeight = 6;
    int volBarY = buttonY + (m_buttonSize - volBarHeight) / 2;
    
    renderSmoothRoundRect(x, volBarY, volBarWidth, volBarHeight, volBarHeight / 2,
                          COLOR_PROGRESS_BG[0], COLOR_PROGRESS_BG[1], COLOR_PROGRESS_BG[2], COLOR_PROGRESS_BG[3]);

    if (!isMuted) {
        float vol = std::max(0, std::min(100, volume)) / 100.0f;
        int fillW = static_cast<int>(volBarWidth * vol);
        if (fillW < volBarHeight) fillW = volBarHeight;
        renderSmoothRoundRect(x, volBarY, fillW, volBarHeight, volBarHeight / 2,
                              COLOR_PROGRESS_FILL[0], COLOR_PROGRESS_FILL[1], COLOR_PROGRESS_FILL[2], COLOR_PROGRESS_FILL[3]);
    }

    // 记录控件位置
    m_controlRects.push_back({x, volBarY, volBarWidth, volBarHeight, ControlType::VolumeBar, 0});
}

void SDLRenderer::renderTimeDisplay(int64_t position, int64_t duration, int controlY) {
    int x = 310;
    int y = controlY + 32;
    
    // 格式化时间
    std::string timeText = VideoPlay::formatTime(position) + " / " + VideoPlay::formatTime(duration);
    
    // 渲染时间文字
    drawText(timeText, x, y + (m_buttonSize - getFontHeight()) / 2,
             COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2]);
}

void SDLRenderer::renderFilename(const std::string& filename) {
    if (filename.empty()) return;
    
    // 在视频区域顶部显示文件名
    int y = m_menuBarHeight + 5;
    
    // 获取文件名（不含路径）
    std::string name = filename;
    size_t pos = filename.find_last_of("/\\");
    if (pos != std::string::npos) {
        name = filename.substr(pos + 1);
    }
    
    // 计算文字宽度
    int textW = getTextWidth(name);
    int maxWidth = m_windowWidth - 40;
    
    // 截断过长的文件名
    if (textW > maxWidth) {
        while (textW > maxWidth - getTextWidth("...") && name.length() > 3) {
            name = name.substr(0, name.length() - 1);
            textW = getTextWidth(name + "...");
        }
        name += "...";
        textW = getTextWidth(name);
    }
    
    // 背景条
    int bgWidth = textW + 20;
    fillRect(10, y, bgWidth, 25, 0, 0, 0, 150);
    
    // 渲染文件名
    drawText(name, 20, y + 5, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2]);
}

void SDLRenderer::renderPlaylistPanel(const std::vector<std::string>& playlist, size_t currentIndex) {
    int panelW = 260;
    int panelX = m_windowWidth - 24 - panelW;
    int panelY = m_menuBarHeight + 20;
    int panelBottomMargin = m_windowHeight - m_controlHeight - 34;
    int panelH = panelBottomMargin - panelY;
    if (panelH < 60) return;
    int radius = 20;

    // 投影
    renderSmoothRoundRect(panelX + 2, panelY + 4, panelW, panelH, radius, 0, 0, 0, 80);
    // 白边
    renderSmoothRoundRect(panelX - 1, panelY - 1, panelW + 2, panelH + 2, radius + 1, 255, 255, 255, 55);
    // 背景
    renderSmoothRoundRect(panelX, panelY, panelW, panelH, radius,
                          COLOR_CONTROL_BG[0], COLOR_CONTROL_BG[1], COLOR_CONTROL_BG[2], COLOR_CONTROL_BG[3]);

    // 标题
    int titleY = panelY + 16;
    drawText("播放列表", panelX + 16, titleY, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 13);

    int itemStartY = titleY + 28;
    int itemH = 28;
    int maxVisible = (panelY + panelH - 16 - itemStartY) / itemH;
    if (maxVisible < 1) maxVisible = 1;

    // 计算起始索引，保证当前项尽量可见
    size_t startIndex = 0;
    if (currentIndex >= static_cast<size_t>(maxVisible)) {
        startIndex = currentIndex - static_cast<size_t>(maxVisible) + 1;
    }
    size_t endIndex = startIndex + maxVisible;
    if (endIndex > playlist.size()) endIndex = playlist.size();

    for (size_t i = startIndex; i < endIndex; ++i) {
        int itemY = itemStartY + static_cast<int>(i - startIndex) * itemH;
        bool isCurrent = (i == currentIndex);
        bool hovered = (m_mouseX >= panelX + 12 && m_mouseX <= panelX + panelW - 12 &&
                        m_mouseY >= itemY && m_mouseY <= itemY + itemH);

        if (isCurrent) {
            renderSmoothRoundRect(panelX + 12, itemY, panelW - 24, itemH, 8,
                                  COLOR_MENU_ACTIVE[0], COLOR_MENU_ACTIVE[1], COLOR_MENU_ACTIVE[2], 200);
        } else if (hovered) {
            renderSmoothRoundRect(panelX + 12, itemY, panelW - 24, itemH, 8,
                                  COLOR_MENU_HOVER[0], COLOR_MENU_HOVER[1], COLOR_MENU_HOVER[2], 160);
        }

        // 记录控件位置用于点击检测
        m_controlRects.push_back({panelX + 12, itemY, panelW - 24, itemH, ControlType::PlaylistItem, static_cast<int>(i)});

        // 文件名
        std::string name = playlist[i];
        size_t pos = name.find_last_of("/\\");
        if (pos != std::string::npos) {
            name = name.substr(pos + 1);
        }
        // 截断
        int maxTextW = panelW - 48;
        int textW = getTextWidth(name, 12);
        if (textW > maxTextW) {
            while (textW > maxTextW - getTextWidth("...", 12) && name.length() > 3) {
                name = name.substr(0, name.length() - 1);
                textW = getTextWidth(name + "...", 12);
            }
            name += "...";
        }

        int textColorR = isCurrent ? 255 : COLOR_TEXT[0];
        int textColorG = isCurrent ? 255 : COLOR_TEXT[1];
        int textColorB = isCurrent ? 255 : COLOR_TEXT[2];
        drawText(name, panelX + 20, itemY + (itemH - getFontHeight(12)) / 2, textColorR, textColorG, textColorB, 12);
    }
}

void SDLRenderer::renderSyncInfo(int64_t audioPts, int64_t videoPts, double avDiff) {
    // 仅在播放状态且有有效 PTS 时显示（或者总是显示调试信息）
    std::ostringstream oss;
    oss << "A:" << VideoPlay::formatTime(audioPts)
        << " V:" << VideoPlay::formatTime(videoPts)
        << " Diff:" << std::fixed << std::setprecision(0) << (avDiff * 1000.0) << "ms";
    std::string syncText = oss.str();
    
    int textW = getTextWidth(syncText, 11);
    int x = m_windowWidth - textW - 15;
    int y = m_menuBarHeight + 5;
    
    // 半透明背景
    fillRect(x - 10, y, textW + 20, 22, 0, 0, 0, 150);
    
    // 根据 diff 大小改变颜色：正常为绿色，偏差大为黄色/红色
    uint8_t r = 0, g = 255, b = 0;
    double absDiff = std::abs(avDiff);
    if (absDiff > 0.080) {
        r = 255; g = 0; b = 0;
    } else if (absDiff > 0.040) {
        r = 255; g = 255; b = 0;
    }
    
    drawText(syncText, x, y + 3, r, g, b, 11);
}

bool SDLRenderer::loadFont(const std::string& fontPath, int fontSize) {
#ifdef HAS_SDL_TTF
    closeFont();

    m_font = TTF_OpenFont(fontPath.c_str(), fontSize);
    if (!m_font) {
        Logger::instance().error("Failed to load font: " + std::string(SDL_GetError()));
        return false;
    }

    m_fontSmall = TTF_OpenFont(fontPath.c_str(), fontSize - 2);
    m_fontLarge = TTF_OpenFont(fontPath.c_str(), fontSize + 4);
    m_fontPath = fontPath;

    return true;
#else
    return false;
#endif
}

void SDLRenderer::closeFont() {
#ifdef HAS_SDL_TTF
    if (m_font) {
        TTF_CloseFont(m_font);
        m_font = nullptr;
    }
    if (m_fontSmall) {
        TTF_CloseFont(m_fontSmall);
        m_fontSmall = nullptr;
    }
    if (m_fontLarge) {
        TTF_CloseFont(m_fontLarge);
        m_fontLarge = nullptr;
    }
#endif
}

void SDLRenderer::drawText(const std::string& text, int x, int y, uint8_t r, uint8_t g, uint8_t b, int fontSize) {
#ifdef HAS_SDL_TTF
    if (!m_font || !m_renderer || text.empty()) return;

    std::string cacheKey = text + "|" + std::to_string(fontSize) + "|" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b);
    auto it = m_textCache.find(cacheKey);
    if (it != m_textCache.end()) {
        SDL_FRect dstRect = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(it->second.width), static_cast<float>(it->second.height) };
        SDL_RenderTexture(m_renderer, it->second.texture, nullptr, &dstRect);
        return;
    }

    TTF_Font* font = m_font;
    if (fontSize > 0) {
        if (fontSize <= 10 && m_fontSmall) font = m_fontSmall;
        else if (fontSize >= 18 && m_fontLarge) font = m_fontLarge;
    }

    SDL_Color color = { r, g, b, 255 };
    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), 0, color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    if (texture) {
        SDL_FRect dstRect = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(surface->w), static_cast<float>(surface->h) };
        SDL_RenderTexture(m_renderer, texture, nullptr, &dstRect);

        TextCacheEntry entry;
        entry.texture = texture;
        entry.width = surface->w;
        entry.height = surface->h;
        m_textCache[cacheKey] = entry;
    }

    SDL_DestroySurface(surface);
#endif
}

void SDLRenderer::clearTextCache() {
    for (auto& pair : m_textCache) {
        if (pair.second.texture) {
            SDL_DestroyTexture(pair.second.texture);
        }
    }
    m_textCache.clear();
}

int SDLRenderer::getTextWidth(const std::string& text, int fontSize) {
#ifdef HAS_SDL_TTF
    if (!m_font) return text.length() * 8;
    
    TTF_Font* font = m_font;
    if (fontSize > 0) {
        if (fontSize <= 10 && m_fontSmall) font = m_fontSmall;
        else if (fontSize >= 18 && m_fontLarge) font = m_fontLarge;
    }
    
    int w, h;
    if (TTF_GetStringSize(font, text.c_str(), 0, &w, &h)) {
        return w;
    }
#endif
    return text.length() * 8;
}

int SDLRenderer::getFontHeight(int fontSize) {
#ifdef HAS_SDL_TTF
    if (!m_font) return 14;
    
    TTF_Font* font = m_font;
    if (fontSize > 0) {
        if (fontSize <= 10 && m_fontSmall) font = m_fontSmall;
        else if (fontSize >= 18 && m_fontLarge) font = m_fontLarge;
    }
    
    return TTF_GetFontHeight(font);
#else
    return 14;
#endif
}

void SDLRenderer::drawRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!m_renderer) return;
    SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
    SDL_FRect rect = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h) };
    SDL_RenderRect(m_renderer, &rect);
}

void SDLRenderer::fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!m_renderer) return;
    SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
    SDL_FRect rect = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h) };
    SDL_RenderFillRect(m_renderer, &rect);
}

void SDLRenderer::fillRoundRect(int x, int y, int w, int h, int radius, uint8_t red, uint8_t green, uint8_t blue, uint8_t a) {
    if (!m_renderer || w <= 0 || h <= 0) return;
    if (radius <= 0) {
        fillRect(x, y, w, h, red, green, blue, a);
        return;
    }
    int r = std::min(radius, std::min(w / 2, h / 2));

    std::vector<SDL_Vertex> vertices;
    auto addPoint = [&](float px, float py) {
        SDL_Vertex v;
        v.position.x = px;
        v.position.y = py;
        v.color.r = red / 255.0f;
        v.color.g = green / 255.0f;
        v.color.b = blue / 255.0f;
        v.color.a = a / 255.0f;
        v.tex_coord.x = 0;
        v.tex_coord.y = 0;
        vertices.push_back(v);
    };

    // center point
    addPoint(x + w / 2.0f, y + h / 2.0f);

    const float PI = 3.14159265f;
    const int segments = 64;
    // top-left arc
    for (int i = 0; i <= segments; ++i) {
        float angle = PI + PI / 2.0f * i / segments;
        addPoint(x + r + r * std::cos(angle), y + r + r * std::sin(angle));
    }
    // top-right arc
    for (int i = 0; i <= segments; ++i) {
        float angle = PI * 1.5f + PI / 2.0f * i / segments;
        addPoint(x + w - r + r * std::cos(angle), y + r + r * std::sin(angle));
    }
    // bottom-right arc
    for (int i = 0; i <= segments; ++i) {
        float angle = 0.0f + PI / 2.0f * i / segments;
        addPoint(x + w - r + r * std::cos(angle), y + h - r + r * std::sin(angle));
    }
    // bottom-left arc
    for (int i = 0; i <= segments; ++i) {
        float angle = PI / 2.0f + PI / 2.0f * i / segments;
        addPoint(x + r + r * std::cos(angle), y + h - r + r * std::sin(angle));
    }

    // Generate triangle list indices for a fan from center
    std::vector<int> indices;
    size_t boundaryCount = vertices.size() - 1;
    for (size_t i = 0; i < boundaryCount; ++i) {
        indices.push_back(0);
        indices.push_back(static_cast<int>(1 + i));
        indices.push_back(static_cast<int>(1 + ((i + 1) % boundaryCount)));
    }

    SDL_RenderGeometry(m_renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
}

void SDLRenderer::fillCircle(int cx, int cy, int radius, uint8_t red, uint8_t green, uint8_t blue, uint8_t a) {
    if (!m_renderer || radius <= 0) return;
    std::vector<SDL_Vertex> vertices;
    SDL_Vertex center;
    center.position.x = static_cast<float>(cx);
    center.position.y = static_cast<float>(cy);
    center.color.r = red / 255.0f; center.color.g = green / 255.0f; center.color.b = blue / 255.0f; center.color.a = a / 255.0f;
    center.tex_coord.x = 0; center.tex_coord.y = 0;
    vertices.push_back(center);

    const int segments = 64;
    const float PI = 3.14159265f;
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * PI * i / segments;
        SDL_Vertex v;
        v.position.x = cx + radius * std::cos(angle);
        v.position.y = cy + radius * std::sin(angle);
        v.color = center.color;
        v.tex_coord.x = 0; v.tex_coord.y = 0;
        vertices.push_back(v);
    }

    std::vector<int> indices;
    for (int i = 0; i < segments; ++i) {
        indices.push_back(0);
        indices.push_back(1 + i);
        indices.push_back(1 + i + 1);
    }

    SDL_RenderGeometry(m_renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
}

void SDLRenderer::renderSmoothRoundRect(int x, int y, int w, int h, int radius, uint8_t red, uint8_t green, uint8_t blue, uint8_t a) {
    if (!m_renderer || w <= 0 || h <= 0) return;
    const int scale = 8;
    int texW = w * scale;
    int texH = h * scale;
    int texR = radius * scale;
    if (texW < 1) texW = 1;
    if (texH < 1) texH = 1;

    SDL_Texture* target = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, texW, texH);
    if (!target) return;
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(target, SDL_SCALEMODE_LINEAR);

    SDL_Texture* oldTarget = SDL_GetRenderTarget(m_renderer);
    SDL_SetRenderTarget(m_renderer, target);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
    SDL_RenderClear(m_renderer);

    // 软边过渡层：扩大 8 高分辨率像素（≈1 屏幕像素），alpha 40%
    uint8_t edgeA = static_cast<uint8_t>(std::min(255, a * 40 / 100));
    if (edgeA > 0) {
        fillRoundRect(-8, -8, texW + 16, texH + 16, texR, red, green, blue, edgeA);
    }
    // 核心实心层
    fillRoundRect(0, 0, texW, texH, texR, red, green, blue, a);

    SDL_SetRenderTarget(m_renderer, oldTarget);

    SDL_FRect dst = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h) };
    SDL_RenderTexture(m_renderer, target, nullptr, &dst);
    SDL_DestroyTexture(target);
}

void SDLRenderer::renderSmoothCircle(int cx, int cy, int radius, uint8_t red, uint8_t green, uint8_t blue, uint8_t a) {
    if (!m_renderer || radius <= 0) return;
    const int scale = 8;
    int size = radius * 2 * scale;
    if (size < 2) size = 2;

    SDL_Texture* target = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, size, size);
    if (!target) return;
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(target, SDL_SCALEMODE_LINEAR);

    SDL_Texture* oldTarget = SDL_GetRenderTarget(m_renderer);
    SDL_SetRenderTarget(m_renderer, target);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
    SDL_RenderClear(m_renderer);

    int c = size / 2;
    int r = radius * scale;
    uint8_t edgeA = static_cast<uint8_t>(std::min(255, a * 40 / 100));
    if (edgeA > 0) {
        fillCircle(c, c, r + 8, red, green, blue, edgeA);
    }
    fillCircle(c, c, r, red, green, blue, a);

    SDL_SetRenderTarget(m_renderer, oldTarget);

    SDL_FRect dst = { static_cast<float>(cx - radius), static_cast<float>(cy - radius), static_cast<float>(radius * 2), static_cast<float>(radius * 2) };
    SDL_RenderTexture(m_renderer, target, nullptr, &dst);
    SDL_DestroyTexture(target);
}

void SDLRenderer::drawGradientVignette() {
    if (!m_renderer) return;
    int h = 180;
    int y0 = m_windowHeight - h;
    if (y0 < 0) y0 = 0;

    SDL_Vertex verts[4];
    auto setV = [&](int idx, float px, float py, uint8_t alpha) {
        verts[idx].position.x = px;
        verts[idx].position.y = py;
        verts[idx].color.r = 0.0f;
        verts[idx].color.g = 0.0f;
        verts[idx].color.b = 0.0f;
        verts[idx].color.a = alpha / 255.0f;
        verts[idx].tex_coord.x = 0;
        verts[idx].tex_coord.y = 0;
    };
    setV(0, 0.0f, static_cast<float>(y0), 0);
    setV(1, static_cast<float>(m_windowWidth), static_cast<float>(y0), 0);
    setV(2, static_cast<float>(m_windowWidth), static_cast<float>(m_windowHeight), 200);
    setV(3, 0.0f, static_cast<float>(m_windowHeight), 200);

    int indices[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(m_renderer, nullptr, verts, 4, indices, 6);
}

void SDLRenderer::drawButton(int x, int y, int w, int h, const std::string& iconType, bool hovered, bool pressed) {
    int cx = x + w / 2;
    int cy = y + h / 2;
    // 圆形 hover / pressed 背景（现代播放器风格）
    if (pressed) {
        renderSmoothCircle(cx, cy, w / 2 - 2, COLOR_BUTTON_BG_PRESSED[0], COLOR_BUTTON_BG_PRESSED[1], COLOR_BUTTON_BG_PRESSED[2], COLOR_BUTTON_BG_PRESSED[3]);
    } else if (hovered) {
        renderSmoothCircle(cx, cy, w / 2 - 2, COLOR_BUTTON_BG_HOVER[0], COLOR_BUTTON_BG_HOVER[1], COLOR_BUTTON_BG_HOVER[2], COLOR_BUTTON_BG_HOVER[3]);
    }
    
    // 绘制图标
    drawIcon(cx, cy, iconType, hovered);
}

SDL_Texture* SDLRenderer::createIconTexture(const std::string& type) {
    if (!m_renderer) return nullptr;
    const int size = 256;
    SDL_Texture* target = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, size, size);
    if (!target) return nullptr;
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(target, SDL_SCALEMODE_LINEAR);
    SDL_SetRenderTarget(m_renderer, target);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
    SDL_RenderClear(m_renderer);

    auto mkVert = [&](float px, float py) {
        SDL_Vertex v;
        v.position.x = px;
        v.position.y = py;
        v.color.r = 1.0f; v.color.g = 1.0f; v.color.b = 1.0f; v.color.a = 1.0f;
        v.tex_coord.x = 0; v.tex_coord.y = 0;
        return v;
    };
    auto drawTri = [&](const std::vector<SDL_Vertex>& verts) {
        std::vector<int> idx = {0, 1, 2};
        SDL_RenderGeometry(m_renderer, nullptr, verts.data(), static_cast<int>(verts.size()), idx.data(), static_cast<int>(idx.size()));
    };
    auto drawQuad = [&](const std::vector<SDL_Vertex>& verts) {
        std::vector<int> idx = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(m_renderer, nullptr, verts.data(), static_cast<int>(verts.size()), idx.data(), static_cast<int>(idx.size()));
    };

    int cx = size / 2;
    int cy = size / 2;

    // 辅助：先画稍大、alpha 50% 的过渡层，再画实心主体，产生边缘羽化
    auto drawSoftTri = [&](const std::vector<SDL_Vertex>& base, float expand) {
        if (expand > 0) {
            std::vector<SDL_Vertex> soft;
            for (const auto& v : base) {
                SDL_Vertex sv = v;
                sv.color.a = 0.5f;
                soft.push_back(sv);
            }
            drawTri(soft);
        }
        drawTri(base);
    };
    auto drawSoftRoundRect = [&](int rx, int ry, int rw, int rh, int rr) {
        fillRoundRect(rx - 8, ry - 8, rw + 16, rh + 16, rr, 255, 255, 255, 100);
        fillRoundRect(rx, ry, rw, rh, rr, 255, 255, 255, 255);
    };

    if (type == "play") {
        drawSoftTri({ mkVert(cx - 56, cy - 72), mkVert(cx + 80, cy), mkVert(cx - 56, cy + 72) }, 8);
    } else if (type == "pause") {
        drawSoftRoundRect(cx - 56, cy - 72, 40, 144, 16);
        drawSoftRoundRect(cx + 16, cy - 72, 40, 144, 16);
    } else if (type == "stop") {
        drawSoftRoundRect(cx - 64, cy - 64, 128, 128, 24);
    } else if (type == "prev") {
        drawSoftRoundRect(cx + 32, cy - 64, 32, 128, 12);
        drawSoftTri({ mkVert(cx - 16, cy), mkVert(cx + 56, cy - 72), mkVert(cx + 56, cy + 72) }, 8);
    } else if (type == "next") {
        drawSoftRoundRect(cx - 64, cy - 64, 32, 128, 12);
        drawSoftTri({ mkVert(cx + 16, cy), mkVert(cx - 56, cy - 72), mkVert(cx - 56, cy + 72) }, 8);
    } else if (type == "volume") {
        drawSoftRoundRect(cx - 80, cy - 40, 56, 80, 12);
        drawQuad({ mkVert(cx - 32, cy - 64), mkVert(cx + 72, cy - 88), mkVert(cx + 72, cy + 88), mkVert(cx - 32, cy + 64) });
    } else if (type == "mute") {
        drawSoftRoundRect(cx - 80, cy - 40, 56, 80, 12);
        drawQuad({ mkVert(cx - 32, cy - 64), mkVert(cx + 72, cy - 88), mkVert(cx + 72, cy + 88), mkVert(cx - 32, cy + 64) });
        drawSoftRoundRect(cx - 24, cy - 80, 20, 160, 8);
    } else if (type == "playlist") {
        // 汉堡菜单图标：三条横线
        int lineW = 128;
        int lineH = 20;
        int gap = 28;
        drawSoftRoundRect(cx - lineW/2, cy - gap - lineH/2, lineW, lineH, lineH/2);
        drawSoftRoundRect(cx - lineW/2, cy - lineH/2, lineW, lineH, lineH/2);
        drawSoftRoundRect(cx - lineW/2, cy + gap - lineH/2, lineW, lineH, lineH/2);
    } else {
        fillCircle(cx, cy, 72, 255, 255, 255, 100);
        fillCircle(cx, cy, 64, 255, 255, 255, 255);
    }

    SDL_SetRenderTarget(m_renderer, nullptr);
    return target;
}

SDL_Texture* SDLRenderer::getIconTexture(const std::string& type) {
    auto it = m_iconTextures.find(type);
    if (it != m_iconTextures.end()) {
        return it->second;
    }

    if (!m_renderer) {
        Logger::instance().debug("getIconTexture: renderer not ready for " + type);
        return nullptr;
    }

    SDL_Texture* texture = createIconTexture(type);
    if (texture) {
        m_iconTextures[type] = texture;
        Logger::instance().info("Generated icon texture for: " + type);
    }
    return texture;
}

void SDLRenderer::loadIconTextures() {
    const std::vector<std::string> iconNames = {
        "play", "pause", "stop", "prev", "next", "volume", "mute", "playlist"
    };
    for (const auto& name : iconNames) {
        getIconTexture(name);
    }
}

void SDLRenderer::clearIconTextures() {
    for (auto& pair : m_iconTextures) {
        if (pair.second) {
            SDL_DestroyTexture(pair.second);
        }
    }
    m_iconTextures.clear();
}

void SDLRenderer::drawIcon(int cx, int cy, const std::string& type, bool hovered) {
    const uint8_t* color = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;

    SDL_Texture* texture = getIconTexture(type);
    if (texture) {
        int drawSize = 24;
        SDL_FRect dstRect = { static_cast<float>(cx - drawSize / 2), static_cast<float>(cy - drawSize / 2), static_cast<float>(drawSize), static_cast<float>(drawSize) };
        SDL_RenderTexture(m_renderer, texture, nullptr, &dstRect);
        return;
    }

    // Fallback: primitive drawing when PNG icon unavailable
    int size = 24;
    int r = size / 3;
    SDL_SetRenderDrawColor(m_renderer, color[0], color[1], color[2], color[3]);

    if (type == "play") {
        for (int row = -r; row <= r; row++) {
            int y = cy + row;
            float progress = (row + r) / (float)(2 * r);
            int lineWidth = static_cast<int>(progress * r * 1.5);
            int xStart = cx - r/2;
            for (int i = 0; i < lineWidth; i++) {
                SDL_RenderPoint(m_renderer, static_cast<float>(xStart + i), static_cast<float>(y));
            }
        }
    } else if (type == "pause") {
        int barWidth = r / 2;
        int gap = r / 2;
        fillRect(cx - gap - barWidth, cy - r, barWidth, r * 2, color[0], color[1], color[2], color[3]);
        fillRect(cx + gap, cy - r, barWidth, r * 2, color[0], color[1], color[2], color[3]);
    } else if (type == "stop") {
        int s = r * 3 / 2;
        fillRect(cx - s/2, cy - s/2, s, s, color[0], color[1], color[2], color[3]);
    } else if (type == "prev") {
        fillRect(cx - r, cy - r, r/3, r * 2, color[0], color[1], color[2], color[3]);
        for (int row = -r; row <= r; row++) {
            int y = cy + row;
            float progress = 1.0f - (row + r) / (float)(2 * r);
            int lineWidth = static_cast<int>(progress * r);
            int xStart = cx - r/3;
            for (int i = 0; i < lineWidth; i++) {
                SDL_RenderPoint(m_renderer, static_cast<float>(xStart + i), static_cast<float>(y));
            }
        }
    } else if (type == "next") {
        fillRect(cx + r - r/3, cy - r, r/3, r * 2, color[0], color[1], color[2], color[3]);
        for (int row = -r; row <= r; row++) {
            int y = cy + row;
            float progress = (row + r) / (float)(2 * r);
            int lineWidth = static_cast<int>(progress * r);
            int xStart = cx - r/3 - lineWidth;
            for (int i = 0; i < lineWidth; i++) {
                SDL_RenderPoint(m_renderer, static_cast<float>(xStart + i), static_cast<float>(y));
            }
        }
    } else if (type == "volume") {
        fillRect(cx - r, cy - r/2, r/2, r, color[0], color[1], color[2], color[3]);
        for (int row = -r; row <= r; row++) {
            int y = cy + row;
            int distFromCenter = abs(row);
            int lineWidth = r - distFromCenter / 2;
            int xStart = cx - r/2;
            for (int i = 0; i < lineWidth; i++) {
                SDL_RenderPoint(m_renderer, static_cast<float>(xStart + i), static_cast<float>(y));
            }
        }
    } else if (type == "mute") {
        fillRect(cx - r, cy - r/2, r/2, r, 150, 150, 150, 255);
        for (int row = -r; row <= r; row++) {
            int y = cy + row;
            int distFromCenter = abs(row);
            int lineWidth = r - distFromCenter / 2;
            int xStart = cx - r/2;
            for (int i = 0; i < lineWidth; i++) {
                SDL_RenderPoint(m_renderer, static_cast<float>(xStart + i), static_cast<float>(y));
            }
        }
        SDL_SetRenderDrawColor(m_renderer, 255, 100, 100, 255);
        SDL_RenderLine(m_renderer, static_cast<float>(cx), static_cast<float>(cy - r/2), static_cast<float>(cx + r/2), static_cast<float>(cy + r/2));
        SDL_RenderLine(m_renderer, static_cast<float>(cx), static_cast<float>(cy + r/2), static_cast<float>(cx + r/2), static_cast<float>(cy - r/2));
    }
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
        { "媒体文件", "mp4;mkv;avi;mov;wmv;flv;webm;mp3;aac;wav;flac;ogg;srt;ass;vtt" }
    };

    SDL_ShowOpenFileDialog(sdlCallback, this, m_window, sdlFilters, 1, nullptr, false);
}

void SDLRenderer::showMessageBox(const std::string& title, const std::string& message, bool isError) {
    SDL_ShowSimpleMessageBox(
        isError ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION,
        title.c_str(),
        message.c_str(),
        m_window
    );
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

} // namespace VideoPlay
