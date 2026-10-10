#include "renderer/ui_draw.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace VideoPlay::UiDraw {

void fillRoundRect(SDL_Renderer* renderer, int x, int y, int w, int h, int radius,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!renderer || w <= 0 || h <= 0) return;
    if (radius <= 0) {
        SDL_SetRenderDrawColor(renderer, r, g, b, a);
        SDL_FRect rect = {static_cast<float>(x), static_cast<float>(y),
                          static_cast<float>(w), static_cast<float>(h)};
        SDL_RenderFillRect(renderer, &rect);
        return;
    }
    int rr = std::min(radius, std::min(w / 2, h / 2));

    // Reuse static buffers to avoid per-frame allocations
    static thread_local std::vector<SDL_Vertex> vertices;
    static thread_local std::vector<int> indices;
    vertices.clear();
    indices.clear();

    auto addPoint = [&](float px, float py) {
        SDL_Vertex v;
        v.position.x = px;
        v.position.y = py;
        v.color.r = r / 255.0f;
        v.color.g = g / 255.0f;
        v.color.b = b / 255.0f;
        v.color.a = a / 255.0f;
        v.tex_coord.x = 0;
        v.tex_coord.y = 0;
        vertices.push_back(v);
    };

    // center point
    addPoint(x + w / 2.0f, y + h / 2.0f);

    const float PI = 3.14159265f;
    const int segments = 32;
    // top-left arc
    for (int i = 0; i <= segments; ++i) {
        float angle = PI + PI / 2.0f * i / segments;
        addPoint(x + rr + rr * std::cos(angle), y + rr + rr * std::sin(angle));
    }
    // top-right arc
    for (int i = 0; i <= segments; ++i) {
        float angle = PI * 1.5f + PI / 2.0f * i / segments;
        addPoint(x + w - rr + rr * std::cos(angle), y + rr + rr * std::sin(angle));
    }
    // bottom-right arc
    for (int i = 0; i <= segments; ++i) {
        float angle = 0.0f + PI / 2.0f * i / segments;
        addPoint(x + w - rr + rr * std::cos(angle), y + h - rr + rr * std::sin(angle));
    }
    // bottom-left arc
    for (int i = 0; i <= segments; ++i) {
        float angle = PI / 2.0f + PI / 2.0f * i / segments;
        addPoint(x + rr + rr * std::cos(angle), y + h - rr + rr * std::sin(angle));
    }

    // Generate triangle list indices for a fan from center
    size_t boundaryCount = vertices.size() - 1;
    indices.reserve(boundaryCount * 3);
    for (size_t i = 0; i < boundaryCount; ++i) {
        indices.push_back(0);
        indices.push_back(static_cast<int>(1 + i));
        indices.push_back(static_cast<int>(1 + ((i + 1) % boundaryCount)));
    }

    SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()),
                       indices.data(), static_cast<int>(indices.size()));
}

} // namespace VideoPlay::UiDraw
