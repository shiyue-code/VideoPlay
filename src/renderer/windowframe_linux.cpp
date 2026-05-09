#ifndef _WIN32

#include "renderer/windowframe_linux.h"
#include "utils/logger.h"
#include <SDL3/SDL.h>

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto logger = Logger::get("renderer.windowframe");
    return *logger;
}
}


WindowFrameLinux::WindowFrameLinux() = default;

WindowFrameLinux::~WindowFrameLinux() {
    disable();
}

bool WindowFrameLinux::enable(SDL_Window* window) {
    if (m_enabled || !window) {
        return m_enabled;
    }

    m_window = window;
    SDL_SetWindowBordered(window, false);

    logger().info("WindowFrameLinux enabled");
    m_enabled = true;
    return true;
}

void WindowFrameLinux::disable() {
    if (!m_enabled || !m_window) {
        return;
    }

    SDL_SetWindowBordered(m_window, true);

    logger().info("WindowFrameLinux disabled");
    m_enabled = false;
    m_window = nullptr;
    m_dragging = false;
    m_resizing = false;
}

bool WindowFrameLinux::isEnabled() const {
    return m_enabled;
}

bool WindowFrameLinux::processEvent(const SDL_Event& event) {
    if (!m_enabled || !m_window) {
        return false;
    }

    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION: {
            int x = static_cast<int>(event.motion.x);
            int y = static_cast<int>(event.motion.y);
            FrameHitTest hit = hitTest(x, y);
            updateCursor(hit);

            // 处理拖动
            if (m_dragging) {
                int newX = m_dragStartWindowX + (x - m_dragStartMouseX);
                int newY = m_dragStartWindowY + (y - m_dragStartMouseY);
                SDL_SetWindowPosition(m_window, newX, newY);
                return false; // 让 SDLRenderer 也能收到鼠标移动
            }

            // 处理 resize
            if (m_resizing && m_resizeMode != FrameHitTest::None) {
                int dx = x - m_resizeStartMouseX;
                int dy = y - m_resizeStartMouseY;
                int newX = m_resizeStartWindowX;
                int newY = m_resizeStartWindowY;
                int newW = m_resizeStartWindowW;
                int newH = m_resizeStartWindowH;

                switch (m_resizeMode) {
                    case FrameHitTest::ResizeLeft:
                        newX += dx;
                        newW -= dx;
                        break;
                    case FrameHitTest::ResizeRight:
                        newW += dx;
                        break;
                    case FrameHitTest::ResizeTop:
                        newY += dy;
                        newH -= dy;
                        break;
                    case FrameHitTest::ResizeBottom:
                        newH += dy;
                        break;
                    case FrameHitTest::ResizeTopLeft:
                        newX += dx;
                        newY += dy;
                        newW -= dx;
                        newH -= dy;
                        break;
                    case FrameHitTest::ResizeTopRight:
                        newY += dy;
                        newW += dx;
                        newH -= dy;
                        break;
                    case FrameHitTest::ResizeBottomLeft:
                        newX += dx;
                        newW -= dx;
                        newH += dy;
                        break;
                    case FrameHitTest::ResizeBottomRight:
                        newW += dx;
                        newH += dy;
                        break;
                    default:
                        break;
                }

                // 最小窗口尺寸限制
                if (newW < 400) {
                    if (m_resizeMode == FrameHitTest::ResizeLeft ||
                        m_resizeMode == FrameHitTest::ResizeTopLeft ||
                        m_resizeMode == FrameHitTest::ResizeBottomLeft) {
                        newX = m_resizeStartWindowX + m_resizeStartWindowW - 400;
                    }
                    newW = 400;
                }
                if (newH < 300) {
                    if (m_resizeMode == FrameHitTest::ResizeTop ||
                        m_resizeMode == FrameHitTest::ResizeTopLeft ||
                        m_resizeMode == FrameHitTest::ResizeTopRight) {
                        newY = m_resizeStartWindowY + m_resizeStartWindowH - 300;
                    }
                    newH = 300;
                }

                SDL_SetWindowPosition(m_window, newX, newY);
                SDL_SetWindowSize(m_window, newW, newH);
                return false;
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event.button.button == SDL_BUTTON_LEFT) {
                int x = static_cast<int>(event.button.x);
                int y = static_cast<int>(event.button.y);
                FrameHitTest hit = hitTest(x, y);

                if (hit == FrameHitTest::Caption) {
                    m_dragging = true;
                    m_dragStartMouseX = x;
                    m_dragStartMouseY = y;
                    SDL_GetWindowPosition(m_window, &m_dragStartWindowX, &m_dragStartWindowY);
                    return false;
                }

                if (hit == FrameHitTest::MinButton) {
                    minimizeWindow();
                    return true;
                }
                if (hit == FrameHitTest::MaxButton) {
                    if (isMaximized()) {
                        restoreWindow();
                    } else {
                        maximizeWindow();
                    }
                    return true;
                }
                if (hit == FrameHitTest::CloseButton) {
                    closeWindow();
                    return true;
                }

                if (hit == FrameHitTest::ResizeLeft || hit == FrameHitTest::ResizeRight ||
                    hit == FrameHitTest::ResizeTop || hit == FrameHitTest::ResizeBottom ||
                    hit == FrameHitTest::ResizeTopLeft || hit == FrameHitTest::ResizeTopRight ||
                    hit == FrameHitTest::ResizeBottomLeft || hit == FrameHitTest::ResizeBottomRight) {
                    m_resizing = true;
                    m_resizeMode = hit;
                    m_resizeStartMouseX = x;
                    m_resizeStartMouseY = y;
                    SDL_GetWindowPosition(m_window, &m_resizeStartWindowX, &m_resizeStartWindowY);
                    SDL_GetWindowSize(m_window, &m_resizeStartWindowW, &m_resizeStartWindowH);
                    return false;
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_dragging = false;
                m_resizing = false;
                m_resizeMode = FrameHitTest::None;
            }
            break;
        }

        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
            // 状态变化时刷新
            break;
    }

    return false;
}

FrameHitTest WindowFrameLinux::hitTest(int clientX, int clientY) const {
    if (!m_window) {
        return FrameHitTest::None;
    }

    int w = 0, h = 0;
    SDL_GetWindowSize(m_window, &w, &h);

    if (w <= 0 || h <= 0) {
        return FrameHitTest::None;
    }

    const int menuBarHeight = kMenuBarHeight;
    const int btnSize = kSysBtnSize;
    const int btnGap = kSysBtnGap;
    const int rightMargin = kSysBtnRightMargin;
    const int border = kResizeBorder;

    // 系统按钮检测
    if (clientY >= 0 && clientY < menuBarHeight) {
        int startX = w - rightMargin - 3 * btnSize - 2 * btnGap;

        int bxClose = startX + 2 * (btnSize + btnGap);
        if (clientX >= bxClose - 2 && clientX < bxClose + btnSize + 2) {
            return FrameHitTest::CloseButton;
        }

        int bxMax = startX + btnSize + btnGap;
        if (clientX >= bxMax - 2 && clientX < bxMax + btnSize + 2) {
            return FrameHitTest::MaxButton;
        }

        int bxMin = startX;
        if (clientX >= bxMin - 2 && clientX < bxMin + btnSize + 2) {
            return FrameHitTest::MinButton;
        }

        if (clientX < startX - 10) {
            return FrameHitTest::Caption;
        }
    }

    // Resize 检测
    if (!isMaximized()) {
        bool onLeft   = clientX < border;
        bool onRight  = clientX >= w - border;
        bool onTop    = clientY < border;
        bool onBottom = clientY >= h - border;

        if (onTop && onLeft)       return FrameHitTest::ResizeTopLeft;
        if (onTop && onRight)      return FrameHitTest::ResizeTopRight;
        if (onBottom && onLeft)    return FrameHitTest::ResizeBottomLeft;
        if (onBottom && onRight)   return FrameHitTest::ResizeBottomRight;
        if (onTop)                 return FrameHitTest::ResizeTop;
        if (onBottom)              return FrameHitTest::ResizeBottom;
        if (onLeft)                return FrameHitTest::ResizeLeft;
        if (onRight)               return FrameHitTest::ResizeRight;
    }

    return FrameHitTest::None;
}

int WindowFrameLinux::resizeBorderWidth() const {
    return kResizeBorder;
}

void WindowFrameLinux::startDrag() {
    // Linux 上的拖动由 processEvent 中的 Caption 处理
    // 此方法供外部调用（如双击后的拖动）
    if (!m_window) return;
    m_dragging = true;
    int x, y;
    SDL_GetMouseState(&x, &y);
    m_dragStartMouseX = x;
    m_dragStartMouseY = y;
    SDL_GetWindowPosition(m_window, &m_dragStartWindowX, &m_dragStartWindowY);
}

void WindowFrameLinux::minimizeWindow() {
    if (m_window) SDL_MinimizeWindow(m_window);
}

void WindowFrameLinux::maximizeWindow() {
    if (m_window) SDL_MaximizeWindow(m_window);
}

void WindowFrameLinux::restoreWindow() {
    if (m_window) SDL_RestoreWindow(m_window);
}

void WindowFrameLinux::closeWindow() {
    if (m_window) {
        SDL_Event ev;
        ev.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&ev);
    }
}

bool WindowFrameLinux::isMaximized() const {
    if (!m_window) return false;
    return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) != 0;
}

void WindowFrameLinux::setTitle(const char* title) {
    if (m_window && title) {
        SDL_SetWindowTitle(m_window, title);
    }
}

void WindowFrameLinux::updateFrame() {
    // Linux 上不需要额外刷新
}

void WindowFrameLinux::updateCursor(FrameHitTest hit) const {
    if (!m_window) return;

    static SDL_Cursor* s_cursors[6] = {nullptr};
    static FrameHitTest s_lastHit = FrameHitTest::None;
    if (hit == s_lastHit) return;
    s_lastHit = hit;

    int idx = 0;
    switch (hit) {
        case FrameHitTest::ResizeLeft:
        case FrameHitTest::ResizeRight:
            idx = 1; break;
        case FrameHitTest::ResizeTop:
        case FrameHitTest::ResizeBottom:
            idx = 2; break;
        case FrameHitTest::ResizeTopLeft:
        case FrameHitTest::ResizeBottomRight:
            idx = 3; break;
        case FrameHitTest::ResizeTopRight:
        case FrameHitTest::ResizeBottomLeft:
            idx = 4; break;
        default:
            idx = 0; break;
    }

    if (!s_cursors[idx]) {
        SDL_SystemCursor sc = SDL_SYSTEM_CURSOR_DEFAULT;
        switch (idx) {
            case 1: sc = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
            case 2: sc = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
            case 3: sc = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
            case 4: sc = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
            default: sc = SDL_SYSTEM_CURSOR_DEFAULT; break;
        }
        s_cursors[idx] = SDL_CreateSystemCursor(sc);
    }

    if (s_cursors[idx]) {
        SDL_SetCursor(s_cursors[idx]);
    }
}

} // namespace VideoPlay

#endif // !_WIN32
