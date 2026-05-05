#ifndef INPUTFIELD_H
#define INPUTFIELD_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <functional>

namespace VideoPlay {

class InputField {
public:
    InputField(TTF_Font* font);
    ~InputField();

    // 设置属性
    void setRect(const SDL_FRect& rect);
    void setValue(const std::string& value);
    void setPassword(bool isPassword);
    void setPlaceholder(const std::string& placeholder);

    // 获取值
    const std::string& getValue() const { return m_value; }
    std::string getDisplayValue() const;

    // 状态管理
    void setActive(bool active);
    bool isActive() const { return m_active; }

    // 事件处理
    bool handleEvent(const SDL_Event& event);

    // 渲染
    void render(SDL_Renderer* renderer, float mouseX, float mouseY);

private:
    TTF_Font* m_font;
    SDL_FRect m_rect = {0, 0, 0, 0};
    std::string m_value;
    std::string m_placeholder;
    bool m_isPassword = false;
    bool m_active = false;

    // 光标和选择状态
    int m_cursorPos = 0;
    int m_selectionStart = -1;
    int m_selectionEnd = -1;
    bool m_selecting = false;

    // 光标闪烁
    uint64_t m_cursorBlinkTime = 0;
    bool m_cursorVisible = true;

    // 辅助函数
    int getCursorPosFromMouseX(int mouseX) const;
    void deleteSelection();
    void selectAll();
    void moveCursorHome();
    void moveCursorEnd();
    void moveCursorLeft();
    void moveCursorRight();
    void deleteCharBefore();
    void deleteCharAfter();

    void drawText(SDL_Renderer* renderer, const std::string& text, int x, int y, 
                  uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    int getTextWidth(const std::string& text) const;
    int getFontHeight() const;
    std::string maskPassword(const std::string& text) const;
};

} // namespace VideoPlay

#endif // INPUTFIELD_H
