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

    std::string m_title;
    std::string m_message;
    bool m_isError = false;
    bool m_running = false;
    bool m_dragging = false;
    int m_dragOffsetX = 0;
    int m_dragOffsetY = 0;
    int m_windowWidth = 500;
    int m_windowHeight = 300;

    void render();
    void handleEvents();
    void calculateSize();
    void drawText(const std::string& text, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    int getTextWidth(const std::string& text);
    int getFontHeight();
    std::vector<std::string> wrapText(const std::string& text, int maxWidth);
};

} // namespace VideoPlay

#endif // CUSTOMMESSAGEBOX_H
