#ifndef MODALDIALOG_H
#define MODALDIALOG_H

#include "renderer/textutils.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstdint>
#include <string>
#include <vector>

namespace VideoPlay {

// 无边框模态对话框基类。
// 基类承担：窗口/渲染器创建与销毁、父窗口居中、Win11 圆角、
// 标题栏拖动（Win32 走 HTCAPTION）、关闭按钮、Esc/QUIT 退出、
// 文本绘制与测量助手。子类实现 onEvent / renderContent。
class ModalDialog {
public:
    ModalDialog(SDL_Window* parentWindow, TTF_Font* font);
    virtual ~ModalDialog();

    ModalDialog(const ModalDialog&) = delete;
    ModalDialog& operator=(const ModalDialog&) = delete;

protected:
    static constexpr int kTitleHeight = 32;
    static constexpr int kCloseWidth = 46;

    // 通用配色（三个对话框原本各自重复定义同一套）
    static constexpr uint8_t kColorBg[4] = {30, 30, 30, 240};
    static constexpr uint8_t kColorTitleBg[4] = {40, 40, 40, 255};
    static constexpr uint8_t kColorTitleText[4] = {255, 255, 255, 255};
    static constexpr uint8_t kColorButtonBg[4] = {60, 60, 60, 255};
    static constexpr uint8_t kColorButtonHover[4] = {80, 80, 80, 255};
    static constexpr uint8_t kColorButtonText[4] = {255, 255, 255, 255};
    static constexpr uint8_t kColorCloseHover[4] = {232, 17, 35, 255};

    // 子类先设置 m_windowWidth/m_windowHeight，再调 openWindow 创建窗口。
    // enableTextInput 为 true 时在循环期间启用 SDL 文本输入（IME/编辑事件）。
    bool openWindow(const std::string& title, bool enableTextInput);

    // 阻塞事件循环，直到 requestClose()。每帧：先 onEvent 再处理通用事件，
    // 然后 清屏 → renderChrome → renderContent → Present。
    void runModal();
    void requestClose();

    // 返回 true 表示事件已被子类消费；未消费的事件交给基类处理
    // （QUIT/Esc/关闭按钮/标题栏拖动）。
    virtual bool onEvent(const SDL_Event& event);
    // 绘制对话框内容（标题栏与关闭按钮由 renderChrome 绘制）。
    virtual void renderContent();

    // 绘制标题栏背景、标题文本与关闭按钮（带 hover）
    void renderChrome();
    void destroyWindow();

    // 命中助手
    bool hitCloseButton(int mx, int my) const;
    bool hitTitleBar(int mx, int my) const;
    bool isPointInRect(float x, float y, const SDL_FRect& rect) const;

    // 文本助手（委托 TextRender）
    void drawText(const std::string& text, int x, int y,
                  uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    int getTextWidth(const std::string& text) const;
    int getFontHeight() const;
    std::vector<std::string> wrapText(const std::string& text, int maxWidth) const;

    SDL_Window* m_parentWindow;
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    TTF_Font* m_font;

    std::string m_title;
    bool m_running = false;
    bool m_dragging = false;
    bool m_textInputEnabled = false;
    int m_dragOffsetX = 0;
    int m_dragOffsetY = 0;
    int m_windowWidth = 400;
    int m_windowHeight = 200;
};

} // namespace VideoPlay

#endif // MODALDIALOG_H
