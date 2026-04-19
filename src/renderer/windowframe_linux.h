#pragma once

#ifndef _WIN32

#include "renderer/windowframe.h"
#include <SDL3/SDL.h>

namespace VideoPlay {

class WindowFrameLinux : public WindowFrame {
public:
    WindowFrameLinux();
    ~WindowFrameLinux() override;

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

private:
    SDL_Window* m_window = nullptr;
    bool m_enabled = false;

    // 控件布局参数（与 SDLRenderer 保持一致）
    static constexpr int kMenuBarHeight = 24;
    static constexpr int kSysBtnSize = 12;
    static constexpr int kSysBtnGap = 8;
    static constexpr int kSysBtnRightMargin = 10;
    static constexpr int kResizeBorder = 16;

    // 拖动状态
    bool m_dragging = false;
    int m_dragStartMouseX = 0;
    int m_dragStartMouseY = 0;
    int m_dragStartWindowX = 0;
    int m_dragStartWindowY = 0;

    // Resize 状态
    bool m_resizing = false;
    FrameHitTest m_resizeMode = FrameHitTest::None;
    int m_resizeStartMouseX = 0;
    int m_resizeStartMouseY = 0;
    int m_resizeStartWindowX = 0;
    int m_resizeStartWindowY = 0;
    int m_resizeStartWindowW = 0;
    int m_resizeStartWindowH = 0;

    void updateCursor(FrameHitTest hit) const;
};

} // namespace VideoPlay

#endif // !_WIN32
