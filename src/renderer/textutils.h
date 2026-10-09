#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <vector>
#include <cstdint>

// 各弹窗/输入框共享的文字绘制与测量工具（独立窗口渲染场景，
// 不带 SDLRenderer 的纹理缓存；主渲染层走 SDLRenderer::drawText）
namespace VideoPlay::TextRender {

inline int fontHeight(TTF_Font* font) {
    return font ? TTF_GetFontHeight(font) : 0;
}

inline int textWidth(TTF_Font* font, const std::string& text) {
    if (text.empty() || !font) return 0;
    int w = 0;
    TTF_GetStringSize(font, text.c_str(), 0, &w, nullptr);
    return w;
}

inline void drawText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
                     int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    if (!renderer || text.empty() || !font) return;

    SDL_Color color = {r, g, b, a};
    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), 0, color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_FRect dstRect = {static_cast<float>(x), static_cast<float>(y),
                             static_cast<float>(surface->w), static_cast<float>(surface->h)};
        SDL_RenderTexture(renderer, texture, nullptr, &dstRect);
        SDL_DestroyTexture(texture);
    }
    SDL_DestroySurface(surface);
}

// 返回 text[pos] 处 UTF-8 字符的字节长度（截断时返回剩余长度）
inline size_t utf8CharLength(const std::string& text, size_t pos) {
    if (pos >= text.size()) return 0;
    unsigned char ch = static_cast<unsigned char>(text[pos]);
    if (ch < 0x80) return 1;
    if ((ch & 0xE0) == 0xC0) return (pos + 1 < text.size()) ? 2 : 1;
    if ((ch & 0xF0) == 0xE0) return (pos + 2 < text.size()) ? 3 : 1;
    if ((ch & 0xF8) == 0xF0) return (pos + 3 < text.size()) ? 4 : 1;
    return 1;
}

// 按像素宽度对 UTF-8 文本做换行拆分
inline std::vector<std::string> wrapText(TTF_Font* font, const std::string& text,
                                         int maxWidth) {
    std::vector<std::string> lines;
    std::string currentLine;

    for (size_t i = 0; i < text.length();) {
        if (text[i] == '\r') {
            ++i;
            continue;
        }
        if (text[i] == '\n') {
            lines.push_back(currentLine);
            currentLine.clear();
            ++i;
            continue;
        }

        size_t charLen = utf8CharLength(text, i);
        std::string nextChar = text.substr(i, charLen);
        i += charLen;

        std::string candidate = currentLine + nextChar;
        if (!currentLine.empty() && textWidth(font, candidate) > maxWidth) {
            lines.push_back(currentLine);
            currentLine = nextChar;
        } else {
            currentLine = candidate;
        }
    }

    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }
    return lines;
}

} // namespace VideoPlay::TextRender
