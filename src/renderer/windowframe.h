#pragma once

#include <SDL3/SDL.h>
#include <memory>
#include <functional>

namespace VideoPlay {

// 窗口框架 hit-test 结果
enum class FrameHitTest {
    None,           // 无特殊处理
    Caption,        // 标题栏（可拖动）
    MinButton,      // 最小化按钮
    MaxButton,      // 最大化/还原按钮
    CloseButton,    // 关闭按钮
    ResizeLeft,     // 左侧 resize
    ResizeRight,    // 右侧 resize
    ResizeTop,      // 顶部 resize
    ResizeBottom,   // 底部 resize
    ResizeTopLeft,  // 左上 resize
    ResizeTopRight, // 右上 resize
    ResizeBottomLeft,  // 左下 resize
    ResizeBottomRight  // 右下 resize
};

// 非客户区（标题栏/系统按钮）鼠标动作
enum class FrameMouseAction {
    Move,   // 在标题栏或系统按钮上移动
    Leave,  // 离开非客户区
    Click   // 在系统按钮上按下
};

// 无边框窗口框架管理器接口
class WindowFrame {
public:
    virtual ~WindowFrame() = default;

    // 标题栏命中回调：由渲染层提供，把客户区坐标映射为标题栏语义。
    // 返回 Caption 表示空白可拖动区域，MinButton/MaxButton/CloseButton 表示系统按钮，
    // None 表示该点属于普通 UI 控件（应交给客户区处理）。
    using CaptionHitTestFn = std::function<FrameHitTest(int, int)>;
    // 非客户区鼠标事件回调：用于维持 UI 的 hover 视觉状态与系统按钮点击
    using FrameMouseFn = std::function<void(FrameHitTest, FrameMouseAction)>;
    // 系统模态循环（原生拖动/缩放）期间的实时重绘回调
    using LiveRenderFn = std::function<void()>;

    void setCaptionHitTest(CaptionHitTestFn fn) { m_captionHitTest = std::move(fn); }
    void setFrameMouseHandler(FrameMouseFn fn) { m_frameMouse = std::move(fn); }
    void setLiveRenderHandler(LiveRenderFn fn) { m_liveRender = std::move(fn); }

    // 是否由系统（原生窗口管理器）负责拖动与缩放。
    // 返回 true 时渲染层不再实现自绘 resize/拖动逻辑。
    virtual bool usesNativeResize() const { return false; }

    // 启用/禁用无边框模式
    virtual bool enable(SDL_Window* window) = 0;
    virtual void disable() = 0;
    virtual bool isEnabled() const = 0;

    // 处理平台特定事件（在主事件循环中调用）
    // 返回 true 表示事件已被处理，不需要进一步传递
    virtual bool processEvent(const SDL_Event& event) = 0;

    // Hit-test：给定客户区坐标，返回该区域的功能
    // 由 SDLRenderer 调用以统一处理跨平台逻辑
    virtual FrameHitTest hitTest(int clientX, int clientY) const = 0;

    // 获取推荐的 resize 边框宽度（像素）
    virtual int resizeBorderWidth() const = 0;

    // 开始窗口拖动（在 Caption 区域按下时调用）
    virtual void startDrag() = 0;

    // 执行系统命令（最小化/最大化/还原/关闭）
    virtual void minimizeWindow() = 0;
    virtual void maximizeWindow() = 0;
    virtual void restoreWindow() = 0;
    virtual void closeWindow() = 0;
    virtual bool isMaximized() const = 0;

    // 设置窗口标题
    virtual void setTitle(const char* title) = 0;

    // 更新窗口框架（样式改变后调用）
    virtual void updateFrame() = 0;

    // 工厂方法：创建平台特定的实现
    static std::unique_ptr<WindowFrame> create();

protected:
    CaptionHitTestFn m_captionHitTest;
    FrameMouseFn m_frameMouse;
    LiveRenderFn m_liveRender;
};

} // namespace VideoPlay
