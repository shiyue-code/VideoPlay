#include "renderer/custommessagebox.h"
#include "utils/logger.h"
#include <algorithm>

namespace VideoPlay {

CustomMessageBox::CustomMessageBox(SDL_Window* parentWindow, TTF_Font* font)
    : m_parentWindow(parentWindow), m_font(font) {
}

CustomMessageBox::~CustomMessageBox() {
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
}

void CustomMessageBox::show(const std::string& title, const std::string& message, bool isError) {
    m_title = title;
    m_message = message;
    m_isError = isError;

    calculateSize();

    m_window = SDL_CreateWindow(title.c_str(), m_windowWidth, m_windowHeight, 
                                SDL_WINDOW_POPUP_MENU | SDL_WINDOW_ALWAYS_ON_TOP);
    if (!m_window) {
        Logger::instance().error("Failed to create message box window: " + std::string(SDL_GetError()));
        return;
    }

    m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (!m_renderer) {
        Logger::instance().error("Failed to create message box renderer: " + std::string(SDL_GetError()));
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        return;
    }

    if (m_parentWindow) {
        int parentX, parentY, parentW, parentH;
        SDL_GetWindowPosition(m_parentWindow, &parentX, &parentY);
        SDL_GetWindowSize(m_parentWindow, &parentW, &parentH);
        
        int x = parentX + (parentW - m_windowWidth) / 2;
        int y = parentY + (parentH - m_windowHeight) / 2;
        SDL_SetWindowPosition(m_window, x, y);
    }

    m_running = true;
    while (m_running) {
        handleEvents();
        render();
        SDL_Delay(16);
    }

    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

void CustomMessageBox::calculateSize() {
    int titleWidth = getTextWidth(m_title, m_fontTitle) + 40;
    int minTitleWidth = 300;
    m_windowWidth = std::max(minTitleWidth, titleWidth);

    auto lines = wrapText(m_message, m_windowWidth - 60);
    int lineHeight = getFontHeight(m_font) + 4;
    int contentHeight = static_cast<int>(lines.size()) * lineHeight;
    m_windowHeight = 40 + 20 + contentHeight + 20 + 40 + 20;

    m_windowWidth = std::max(m_windowWidth, 400);
    m_windowWidth = std::min(m_windowWidth, 800);
    m_windowHeight = std::max(m_windowHeight, 200);
    m_windowHeight = std::min(m_windowHeight, 600);
}

std::vector<std::string> CustomMessageBox::wrapText(const std::string& text, int maxWidth) {
    std::vector<std::string> lines;
    std::string currentLine;
    
    for (size_t i = 0; i < text.length(); i++) {
        if (text[i] == '\n') {
            lines.push_back(currentLine);
            currentLine.clear();
            continue;
        }
        
        currentLine += text[i];
        int width = getTextWidth(currentLine, m_font);
        if (width > maxWidth && !currentLine.empty()) {
            std::string lastChar = currentLine.substr(currentLine.length() - 1);
            currentLine.pop_back();
            lines.push_back(currentLine);
            currentLine = lastChar;
        }
    }
    
    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }
    
    return lines;
}

void CustomMessageBox::render() {
    if (!m_renderer) return;

    SDL_SetRenderDrawColor(m_renderer, COLOR_BG[0], COLOR_BG[1], COLOR_BG[2], COLOR_BG[3]);
    SDL_RenderClear(m_renderer);

    SDL_FRect titleBar = {0, 0, static_cast<float>(m_windowWidth), 40};
    SDL_SetRenderDrawColor(m_renderer, COLOR_TITLE_BG[0], COLOR_TITLE_BG[1], COLOR_TITLE_BG[2], COLOR_TITLE_BG[3]);
    SDL_RenderFillRect(m_renderer, &titleBar);

    SDL_SetRenderDrawColor(m_renderer, COLOR_BORDER[0], COLOR_BORDER[1], COLOR_BORDER[2], COLOR_BORDER[3]);
    SDL_RenderRect(m_renderer, &titleBar);

    drawText(m_title, 15, 10, COLOR_TITLE_TEXT[0], COLOR_TITLE_TEXT[1], COLOR_TITLE_TEXT[2], 255, m_fontTitle);

    int closeX = m_windowWidth - 35;
    int closeY = 5;
    int closeSize = 30;
    SDL_FRect closeBtn = {static_cast<float>(closeX), static_cast<float>(closeY), 
                          static_cast<float>(closeSize), static_cast<float>(closeSize)};
    
    float mx, my;
    SDL_GetMouseState(&mx, &my);
    bool closeHovered = (mx >= closeX && mx <= closeX + closeSize && my >= closeY && my <= closeY + closeSize);
    
    if (closeHovered) {
        SDL_SetRenderDrawColor(m_renderer, COLOR_CLOSE_HOVER[0], COLOR_CLOSE_HOVER[1], COLOR_CLOSE_HOVER[2], COLOR_CLOSE_HOVER[3]);
    } else {
        SDL_SetRenderDrawColor(m_renderer, COLOR_BUTTON_BG[0], COLOR_BUTTON_BG[1], COLOR_BUTTON_BG[2], COLOR_BUTTON_BG[3]);
    }
    SDL_RenderFillRect(m_renderer, &closeBtn);

    SDL_SetRenderDrawColor(m_renderer, COLOR_BUTTON_TEXT[0], COLOR_BUTTON_TEXT[1], COLOR_BUTTON_TEXT[2], COLOR_BUTTON_TEXT[3]);
    SDL_RenderLine(m_renderer, closeX + 8, closeY + 8, closeX + closeSize - 8, closeY + closeSize - 8);
    SDL_RenderLine(m_renderer, closeX + closeSize - 8, closeY + 8, closeX + 8, closeY + closeSize - 8);

    auto lines = wrapText(m_message, m_windowWidth - 60);
    int lineHeight = getFontHeight(m_font) + 4;
    int textY = 60;
    for (const auto& line : lines) {
        drawText(line, 30, textY, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 255, m_font);
        textY += lineHeight;
    }

    int okBtnWidth = 80;
    int okBtnHeight = 30;
    int okBtnX = (m_windowWidth - okBtnWidth) / 2;
    int okBtnY = m_windowHeight - okBtnHeight - 15;
    SDL_FRect okBtn = {static_cast<float>(okBtnX), static_cast<float>(okBtnY), 
                       static_cast<float>(okBtnWidth), static_cast<float>(okBtnHeight)};
    
    bool okHovered = (mx >= okBtnX && mx <= okBtnX + okBtnWidth && my >= okBtnY && my <= okBtnY + okBtnHeight);
    
    if (okHovered) {
        SDL_SetRenderDrawColor(m_renderer, COLOR_BUTTON_HOVER[0], COLOR_BUTTON_HOVER[1], COLOR_BUTTON_HOVER[2], COLOR_BUTTON_HOVER[3]);
    } else {
        SDL_SetRenderDrawColor(m_renderer, COLOR_BUTTON_BG[0], COLOR_BUTTON_BG[1], COLOR_BUTTON_BG[2], COLOR_BUTTON_BG[3]);
    }
    SDL_RenderFillRect(m_renderer, &okBtn);

    SDL_SetRenderDrawColor(m_renderer, COLOR_BORDER[0], COLOR_BORDER[1], COLOR_BORDER[2], COLOR_BORDER[3]);
    SDL_RenderRect(m_renderer, &okBtn);

    std::string okText = "确定";
    int okTextWidth = getTextWidth(okText, m_fontButton);
    drawText(okText, okBtnX + (okBtnWidth - okTextWidth) / 2, okBtnY + 7, 
             COLOR_BUTTON_TEXT[0], COLOR_BUTTON_TEXT[1], COLOR_BUTTON_TEXT[2], 255, m_fontButton);

    SDL_RenderPresent(m_renderer);
}

void CustomMessageBox::handleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                m_running = false;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int mx = static_cast<int>(event.button.x);
                    int my = static_cast<int>(event.button.y);

                    int closeX = m_windowWidth - 35;
                    int closeY = 5;
                    int closeSize = 30;
                    if (mx >= closeX && mx <= closeX + closeSize && my >= closeY && my <= closeY + closeSize) {
                        m_running = false;
                        break;
                    }

                    int okBtnWidth = 80;
                    int okBtnHeight = 30;
                    int okBtnX = (m_windowWidth - okBtnWidth) / 2;
                    int okBtnY = m_windowHeight - okBtnHeight - 15;
                    if (mx >= okBtnX && mx <= okBtnX + okBtnWidth && my >= okBtnY && my <= okBtnY + okBtnHeight) {
                        m_running = false;
                        break;
                    }
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_RETURN) {
                    m_running = false;
                }
                break;
        }
    }
}

void CustomMessageBox::drawText(const std::string& text, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, TTF_Font* font) {
    if (!m_renderer || text.empty()) return;
    
    TTF_Font* useFont = font ? font : m_font;
    if (!useFont) return;

    SDL_Color color = {r, g, b, a};
    SDL_Surface* surface = TTF_RenderText_Blended(useFont, text.c_str(), 0, color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    if (texture) {
        SDL_FRect dstRect = {static_cast<float>(x), static_cast<float>(y), 
                             static_cast<float>(surface->w), static_cast<float>(surface->h)};
        SDL_RenderTexture(m_renderer, texture, nullptr, &dstRect);
        SDL_DestroyTexture(texture);
    }

    SDL_DestroySurface(surface);
}

int CustomMessageBox::getTextWidth(const std::string& text, TTF_Font* font) {
    if (text.empty()) return 0;
    
    TTF_Font* useFont = font ? font : m_font;
    if (!useFont) return 0;

    int w = 0;
    TTF_GetStringSize(useFont, text.c_str(), 0, &w, nullptr);
    return w;
}

int CustomMessageBox::getFontHeight(TTF_Font* font) {
    TTF_Font* useFont = font ? font : m_font;
    if (!useFont) return 0;
    return TTF_GetFontHeight(useFont);
}

} // namespace VideoPlay
