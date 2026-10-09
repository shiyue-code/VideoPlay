#ifndef INPUTDIALOG_H
#define INPUTDIALOG_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <functional>
#include <memory>
#include <string>

namespace VideoPlay {

class InputField;

// 单行文本输入对话框：独立无边框窗口模态运行。
// 确定/回车返回输入文本，取消/Esc 返回空串（通过回调区分）。
class InputDialog {
public:
    InputDialog(SDL_Window* parentWindow, TTF_Font* font);
    ~InputDialog();

    // 阻塞显示对话框；确定时以输入文本调用 callback，取消时调用 callback("")
    void show(const std::string& title, const std::string& placeholder,
              const std::string& initialValue,
              std::function<void(const std::string&)> callback);

private:
    SDL_Window* m_parentWindow;
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    TTF_Font* m_font;

    std::unique_ptr<InputField> m_input;
    std::function<void(const std::string&)> m_callback;
    std::string m_title;

    bool m_running = false;
    bool m_dragging = false;
    bool m_accepted = false;
    int m_dragOffsetX = 0;
    int m_dragOffsetY = 0;
    int m_windowWidth = 520;
    int m_windowHeight = 150;

    void render();
    void handleEvents();
    void finish(bool accepted);
    void drawText(const std::string& text, int x, int y,
                  uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    int getTextWidth(const std::string& text);
    int getFontHeight();
};

} // namespace VideoPlay

#endif // INPUTDIALOG_H
