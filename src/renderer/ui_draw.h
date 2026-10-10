#pragma once

#include <cstdint>
#include <string>

struct SDL_Renderer;
struct SDL_FRect;
typedef struct TTF_Font TTF_Font;

// 不依赖 SDLRenderer 的通用绘制原语（供独立窗口弹窗复用）
namespace VideoPlay::UiDraw {

// 实心圆角矩形（SDL_RenderGeometry 扇形三角）
void fillRoundRect(SDL_Renderer* renderer, int x, int y, int w, int h, int radius,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a);
// SDL_FRect 便捷重载（坐标截断为 int）
void fillRoundRect(SDL_Renderer* renderer, const SDL_FRect& rect, float radius,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a);

// 与信息面板一致的「×」：悬停/按下半透明底色 + 交叉线
void drawCloseButton(SDL_Renderer* renderer, int x, int y, int size,
                     bool hovered, bool pressed, int radius, int inset,
                     int hoverAlpha, uint8_t lineShade, uint8_t lineAlpha);

// 主/次按钮：圆角底 + 居中白字
void drawButton(SDL_Renderer* renderer, TTF_Font* font, const SDL_FRect& rect,
                const std::string& text, bool hovered, bool pressed, bool primary);

} // namespace VideoPlay::UiDraw
