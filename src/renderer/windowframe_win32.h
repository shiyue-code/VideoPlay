#pragma once

#ifdef _WIN32

#include "renderer/windowframe.h"
#include <SDL3/SDL.h>

#define NOMINMAX
#include <windows.h>

namespace VideoPlay {

// Win32 无边框实现。
//
// 设计要点（"完美无边框"）：
// 1. 窗口样式仍保留 WS_CAPTION | WS_THICKFRAME（由 SDL_SetWindowBordered(false) 应用），
//    因此 DWM 阴影、Win11 圆角、贴靠(Snap)、最小化动画、任务栏预览全部保持原生行为；
//    真正的"无边框"来自 SDL 在 WM_NCCALCSIZE 中把非客户区裁剪为 0。
// 2. 拖动、缩放、双击最大化、抖动最小化、Snap Layouts 全部交给系统 DefWindowProc，
//    只通过 WM_NCHITTEST 告诉系统"哪里是标题栏、哪里是缩放边框"。
// 3. UI 布局信息由渲染层通过 CaptionHitTestFn 提供，平台层不复制 UI 几何。
// 4. 系统拖动/缩放期间 Windows 会进入模态消息循环，通过定时器回调 LiveRenderFn
//    保持画面实时刷新（避免拖动时视频卡住）。
class WindowFrameWin32 : public WindowFrame {
public:
    WindowFrameWin32();
    ~WindowFrameWin32() override;

    bool enable(SDL_Window* window) override;
    void disable() override;
    bool isEnabled() const override;

    bool usesNativeResize() const override { return true; }

    bool processEvent(const SDL_Event& event) override;
    FrameHitTest hitTest(int clientX, int clientY) const override;

    int resizeBorderWidth() const override;
    void startDrag() override;

    void minimizeWindow() override;
    void maximizeWindow() override;
    void restoreWindow() override;
    void closeWindow() override;
    bool isMaximized() const override;

    void setTitle(const char* title) override;
    void updateFrame() override;

private:
    SDL_Window* m_window = nullptr;
    HWND m_hwnd = nullptr;
    bool m_enabled = false;
    bool m_ncTracking = false;      // 是否已注册非客户区 mouse-leave 跟踪
    bool m_inLiveRender = false;    // 防止实时重绘回调重入
    bool m_inModalLoop = false;     // 是否处于系统拖动/缩放模态循环
    DWORD m_lastLiveRenderTick = 0; // 上次实时重绘时间（限流用）

    static constexpr UINT kLiveRenderMinIntervalMs = 13;  // 实时重绘限流（约 60 FPS 上限）

    // 控件布局参数（与 SDLRenderer 保持一致）
    static constexpr int kMenuBarHeight = 32;
    static constexpr int kResizeBorder = 8;     // 客户区内侧缩放热区宽度
    static constexpr int kTopResizeBorder = 5;  // 顶部缩放热区（避免抢占菜单栏点击）
    static constexpr int kOuterGrab = 8;        // 窗口外侧（阴影区）缩放热区宽度
    static constexpr int kMinWindowWidth = 480;
    static constexpr int kMinWindowHeight = 320;

    void applyDwmAttributes(bool borderless);
    void refreshWindow();
    bool isFullscreenWindow() const;

    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, bool& handled);
    void requestLiveRender();
    void notifyFrameMouse(FrameHitTest hit, FrameMouseAction action);
    void trackNcMouseLeave(HWND hwnd);

    static LRESULT CALLBACK subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    // 外侧缩放环：覆盖窗口四周阴影区域的隐形输入窗口，
    // 把阴影区的按下转换为主窗口的原生缩放（WM_NCLBUTTONDOWN）。
    HWND m_ringHwnd = nullptr;
    int m_ringWidth = 0;
    int m_ringHeight = 0;

    bool createResizeRing();
    void destroyResizeRing();
    void syncResizeRing();
    static LRESULT CALLBACK ringWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

} // namespace VideoPlay

#endif // _WIN32
