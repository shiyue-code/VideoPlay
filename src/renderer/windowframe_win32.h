#pragma once

#ifdef _WIN32

#include "renderer/windowframe.h"
#include <SDL3/SDL.h>
#include <windows.h>

namespace VideoPlay {

class WindowFrameWin32 : public WindowFrame {
public:
    WindowFrameWin32();
    ~WindowFrameWin32() override;

    bool enable(SDL_Window* window) override;
    void disable() override;
    bool isEnabled() const override;

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

    // 供子类化回调调用
    void syncShadowWindow();
    void updateShadowVisibility();

private:
    SDL_Window* m_window = nullptr;
    HWND m_hwnd = nullptr;
    LONG m_originalStyle = 0;
    bool m_enabled = false;

    // ShadowWindow：透明辅助窗口，用于接收外部鼠标事件实现外部 resize
    HWND m_shadowHwnd = nullptr;
    bool m_syncing = false;  // 防止双向同步递归

    // 控件布局参数（与 SDLRenderer 保持一致）
    static constexpr int kMenuBarHeight = 32;
    static constexpr int kSysBtnSize = 14;
    static constexpr int kSysBtnGap = 12;
    static constexpr int kSysBtnRightMargin = 14;
    static constexpr int kResizeBorder = 16;  // 内部 resize 热区宽度
    static constexpr int kShadowBorder = 8;   // ShadowWindow 外扩边框宽度
    static constexpr int kCornerRadius = 8;   // 窗口圆角半径

    // 自定义拖动状态
    bool m_draggingWindow = false;
    int m_dragStartMouseX = 0;
    int m_dragStartMouseY = 0;
    RECT m_dragStartRect = {};

    void applyStyle();
    void restoreStyle();
    void refreshWindow();

    void createShadowWindow();
    void destroyShadowWindow();

    void performAeroSnap(int screenX, int screenY);

    // Aero Snap 预览窗口
    HWND m_previewHwnd = nullptr;
    void createPreviewWindow();
    void destroyPreviewWindow();
    void updatePreview(int screenX, int screenY);
    void hidePreview();

    static LRESULT CALLBACK shadowWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

} // namespace VideoPlay

#endif // _WIN32
