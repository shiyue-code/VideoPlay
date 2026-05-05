#ifndef CUSTOMMESSAGEBOX_H
#define CUSTOMMESSAGEBOX_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <functional>
#include <vector>

namespace VideoPlay {

class CustomMessageBox {
public:
    CustomMessageBox(SDL_Window* parentWindow, TTF_Font* font);
    ~CustomMessageBox();

    void show(const std::string& title, const std::string& message, bool isError = false);

private:
    SDL_Window* m_parentWindow;
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    TTF_Font* m_font;
    TTF_Font* m_fontTitle = nullptr;
    TTF_Font* m_fontButton = nullptr;

    std::string m_title;
    std::string m_message;
    bool m_isError = false;
    bool m_running = false;
    int m_windowWidth = 500;
    int m_windowHeight = 300;

    // 颜色常量
    static constexpr uint8_t COLOR_BG[4] = {30, 30, 30, 240};
    static constexpr uint8_t COLOR_TITLE_BG[4] = {40, 40, 40, 255};
    static constexpr uint8_t COLOR_TEXT[4] = {220, 220, 220, 255};
    static constexpr uint8_t COLOR_TITLE_TEXT[4] = {255, 255, 255, 255};
    static constexpr uint8_t COLOR_BUTTON_BG[4] = {60, 60, 60, 255};
    static constexpr uint8_t COLOR_BUTTON_HOVER[4] = {80, 80, 80, 255};
    static constexpr uint8_t COLOR_BUTTON_TEXT[4] = {255, 255, 255, 255};
    static constexpr uint8_t COLOR_BORDER[4] = {80, 80, 80, 255};
    static constexpr uint8_t COLOR_CLOSE_HOVER[4] = {232, 17, 35, 255};

    void render();
    void handleEvents();
    void calculateSize();
    void drawText(const std::string& text, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255, TTF_Font* font = nullptr);
    int getTextWidth(const std::string& text, TTF_Font* font = nullptr);
    int getFontHeight(TTF_Font* font = nullptr);
    std::vector<std::string> wrapText(const std::string& text, int maxWidth);
};

} // namespace VideoPlay

#endif // CUSTOMMESSAGEBOX_H
