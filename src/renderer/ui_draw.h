#pragma once

#include <cstdint>

struct SDL_Renderer;

// 不依赖 SDLRenderer 的通用绘制原语（供独立窗口弹窗复用）
namespace VideoPlay::UiDraw {

// 实心圆角矩形（SDL_RenderGeometry 扇形三角）
void fillRoundRect(SDL_Renderer* renderer, int x, int y, int w, int h, int radius,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a);

} // namespace VideoPlay::UiDraw
