#include "renderer/sdlrenderer.h"
#include "core/episodedetector.h"
#include "utils/logger.h"

#include "renderer/windowframe.h"

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

    // 初始�?SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        Logger::instance().error("SDL_Init failed: " + std::string(SDL_GetError()));
        return false;
    }

    // 开�?OpenGL 多重采样（若后端�?OpenGL 则生效）
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 16);

    //  调试 D3D11 渲染后端（仅在调试时有用�?
    SDL_SetHint(SDL_HINT_RENDER_DIRECT3D11_DEBUG, "1");

#ifdef HAS_SDL_TTF
    // 初始�?SDL_ttf
    if (!TTF_Init()) {
        Logger::instance().error("TTF_Init failed: " + std::string(SDL_GetError()));
        //  继续运行，只是没有文�?
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

    // 创建渲染器：优先尝试 OpenGL（使多重采样�?GL 特性生效）
    m_renderer = SDL_CreateRenderer(m_window, "opengl");
    if (m_renderer) {
        Logger::instance().info("Renderer backend: opengl");
    } else {
        Logger::instance().warning("OpenGL renderer unavailable, falling back to default: " + std::string(SDL_GetError()));
        m_renderer = SDL_CreateRenderer(m_window, NULL);
        if (!m_renderer) {
            Logger::instance().error("SDL_CreateRenderer failed: " + std::string(SDL_GetError()));
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

    Logger::instance().info("SDLRenderer shutdown");
}

void SDLRenderer::initMenus() {
    // 文件菜单
    Menu fileMenu;
    fileMenu.label = "文件";
    fileMenu.items = {
        {1, "打开文件...", "Ctrl+O", false, true},
        {2, "打开文件夹..", "", false, true},
        {4, "导入字幕...", "", false, true},
        {0, "", "", true}, // 分隔线
        {3, "退出", "Alt+F4", false, true},
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
        {19, "上一集", "Ctrl+Shift+Left", false, true},
        {22, "下一集", "Ctrl+Shift+Right", false, true},
        {0, "", "", true},
        {18, "播放列表", "Ctrl+L", false, true},
        {0, "", "", true},
        {14, "增加速度", "]", false, true},
        {15, "降低速度", "[", false, true},
        {0, "", "", true},
        {16, "全屏", "F", false, true},
        {0, "", "", true},
        {17, "无边框模式", "B", false, true}
    };
    m_menus.push_back(playMenu);

    // 剧集菜单
    Menu episodeMenu;
    episodeMenu.label = "剧集";
    episodeMenu.items = {
        {30, "上一集", "Ctrl+Shift+Left", false, true},
        {31, "下一集", "Ctrl+Shift+Right", false, true},
        {0, "", "", true},
        {32, "切换选集面板", "Ctrl+E", false, true}
    };
    m_menus.push_back(episodeMenu);

    // 帮助菜单
    Menu helpMenu;
    helpMenu.label = "帮助";
    helpMenu.items = {
        {50, "快捷键", "F1", false, true},
        {51, "关于", "", false, true}
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

void SDLRenderer::toggleBorderless() {
    if (!m_window) return;

    m_borderless = !m_borderless;
    Logger::instance().info("toggleBorderless called, new state: " + std::string(m_borderless ? "true" : "false"));

    if (!m_windowFrame) {
        m_windowFrame = WindowFrame::create();
    }

    if (m_borderless) {
        m_windowFrame->enable(m_window);
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

    // 计算视频显示区域 (保持宽高比，铺满整个窗口，UI 悬浮在上�?
    float windowAspect = static_cast<float>(m_windowWidth) / m_windowHeight;
    float videoAspect = static_cast<float>(frame.width) / frame.height;

    SDL_FRect dstRect;
    if (windowAspect > videoAspect) {
        // 窗口更宽，以高度为基�?        dstRect.h = static_cast<float>(m_windowHeight);
        dstRect.w = static_cast<float>(m_windowHeight) * videoAspect;
        dstRect.x = (m_windowWidth - dstRect.w) / 2.0f;
        dstRect.y = 0;
    } else {
        // 窗口更高，以宽度为基�?        dstRect.w = static_cast<float>(m_windowWidth);
        dstRect.h = static_cast<float>(m_windowWidth) / videoAspect;
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
            
            //  菜单快捷键处�?
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

            //  内置快捷�?
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
                    case SDLK_LEFT: {
                        bool ctrlShift = (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) == (SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
                        if (ctrlShift && m_episodePrevCallback) {
                            m_episodePrevCallback();
                        } else if (!ctrlShift && m_seekCallback) {
                            m_seekCallback(-5.0); //  后退5�?
                            m_seekCallback(-5.0); }
                        break;
                    }
                    case SDLK_RIGHT: {
                        bool ctrlShift = (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) == (SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
                        if (ctrlShift && m_episodeNextCallback) {
                            m_episodeNextCallback();
                        } else if (!ctrlShift && m_seekCallback) {
                            m_seekCallback(5.0);  //  前进5�?
                            m_seekCallback(5.0);  }
                        break;
                    }
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
    ControlType lastHovered = m_hoveredControl;
    m_hoveredControl = getControlAt(x, y);

    // Tooltip 更新（所有可交互控件）
    if (lastHovered != m_hoveredControl) {
        m_tooltipTime = SDL_GetTicks();
        m_tooltipShowTime = 0;
        switch (m_hoveredControl) {
            case ControlType::PlayButton:
                m_tooltip = m_isPlaying ? "暂停  Space" : "播放  Space";
                break;
            case ControlType::StopButton:
                m_tooltip = "停止  S";
                break;
            case ControlType::PrevButton:
                m_tooltip = m_tooltipPrev.empty() ? "上一个 P" : m_tooltipPrev;
                break;
            case ControlType::NextButton:
                m_tooltip = m_tooltipNext.empty() ? "下一个 N" : m_tooltipNext;
                break;
            case ControlType::SpeedButton:
                m_tooltip = "切换速度  .";
                break;
            case ControlType::VolumeButton:
                m_tooltip = "静音  M";
                break;
            case ControlType::VolumeBar:
                m_tooltip = "拖动调节音量";
                break;
            case ControlType::PlaylistButton:
                m_tooltip = "播放列表  Ctrl+L";
                break;
            case ControlType::EpisodePrev:
                m_tooltip = "上一集";
                break;
            case ControlType::EpisodeNext:
                m_tooltip = "下一集";
                break;
            case ControlType::ProgressBar:
                m_tooltip = "拖动跳转";
                break;
            default:
                m_tooltip.clear();
                m_tooltipShowTime = 0;
                break;
        }
    } else if (m_hoveredControl == ControlType::None) {
        m_tooltip.clear();
        m_tooltipShowTime = 0;
    }

    // 检测菜单栏悬浮
    m_menuBarHovered = (y < m_menuBarHeight);
    
    // 菜单�?hover 自动切换（当已有菜单打开时）
    if (m_menuBarHovered && (m_activeMenu >= 0 || m_menuAnimating)) {
        int menuX = 10;
        for (int i = 0; i < (int)m_menus.size(); i++) {
            int textW = getTextWidth(m_menus[i].label);
            int itemWidth = textW + 20;
            if (m_mouseX >= menuX && m_mouseX <= menuX + itemWidth) {
                if (m_activeMenu != i) {
                    m_activeMenu = i;
                    m_pendingMenu = i;
                    m_menuAnimStartTime = SDL_GetTicks();
                    m_menuAnimating = true;
                }
                break;
            }
            menuX += itemWidth + 10;
        }
    }
    
    //  处理进度条拖动（仅更�?UI，不 seek�?
    if (m_draggingProgress) {
        for (const auto& rect : m_controlRects) {
            if (rect.type == ControlType::ProgressBar) {
                m_dragProgressRatio = static_cast<float>(x - rect.x) / rect.w;
                m_dragProgressRatio = std::max(0.0f, std::min(1.0f, m_dragProgressRatio));
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
    
    // 无边框模式：处理 resize 区域光标�?resize 拖动
    if (m_borderless && m_windowFrame) {
        FrameHitTest hit = m_windowFrame->hitTest(x, y);
        // 如果不在控件上，更新光标�?resize 光标
        if (m_hoveredControl == ControlType::None) {
            switch (hit) {
                case FrameHitTest::ResizeLeft:
                case FrameHitTest::ResizeRight:
                    SDL_SetCursor(m_cursorSizeWE);
                    break;
                case FrameHitTest::ResizeTop:
                case FrameHitTest::ResizeBottom:
                    SDL_SetCursor(m_cursorSizeNS);
                    break;
                case FrameHitTest::ResizeTopLeft:
                case FrameHitTest::ResizeBottomRight:
                    SDL_SetCursor(m_cursorSizeNWSE);
                    break;
                case FrameHitTest::ResizeTopRight:
                case FrameHitTest::ResizeBottomLeft:
                    SDL_SetCursor(m_cursorSizeNESW);
                    break;
                case FrameHitTest::Caption:
                    SDL_SetCursor(m_cursorDefault);
                    break;
                default:
                    break;
            }
        }

        // 处理 resize 拖动
        if (m_resizingWindow && m_window) {
            int dx = x - m_resizeStartMouseX;
            int dy = y - m_resizeStartMouseY;
            int newX = m_resizeStartWindowX;
            int newY = m_resizeStartWindowY;
            int newW = m_resizeStartWindowW;
            int newH = m_resizeStartWindowH;

            switch (m_resizeMode) {
                case ResizeMode::Left:
                    newX += dx;
                    newW -= dx;
                    break;
                case ResizeMode::Right:
                    newW += dx;
                    break;
                case ResizeMode::Top:
                    newY += dy;
                    newH -= dy;
                    break;
                case ResizeMode::Bottom:
                    newH += dy;
                    break;
                case ResizeMode::TopLeft:
                    newX += dx;
                    newY += dy;
                    newW -= dx;
                    newH -= dy;
                    break;
                case ResizeMode::TopRight:
                    newY += dy;
                    newW += dx;
                    newH -= dy;
                    break;
                case ResizeMode::BottomLeft:
                    newX += dx;
                    newW -= dx;
                    newH += dy;
                    break;
                case ResizeMode::BottomRight:
                    newW += dx;
                    newH += dy;
                    break;
                default:
                    break;
            }

            //  最小窗口尺寸限�?
            const int MIN_W = 400;
            const int MIN_H = 300;
            if (newW < MIN_W) {
                if (m_resizeMode == ResizeMode::Left ||
                    m_resizeMode == ResizeMode::TopLeft ||
                    m_resizeMode == ResizeMode::BottomLeft) {
                    newX = m_resizeStartWindowX + m_resizeStartWindowW - MIN_W;
                }
                newW = MIN_W;
            }
            if (newH < MIN_H) {
                if (m_resizeMode == ResizeMode::Top ||
                    m_resizeMode == ResizeMode::TopLeft ||
                    m_resizeMode == ResizeMode::TopRight) {
                    newY = m_resizeStartWindowY + m_resizeStartWindowH - MIN_H;
                }
                newH = MIN_H;
            }

            SDL_SetWindowPosition(m_window, newX, newY);
            SDL_SetWindowSize(m_window, newW, newH);
        }
    }
}

void SDLRenderer::handleMouseButtonDown(int x, int y) {
    // 无边框模式下优先检�?resize 区域（避免被控件检测拦截）
    if (m_borderless && m_windowFrame && !(SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED)) {
        ResizeMode mode = getResizeModeAt(x, y);
        if (mode != ResizeMode::None) {
            m_resizingWindow = true;
            m_resizeMode = mode;
            m_resizeStartMouseX = x;
            m_resizeStartMouseY = y;
            SDL_GetWindowPosition(m_window, &m_resizeStartWindowX, &m_resizeStartWindowY);
            SDL_GetWindowSize(m_window, &m_resizeStartWindowW, &m_resizeStartWindowH);
            return;
        }
    }

    m_pressedControl = getControlAt(x, y, &m_pressedControlValue);

    //  处理系统按钮（无边框模式下位于菜单栏区域�?
    if (m_pressedControl == ControlType::SysMinButton) {
        SDL_MinimizeWindow(m_window);
        return;
    }
    if (m_pressedControl == ControlType::SysMaxButton) {
        if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) {
            SDL_RestoreWindow(m_window);
        } else {
            SDL_MaximizeWindow(m_window);
        }
        return;
    }
    if (m_pressedControl == ControlType::SysCloseButton) {
        SDL_Event quitEvent;
        quitEvent.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quitEvent);
        return;
    }
    
    // 处理菜单栏区域的点击
    if (y < m_menuBarHeight) {
        bool clickedMenu = handleMenuClick(x, y);
        
        //  无边框模式下点击菜单栏空白处：双击最大化/还原，单击拖�?
        if (m_borderless && !clickedMenu && m_windowFrame) {
            uint64_t now = SDL_GetTicks();
            if (now - m_lastClickTime < 300 &&
                std::abs(x - m_lastClickX) < 5 && std::abs(y - m_lastClickY) < 5) {
                // 双击：最大化/还原
                if (m_windowFrame->isMaximized()) {
                    m_windowFrame->restoreWindow();
                } else {
                    m_windowFrame->maximizeWindow();
                }
                m_lastClickTime = 0;
            } else {
                //  单击：记录时间并开始拖�?
                m_lastClickTime = now;
                m_lastClickX = x;
                m_lastClickY = y;
                FrameHitTest hit = m_windowFrame->hitTest(x, y);
                if (hit == FrameHitTest::Caption) {
                    m_windowFrame->startDrag();
                }
            }
        }
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
                    //  处理菜单项点�?
                    int itemIndex = (y - m_menuBarHeight - 4) / 24;
                    if (itemIndex >= 0 && itemIndex < (int)m_menus[i].items.size()) {
                        const auto& item = m_menus[i].items[itemIndex];
                        if (!item.separator && item.enabled && m_menuCallback) {
                            if (!(m_menuAnimating && m_pendingMenu < 0)) {
                                m_menuCallback(item.id);
                            }
                            closeAllMenus();
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
            // 记录拖动位置用于实时渲染，释放时�?seek
            for (const auto& rect : m_controlRects) {
                if (rect.type == ControlType::ProgressBar) {
                    m_dragProgressRatio = static_cast<float>(x - rect.x) / rect.w;
                    m_dragProgressRatio = std::max(0.0f, std::min(1.0f, m_dragProgressRatio));
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
        default:
            break;
    }
}

void SDLRenderer::handleMouseButtonUp(int x, int y) {
    // 进度条释放时才执�?seek
    if (m_draggingProgress && m_seekCallback) {
        m_seekCallback(m_dragProgressRatio * 1000 + 1000); // 传回绝对位置 (1000~2000)
    }

    //  列表/面板项的单击操作在鼠标释放时触发（按下和释放需在同一个控件上�?
    if (!m_draggingProgress && !m_draggingVolume && m_pressedControl != ControlType::None) {
        int releaseValue = 0;
        ControlType releaseControl = getControlAt(x, y, &releaseValue);
        if (releaseControl == m_pressedControl && releaseValue == m_pressedControlValue) {
            switch (m_pressedControl) {
                case ControlType::PlaylistItem:
                    if (m_playlistItemCallback) m_playlistItemCallback(static_cast<size_t>(m_pressedControlValue));
                    break;
                case ControlType::EpisodeItem:
                    if (m_episodeItemCallback) m_episodeItemCallback(static_cast<size_t>(m_pressedControlValue));
                    break;
                case ControlType::EpisodePrev:
                    if (m_episodePrevCallback) m_episodePrevCallback();
                    break;
                case ControlType::EpisodeNext:
                    if (m_episodeNextCallback) m_episodeNextCallback();
                    break;
                default:
                    break;
            }
        }
    }

    m_draggingProgress = false;
    m_draggingVolume = false;
    m_resizingWindow = false;
    m_resizeMode = ResizeMode::None;
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

ResizeMode SDLRenderer::getResizeModeAt(int x, int y) const {
    if (!m_borderless || !m_windowFrame) {
        return ResizeMode::None;
    }
    if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) {
        return ResizeMode::None;
    }

    FrameHitTest hit = m_windowFrame->hitTest(x, y);
    switch (hit) {
        case FrameHitTest::ResizeLeft:        return ResizeMode::Left;
        case FrameHitTest::ResizeRight:       return ResizeMode::Right;
        case FrameHitTest::ResizeTop:         return ResizeMode::Top;
        case FrameHitTest::ResizeBottom:      return ResizeMode::Bottom;
        case FrameHitTest::ResizeTopLeft:     return ResizeMode::TopLeft;
        case FrameHitTest::ResizeTopRight:    return ResizeMode::TopRight;
        case FrameHitTest::ResizeBottomLeft:  return ResizeMode::BottomLeft;
        case FrameHitTest::ResizeBottomRight: return ResizeMode::BottomRight;
        default:                              return ResizeMode::None;
    }
}

void SDLRenderer::updateCursorForResize(ResizeMode mode) {
    SDL_Cursor* target = m_cursorDefault;
    switch (mode) {
        case ResizeMode::Left:
        case ResizeMode::Right:
            target = m_cursorSizeWE;
            break;
        case ResizeMode::Top:
        case ResizeMode::Bottom:
            target = m_cursorSizeNS;
            break;
        case ResizeMode::TopLeft:
        case ResizeMode::BottomRight:
            target = m_cursorSizeNWSE;
            break;
        case ResizeMode::TopRight:
        case ResizeMode::BottomLeft:
            target = m_cursorSizeNESW;
            break;
        default:
            break;
    }
    if (target) {
        SDL_SetCursor(target);
    }
}

bool SDLRenderer::handleMenuClick(int x, int y) {
    int menuX = 10;
    for (int i = 0; i < (int)m_menus.size(); i++) {
        int textW = getTextWidth(m_menus[i].label);
        int menuWidth = textW + 20;
        if (x >= menuX && x <= menuX + menuWidth) {
            if (m_activeMenu == i && !m_menuAnimating) {
                m_pendingMenu = -1;
                m_menuAnimStartTime = SDL_GetTicks();
                m_menuAnimating = true;
            } else {
                m_activeMenu = i;
                m_pendingMenu = i;
                m_menuAnimStartTime = SDL_GetTicks();
                m_menuAnimating = true;
            }
            return true;
        }
        menuX += menuWidth + 10;
    }
    closeAllMenus();
    return false;
}

void SDLRenderer::closeAllMenus(bool animate) {
    if (m_activeMenu < 0 && m_menuAnimAlpha <= 0.0f) return;
    if (!animate) {
        m_activeMenu = -1;
        m_pendingMenu = -1;
        m_menuAnimAlpha = 0.0f;
        m_menuAnimating = false;
        return;
    }
    m_pendingMenu = -1;
    m_menuAnimStartTime = SDL_GetTicks();
    m_menuAnimating = true;
}

bool SDLRenderer::isMenuOpen() const {
    return m_activeMenu >= 0;
}

void SDLRenderer::updateMenuAnimation() {
    if (!m_menuAnimating) {
        m_menuAnimAlpha = (m_activeMenu >= 0) ? 1.0f : 0.0f;
        return;
    }

    uint64_t elapsed = SDL_GetTicks() - m_menuAnimStartTime;
    float t = std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(MENU_ANIM_DURATION_MS));

    if (m_pendingMenu >= 0) {
        //  打开动画（ease-out�?
        float ease = 1.0f - (1.0f - t) * (1.0f - t);
        m_menuAnimAlpha = ease;
        if (t >= 1.0f) {
            m_menuAnimating = false;
            m_menuAnimAlpha = 1.0f;
        }
    } else {
        //  关闭动画（ease-in�?
        float ease = t * t;
        m_menuAnimAlpha = 1.0f - ease;
        if (t >= 1.0f) {
            m_menuAnimating = false;
            m_menuAnimAlpha = 0.0f;
            m_activeMenu = -1;
        }
    }
}

void SDLRenderer::renderUI(int64_t position, int64_t duration, int volume, bool isMuted,
                           bool isPlaying, double speed, const std::string& filename,
                           const std::string& subtitle,
                           const std::vector<std::string>& playlist, size_t currentPlaylistIndex,
                           int64_t audioPts, int64_t videoPts, double avDiff,
                           bool isPreloading) {
    // 清空控件区域
    m_controlRects.clear();
    m_isPlaying = isPlaying;

    // 更新进度条宽度动画（无论控制栏是否显示都更新，保证平滑）
    {
        uint64_t now = SDL_GetTicks();
        float dt = 0.0f;
        if (m_lastProgressAnimTime > 0) {
            dt = std::min((now - m_lastProgressAnimTime) / 1000.0f, 0.05f);
        }
        m_lastProgressAnimTime = now;

        bool isStopped = !isPlaying && !isPreloading && position == 0;
        float targetScale = (duration > 0 && !isStopped) ? 1.0f : 0.0f;
        float diff = targetScale - m_progressBarScale;
        if (std::abs(diff) > 0.0005f) {
            // 指数衰减插值：越接近目标速度越慢，天�?ease-out 效果
            m_progressBarScale += diff * std::min(1.0f, PROGRESS_BAR_ANIM_SPEED * dt);
        } else {
            m_progressBarScale = targetScale;
        }
    }

    // 自动隐藏控制栏（3秒无鼠标操作）
    if (m_showControls && SDL_GetTicks() - m_lastMouseMove > 3000) {
        m_showControls = false;
    }

    // 菜单栏始终显示（不受控制栏自动隐藏影响）
    renderMenuBar();

    if (m_showControls) {
        // 底部渐变遮罩，让控制栏自然融入视频
        drawGradientVignette();
        renderControls(position, duration, volume, isMuted, isPlaying, speed, isPreloading);
        // 渲染文件名（左上角，随控制栏自动隐藏）
        if (!filename.empty()) {
            renderFilename(filename);
        }
        // 渲染音视频同步调试信息（右上角，随控制栏自动隐藏）
        renderSyncInfo(audioPts, videoPts, avDiff, m_showPlaylistPanel && !playlist.empty());
    } else {
        // 控制栏隐藏时关闭已打开的菜单
        closeAllMenus(false);
    }

    // 小窗口保护：宽度不足时自动关闭剧集面板（播放列表优先级更高）
    const int MIN_VIDEO_WIDTH = 400;
    const int PANEL_WIDTH = 284; // 260 + 24 padding
    bool canShowBoth = m_windowWidth >= MIN_VIDEO_WIDTH + PANEL_WIDTH * 2;
    if (!canShowBoth && m_showPlaylistPanel && m_showEpisodePanel) {
        m_showEpisodePanel = false;
    }

    // 侧边面板拥有独立生命周期，不受控制栏自动隐藏影响
    if (m_showPlaylistPanel && !playlist.empty()) {
        renderPlaylistPanel(playlist, currentPlaylistIndex);
    }
    if (m_showEpisodePanel && m_episodeData && !m_episodeData->empty()) {
        renderEpisodePanel();
    }

    // 渲染字幕（始终显示，不受控制栏影响）
    if (!subtitle.empty()) {
        renderSubtitle(subtitle);
    }

    // 预缓冲加载动画已集成到进度条�?    
    //  渲染打开的菜�?
    updateMenuAnimation();
    if ((m_activeMenu >= 0 && m_activeMenu < (int)m_menus.size()) || m_menuAnimating) {
        if (m_menuAnimAlpha > 0.01f) {
            int menuX = 10;
            for (int i = 0; i < m_activeMenu; i++) {
                int textW = getTextWidth(m_menus[i].label);
                menuX += textW + 20 + 10;
            }
            renderMenu(m_menus[m_activeMenu], menuX, m_menuBarHeight, m_menuAnimAlpha);
        }
    }

    // 渲染 Tooltip
    renderTooltip();

    // 自绘 1px 边框，确�?Win10 �?Win11 显示效果完全一�?    // （Win11 �?DWMWA_BORDER_COLOR 是独占特性，Win10 不支持，因此统一�?SDL 自绘�?    // 圆角窗口下不绘制四边直边框，�?DWM 圆角自然呈现
    if (m_borderless && !(SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED)) {
        uint8_t br = COLOR_MENU_BG[0];
        uint8_t bg = COLOR_MENU_BG[1];
        uint8_t bb = COLOR_MENU_BG[2];
        const int r = 8; // 圆角半径
        //  上边框（避开圆角区域�?
        fillRect(r, 0, m_windowWidth - r * 2, 1, br, bg, bb, 255);
        //  下边�?
        fillRect(r, m_windowHeight - 1, m_windowWidth - r * 2, 1, br, bg, bb, 255);
        //  左边框（避开圆角区域�?
        fillRect(0, r, 1, m_windowHeight - r * 2, br, bg, bb, 255);
        //  右边框（避开圆角区域�?
        fillRect(m_windowWidth - 1, r, 1, m_windowHeight - r * 2, br, bg, bb, 255);
    }
}

void SDLRenderer::renderMenuBar() {
    //  菜单栏背�?
    fillRect(0, 0, m_windowWidth, m_menuBarHeight, 
             COLOR_MENU_BG[0], COLOR_MENU_BG[1], COLOR_MENU_BG[2], COLOR_MENU_BG[3]);
    
    //  菜单�?
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

    //  无边框模式下的系统按�?
    if (m_borderless) {
        int btnSize = 14;
        int btnGap = 12;
        int rightMargin = 14;
        int btnY = (m_menuBarHeight - btnSize) / 2;
        int startX = m_windowWidth - rightMargin - 3 * btnSize - 2 * btnGap;

        // 最小化按钮
        {
            int bx = startX;
            bool hovered = (m_hoveredControl == ControlType::SysMinButton);
            bool pressed = (m_pressedControl == ControlType::SysMinButton);
            const uint8_t* c = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;
            if (pressed) {
                fillRect(bx - 2, btnY - 2, btnSize + 4, btnSize + 4,
                         COLOR_BUTTON_BG_PRESSED[0], COLOR_BUTTON_BG_PRESSED[1], COLOR_BUTTON_BG_PRESSED[2], COLOR_BUTTON_BG_PRESSED[3]);
            } else if (hovered) {
                fillRect(bx - 2, btnY - 2, btnSize + 4, btnSize + 4,
                         COLOR_BUTTON_BG_HOVER[0], COLOR_BUTTON_BG_HOVER[1], COLOR_BUTTON_BG_HOVER[2], COLOR_BUTTON_BG_HOVER[3]);
            }
            // 横线
            int lineW = btnSize - 4;
            int lineH = 2;
            int lineX = bx + (btnSize - lineW) / 2;
            int lineY = btnY + (btnSize - lineH) / 2;
            fillRect(lineX, lineY, lineW, lineH, c[0], c[1], c[2], c[3]);
            m_controlRects.push_back({bx, btnY, btnSize, btnSize, ControlType::SysMinButton, 0});
        }

        // 最大化/还原按钮
        {
            int bx = startX + btnSize + btnGap;
            bool hovered = (m_hoveredControl == ControlType::SysMaxButton);
            bool pressed = (m_pressedControl == ControlType::SysMaxButton);
            const uint8_t* c = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;
            if (pressed) {
                fillRect(bx - 2, btnY - 2, btnSize + 4, btnSize + 4,
                         COLOR_BUTTON_BG_PRESSED[0], COLOR_BUTTON_BG_PRESSED[1], COLOR_BUTTON_BG_PRESSED[2], COLOR_BUTTON_BG_PRESSED[3]);
            } else if (hovered) {
                fillRect(bx - 2, btnY - 2, btnSize + 4, btnSize + 4,
                         COLOR_BUTTON_BG_HOVER[0], COLOR_BUTTON_BG_HOVER[1], COLOR_BUTTON_BG_HOVER[2], COLOR_BUTTON_BG_HOVER[3]);
            }
            bool isMaximized = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED);
            if (isMaximized) {
                // 还原：两个错位小方框
                int s = btnSize - 6;
                int ox = bx + 2;
                int oy = btnY + 2;
                drawRect(ox, oy + 2, s, s, c[0], c[1], c[2], c[3]);
                drawRect(ox + 3, oy - 1, s, s, c[0], c[1], c[2], c[3]);
            } else {
                //  最大化：空心方�?
                int s = btnSize - 4;
                int ox = bx + 2;
                int oy = btnY + 2;
                drawRect(ox, oy, s, s, c[0], c[1], c[2], c[3]);
            }
            m_controlRects.push_back({bx, btnY, btnSize, btnSize, ControlType::SysMaxButton, 0});
        }

        // 关闭按钮
        {
            int bx = startX + 2 * (btnSize + btnGap);
            bool hovered = (m_hoveredControl == ControlType::SysCloseButton);
            bool pressed = (m_pressedControl == ControlType::SysCloseButton);
            const uint8_t* c = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;
            if (pressed) {
                fillRect(bx - 2, btnY - 2, btnSize + 4, btnSize + 4,
                         COLOR_BUTTON_BG_PRESSED[0], COLOR_BUTTON_BG_PRESSED[1], COLOR_BUTTON_BG_PRESSED[2], COLOR_BUTTON_BG_PRESSED[3]);
            } else if (hovered) {
                //  关闭按钮 hover 用红色背景（现代风格�?
                fillRect(bx - 2, btnY - 2, btnSize + 4, btnSize + 4, 232, 17, 35, 255);
                c = COLOR_BUTTON_HOVER;
            }
            // X
            SDL_SetRenderDrawColor(m_renderer, c[0], c[1], c[2], c[3]);
            int pad = 3;
            SDL_RenderLine(m_renderer, static_cast<float>(bx + pad), static_cast<float>(btnY + pad),
                           static_cast<float>(bx + btnSize - 1 - pad), static_cast<float>(btnY + btnSize - 1 - pad));
            SDL_RenderLine(m_renderer, static_cast<float>(bx + btnSize - 1 - pad), static_cast<float>(btnY + pad),
                           static_cast<float>(bx + pad), static_cast<float>(btnY + btnSize - 1 - pad));
            m_controlRects.push_back({bx, btnY, btnSize, btnSize, ControlType::SysCloseButton, 0});
        }
    }
}

void SDLRenderer::renderMenu(const Menu& menu, int x, int y, float alpha) {
    int itemHeight = 24;
    int labelMaxW = 0;
    int shortcutMaxW = 0;
    const int labelFontSize = 12;
    const int shortcutFontSize = 11;
    const int shortcutGap = 24; // 标签与快捷键之间的最小间距
    const int hPadding = 20;    // 左右总内边距

    // 预先计算所需宽度
    for (const auto& item : menu.items) {
        if (!item.separator && item.enabled) {
            int lw = getTextWidth(item.label, labelFontSize);
            if (lw > labelMaxW) labelMaxW = lw;
            if (!item.shortcut.empty()) {
                int sw = getTextWidth(item.shortcut, shortcutFontSize);
                if (sw > shortcutMaxW) shortcutMaxW = sw;
            }
        }
    }

    int menuWidth = hPadding + labelMaxW;
    if (shortcutMaxW > 0) {
        menuWidth += shortcutGap + shortcutMaxW;
    }
    // 最小宽度保证
    if (menuWidth < 140) menuWidth = 140;

    int menuHeight = (int)menu.items.size() * itemHeight + 8;
    uint8_t baseAlpha = static_cast<uint8_t>(240 * alpha);

    // 菜单背景
    fillRect(x, y, menuWidth, menuHeight,
             COLOR_MENU_BG[0], COLOR_MENU_BG[1], COLOR_MENU_BG[2], baseAlpha);

    //  菜单�?
    int itemY = y + 4;
    for (const auto& item : menu.items) {
        if (item.separator) {
            //  分隔�?
            fillRect(x + 5, itemY + itemHeight/2 - 1, menuWidth - 10, 2, 100, 100, 100, static_cast<uint8_t>(255 * alpha));
        } else {
            //  检测悬�?
            bool hovered = (m_mouseX >= x && m_mouseX <= x + menuWidth &&
                           m_mouseY >= itemY && m_mouseY <= itemY + itemHeight);

            if (hovered && item.enabled) {
                fillRect(x + 2, itemY, menuWidth - 4, itemHeight,
                         COLOR_MENU_ACTIVE[0], COLOR_MENU_ACTIVE[1], COLOR_MENU_ACTIVE[2], static_cast<uint8_t>(COLOR_MENU_ACTIVE[3] * alpha));
            }

            //  渲染菜单项文�?
            if (item.enabled) {
                drawText(item.label, x + 10, itemY + 4,
                        COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], labelFontSize);

                // 渲染快捷键（右对齐）
                if (!item.shortcut.empty()) {
                    int sw = getTextWidth(item.shortcut, shortcutFontSize);
                    drawText(item.shortcut, x + menuWidth - 10 - sw, itemY + 4,
                            150, 150, 150, shortcutFontSize);
                }
            }
        }
        itemY += itemHeight;
    }
}

void SDLRenderer::renderControls(int64_t position, int64_t duration, int volume, bool isMuted,
                                 bool isPlaying, double speed, bool isPreloading) {
    int marginX = 24;
    int marginBottom = 24;
    int controlY = m_windowHeight - m_controlHeight - marginBottom;
    int controlW = m_windowWidth - marginX * 2;
    int radius = 20;
    
    //  底部投影（增加悬浮感�?
    fillRoundRect(marginX + 2, controlY + 4, controlW, m_controlHeight, radius,
                          0, 0, 0, 80);

    // 外层细白边框（玻璃拟态边框效果）
    fillRoundRect(marginX - 1, controlY - 1, controlW + 2, m_controlHeight + 2, radius + 1,
                          255, 255, 255, 55);

    //  内层主背�?
    fillRoundRect(marginX, controlY, controlW, m_controlHeight, radius,
                          COLOR_CONTROL_BG[0], COLOR_CONTROL_BG[1], COLOR_CONTROL_BG[2], COLOR_CONTROL_BG[3]);
    
    //  进度条（在最上面�?
    renderProgressBar(position, duration, controlY, isPreloading, isPlaying);
    
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

void SDLRenderer::renderProgressBar(int64_t position, int64_t duration, int controlY, bool isPreloading, bool isPlaying) {
    int barY = controlY + 14;
    int barHeight = 6;

    //  根据动画状态计算进度条宽度和边�?
    int fullBarWidth = m_windowWidth - 44 * 2;
    int compactBarWidth = m_windowWidth / 3;
    int barWidth = static_cast<int>(fullBarWidth * m_progressBarScale + compactBarWidth * (1.0f - m_progressBarScale));
    int margin = (m_windowWidth - barWidth) / 2;

    //  检测悬�?
    bool hovered = (m_mouseY >= barY && m_mouseY <= barY + barHeight &&
                   m_mouseX >= margin && m_mouseX <= margin + barWidth);
    bool pressed = m_draggingProgress;

    //  完全停止状态：刚启动或停止播放后（position=0，不播放，不预载�?
    bool isCompletelyStopped = !isPlaying && !isPreloading && position == 0;

    // 背景（圆角）
    uint8_t bgAlpha = isPreloading ? 180 : (duration > 0 ? COLOR_PROGRESS_BG[3] : 120);
    fillRoundRect(margin, barY, barWidth, barHeight, barHeight / 2,
                          COLOR_PROGRESS_BG[0], COLOR_PROGRESS_BG[1], COLOR_PROGRESS_BG[2], bgAlpha);

    // 进度填充 + thumb（停止状态下只保留背景条，和刚启动时一致）
    if (duration > 0 && !isCompletelyStopped) {
        float progress = m_draggingProgress ? m_dragProgressRatio
                                            : static_cast<float>(position) / duration;
        progress = std::max(0.0f, std::min(1.0f, progress));

        const uint8_t* fillColor = pressed ? COLOR_PROGRESS_HOVER :
                                   (hovered ? COLOR_PROGRESS_HOVER : COLOR_PROGRESS_FILL);

        int fillW = static_cast<int>(barWidth * progress);
        if (fillW < barHeight) fillW = barHeight;

        uint8_t fillAlpha = isPreloading ? 70 : fillColor[3];
        fillRoundRect(margin, barY, fillW, barHeight, barHeight / 2,
                              fillColor[0], fillColor[1], fillColor[2], fillAlpha);

        //  圆形进度 thumb（预载期间用蓝色，播放时白色，避免半透明发灰�?
        int knobX = margin + static_cast<int>(barWidth * progress);
        int knobY = barY + barHeight / 2;
        int thumbRadius = (hovered || pressed) ? 7 : 5;
        if (isPreloading) {
            fillCircle(knobX, knobY, thumbRadius,
                               COLOR_PROGRESS_FILL[0], COLOR_PROGRESS_FILL[1], COLOR_PROGRESS_FILL[2], 255);
        } else {
            fillCircle(knobX, knobY, thumbRadius, 255, 255, 255, 255);
        }
    }

    // 记录控件位置
    m_controlRects.push_back({margin, barY, barWidth, barHeight,
                              ControlType::ProgressBar, 0});
}

void SDLRenderer::renderPlaybackControls(bool isPlaying, int controlY) {
    int buttonY = controlY + 26;
    int x = 44;
    
    //  上一首按�?
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
    
    //  下一首按�?
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
    fillRoundRect(x, buttonY, 50, m_buttonSize, 8, bgColor[0], bgColor[1], bgColor[2], bgColor[3]);
    
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
    
    //  音量�?
    int volBarWidth = m_volumeWidth;
    int volBarHeight = 6;
    int volBarY = buttonY + (m_buttonSize - volBarHeight) / 2;
    
    fillRoundRect(x, volBarY, volBarWidth, volBarHeight, volBarHeight / 2,
                          COLOR_PROGRESS_BG[0], COLOR_PROGRESS_BG[1], COLOR_PROGRESS_BG[2], COLOR_PROGRESS_BG[3]);

    if (!isMuted) {
        float vol = std::max(0, std::min(100, volume)) / 100.0f;
        int fillW = static_cast<int>(volBarWidth * vol);
        if (fillW < volBarHeight) fillW = volBarHeight;
        fillRoundRect(x, volBarY, fillW, volBarHeight, volBarHeight / 2,
                              COLOR_PROGRESS_FILL[0], COLOR_PROGRESS_FILL[1], COLOR_PROGRESS_FILL[2], COLOR_PROGRESS_FILL[3]);
    }

    // 记录控件位置
    m_controlRects.push_back({x, volBarY, volBarWidth, volBarHeight, ControlType::VolumeBar, 0});
}

void SDLRenderer::renderTimeDisplay(int64_t position, int64_t duration, int controlY) {
    int x = 310;
    int y = controlY + 32;
    
    //  格式化时�?
    std::string timeText = VideoPlay::formatTime(position) + " / " + VideoPlay::formatTime(duration);
    
    // 渲染时间文字
    drawText(timeText, x, y + (m_buttonSize - getFontHeight()) / 2,
             COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2]);
}

void SDLRenderer::renderFilename(const std::string& filename) {
    if (filename.empty()) return;
    
    // 在视频区域顶部显示文件名
    int y = m_menuBarHeight + 5;
    
    //  获取文件名（不含路径�?
    std::string name = filename;
    size_t pos = filename.find_last_of("/\\");
    if (pos != std::string::npos) {
        name = filename.substr(pos + 1);
    }
    
    //  计算文字宽度，限制为不与左右侧面板重�?
    int textW = getTextWidth(name);
    int maxWidth = m_windowWidth - 340; // �?20px 安全间隙
    if (m_showEpisodePanel && m_episodeData && !m_episodeData->empty()) {
        // 左侧选集面板�?260+24=284px
        maxWidth -= 284;
    }
    if (m_showPlaylistPanel) {
        // 右侧播放列表面板�?260+24=284px
        maxWidth -= 284;
    }
    if (maxWidth < 120) maxWidth = 120;
    
    // 截断过长的文件名
    if (textW > maxWidth) {
        while (textW > maxWidth - getTextWidth("...") && name.length() > 3) {
            name = name.substr(0, name.length() - 1);
            textW = getTextWidth(name + "...");
        }
        name += "...";
        textW = getTextWidth(name);
    }
    
    //  背景�?
    int bgWidth = textW + 20;
    fillRect(10, y, bgWidth, 25, 0, 0, 0, 150);
    
    //  渲染文件�?
    drawText(name, 20, y + 5, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2]);
}

void SDLRenderer::renderSubtitle(const std::string& subtitle) {
#ifdef HAS_SDL_TTF
    if (!m_font || subtitle.empty()) return;

    // 按换行符分割字幕文本
    std::vector<std::string> lines;
    std::string current;
    for (char c : subtitle) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    if (lines.empty()) return;

    int fontSize = 16;
    int maxWidth = 0;
    int lineHeight = getFontHeight(fontSize);
    for (const auto& line : lines) {
        int w = getTextWidth(line, fontSize);
        if (w > maxWidth) maxWidth = w;
    }

    int paddingX = 24;
    int paddingY = 12;
    int totalWidth = maxWidth + paddingX * 2;
    int totalHeight = lines.size() * lineHeight + paddingY * 2;

    //  底部边距：控制栏显示时在其上方，否则留一定边�?
    int bottomMargin = m_showControls ? m_controlHeight + 20 : 60;
    int x = (m_windowWidth - totalWidth) / 2;
    int y = m_windowHeight - bottomMargin - totalHeight;
    if (y < m_menuBarHeight + 10) y = m_menuBarHeight + 10;

    // 绘制半透明背景
    fillRoundRect(x, y, totalWidth, totalHeight, 8, 0, 0, 0, 180);

    //  绘制字幕文字（白色带轻微描边效果通过背景实现�?
    int textY = y + paddingY;
    for (const auto& line : lines) {
        int textW = getTextWidth(line, fontSize);
        int textX = x + (totalWidth - textW) / 2;
        drawText(line, textX, textY, 255, 255, 255, fontSize);
        textY += lineHeight;
    }
#endif
}

void SDLRenderer::renderPlaylistPanel(const std::vector<std::string>& playlist, size_t currentIndex) {
    int panelW = 260;
    int panelX = m_windowWidth - 24 - panelW;
    int panelY = m_menuBarHeight + 10; // 上边�?10px，与下方控制栏间距一�?    // 控制栏底部有 24px 边距，其顶部�?m_windowHeight - m_controlHeight - 24
    // 播放列表面板底部需位于控制栏上方，�?10px 安全间隙
    int panelBottomMargin = m_windowHeight - m_controlHeight - 24 - 10;
    int panelH = panelBottomMargin - panelY;
    if (panelH < 60) return;
    int radius = 20;

    // 投影
    fillRoundRect(panelX + 2, panelY + 4, panelW, panelH, radius, 0, 0, 0, 80);
    // 白边
    fillRoundRect(panelX - 1, panelY - 1, panelW + 2, panelH + 2, radius + 1, 255, 255, 255, 55);
    // 背景
    fillRoundRect(panelX, panelY, panelW, panelH, radius,
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
            fillRoundRect(panelX + 12, itemY, panelW - 24, itemH, 8,
                                  COLOR_MENU_ACTIVE[0], COLOR_MENU_ACTIVE[1], COLOR_MENU_ACTIVE[2], 200);
        } else if (hovered) {
            fillRoundRect(panelX + 12, itemY, panelW - 24, itemH, 8,
                                  COLOR_MENU_HOVER[0], COLOR_MENU_HOVER[1], COLOR_MENU_HOVER[2], 160);
        }

        //  记录控件位置用于点击检�?
        m_controlRects.push_back({panelX + 12, itemY, panelW - 24, itemH, ControlType::PlaylistItem, static_cast<int>(i)});

        //  文件�?
        std::string name = playlist[i];
        size_t pos = name.find_last_of("/\\");
        if (pos != std::string::npos) {
            name = name.substr(pos + 1);
        }

        // 悬浮时显示进度百分比
        bool showProgress = hovered && i < m_playlistProgress.size() && m_playlistProgress[i] > 0.0f;
        std::string progressText;
        int progressTextW = 0;
        if (showProgress) {
            std::ostringstream pss;
            pss << static_cast<int>(m_playlistProgress[i] * 100.0f) << "%";
            progressText = pss.str();
            progressTextW = getTextWidth(progressText, 10) + 8;
        }

        // 截断
        int maxTextW = panelW - 48 - progressTextW;
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

        // 悬浮时右侧显示进度百分比
        if (showProgress) {
            int ptY = itemY + (itemH - getFontHeight(10)) / 2;
            drawText(progressText, panelX + panelW - 24 - progressTextW + 4, ptY, 150, 150, 150, 10);
        }
    }
}

void SDLRenderer::renderLoadingAnimation() {
    if (!m_renderer) return;

    int cx = m_windowWidth / 2;
    int cy = m_windowHeight / 2;
    int radius = 36;
    int dotCount = 8;
    
    // 基于时间计算旋转角度
    uint64_t now = SDL_GetTicks();
    float rotation = (now % 1500) / 1500.0f * 2.0f * 3.14159265f;
    
    for (int i = 0; i < dotCount; ++i) {
        float angle = rotation + i * (2.0f * 3.14159265f / dotCount);
        int dx = static_cast<int>(cx + std::cos(angle) * radius);
        int dy = static_cast<int>(cy + std::sin(angle) * radius);
        
        // 渐隐效果：前面的点更透明
        uint8_t alpha = static_cast<uint8_t>(255 * (1.0f - i / static_cast<float>(dotCount)));
        int dotRadius = 5 - i / 3;
        if (dotRadius < 2) dotRadius = 2;
        
        fillCircle(dx, dy, dotRadius, 0, 170, 255, alpha);
    }
    
    // 绘制 "加载中.." 文字
    std::string text = "加载中..";
    int textW = getTextWidth(text, 14);
    drawText(text, cx - textW / 2, cy + radius + 20, 200, 200, 200, 14);
}

void SDLRenderer::renderEpisodePanel() {
    if (!m_episodeData || m_episodeData->empty()) return;

    int panelW = 260;
    int panelX = 24; // 左侧，与右侧播放列表面板对称
    int panelY = m_menuBarHeight + 10;
    int panelBottomMargin = m_windowHeight - m_controlHeight - 24 - 10;
    int panelH = panelBottomMargin - panelY;
    if (panelH < 60) return;
    int radius = 20;

    // 投影
    fillRoundRect(panelX + 2, panelY + 4, panelW, panelH, radius, 0, 0, 0, 80);
    // 白边
    fillRoundRect(panelX - 1, panelY - 1, panelW + 2, panelH + 2, radius + 1, 255, 255, 255, 55);
    // 背景
    fillRoundRect(panelX, panelY, panelW, panelH, radius,
                          COLOR_CONTROL_BG[0], COLOR_CONTROL_BG[1], COLOR_CONTROL_BG[2], COLOR_CONTROL_BG[3]);

    // 标题
    int titleY = panelY + 16;
    std::string panelTitle = "选集";
    if (!m_episodeSeriesName.empty()) {
        if (m_episodeSeasonNumber > 0) {
            panelTitle = m_episodeSeriesName + " S" + std::to_string(m_episodeSeasonNumber);
        } else {
            panelTitle = m_episodeSeriesName;
        }
    }
    drawText(panelTitle, panelX + 16, titleY, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 13);

    // 底部按钮区域
    const int buttonAreaH = 44;
    const int btnW = 90;
    const int btnH = 30;
    const int btnGap = 16;
    int buttonAreaY = panelY + panelH - buttonAreaH;

    int itemStartY = titleY + 28;
    int itemH = 28;
    int listBottomY = buttonAreaY - 8;
    int maxVisible = (listBottomY - itemStartY) / itemH;
    if (maxVisible < 1) maxVisible = 1;

    size_t currentIndex = m_currentEpisodeIndex;
    size_t startIndex = 0;
    if (currentIndex >= static_cast<size_t>(maxVisible)) {
        startIndex = currentIndex - static_cast<size_t>(maxVisible) + 1;
    }
    size_t endIndex = startIndex + maxVisible;
    if (endIndex > m_episodeData->size()) endIndex = m_episodeData->size();

    for (size_t i = startIndex; i < endIndex; ++i) {
        int itemY = itemStartY + static_cast<int>(i - startIndex) * itemH;
        bool isCurrent = (i == currentIndex);
        bool hovered = (m_mouseX >= panelX + 12 && m_mouseX <= panelX + panelW - 12 &&
                        m_mouseY >= itemY && m_mouseY <= itemY + itemH);

        if (isCurrent) {
            fillRoundRect(panelX + 12, itemY, panelW - 24, itemH, 8,
                                  COLOR_MENU_ACTIVE[0], COLOR_MENU_ACTIVE[1], COLOR_MENU_ACTIVE[2], 200);
            //  当前集左�?3px 竖条指示�?
            fillRoundRect(panelX + 12, itemY + 6, 3, itemH - 12, 2,
                          255, 255, 255, 220);
        } else if (hovered) {
            fillRoundRect(panelX + 12, itemY, panelW - 24, itemH, 8,
                                  COLOR_MENU_HOVER[0], COLOR_MENU_HOVER[1], COLOR_MENU_HOVER[2], 160);
        }

        m_controlRects.push_back({panelX + 12, itemY, panelW - 24, itemH, ControlType::EpisodeItem, static_cast<int>(i)});

        const auto& ep = (*m_episodeData)[i];
        std::string label = ep.title;

        // 已播放小圆点（右侧）
        bool hasProgress = (i < m_episodeProgress.size() && m_episodeProgress[i] > 0.0f);
        int dotX = panelX + panelW - 28;
        int dotY = itemY + itemH / 2;
        if (hasProgress) {
            fillCircle(dotX, dotY, 3, 0, 170, 255, 200);
        }

        int maxTextW = panelW - (hasProgress ? 56 : 48);
        int textW = getTextWidth(label, 12);
        if (textW > maxTextW) {
            while (textW > maxTextW - getTextWidth("...", 12) && label.length() > 3) {
                label = label.substr(0, label.length() - 1);
                textW = getTextWidth(label + "...", 12);
            }
            label += "...";
        }

        int textColorR = isCurrent ? 255 : COLOR_TEXT[0];
        int textColorG = isCurrent ? 255 : COLOR_TEXT[1];
        int textColorB = isCurrent ? 255 : COLOR_TEXT[2];
        drawText(label, panelX + 20, itemY + (itemH - getFontHeight(12)) / 2, textColorR, textColorG, textColorB, 12);
    }

    //  绘制分隔�?
    fillRect(panelX + 16, buttonAreaY - 4, panelW - 32, 1, 255, 255, 255, 40);

    //  上一集按�?
    bool canPrev = (currentIndex > 0);
    int prevBtnX = panelX + (panelW - btnW * 2 - btnGap) / 2;
    int prevBtnY = buttonAreaY + (buttonAreaH - btnH) / 2;
    {
        bool hovered = canPrev && (m_hoveredControl == ControlType::EpisodePrev);
        bool pressed = canPrev && (m_pressedControl == ControlType::EpisodePrev);
        uint8_t bgAlpha = canPrev ? (hovered ? 160 : 100) : 50;
        uint8_t textAlpha = canPrev ? 255 : 120;
        fillRoundRect(prevBtnX, prevBtnY, btnW, btnH, 6,
                              COLOR_MENU_HOVER[0], COLOR_MENU_HOVER[1], COLOR_MENU_HOVER[2], bgAlpha);
        std::string txt = "上一集";
        int tw = getTextWidth(txt, 11);
        drawText(txt, prevBtnX + (btnW - tw) / 2, prevBtnY + (btnH - getFontHeight(11)) / 2,
                 COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 11);
        if (canPrev) {
            m_controlRects.push_back({prevBtnX, prevBtnY, btnW, btnH, ControlType::EpisodePrev, 0});
        }
    }

    //  下一集按�?
    bool canNext = (currentIndex + 1 < m_episodeData->size());
    int nextBtnX = prevBtnX + btnW + btnGap;
    int nextBtnY = prevBtnY;
    {
        bool hovered = canNext && (m_hoveredControl == ControlType::EpisodeNext);
        bool pressed = canNext && (m_pressedControl == ControlType::EpisodeNext);
        uint8_t bgAlpha = canNext ? (hovered ? 160 : 100) : 50;
        fillRoundRect(nextBtnX, nextBtnY, btnW, btnH, 6,
                              COLOR_MENU_HOVER[0], COLOR_MENU_HOVER[1], COLOR_MENU_HOVER[2], bgAlpha);
        std::string txt = "下一集";
        int tw = getTextWidth(txt, 11);
        drawText(txt, nextBtnX + (btnW - tw) / 2, nextBtnY + (btnH - getFontHeight(11)) / 2,
                 COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 11);
        if (canNext) {
            m_controlRects.push_back({nextBtnX, nextBtnY, btnW, btnH, ControlType::EpisodeNext, 0});
        }
    }
}

void SDLRenderer::renderTooltip() {
    if (m_tooltip.empty() || m_tooltipTime == 0) return;

    // 延迟 400ms 显示，避免鼠标快速滑过时闪烁
    uint64_t elapsed = SDL_GetTicks() - m_tooltipTime;
    if (elapsed < 400) return;

    int fontSize = 11;
    int textW = getTextWidth(m_tooltip, fontSize);
    int textH = getFontHeight(fontSize);
    int paddingX = 10;
    int paddingY = 6;
    int bgW = textW + paddingX * 2;
    int bgH = textH + paddingY * 2 + 2; // +2 给底部蓝色条

    // Tooltip 位于鼠标上方，带 10px 间距
    int tx = m_mouseX - bgW / 2;
    int ty = m_mouseY - bgH - 10;
    if (tx < 6) tx = 6;
    if (tx + bgW > m_windowWidth - 6) tx = m_windowWidth - 6 - bgW;
    if (ty < 6) ty = m_mouseY + 18; // 如果上方空间不足则显示在下方

    //  1px 圆角边框�?
    fillRoundRect(tx, ty, bgW, bgH, 6, 120, 120, 130, 255);
    // 背景：更深更实心的底色，缩进 1px 形成圆角边框效果
    fillRoundRect(tx + 1, ty + 1, bgW - 2, bgH - 2, 5, 28, 30, 36, 250);
    // 底部蓝色胶囊装饰（主题色），用圆+矩形拼接确保圆角绝对可见
    {
        int barH = 4;
        int barW = std::max(20, static_cast<int>(bgW * 0.30f));
        int barX = tx + (bgW - barW) / 2;
        int barY = ty + bgH - barH - 2;
        int r = barH / 2; // 2
        int cy = barY + r;
        //  左半�?
        fillCircle(barX + r, cy, r, 0, 170, 255, 255);
        //  右半�?
        fillCircle(barX + barW - r - 1, cy, r, 0, 170, 255, 255);
        // 中间矩形连接
        fillRect(barX + r, barY, barW - barH, barH, 0, 170, 255, 255);
    }
    //  文字：纯白色，高对比�?
    drawText(m_tooltip, tx + paddingX, ty + paddingY, 255, 255, 255, fontSize);
}

void SDLRenderer::renderSyncInfo(int64_t audioPts, int64_t videoPts, double avDiff, bool playlistVisible) {
    // 分三列渲染，每列独立圆角背景，列间留白自然分隔，不使用线�?    // A/V/Diff 固定在右上角，播放列表面板在 y=44 开始，自然垂直错开
    const int COL_A_WIDTH = 65;
    const int COL_V_WIDTH = 65;
    const int COL_DIFF_WIDTH = 80;
    const int COL_GAP = 10;
    const int PADDING = 8;
    const int RIGHT_MARGIN = 15;
    const int fontSize = 11;
    
    std::string aTime = VideoPlay::formatTime(audioPts);
    std::string vTime = VideoPlay::formatTime(videoPts);
    std::ostringstream diffOss;
    diffOss << std::fixed << std::setprecision(0) << (avDiff * 1000.0);
    std::string diffVal = diffOss.str() + "ms";
    
    int aTimeW = getTextWidth(aTime, fontSize);
    int vTimeW = getTextWidth(vTime, fontSize);
    int diffValW = getTextWidth(diffVal, fontSize);
    int aLabelW = getTextWidth("A:", fontSize);
    int vLabelW = getTextWidth("V:", fontSize);
    int diffLabelW = getTextWidth("Diff:", fontSize);
    
    int totalWidth = COL_A_WIDTH + COL_GAP + COL_V_WIDTH + COL_GAP + COL_DIFF_WIDTH + PADDING * 2;
    
    //  播放列表面板或选集面板显示时水平避�?
    int panelRight = m_windowWidth - RIGHT_MARGIN;
    if (playlistVisible) {
        int playlistPanelX = m_windowWidth - 24 - 260;
        panelRight = std::min(panelRight, playlistPanelX - 20);
    }
    if (m_showEpisodePanel && m_episodeData && !m_episodeData->empty()) {
        int episodePanelRight = 24 + 260;
        // sync info 在右上角，选集面板在左上角，不冲突，不需要避�?        (void)episodePanelRight;
    }
    
    int x = panelRight - totalWidth;
    // 固定在菜单栏下方，与播放列表面板顶部对齐，上边距 10px
    int y = m_menuBarHeight + 10;
    
    // 根据 diff 大小改变颜色
    uint8_t r = 0, g = 255, b = 0;
    double absDiff = std::abs(avDiff);
    if (absDiff > 0.080) {
        r = 255; g = 0; b = 0;
    } else if (absDiff > 0.040) {
        r = 255; g = 255; b = 0;
    }
    
    //  每列左边缘固定（从右向左推导，确保整体右对齐�?
    int diffColX = panelRight - PADDING - COL_DIFF_WIDTH;
    int vColX = diffColX - COL_GAP - COL_V_WIDTH;
    int aColX = vColX - COL_GAP - COL_A_WIDTH;
    
    //  每列独立圆角高亮背景，自然分隔三�?
    fillRoundRect(aColX, y, COL_A_WIDTH, 22, 4, 40, 40, 40, 160);
    fillRoundRect(vColX, y, COL_V_WIDTH, 22, 4, 40, 40, 40, 160);
    fillRoundRect(diffColX, y, COL_DIFF_WIDTH, 22, 4, 40, 40, 40, 160);
    
    // A 列：标签左对齐，时间右对齐（与标签保持最小间距）
    drawText("A:", aColX + 4, y + 3, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], fontSize);
    drawText(aTime, std::max(aColX + aLabelW + 8, aColX + COL_A_WIDTH - aTimeW - 4), y + 3,
             COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], fontSize);
    
    //  V 列：标签左对齐，时间右对�?
    drawText("V:", vColX + 4, y + 3, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], fontSize);
    drawText(vTime, std::max(vColX + vLabelW + 8, vColX + COL_V_WIDTH - vTimeW - 4), y + 3,
             COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], fontSize);
    
    // Diff 列：标签左对齐，数值右对齐
    drawText("Diff:", diffColX + 4, y + 3, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], fontSize);
    drawText(diffVal, std::max(diffColX + diffLabelW + 8, diffColX + COL_DIFF_WIDTH - diffValW - 4), y + 3,
             r, g, b, fontSize);
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

void SDLRenderer::renderGlowBar(int cx, int cy, int width, int height, uint8_t red, uint8_t green, uint8_t blue, uint8_t centerAlpha, int clipLeft, int clipRight) {
    if (!m_renderer || width <= 0 || height <= 0 || centerAlpha == 0) return;

    int halfW = width / 2;
    int halfH = height / 2;
    int left   = std::max(clipLeft, cx - halfW);
    int right  = std::min(clipRight, cx + halfW);
    int top    = cy - halfH;
    int bottom = cy + halfH;
    if (right <= left) return;

    // 实际中心可能因裁剪偏移，重新计算插值用的中�?alpha
    int actualCenterX = cx;
    if (actualCenterX < left) actualCenterX = left;
    if (actualCenterX > right) actualCenterX = right;

    float rf = red / 255.0f;
    float gf = green / 255.0f;
    float bf = blue / 255.0f;
    float af = centerAlpha / 255.0f;

    SDL_Vertex v[6];
    // top-left
    v[0].position = { static_cast<float>(left),  static_cast<float>(top) };
    v[0].color    = { rf, gf, bf, 0.0f };
    v[0].tex_coord = { 0, 0 };
    // top-center
    v[1].position = { static_cast<float>(actualCenterX), static_cast<float>(top) };
    v[1].color    = { rf, gf, bf, af };
    v[1].tex_coord = { 0, 0 };
    // top-right
    v[2].position = { static_cast<float>(right), static_cast<float>(top) };
    v[2].color    = { rf, gf, bf, 0.0f };
    v[2].tex_coord = { 0, 0 };
    // bottom-left
    v[3].position = { static_cast<float>(left),  static_cast<float>(bottom) };
    v[3].color    = { rf, gf, bf, 0.0f };
    v[3].tex_coord = { 0, 0 };
    // bottom-center
    v[4].position = { static_cast<float>(actualCenterX), static_cast<float>(bottom) };
    v[4].color    = { rf, gf, bf, af };
    v[4].tex_coord = { 0, 0 };
    // bottom-right
    v[5].position = { static_cast<float>(right), static_cast<float>(bottom) };
    v[5].color    = { rf, gf, bf, 0.0f };
    v[5].tex_coord = { 0, 0 };

    int indices[] = { 0, 1, 3, 1, 3, 4, 1, 2, 4, 2, 4, 5 };
    SDL_RenderGeometry(m_renderer, nullptr, v, 6, indices, 12);
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
    //  核心实心�?
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
    //  圆形 hover / pressed 背景（现代播放器风格�?
    if (pressed) {
        fillCircle(cx, cy, w / 2 - 2, COLOR_BUTTON_BG_PRESSED[0], COLOR_BUTTON_BG_PRESSED[1], COLOR_BUTTON_BG_PRESSED[2], COLOR_BUTTON_BG_PRESSED[3]);
    } else if (hovered) {
        fillCircle(cx, cy, w / 2 - 2, COLOR_BUTTON_BG_HOVER[0], COLOR_BUTTON_BG_HOVER[1], COLOR_BUTTON_BG_HOVER[2], COLOR_BUTTON_BG_HOVER[3]);
    }
    
    //  绘制图标（pressed 时缩�?8%，增加轻微按压感�?
    float scale = pressed ? 0.92f : 1.0f;
    drawIcon(cx, cy, iconType, hovered, scale);
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
        // 向右三角，以纹理中心对称
        drawSoftTri({ mkVert(cx + 56, cy), mkVert(cx - 56, cy - 72), mkVert(cx - 56, cy + 72) }, 8);
    } else if (type == "pause") {
        drawSoftRoundRect(cx - 56, cy - 72, 40, 144, 16);
        drawSoftRoundRect(cx + 16, cy - 72, 40, 144, 16);
    } else if (type == "stop") {
        drawSoftRoundRect(cx - 64, cy - 64, 128, 128, 24);
    } else if (type == "prev") {
        //  上一首：竖条在右 + 向左三角，整体居�?
        drawSoftRoundRect(cx + 28, cy - 48, 20, 96, 8);
        drawSoftTri({ mkVert(cx - 48, cy), mkVert(cx + 28, cy - 48), mkVert(cx + 28, cy + 48) }, 6);
    } else if (type == "next") {
        //  下一首：竖条在左 + 向右三角，整体居�?
        drawSoftRoundRect(cx - 48, cy - 48, 20, 96, 8);
        drawSoftTri({ mkVert(cx + 48, cy), mkVert(cx - 28, cy - 48), mkVert(cx - 28, cy + 48) }, 6);
    } else if (type == "volume") {
        //  喇叭：左侧窄手柄 + 右侧梯形喇叭�?
        drawSoftRoundRect(cx - 50, cy - 16, 28, 32, 6);
        auto v0 = mkVert(static_cast<float>(cx - 22), static_cast<float>(cy - 20));
        auto v1 = mkVert(static_cast<float>(cx - 22), static_cast<float>(cy + 20));
        auto v2 = mkVert(static_cast<float>(cx + 60), static_cast<float>(cy + 56));
        auto v3 = mkVert(static_cast<float>(cx + 60), static_cast<float>(cy - 56));
        drawSoftTri({ v0, v1, v2 }, 6);
        drawSoftTri({ v0, v2, v3 }, 6);
    } else if (type == "mute") {
        // 喇叭 + 静音斜杠
        drawSoftRoundRect(cx - 50, cy - 16, 28, 32, 6);
        auto v0 = mkVert(static_cast<float>(cx - 22), static_cast<float>(cy - 20));
        auto v1 = mkVert(static_cast<float>(cx - 22), static_cast<float>(cy + 20));
        auto v2 = mkVert(static_cast<float>(cx + 60), static_cast<float>(cy + 56));
        auto v3 = mkVert(static_cast<float>(cx + 60), static_cast<float>(cy - 56));
        drawSoftTri({ v0, v1, v2 }, 6);
        drawSoftTri({ v0, v2, v3 }, 6);
        //  静音斜杠在喇叭开口右�?
        drawSoftRoundRect(cx + 68, cy - 52, 12, 104, 4);
    } else if (type == "playlist") {
        //  汉堡菜单图标：三条横�?
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

void SDLRenderer::drawIcon(int cx, int cy, const std::string& type, bool hovered, float scale) {
    const uint8_t* color = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;

    SDL_Texture* texture = getIconTexture(type);
    if (texture) {
        int drawSize = static_cast<int>(32 * scale);
        SDL_FRect dstRect = { static_cast<float>(cx - drawSize / 2), static_cast<float>(cy - drawSize / 2), static_cast<float>(drawSize), static_cast<float>(drawSize) };
        // 颜色调制：正常灰白，hover 纯白
        SDL_SetTextureColorMod(texture, color[0], color[1], color[2]);
        SDL_RenderTexture(m_renderer, texture, nullptr, &dstRect);
        SDL_SetTextureColorMod(texture, 255, 255, 255); // 重置，避免影响后续渲染
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
        int hw = r / 2;
        int hh = r;
        int mouthW = r;
        int mouthH = r * 2;
        // 手柄
        fillRect(cx - r, cy - hh / 2, hw, hh, color[0], color[1], color[2], color[3]);
        //  喇叭口梯�?
        SDL_Vertex verts[4];
        verts[0] = { {static_cast<float>(cx - r + hw), static_cast<float>(cy - hh / 2)}, {1, 1, 1, 1}, {0, 0} };
        verts[1] = { {static_cast<float>(cx - r + hw), static_cast<float>(cy + hh / 2)}, {1, 1, 1, 1}, {0, 0} };
        verts[2] = { {static_cast<float>(cx + mouthW), static_cast<float>(cy + mouthH / 2)}, {1, 1, 1, 1}, {0, 0} };
        verts[3] = { {static_cast<float>(cx + mouthW), static_cast<float>(cy - mouthH / 2)}, {1, 1, 1, 1}, {0, 0} };
        int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(m_renderer, nullptr, verts, 4, idx, 6);
    } else if (type == "mute") {
        int hw = r / 2;
        int hh = r;
        int mouthW = r;
        int mouthH = r * 2;
        fillRect(cx - r, cy - hh / 2, hw, hh, 150, 150, 150, 255);
        SDL_Vertex verts[4];
        verts[0] = { {static_cast<float>(cx - r + hw), static_cast<float>(cy - hh / 2)}, {1, 1, 1, 1}, {0, 0} };
        verts[1] = { {static_cast<float>(cx - r + hw), static_cast<float>(cy + hh / 2)}, {1, 1, 1, 1}, {0, 0} };
        verts[2] = { {static_cast<float>(cx + mouthW), static_cast<float>(cy + mouthH / 2)}, {1, 1, 1, 1}, {0, 0} };
        verts[3] = { {static_cast<float>(cx + mouthW), static_cast<float>(cy - mouthH / 2)}, {1, 1, 1, 1}, {0, 0} };
        int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(m_renderer, nullptr, verts, 4, idx, 6);
        // 静音斜杠
        SDL_SetRenderDrawColor(m_renderer, 255, 100, 100, 255);
        int sx = cx + mouthW + 2;
        int sy1 = cy - mouthH / 2 - 2;
        int sy2 = cy + mouthH / 2 + 2;
        SDL_RenderLine(m_renderer, static_cast<float>(sx), static_cast<float>(sy1), static_cast<float>(sx + r / 2), static_cast<float>(sy2));
        SDL_RenderLine(m_renderer, static_cast<float>(sx + 1), static_cast<float>(sy1), static_cast<float>(sx + 1 + r / 2), static_cast<float>(sy2));
    }
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

    SDL_ShowOpenFileDialog(sdlCallback, this, m_window, sdlFilters, 1, nullptr, false);
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

    SDL_ShowOpenFolderDialog(sdlCallback, this, m_window, nullptr, false);
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

void SDLRenderer::setEpisodeItemCallback(EpisodeItemCallback callback) {
    m_episodeItemCallback = callback;
}

void SDLRenderer::setEpisodePrevCallback(EpisodePrevCallback callback) {
    m_episodePrevCallback = callback;
}

void SDLRenderer::setEpisodeNextCallback(EpisodeNextCallback callback) {
    m_episodeNextCallback = callback;
}

void SDLRenderer::setEpisodeData(const std::vector<EpisodeInfo>* episodes, size_t currentIndex,
                                  const std::string& seriesName, int seasonNumber) {
    m_episodeData = episodes;
    m_currentEpisodeIndex = currentIndex;
    m_episodeSeriesName = seriesName;
    m_episodeSeasonNumber = seasonNumber;
}

void SDLRenderer::toggleEpisodePanel() {
    m_showEpisodePanel = !m_showEpisodePanel;
}

void SDLRenderer::togglePlaylistPanel() {
    m_showPlaylistPanel = !m_showPlaylistPanel;
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
