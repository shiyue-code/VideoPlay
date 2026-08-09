#ifdef _WIN32

#include "renderer/windowframe_win32.h"
#include "utils/logger.h"
#include <SDL3/SDL.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

// DWM 属性常量。新 SDK 已在 dwmapi.h 中定义同名枚举，这里使用本地常量
// 避免在新旧 Windows SDK 之间重复定义类型。
constexpr DWORD kDwmwaWindowCornerPreference = 33;
constexpr int kDwmWindowCornerDefault = 0;
constexpr int kDwmWindowCornerRound = 2;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace VideoPlay {

namespace {

Logger& logger() {
    static auto logger = Logger::get("renderer.windowframe");
    return *logger;
}

constexpr UINT_PTR kSubclassId = 1;
constexpr UINT_PTR kLiveRenderTimerId = 0x5650;  // 'VP'
constexpr UINT kLiveRenderIntervalMs = 16;       // 约 60 FPS

// 把非客户区消息的 hit-test 码转换为框架语义
FrameHitTest fromHitTestCode(WPARAM wParam) {
    switch (static_cast<int>(wParam)) {
        case HTMINBUTTON: return FrameHitTest::MinButton;
        case HTMAXBUTTON: return FrameHitTest::MaxButton;
        case HTCLOSE:     return FrameHitTest::CloseButton;
        case HTCAPTION:   return FrameHitTest::Caption;
        default:          return FrameHitTest::None;
    }
}

bool isSysButton(FrameHitTest hit) {
    return hit == FrameHitTest::MinButton ||
           hit == FrameHitTest::MaxButton ||
           hit == FrameHitTest::CloseButton;
}

// 缩放环命中测试：ringW/ringH 为环窗口客户区尺寸，border 为环厚度
int ringHitTest(int ringW, int ringH, int x, int y, int border) {
    if (ringW <= border * 2 || ringH <= border * 2) return HTNOWHERE;

    const int corner = border + 12;  // 角落判定范围放宽，便于斜向缩放
    const bool onLeft   = x < border;
    const bool onRight  = x >= ringW - border;
    const bool onTop    = y < border;
    const bool onBottom = y >= ringH - border;
    const bool nearLeft   = x < corner;
    const bool nearRight  = x >= ringW - corner;
    const bool nearTop    = y < corner;
    const bool nearBottom = y >= ringH - corner;

    if ((onTop && nearLeft) || (onLeft && nearTop))       return HTTOPLEFT;
    if ((onTop && nearRight) || (onRight && nearTop))     return HTTOPRIGHT;
    if ((onBottom && nearLeft) || (onLeft && nearBottom)) return HTBOTTOMLEFT;
    if ((onBottom && nearRight) || (onRight && nearBottom)) return HTBOTTOMRIGHT;
    if (onTop)    return HTTOP;
    if (onBottom) return HTBOTTOM;
    if (onLeft)   return HTLEFT;
    if (onRight)  return HTRIGHT;
    return HTNOWHERE;
}

LPCTSTR cursorForHitTest(int ht) {
    switch (ht) {
        case HTLEFT:
        case HTRIGHT:       return IDC_SIZEWE;
        case HTTOP:
        case HTBOTTOM:      return IDC_SIZENS;
        case HTTOPLEFT:
        case HTBOTTOMRIGHT: return IDC_SIZENWSE;
        case HTTOPRIGHT:
        case HTBOTTOMLEFT:  return IDC_SIZENESW;
        default:            return IDC_ARROW;
    }
}

} // anonymous namespace

WindowFrameWin32::WindowFrameWin32() = default;

WindowFrameWin32::~WindowFrameWin32() {
    disable();
}

LRESULT CALLBACK WindowFrameWin32::subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                                UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, subclassProc, uIdSubclass);
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    auto* self = reinterpret_cast<WindowFrameWin32*>(dwRefData);
    if (self) {
        bool handled = false;
        LRESULT result = self->handleMessage(hwnd, msg, wParam, lParam, handled);
        if (handled) {
            return result;
        }
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT WindowFrameWin32::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, bool& handled)
{
    handled = false;

    switch (msg) {
        case WM_NCHITTEST: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (!ScreenToClient(hwnd, &pt)) {
                break;
            }
            LRESULT code = HTCLIENT;
            switch (hitTest(pt.x, pt.y)) {
                case FrameHitTest::ResizeLeft:        code = HTLEFT;        break;
                case FrameHitTest::ResizeRight:       code = HTRIGHT;       break;
                case FrameHitTest::ResizeTop:         code = HTTOP;         break;
                case FrameHitTest::ResizeBottom:      code = HTBOTTOM;      break;
                case FrameHitTest::ResizeTopLeft:     code = HTTOPLEFT;     break;
                case FrameHitTest::ResizeTopRight:    code = HTTOPRIGHT;    break;
                case FrameHitTest::ResizeBottomLeft:  code = HTBOTTOMLEFT;  break;
                case FrameHitTest::ResizeBottomRight: code = HTBOTTOMRIGHT; break;
                case FrameHitTest::MinButton:         code = HTMINBUTTON;   break;
                case FrameHitTest::MaxButton:         code = HTMAXBUTTON;   break;
                case FrameHitTest::CloseButton:       code = HTCLOSE;       break;
                case FrameHitTest::Caption:           code = HTCAPTION;     break;
                default:                              code = HTCLIENT;      break;
            }
            handled = true;
            return code;
        }

        case WM_NCMOUSEMOVE: {
            FrameHitTest hit = fromHitTestCode(wParam);
            if (hit != FrameHitTest::None) {
                trackNcMouseLeave(hwnd);
                notifyFrameMouse(hit, FrameMouseAction::Move);
            } else {
                notifyFrameMouse(FrameHitTest::None, FrameMouseAction::Leave);
            }
            break;
        }

        case WM_NCMOUSELEAVE: {
            m_ncTracking = false;
            notifyFrameMouse(FrameHitTest::None, FrameMouseAction::Leave);
            break;
        }

        case WM_NCLBUTTONDOWN: {
            FrameHitTest hit = fromHitTestCode(wParam);
            if (isSysButton(hit)) {
                // 系统按钮由渲染层执行，避免 DefWindowProc 再次响应
                notifyFrameMouse(hit, FrameMouseAction::Click);
                handled = true;
                return 0;
            }
            // 标题栏/缩放边框全部交给系统：原生拖动、贴靠、抖动最小化、双击最大化
            break;
        }

        case WM_WINDOWPOSCHANGED:
        case WM_SIZE:
        case WM_SHOWWINDOW:
        case WM_STYLECHANGED:
        case WM_DPICHANGED: {
            // 让外侧缩放环始终跟随窗口几何
            syncResizeRing();
            break;
        }

        case WM_ENTERSIZEMOVE: {
            // 系统模态循环期间主循环被阻塞，用定时器驱动实时重绘
            SetTimer(hwnd, kLiveRenderTimerId, kLiveRenderIntervalMs, nullptr);
            break;
        }

        case WM_EXITSIZEMOVE: {
            KillTimer(hwnd, kLiveRenderTimerId);
            break;
        }

        case WM_TIMER: {
            if (wParam == kLiveRenderTimerId) {
                if (m_liveRender && !m_inLiveRender) {
                    m_inLiveRender = true;
                    m_liveRender();
                    m_inLiveRender = false;
                }
                handled = true;
                return 0;
            }
            break;
        }

        default:
            break;
    }

    // 让 DWM 处理非客户区消息，以支持 Win11 Snap Layouts（悬停最大化按钮弹出布局）
    switch (msg) {
        case WM_NCHITTEST:
        case WM_NCMOUSEMOVE:
        case WM_NCMOUSELEAVE: {
            LRESULT dwmResult = 0;
            if (DwmDefWindowProc(hwnd, msg, wParam, lParam, &dwmResult)) {
                handled = true;
                return dwmResult;
            }
            break;
        }
        default:
            break;
    }

    return 0;
}

LRESULT CALLBACK WindowFrameWin32::ringWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    auto* self = reinterpret_cast<WindowFrameWin32*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self || !self->m_hwnd) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        // 环窗口不绘制任何内容，保持完全不可见
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SETCURSOR:
        case WM_MOUSEMOVE: {
            POINT pt;
            GetCursorPos(&pt);
            POINT local = pt;
            ScreenToClient(hwnd, &local);
            int ht = ringHitTest(self->m_ringWidth, self->m_ringHeight, local.x, local.y, kOuterGrab);
            SetCursor(LoadCursor(nullptr, cursorForHitTest(ht)));
            return msg == WM_SETCURSOR ? TRUE : 0;
        }

        case WM_LBUTTONDOWN: {
            POINT pt;
            GetCursorPos(&pt);
            POINT local = pt;
            ScreenToClient(hwnd, &local);
            int ht = ringHitTest(self->m_ringWidth, self->m_ringHeight, local.x, local.y, kOuterGrab);
            if (ht != HTNOWHERE) {
                // 把按下交还主窗口，由系统进入原生缩放循环
                // （自带贴靠、最小尺寸、DPI 迁移、WM_ENTERSIZEMOVE 实时重绘）
                ReleaseCapture();
                SetForegroundWindow(self->m_hwnd);
                PostMessage(self->m_hwnd, WM_NCLBUTTONDOWN,
                            static_cast<WPARAM>(ht), MAKELPARAM(pt.x, pt.y));
            }
            return 0;
        }

        case WM_LBUTTONDBLCLK:
            return 0;

        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool WindowFrameWin32::createResizeRing() {
    if (m_ringHwnd) return true;
    if (!m_hwnd) return false;

    HINSTANCE hInstance = GetModuleHandle(nullptr);

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = ringWndProc;
        wc.hInstance = hInstance;
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"VideoPlayResizeRing";
        if (!RegisterClassExW(&wc)) {
            logger().error("WindowFrameWin32: failed to register resize ring class");
            return false;
        }
        classRegistered = true;
    }

    // WS_EX_LAYERED + alpha=1：几乎完全透明但仍参与命中测试
    // （注意不能加 WS_EX_TRANSPARENT，否则鼠标会穿透）
    // owner 设为主窗口：随主窗口最小化/隐藏/销毁，且不出现在任务栏与 Alt+Tab
    m_ringHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        L"VideoPlayResizeRing",
        nullptr,
        WS_POPUP,
        0, 0, 0, 0,
        m_hwnd, nullptr, hInstance, this
    );

    if (!m_ringHwnd) {
        logger().error("WindowFrameWin32: CreateWindowExW failed for resize ring");
        return false;
    }

    SetLayeredWindowAttributes(m_ringHwnd, 0, 1, LWA_ALPHA);
    syncResizeRing();
    return true;
}

void WindowFrameWin32::destroyResizeRing() {
    if (m_ringHwnd) {
        if (IsWindow(m_ringHwnd)) DestroyWindow(m_ringHwnd);
        m_ringHwnd = nullptr;
        m_ringWidth = 0;
        m_ringHeight = 0;
    }
}

void WindowFrameWin32::syncResizeRing() {
    if (!m_hwnd || !m_ringHwnd) return;
    if (!IsWindow(m_ringHwnd)) {
        m_ringHwnd = nullptr;
        return;
    }

    // 最大化/全屏/最小化时不需要外侧热区
    const bool hidden = IsZoomed(m_hwnd) || IsIconic(m_hwnd) ||
                        isFullscreenWindow() || !IsWindowVisible(m_hwnd);
    if (hidden) {
        if (IsWindowVisible(m_ringHwnd)) {
            ShowWindow(m_ringHwnd, SW_HIDE);
        }
        return;
    }

    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    const int w = (rc.right - rc.left) + kOuterGrab * 2;
    const int h = (rc.bottom - rc.top) + kOuterGrab * 2;
    if (w <= kOuterGrab * 2 || h <= kOuterGrab * 2) return;

    // 中间挖空，只保留外侧一圈，避免遮挡主窗口的鼠标输入
    if (w != m_ringWidth || h != m_ringHeight) {
        m_ringWidth = w;
        m_ringHeight = h;
        HRGN outer = CreateRectRgn(0, 0, w, h);
        HRGN inner = CreateRectRgn(kOuterGrab, kOuterGrab, w - kOuterGrab, h - kOuterGrab);
        CombineRgn(outer, outer, inner, RGN_DIFF);
        DeleteObject(inner);
        SetWindowRgn(m_ringHwnd, outer, FALSE);  // 区域所有权移交系统
    }

    // Z-Order 交给 owner 关系维护（owned popup 始终跟随主窗口），这里只同步几何
    SetWindowPos(m_ringHwnd, nullptr,
                 rc.left - kOuterGrab, rc.top - kOuterGrab, w, h,
                 SWP_NOACTIVATE | SWP_NOZORDER |
                 (IsWindowVisible(m_ringHwnd) ? 0u : SWP_SHOWWINDOW));
}

void WindowFrameWin32::notifyFrameMouse(FrameHitTest hit, FrameMouseAction action) {
    if (m_frameMouse) {
        m_frameMouse(hit, action);
    }
}

void WindowFrameWin32::trackNcMouseLeave(HWND hwnd) {
    if (m_ncTracking) return;

    TRACKMOUSEEVENT tme = {};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE | TME_NONCLIENT;
    tme.hwndTrack = hwnd;
    if (TrackMouseEvent(&tme)) {
        m_ncTracking = true;
    }
}

bool WindowFrameWin32::enable(SDL_Window* window)
{
    if (m_enabled || !window) return m_enabled;

    m_window = window;

    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    m_hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!m_hwnd) {
        logger().error("WindowFrameWin32: failed to get HWND");
        m_window = nullptr;
        return false;
    }

    if (!SetWindowSubclass(m_hwnd, subclassProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this))) {
        logger().error("WindowFrameWin32: SetWindowSubclass failed");
        m_hwnd = nullptr;
        m_window = nullptr;
        return false;
    }

    // 由 SDL 切换窗口样式：保留 WS_CAPTION | WS_THICKFRAME（DWM 阴影/圆角/贴靠/动画），
    // 并在 WM_NCCALCSIZE 中把非客户区裁剪为 0，同时最大化时自动使用显示器工作区。
    if (!SDL_SetWindowBordered(m_window, false)) {
        logger().warning("WindowFrameWin32: SDL_SetWindowBordered failed: " + std::string(SDL_GetError()));
    }

    // 原生缩放没有最小尺寸限制，这里补上，避免 UI 布局被压坏
    SDL_SetWindowMinimumSize(m_window, kMinWindowWidth, kMinWindowHeight);

    applyDwmAttributes(true);
    refreshWindow();

    // 外侧（阴影区）缩放热区；创建失败不影响内侧热区，仅记录警告
    if (!createResizeRing()) {
        logger().warning("WindowFrameWin32: resize ring unavailable, outer grab area disabled");
    }

    m_enabled = true;
    logger().info("WindowFrameWin32 enabled (native frame semantics)");
    return true;
}

void WindowFrameWin32::disable()
{
    if (!m_enabled || !m_hwnd) return;

    KillTimer(m_hwnd, kLiveRenderTimerId);
    destroyResizeRing();
    RemoveWindowSubclass(m_hwnd, subclassProc, kSubclassId);
    applyDwmAttributes(false);

    if (m_window) {
        SDL_SetWindowMinimumSize(m_window, 0, 0);
        SDL_SetWindowBordered(m_window, true);
    }
    refreshWindow();

    logger().info("WindowFrameWin32 disabled");
    m_enabled = false;
    m_ncTracking = false;
    m_hwnd = nullptr;
    m_window = nullptr;
}

bool WindowFrameWin32::isEnabled() const {
    return m_enabled;
}

bool WindowFrameWin32::processEvent(const SDL_Event& event) {
    // 拖动与缩放完全由系统处理，无需在 SDL 事件循环中干预
    (void)event;
    return false;
}

bool WindowFrameWin32::isFullscreenWindow() const {
    return m_window && (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
}

FrameHitTest WindowFrameWin32::hitTest(int clientX, int clientY) const {
    if (!m_hwnd || !m_enabled) return FrameHitTest::None;

    // 全屏时既不能拖动也不能缩放
    if (isFullscreenWindow()) return FrameHitTest::None;

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int w = rc.right;
    const int h = rc.bottom;
    if (w <= 0 || h <= 0) return FrameHitTest::None;

    // 最大化时没有缩放边框
    if (!IsZoomed(m_hwnd)) {
        const bool onLeft   = clientX < kResizeBorder;
        const bool onRight  = clientX >= w - kResizeBorder;
        const bool onTop    = clientY < kTopResizeBorder;
        const bool onBottom = clientY >= h - kResizeBorder;

        if (onTop && onLeft)       return FrameHitTest::ResizeTopLeft;
        if (onTop && onRight)      return FrameHitTest::ResizeTopRight;
        if (onBottom && onLeft)    return FrameHitTest::ResizeBottomLeft;
        if (onBottom && onRight)   return FrameHitTest::ResizeBottomRight;
        if (onTop)                 return FrameHitTest::ResizeTop;
        if (onBottom)              return FrameHitTest::ResizeBottom;
        if (onLeft)                return FrameHitTest::ResizeLeft;
        if (onRight)               return FrameHitTest::ResizeRight;
    }

    // 标题栏语义由渲染层判定（UI 布局的唯一来源）
    if (clientY < kMenuBarHeight && m_captionHitTest) {
        return m_captionHitTest(clientX, clientY);
    }

    return FrameHitTest::None;
}

int WindowFrameWin32::resizeBorderWidth() const {
    return kResizeBorder;
}

void WindowFrameWin32::startDrag() {
    // 正常情况下拖动由系统在 HTCAPTION 上完成；此处提供等效的原生拖动入口
    if (!m_hwnd) return;
    ReleaseCapture();
    SendMessage(m_hwnd, WM_SYSCOMMAND, SC_MOVE | 0x0002 /*HTCAPTION*/, 0);
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
    syncResizeRing();
}

void WindowFrameWin32::applyDwmAttributes(bool borderless) {
    if (!m_hwnd) return;

    // 深色标题栏/边框（无边框时仅影响 DWM 绘制的 1px 边框线）
    BOOL darkMode = borderless ? TRUE : FALSE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    // Win11 圆角
    int cornerPref = borderless ? kDwmWindowCornerRound : kDwmWindowCornerDefault;
    DwmSetWindowAttribute(m_hwnd, kDwmwaWindowCornerPreference, &cornerPref, sizeof(cornerPref));
}

void WindowFrameWin32::refreshWindow() {
    if (!m_hwnd) return;
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

} // namespace VideoPlay

#endif // _WIN32
