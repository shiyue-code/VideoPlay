#ifdef _WIN32

#include "renderer/windowframe_win32.h"
#include "utils/logger.h"
#include <SDL3/SDL.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <unordered_map>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

// DWM 圆角偏好（Win11+）
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
enum DWM_WINDOW_CORNER_PREFERENCE {
    DWMWCP_DEFAULT = 0,
    DWMWCP_DONOTROUND = 1,
    DWMWCP_ROUND = 2,
    DWMWCP_ROUNDSMALL = 3
};
#endif

namespace VideoPlay {

namespace {

    // ShadowWindow HWND -> ContentWindow HWND 映射
    std::unordered_map<HWND, HWND> g_shadowToContent;

    // ShadowWindow resize 状态
    struct ShadowResizeState {
        bool active = false;
        int startX = 0;
        int startY = 0;
        RECT startRect = {};
        int mode = 0;
    };
    std::unordered_map<HWND, ShadowResizeState> g_shadowResizeStates;
    bool g_shadowResizingActive = false;  // ShadowWindow 正在主动 resize，避免 WM_SIZE/WM_MOVE 重复同步

    // 将 ShadowWindow 的客户端坐标转换为对应 ContentWindow 的边框 hit-test 值
    int shadowHitTest(HWND shadowHwnd, int x, int y)
    {
        RECT rc;
        GetClientRect(shadowHwnd, &rc);
        int w = rc.right;
        int h = rc.bottom;
        if (w <= 0 || h <= 0) return HTNOWHERE;

        const int border = 8; // kShadowBorder
        bool left   = (x < border);
        bool right  = (x >= w - border);
        bool top    = (y < border);
        bool bottom = (y >= h - border);

        if (top && left)       return HTTOPLEFT;
        if (top && right)      return HTTOPRIGHT;
        if (bottom && left)    return HTBOTTOMLEFT;
        if (bottom && right)   return HTBOTTOMRIGHT;
        if (top)               return HTTOP;
        if (bottom)            return HTBOTTOM;
        if (left)              return HTLEFT;
        if (right)             return HTRIGHT;
        return HTNOWHERE;
    }

    // 子类化回调
    LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                  UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
    {
        // 让 DWM 先处理消息（支持 Snap Layouts 等）
        LRESULT dwmResult = 0;
        if (SUCCEEDED(DwmDefWindowProc(hwnd, msg, wParam, lParam, &dwmResult)) && dwmResult) {
            return dwmResult;
        }

        auto* self = reinterpret_cast<WindowFrameWin32*>(dwRefData);

        switch (msg) {
            case WM_NCCALCSIZE: {
                if (wParam == TRUE) {
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    if (IsZoomed(hwnd)) {
                        int frameX = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                        int frameY = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                        params->rgrc[0].left   += frameX;
                        params->rgrc[0].right  -= frameX;
                        params->rgrc[0].top    += frameY;
                        params->rgrc[0].bottom -= frameY;
                    }
                    // 非最大化时完全削减，消除任何非客户区绘制可能
                    return 0;
                }
                break;
            }

            case WM_NCHITTEST: {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd, &pt);

                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                int w = rcClient.right;
                int h = rcClient.bottom;

                if (w <= 0 || h <= 0) break;

                const int menuBarHeight = 32;
                const int btnSize = 14;
                const int btnGap = 12;
                const int rightMargin = 14;

                // 系统按钮
                if (pt.y >= 0 && pt.y < menuBarHeight) {
                    int startX = w - rightMargin - 3 * btnSize - 2 * btnGap;

                    int bxClose = startX + 2 * (btnSize + btnGap);
                    if (pt.x >= bxClose - 4 && pt.x < bxClose + btnSize + 4)
                        return HTCLOSE;

                    int bxMax = startX + btnSize + btnGap;
                    if (pt.x >= bxMax - 4 && pt.x < bxMax + btnSize + 4)
                        return HTMAXBUTTON;

                    int bxMin = startX;
                    if (pt.x >= bxMin - 4 && pt.x < bxMin + btnSize + 4)
                        return HTMINBUTTON;

                    if (pt.x < startX - 10)
                        return HTCAPTION;
                }

                // 边框 resize 区域（对应 ShadowWindow 外部的 8px 边框）
                if (!IsZoomed(hwnd)) {
                    const int border = 8;
                    if (pt.x < border) {
                        if (pt.y < border)       return HTTOPLEFT;
                        else if (pt.y >= h - border) return HTBOTTOMLEFT;
                        else                     return HTLEFT;
                    } else if (pt.x >= w - border) {
                        if (pt.y < border)       return HTTOPRIGHT;
                        else if (pt.y >= h - border) return HTBOTTOMRIGHT;
                        else                     return HTRIGHT;
                    } else if (pt.y < border) {
                        return HTTOP;
                    } else if (pt.y >= h - border) {
                        return HTBOTTOM;
                    }
                }

                break;
            }

            case WM_NCMOUSEMOVE: {
                int ht = static_cast<int>(wParam);
                if (ht == HTMINBUTTON || ht == HTMAXBUTTON || ht == HTCLOSE || ht == HTCAPTION) {
                    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    ScreenToClient(hwnd, &pt);
                    PostMessage(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
                }
                break;
            }

            case WM_NCLBUTTONDOWN: {
                int ht = static_cast<int>(wParam);
                if (ht == HTMINBUTTON || ht == HTMAXBUTTON || ht == HTCLOSE) {
                    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    ScreenToClient(hwnd, &pt);
                    PostMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
                    return 0;
                }
                if (ht == HTCAPTION) {
                    // 拦截标题栏点击，转为自定义拖动，避免系统默认拖动出现原生标题栏
                    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    ScreenToClient(hwnd, &pt);
                    SendMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
                    return 0;
                }
                // 拦截所有 resize 边框的鼠标按下，防止系统默认行为
                if (ht == HTLEFT || ht == HTRIGHT || ht == HTTOP || ht == HTBOTTOM ||
                    ht == HTTOPLEFT || ht == HTTOPRIGHT || ht == HTBOTTOMLEFT || ht == HTBOTTOMRIGHT) {
                    return 0;
                }
                break;
            }

            case WM_NCLBUTTONUP: {
                int ht = static_cast<int>(wParam);
                if (ht == HTMINBUTTON || ht == HTMAXBUTTON || ht == HTCLOSE) {
                    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    ScreenToClient(hwnd, &pt);
                    PostMessage(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
                    return 0;
                }
                if (ht == HTCAPTION) {
                    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    ScreenToClient(hwnd, &pt);
                    SendMessage(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
                    return 0;
                }
                // 拦截所有 resize 边框的鼠标释放
                if (ht == HTLEFT || ht == HTRIGHT || ht == HTTOP || ht == HTBOTTOM ||
                    ht == HTTOPLEFT || ht == HTTOPRIGHT || ht == HTBOTTOMLEFT || ht == HTBOTTOMRIGHT) {
                    return 0;
                }
                break;
            }

            case WM_NCLBUTTONDBLCLK: {
                int ht = static_cast<int>(wParam);
                if (ht == HTCAPTION) {
                    if (IsZoomed(hwnd)) {
                        ShowWindow(hwnd, SW_RESTORE);
                    } else {
                        ShowWindow(hwnd, SW_MAXIMIZE);
                    }
                    return 0;
                }
                break;
            }

            case WM_WINDOWPOSCHANGED: {
                if (self) self->syncShadowWindow();
                break;
            }

            case WM_SIZE: {
                if (self) self->updateShadowVisibility();
                break;
            }

            case WM_NCDESTROY:
                RemoveWindowSubclass(hwnd, SubclassProc, uIdSubclass);
                break;

            case WM_NCACTIVATE: {
                // 激活/失活时强制刷新非客户区，防止残留原生标题栏
                RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
                break;
            }
        }

        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

} // anonymous namespace

// ShadowWindow 窗口过程：显示阴影 + 处理四边四角 resize
LRESULT CALLBACK WindowFrameWin32::shadowWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_NCHITTEST: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            int ht = shadowHitTest(hwnd, pt.x, pt.y);
            if (ht != HTNOWHERE) {
                return ht;
            }
            // 非边框区域透明，让事件穿透到下方窗口
            return HTTRANSPARENT;
        }

        case WM_NCLBUTTONDOWN: {
            int ht = static_cast<int>(wParam);
            if (ht == HTLEFT || ht == HTRIGHT || ht == HTTOP || ht == HTBOTTOM ||
                ht == HTTOPLEFT || ht == HTTOPRIGHT || ht == HTBOTTOMLEFT || ht == HTBOTTOMRIGHT) {
                auto it = g_shadowToContent.find(hwnd);
                if (it != g_shadowToContent.end()) {
                    HWND contentHwnd = it->second;
                    RECT rc;
                    GetWindowRect(contentHwnd, &rc);
                    auto& state = g_shadowResizeStates[hwnd];
                    state.active = true;
                    state.startX = GET_X_LPARAM(lParam);
                    state.startY = GET_Y_LPARAM(lParam);
                    state.startRect = rc;
                    state.mode = ht;
                    g_shadowResizingActive = true;
                    SetCapture(hwnd);
                }
                return 0;
            }
            break;
        }

        case WM_MOUSEMOVE: {
            auto it = g_shadowResizeStates.find(hwnd);
            if (it != g_shadowResizeStates.end() && it->second.active) {
                auto& state = it->second;
                POINT curPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ClientToScreen(hwnd, &curPt);
                int curX = curPt.x;
                int curY = curPt.y;
                int dx = curX - state.startX;
                int dy = curY - state.startY;

                int newLeft   = state.startRect.left;
                int newTop    = state.startRect.top;
                int newRight  = state.startRect.right;
                int newBottom = state.startRect.bottom;

                switch (state.mode) {
                    case HTLEFT:       newLeft   += dx; break;
                    case HTRIGHT:      newRight  += dx; break;
                    case HTTOP:        newTop    += dy; break;
                    case HTBOTTOM:     newBottom += dy; break;
                    case HTTOPLEFT:    newLeft   += dx; newTop    += dy; break;
                    case HTTOPRIGHT:   newRight  += dx; newTop    += dy; break;
                    case HTBOTTOMLEFT: newLeft   += dx; newBottom += dy; break;
                    case HTBOTTOMRIGHT:newRight  += dx; newBottom += dy; break;
                }

                // 最小尺寸限制
                const int minW = 200;
                const int minH = 150;
                if (newRight - newLeft < minW) {
                    if (state.mode == HTLEFT || state.mode == HTTOPLEFT || state.mode == HTBOTTOMLEFT)
                        newLeft = newRight - minW;
                    else
                        newRight = newLeft + minW;
                }
                if (newBottom - newTop < minH) {
                    if (state.mode == HTTOP || state.mode == HTTOPLEFT || state.mode == HTTOPRIGHT)
                        newTop = newBottom - minH;
                    else
                        newBottom = newTop + minH;
                }

                auto contentIt = g_shadowToContent.find(hwnd);
                if (contentIt != g_shadowToContent.end()) {
                    HWND contentHwnd = contentIt->second;
                    SetWindowPos(contentHwnd, nullptr,
                        newLeft, newTop,
                        newRight - newLeft, newBottom - newTop,
                        SWP_NOZORDER | SWP_NOACTIVATE);
                }
                return 0;
            }
            break;
        }

        case WM_LBUTTONUP:
        case WM_NCLBUTTONUP: {
            auto it = g_shadowResizeStates.find(hwnd);
            if (it != g_shadowResizeStates.end() && it->second.active) {
                it->second.active = false;
                g_shadowResizingActive = false;
                ReleaseCapture();
                return 0;
            }
            break;
        }

        case WM_NCDESTROY: {
            g_shadowToContent.erase(hwnd);
            g_shadowResizeStates.erase(hwnd);
            break;
        }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

WindowFrameWin32::WindowFrameWin32() = default;

WindowFrameWin32::~WindowFrameWin32() {
    disable();
}

bool WindowFrameWin32::enable(SDL_Window* window)
{
    if (m_enabled || !window) return m_enabled;

    m_window = window;

    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    m_hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!m_hwnd) {
        Logger::instance().error("WindowFrameWin32: failed to get HWND");
        return false;
    }

    m_originalStyle = GetWindowLong(m_hwnd, GWL_STYLE);
    applyStyle();
    SetWindowSubclass(m_hwnd, SubclassProc, 0, reinterpret_cast<DWORD_PTR>(this));

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    refreshWindow();
    createShadowWindow();

    Logger::instance().info("WindowFrameWin32 enabled");
    m_enabled = true;
    return true;
}

void WindowFrameWin32::disable()
{
    if (!m_enabled || !m_hwnd) return;

    destroyShadowWindow();
    destroyPreviewWindow();
    RemoveWindowSubclass(m_hwnd, SubclassProc, 0);
    restoreStyle();
    refreshWindow();

    Logger::instance().info("WindowFrameWin32 disabled");
    m_enabled = false;
    m_hwnd = nullptr;
    m_window = nullptr;
    m_originalStyle = 0;
}

bool WindowFrameWin32::isEnabled() const {
    return m_enabled;
}

bool WindowFrameWin32::processEvent(const SDL_Event& event) {
    if (!m_enabled || !m_hwnd) return false;

    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event.button.button == SDL_BUTTON_LEFT) {
                int x = static_cast<int>(event.button.x);
                int y = static_cast<int>(event.button.y);
                FrameHitTest hit = hitTest(x, y);

                if (hit == FrameHitTest::Caption) {
                    // 开始自定义拖动
                    m_draggingWindow = true;
                    POINT pt;
                    GetCursorPos(&pt);
                    m_dragStartMouseX = pt.x;
                    m_dragStartMouseY = pt.y;
                    GetWindowRect(m_hwnd, &m_dragStartRect);
                    return false; // 让 SDLRenderer 也收到事件（用于菜单点击等）
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_MOTION: {
            if (m_draggingWindow) {
                POINT curPt;
                GetCursorPos(&curPt);
                int newX = m_dragStartRect.left + (curPt.x - m_dragStartMouseX);
                int newY = m_dragStartRect.top + (curPt.y - m_dragStartMouseY);

                // 使用 DeferWindowPos 同时移动主窗口和 ShadowWindow，消除抖动
                if (m_shadowHwnd && IsWindow(m_shadowHwnd)) {
                    RECT rc;
                    GetWindowRect(m_hwnd, &rc);
                    int w = rc.right - rc.left;
                    int h = rc.bottom - rc.top;
                    HDWP hDwp = BeginDeferWindowPos(2);
                    DeferWindowPos(hDwp, m_hwnd, nullptr, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
                    DeferWindowPos(hDwp, m_shadowHwnd, nullptr, newX - kShadowBorder, newY - kShadowBorder,
                        w + kShadowBorder * 2, h + kShadowBorder * 2, SWP_NOACTIVATE | SWP_NOZORDER);
                    EndDeferWindowPos(hDwp);
                } else {
                    SetWindowPos(m_hwnd, nullptr, newX, newY, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
                }

                // Aero Snap 预览
                updatePreview(curPt.x, curPt.y);
                return false;
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event.button.button == SDL_BUTTON_LEFT && m_draggingWindow) {
                m_draggingWindow = false;
                hidePreview();

                // 自定义 Aero Snap：检测释放位置是否在屏幕边缘
                POINT pt;
                GetCursorPos(&pt);
                performAeroSnap(pt.x, pt.y);
                return false;
            }
            break;
        }
    }

    return false;
}

FrameHitTest WindowFrameWin32::hitTest(int clientX, int clientY) const {
    if (!m_hwnd) return FrameHitTest::None;

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int w = rc.right;
    int h = rc.bottom;
    if (w <= 0 || h <= 0) return FrameHitTest::None;

    const int menuBarHeight = kMenuBarHeight;
    const int btnSize = kSysBtnSize;
    const int btnGap = kSysBtnGap;
    const int rightMargin = kSysBtnRightMargin;

    // 系统按钮
    if (clientY >= 0 && clientY < menuBarHeight) {
        int startX = w - rightMargin - 3 * btnSize - 2 * btnGap;
        int bxClose = startX + 2 * (btnSize + btnGap);
        if (clientX >= bxClose - 4 && clientX < bxClose + btnSize + 4)
            return FrameHitTest::CloseButton;

        int bxMax = startX + btnSize + btnGap;
        if (clientX >= bxMax - 4 && clientX < bxMax + btnSize + 4)
            return FrameHitTest::MaxButton;

        int bxMin = startX;
        if (clientX >= bxMin - 4 && clientX < bxMin + btnSize + 4)
            return FrameHitTest::MinButton;

        if (clientX < startX - 10)
            return FrameHitTest::Caption;
    }

    // Windows 上主窗口内部不返回 resize 值，所有 resize 通过 ShadowWindow 外部处理
    return FrameHitTest::None;
}

int WindowFrameWin32::resizeBorderWidth() const {
    return kResizeBorder;
}

void WindowFrameWin32::startDrag() {
    // 拖动现在完全由 processEvent 中的自定义逻辑处理
    // 此方法保留供外部调用兼容，但实际逻辑在 SDL 事件循环中
}

void WindowFrameWin32::minimizeWindow() {
    if (m_hwnd) ShowWindow(m_hwnd, SW_MINIMIZE);
}

void WindowFrameWin32::maximizeWindow() {
    if (m_hwnd) ShowWindow(m_hwnd, SW_MAXIMIZE);
}

void WindowFrameWin32::restoreWindow() {
    if (m_hwnd) ShowWindow(m_hwnd, SW_RESTORE);
}

void WindowFrameWin32::closeWindow() {
    if (m_hwnd) PostMessage(m_hwnd, WM_CLOSE, 0, 0);
}

bool WindowFrameWin32::isMaximized() const {
    if (!m_hwnd) return false;
    return IsZoomed(m_hwnd) != FALSE;
}

void WindowFrameWin32::setTitle(const char* title) {
    if (m_hwnd && title) SetWindowTextA(m_hwnd, title);
}

void WindowFrameWin32::updateFrame() {
    refreshWindow();
}

void WindowFrameWin32::applyStyle() {
    if (!m_hwnd) return;
    LONG newStyle = m_originalStyle;
    newStyle &= ~WS_CAPTION;
    newStyle |= WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU;
    SetWindowLong(m_hwnd, GWL_STYLE, newStyle);

    // 设置窗口圆角 (Win11 DWM)
    DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
}

void WindowFrameWin32::restoreStyle() {
    if (!m_hwnd) return;
    SetWindowLong(m_hwnd, GWL_STYLE, m_originalStyle);

    // 恢复窗口圆角为默认
    DWM_WINDOW_CORNER_PREFERENCE cornerPrefRestore = DWMWCP_DEFAULT;
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPrefRestore, sizeof(cornerPrefRestore));
}

void WindowFrameWin32::refreshWindow() {
    if (!m_hwnd) return;
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
}

// Aero Snap 自定义实现
void WindowFrameWin32::performAeroSnap(int screenX, int screenY) {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    const int threshold = 10;

    if (screenY <= threshold) {
        // 顶部边缘：最大化
        ShowWindow(m_hwnd, SW_MAXIMIZE);
    } else if (screenX <= threshold) {
        // 左边缘：贴靠左半屏
        SetWindowPos(m_hwnd, nullptr, 0, 0, screenW / 2, screenH, SWP_NOZORDER);
    } else if (screenX >= screenW - threshold) {
        // 右边缘：贴靠右半屏
        SetWindowPos(m_hwnd, nullptr, screenW / 2, 0, screenW / 2, screenH, SWP_NOZORDER);
    }
}

// ShadowWindow 管理

void WindowFrameWin32::createShadowWindow() {
    if (m_shadowHwnd || !m_hwnd) return;

    HINSTANCE hInstance = GetModuleHandle(nullptr);

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = shadowWndProc;
        wc.hInstance = hInstance;
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"VideoPlayShadowWindow";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    RECT rc;
    GetWindowRect(m_hwnd, &rc);

    m_shadowHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        L"VideoPlayShadowWindow",
        nullptr,
        WS_POPUP,
        rc.left - kShadowBorder,
        rc.top - kShadowBorder,
        rc.right - rc.left + kShadowBorder * 2,
        rc.bottom - rc.top + kShadowBorder * 2,
        nullptr, nullptr, hInstance, nullptr
    );

    if (m_shadowHwnd) {
        // ShadowWindow 全透明，仅用于接收边框 resize 事件
        SetLayeredWindowAttributes(m_shadowHwnd, 0, 1, LWA_ALPHA);
        g_shadowToContent[m_shadowHwnd] = m_hwnd;

        // Z-Order 设置：ShadowWindow 必须在主窗口上方
        ShowWindow(m_shadowHwnd, SW_SHOW);
        SetWindowPos(m_shadowHwnd, m_hwnd, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(m_hwnd, m_shadowHwnd, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        Logger::instance().info("ShadowWindow created: hwnd=" +
            std::to_string(reinterpret_cast<uintptr_t>(m_shadowHwnd)) +
            " pos=" + std::to_string(rc.left - kShadowBorder) + "," + std::to_string(rc.top - kShadowBorder) +
            " size=" + std::to_string(rc.right - rc.left + kShadowBorder * 2) + "x" + std::to_string(rc.bottom - rc.top + kShadowBorder * 2));
    }
}

void WindowFrameWin32::destroyShadowWindow() {
    if (m_shadowHwnd) {
        if (IsWindow(m_shadowHwnd)) DestroyWindow(m_shadowHwnd);
        g_shadowToContent.erase(m_shadowHwnd);
        g_shadowResizeStates.erase(m_shadowHwnd);
        m_shadowHwnd = nullptr;
    }
}

void WindowFrameWin32::syncShadowWindow() {
    if (m_syncing || !m_hwnd || !m_shadowHwnd) return;
    if (!IsWindow(m_shadowHwnd)) { m_shadowHwnd = nullptr; return; }
    if (IsZoomed(m_hwnd) || IsIconic(m_hwnd)) return;
    if (g_shadowResizingActive) return; // ShadowWindow 正在主动 resize，避免重复同步

    m_syncing = true;
    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    // SWP_NOZORDER 很重要：同步位置时绝对不能改 Z-Order，
    // 否则会把 ShadowWindow 重新压到主窗口下方。
    SetWindowPos(m_shadowHwnd, nullptr,
        rc.left - kShadowBorder, rc.top - kShadowBorder,
        rc.right - rc.left + kShadowBorder * 2, rc.bottom - rc.top + kShadowBorder * 2,
        SWP_NOACTIVATE | SWP_NOZORDER);
    m_syncing = false;
}

void WindowFrameWin32::updateShadowVisibility() {
    if (!m_shadowHwnd || !m_hwnd) return;
    if (!IsWindow(m_shadowHwnd)) { m_shadowHwnd = nullptr; return; }

    if (IsZoomed(m_hwnd) || IsIconic(m_hwnd)) {
        ShowWindow(m_shadowHwnd, SW_HIDE);
    } else {
        ShowWindow(m_shadowHwnd, SW_SHOW);
        syncShadowWindow();
        // 从隐藏恢复后，重新确保 ShadowWindow 在主窗口上方
        SetWindowPos(m_shadowHwnd, m_hwnd, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(m_hwnd, m_shadowHwnd, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

// Aero Snap 预览窗口

void WindowFrameWin32::createPreviewWindow() {
    if (m_previewHwnd) return;

    HINSTANCE hInstance = GetModuleHandle(nullptr);

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        wc.hInstance = hInstance;
        wc.lpszClassName = L"VideoPlayPreviewWindow";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    m_previewHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        L"VideoPlayPreviewWindow",
        nullptr,
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInstance, nullptr
    );

    if (m_previewHwnd) {
        // 半透明白色（alpha = 70）
        SetLayeredWindowAttributes(m_previewHwnd, 0, 70, LWA_ALPHA);
    }
}

void WindowFrameWin32::destroyPreviewWindow() {
    if (m_previewHwnd) {
        if (IsWindow(m_previewHwnd)) DestroyWindow(m_previewHwnd);
        m_previewHwnd = nullptr;
    }
}

void WindowFrameWin32::updatePreview(int screenX, int screenY) {
    if (!m_hwnd) return;
    if (!m_previewHwnd) createPreviewWindow();
    if (!m_previewHwnd) return;

    // 获取屏幕工作区（排除任务栏）
    RECT workArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);

    const int threshold = 10;
    bool nearTop = screenY <= workArea.top + threshold;
    bool nearLeft = screenX <= workArea.left + threshold;
    bool nearRight = screenX >= workArea.right - threshold;

    int previewX = 0, previewY = 0, previewW = 0, previewH = 0;
    bool show = false;

    if (nearTop) {
        // 顶部：最大化预览（整个工作区）
        previewX = workArea.left;
        previewY = workArea.top;
        previewW = workArea.right - workArea.left;
        previewH = workArea.bottom - workArea.top;
        show = true;
    } else if (nearLeft) {
        // 左侧：半屏预览
        previewW = (workArea.right - workArea.left) / 2;
        previewH = workArea.bottom - workArea.top;
        previewX = workArea.left;
        previewY = workArea.top;
        show = true;
    } else if (nearRight) {
        // 右侧：半屏预览
        previewW = (workArea.right - workArea.left) / 2;
        previewH = workArea.bottom - workArea.top;
        previewX = workArea.left + previewW;
        previewY = workArea.top;
        show = true;
    }

    if (show) {
        SetWindowPos(m_previewHwnd, HWND_TOPMOST,
            previewX, previewY, previewW, previewH,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        ShowWindow(m_previewHwnd, SW_HIDE);
    }
}

void WindowFrameWin32::hidePreview() {
    if (m_previewHwnd) {
        ShowWindow(m_previewHwnd, SW_HIDE);
    }
}

} // namespace VideoPlay

#endif // _WIN32
