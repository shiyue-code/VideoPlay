#include "renderer/sdlrenderer.h"
#include "renderer/sdlrenderer_internal.h"
#include "utils/logger.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include "utils/stb_image.h"
#include "utils/stb_image_write.h"

#ifdef _WIN32
#define NOMINMAX
#endif
#if defined(_WIN32)
#include <windows.h>
#endif

namespace VideoPlay {


bool SDLRenderer::loadFont(const std::string& fontPath, int fontSize) {
#ifdef HAS_SDL_TTF
    closeFont();

    m_font = TTF_OpenFont(fontPath.c_str(), fontSize);
    if (!m_font) {
        Logger::instance().error("Failed to load font: " + std::string(SDL_GetError()));
        return false;
    }

    m_fontSmall = TTF_OpenFont(fontPath.c_str(), fontSize - 2);
    m_fontLarge = TTF_OpenFont(fontPath.c_str(), fontSize + 4);
    m_fontPath = fontPath;

    return true;
#else
    return false;
#endif
}

void SDLRenderer::closeFont() {
#ifdef HAS_SDL_TTF
    if (m_font) {
        TTF_CloseFont(m_font);
        m_font = nullptr;
    }
    if (m_fontSmall) {
        TTF_CloseFont(m_fontSmall);
        m_fontSmall = nullptr;
    }
    if (m_fontLarge) {
        TTF_CloseFont(m_fontLarge);
        m_fontLarge = nullptr;
    }
#endif
}

void SDLRenderer::drawText(const std::string& text, int x, int y, uint8_t r, uint8_t g, uint8_t b, int fontSize, uint8_t alpha) {
#ifdef HAS_SDL_TTF
    if (!m_font || !m_renderer || text.empty()) return;

    std::string cacheKey = text + "|" + std::to_string(fontSize) + "|" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "," + std::to_string(alpha);
    auto it = m_textCache.find(cacheKey);
    if (it != m_textCache.end()) {
        SDL_FRect dstRect = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(it->second.width), static_cast<float>(it->second.height) };
        SDL_RenderTexture(m_renderer, it->second.texture, nullptr, &dstRect);
        return;
    }

    TTF_Font* font = m_font;
    if (fontSize > 0) {
        if (fontSize <= 10 && m_fontSmall) font = m_fontSmall;
        else if (fontSize >= 18 && m_fontLarge) font = m_fontLarge;
    }

    SDL_Color color = { r, g, b, alpha };
    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), 0, color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    if (texture) {
        SDL_FRect dstRect = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(surface->w), static_cast<float>(surface->h) };
        SDL_RenderTexture(m_renderer, texture, nullptr, &dstRect);

        TextCacheEntry entry;
        entry.texture = texture;
        entry.width = surface->w;
        entry.height = surface->h;
        m_textCache[cacheKey] = entry;
    }

    SDL_DestroySurface(surface);
#endif
}

void SDLRenderer::clearTextCache() {
    for (auto& pair : m_textCache) {
        if (pair.second.texture) {
            SDL_DestroyTexture(pair.second.texture);
        }
    }
    m_textCache.clear();
}

int SDLRenderer::getTextWidth(const std::string& text, int fontSize) {
#ifdef HAS_SDL_TTF
    if (!m_font) return text.length() * 8;
    
    TTF_Font* font = m_font;
    if (fontSize > 0) {
        if (fontSize <= 10 && m_fontSmall) font = m_fontSmall;
        else if (fontSize >= 18 && m_fontLarge) font = m_fontLarge;
    }
    
    int w, h;
    if (TTF_GetStringSize(font, text.c_str(), 0, &w, &h)) {
        return w;
    }
#endif
    return text.length() * 8;
}

int SDLRenderer::getFontHeight(int fontSize) {
#ifdef HAS_SDL_TTF
    if (!m_font) return 14;
    
    TTF_Font* font = m_font;
    if (fontSize > 0) {
        if (fontSize <= 10 && m_fontSmall) font = m_fontSmall;
        else if (fontSize >= 18 && m_fontLarge) font = m_fontLarge;
    }
    
    return TTF_GetFontHeight(font);
#else
    return 14;
#endif
}

void SDLRenderer::drawRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!m_renderer) return;
    SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
    SDL_FRect rect = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h) };
    SDL_RenderRect(m_renderer, &rect);
}

void SDLRenderer::fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!m_renderer) return;
    SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
    SDL_FRect rect = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h) };
    SDL_RenderFillRect(m_renderer, &rect);
}

void SDLRenderer::fillRoundRect(int x, int y, int w, int h, int radius, uint8_t red, uint8_t green, uint8_t blue, uint8_t a) {
    if (!m_renderer || w <= 0 || h <= 0) return;
    if (radius <= 0) {
        fillRect(x, y, w, h, red, green, blue, a);
        return;
    }
    int r = std::min(radius, std::min(w / 2, h / 2));

    std::vector<SDL_Vertex> vertices;
    auto addPoint = [&](float px, float py) {
        SDL_Vertex v;
        v.position.x = px;
        v.position.y = py;
        v.color.r = red / 255.0f;
        v.color.g = green / 255.0f;
        v.color.b = blue / 255.0f;
        v.color.a = a / 255.0f;
        v.tex_coord.x = 0;
        v.tex_coord.y = 0;
        vertices.push_back(v);
    };

    // center point
    addPoint(x + w / 2.0f, y + h / 2.0f);

    const float PI = 3.14159265f;
    const int segments = 64;
    // top-left arc
    for (int i = 0; i <= segments; ++i) {
        float angle = PI + PI / 2.0f * i / segments;
        addPoint(x + r + r * std::cos(angle), y + r + r * std::sin(angle));
    }
    // top-right arc
    for (int i = 0; i <= segments; ++i) {
        float angle = PI * 1.5f + PI / 2.0f * i / segments;
        addPoint(x + w - r + r * std::cos(angle), y + r + r * std::sin(angle));
    }
    // bottom-right arc
    for (int i = 0; i <= segments; ++i) {
        float angle = 0.0f + PI / 2.0f * i / segments;
        addPoint(x + w - r + r * std::cos(angle), y + h - r + r * std::sin(angle));
    }
    // bottom-left arc
    for (int i = 0; i <= segments; ++i) {
        float angle = PI / 2.0f + PI / 2.0f * i / segments;
        addPoint(x + r + r * std::cos(angle), y + h - r + r * std::sin(angle));
    }

    // Generate triangle list indices for a fan from center
    std::vector<int> indices;
    size_t boundaryCount = vertices.size() - 1;
    for (size_t i = 0; i < boundaryCount; ++i) {
        indices.push_back(0);
        indices.push_back(static_cast<int>(1 + i));
        indices.push_back(static_cast<int>(1 + ((i + 1) % boundaryCount)));
    }

    SDL_RenderGeometry(m_renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
}

void SDLRenderer::fillCircle(int cx, int cy, int radius, uint8_t red, uint8_t green, uint8_t blue, uint8_t a) {
    if (!m_renderer || radius <= 0) return;
    std::vector<SDL_Vertex> vertices;
    SDL_Vertex center;
    center.position.x = static_cast<float>(cx);
    center.position.y = static_cast<float>(cy);
    center.color.r = red / 255.0f; center.color.g = green / 255.0f; center.color.b = blue / 255.0f; center.color.a = a / 255.0f;
    center.tex_coord.x = 0; center.tex_coord.y = 0;
    vertices.push_back(center);

    const int segments = 64;
    const float PI = 3.14159265f;
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * PI * i / segments;
        SDL_Vertex v;
        v.position.x = cx + radius * std::cos(angle);
        v.position.y = cy + radius * std::sin(angle);
        v.color = center.color;
        v.tex_coord.x = 0; v.tex_coord.y = 0;
        vertices.push_back(v);
    }

    std::vector<int> indices;
    for (int i = 0; i < segments; ++i) {
        indices.push_back(0);
        indices.push_back(1 + i);
        indices.push_back(1 + i + 1);
    }

    SDL_RenderGeometry(m_renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
}

void SDLRenderer::renderGlowBar(int cx, int cy, int width, int height, uint8_t red, uint8_t green, uint8_t blue, uint8_t centerAlpha, int clipLeft, int clipRight) {
    if (!m_renderer || width <= 0 || height <= 0 || centerAlpha == 0) return;

    int halfW = width / 2;
    int halfH = height / 2;
    int left   = std::max(clipLeft, cx - halfW);
    int right  = std::min(clipRight, cx + halfW);
    int top    = cy - halfH;
    int bottom = cy + halfH;
    if (right <= left) return;

    // 实际中心可能因裁剪偏移，重新计算插值用的中�?alpha
    int actualCenterX = cx;
    if (actualCenterX < left) actualCenterX = left;
    if (actualCenterX > right) actualCenterX = right;

    float rf = red / 255.0f;
    float gf = green / 255.0f;
    float bf = blue / 255.0f;
    float af = centerAlpha / 255.0f;

    SDL_Vertex v[6];
    // top-left
    v[0].position = { static_cast<float>(left),  static_cast<float>(top) };
    v[0].color    = { rf, gf, bf, 0.0f };
    v[0].tex_coord = { 0, 0 };
    // top-center
    v[1].position = { static_cast<float>(actualCenterX), static_cast<float>(top) };
    v[1].color    = { rf, gf, bf, af };
    v[1].tex_coord = { 0, 0 };
    // top-right
    v[2].position = { static_cast<float>(right), static_cast<float>(top) };
    v[2].color    = { rf, gf, bf, 0.0f };
    v[2].tex_coord = { 0, 0 };
    // bottom-left
    v[3].position = { static_cast<float>(left),  static_cast<float>(bottom) };
    v[3].color    = { rf, gf, bf, 0.0f };
    v[3].tex_coord = { 0, 0 };
    // bottom-center
    v[4].position = { static_cast<float>(actualCenterX), static_cast<float>(bottom) };
    v[4].color    = { rf, gf, bf, af };
    v[4].tex_coord = { 0, 0 };
    // bottom-right
    v[5].position = { static_cast<float>(right), static_cast<float>(bottom) };
    v[5].color    = { rf, gf, bf, 0.0f };
    v[5].tex_coord = { 0, 0 };

    int indices[] = { 0, 1, 3, 1, 3, 4, 1, 2, 4, 2, 4, 5 };
    SDL_RenderGeometry(m_renderer, nullptr, v, 6, indices, 12);
}

void SDLRenderer::renderSmoothRoundRect(int x, int y, int w, int h, int radius, uint8_t red, uint8_t green, uint8_t blue, uint8_t a) {
    if (!m_renderer || w <= 0 || h <= 0) return;
    const int scale = 8;
    int texW = w * scale;
    int texH = h * scale;
    int texR = radius * scale;
    if (texW < 1) texW = 1;
    if (texH < 1) texH = 1;

    SDL_Texture* target = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, texW, texH);
    if (!target) return;
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(target, SDL_SCALEMODE_LINEAR);

    SDL_Texture* oldTarget = SDL_GetRenderTarget(m_renderer);
    SDL_SetRenderTarget(m_renderer, target);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
    SDL_RenderClear(m_renderer);

    // 软边过渡层：扩大 8 高分辨率像素（≈1 屏幕像素），alpha 40%
    uint8_t edgeA = static_cast<uint8_t>(std::min(255, a * 40 / 100));
    if (edgeA > 0) {
        fillRoundRect(-8, -8, texW + 16, texH + 16, texR, red, green, blue, edgeA);
    }
    //  核心实心�?
    fillRoundRect(0, 0, texW, texH, texR, red, green, blue, a);

    SDL_SetRenderTarget(m_renderer, oldTarget);

    SDL_FRect dst = { static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h) };
    SDL_RenderTexture(m_renderer, target, nullptr, &dst);
    SDL_DestroyTexture(target);
}

void SDLRenderer::renderSmoothCircle(int cx, int cy, int radius, uint8_t red, uint8_t green, uint8_t blue, uint8_t a) {
    if (!m_renderer || radius <= 0) return;
    const int scale = 8;
    int size = radius * 2 * scale;
    if (size < 2) size = 2;

    SDL_Texture* target = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, size, size);
    if (!target) return;
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(target, SDL_SCALEMODE_LINEAR);

    SDL_Texture* oldTarget = SDL_GetRenderTarget(m_renderer);
    SDL_SetRenderTarget(m_renderer, target);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
    SDL_RenderClear(m_renderer);

    int c = size / 2;
    int r = radius * scale;
    uint8_t edgeA = static_cast<uint8_t>(std::min(255, a * 40 / 100));
    if (edgeA > 0) {
        fillCircle(c, c, r + 8, red, green, blue, edgeA);
    }
    fillCircle(c, c, r, red, green, blue, a);

    SDL_SetRenderTarget(m_renderer, oldTarget);

    SDL_FRect dst = { static_cast<float>(cx - radius), static_cast<float>(cy - radius), static_cast<float>(radius * 2), static_cast<float>(radius * 2) };
    SDL_RenderTexture(m_renderer, target, nullptr, &dst);
    SDL_DestroyTexture(target);
}

void SDLRenderer::drawGradientVignette() {
    if (!m_renderer) return;
    int h = 180;
    int y0 = m_windowHeight - h;
    if (y0 < 0) y0 = 0;

    SDL_Vertex verts[4];
    auto setV = [&](int idx, float px, float py, uint8_t alpha) {
        verts[idx].position.x = px;
        verts[idx].position.y = py;
        verts[idx].color.r = 0.0f;
        verts[idx].color.g = 0.0f;
        verts[idx].color.b = 0.0f;
        verts[idx].color.a = alpha / 255.0f;
        verts[idx].tex_coord.x = 0;
        verts[idx].tex_coord.y = 0;
    };
    setV(0, 0.0f, static_cast<float>(y0), 0);
    setV(1, static_cast<float>(m_windowWidth), static_cast<float>(y0), 0);
    setV(2, static_cast<float>(m_windowWidth), static_cast<float>(m_windowHeight), 200);
    setV(3, 0.0f, static_cast<float>(m_windowHeight), 200);

    int indices[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(m_renderer, nullptr, verts, 4, indices, 6);
}

void SDLRenderer::drawButton(int x, int y, int w, int h, const std::string& iconType, bool hovered, bool pressed) {
    int cx = x + w / 2;
    int cy = y + h / 2;
    //  圆形 hover / pressed 背景（现代播放器风格�?
    if (pressed) {
        fillCircle(cx, cy, w / 2 - 2, COLOR_BUTTON_BG_PRESSED[0], COLOR_BUTTON_BG_PRESSED[1], COLOR_BUTTON_BG_PRESSED[2], COLOR_BUTTON_BG_PRESSED[3]);
    } else if (hovered) {
        fillCircle(cx, cy, w / 2 - 2, COLOR_BUTTON_BG_HOVER[0], COLOR_BUTTON_BG_HOVER[1], COLOR_BUTTON_BG_HOVER[2], COLOR_BUTTON_BG_HOVER[3]);
    }
    
    //  绘制图标（pressed 时缩�?8%，增加轻微按压感�?
    float scale = pressed ? 0.92f : 1.0f;
    drawIcon(cx, cy, iconType, hovered, scale);
}

SDL_Texture* SDLRenderer::createIconTexture(const std::string& type) {
    if (!m_renderer) return nullptr;
    const int size = 256;
    SDL_Texture* target = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, size, size);
    if (!target) return nullptr;
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(target, SDL_SCALEMODE_LINEAR);
    SDL_SetRenderTarget(m_renderer, target);
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
    SDL_RenderClear(m_renderer);

    auto mkVert = [&](float px, float py) {
        SDL_Vertex v;
        v.position.x = px;
        v.position.y = py;
        v.color.r = 1.0f; v.color.g = 1.0f; v.color.b = 1.0f; v.color.a = 1.0f;
        v.tex_coord.x = 0; v.tex_coord.y = 0;
        return v;
    };
    auto drawTri = [&](const std::vector<SDL_Vertex>& verts) {
        std::vector<int> idx = {0, 1, 2};
        SDL_RenderGeometry(m_renderer, nullptr, verts.data(), static_cast<int>(verts.size()), idx.data(), static_cast<int>(idx.size()));
    };
    auto drawQuad = [&](const std::vector<SDL_Vertex>& verts) {
        std::vector<int> idx = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(m_renderer, nullptr, verts.data(), static_cast<int>(verts.size()), idx.data(), static_cast<int>(idx.size()));
    };

    int cx = size / 2;
    int cy = size / 2;

    // 辅助：先画稍大、alpha 50% 的过渡层，再画实心主体，产生边缘羽化
    auto drawSoftTri = [&](const std::vector<SDL_Vertex>& base, float expand) {
        if (expand > 0) {
            std::vector<SDL_Vertex> soft;
            for (const auto& v : base) {
                SDL_Vertex sv = v;
                sv.color.a = 0.5f;
                soft.push_back(sv);
            }
            drawTri(soft);
        }
        drawTri(base);
    };
    auto drawSoftRoundRect = [&](int rx, int ry, int rw, int rh, int rr) {
        fillRoundRect(rx - 8, ry - 8, rw + 16, rh + 16, rr, 255, 255, 255, 100);
        fillRoundRect(rx, ry, rw, rh, rr, 255, 255, 255, 255);
    };

    if (type == "play") {
        // 向右三角，以纹理中心对称
        drawSoftTri({ mkVert(cx + 56, cy), mkVert(cx - 56, cy - 72), mkVert(cx - 56, cy + 72) }, 8);
    } else if (type == "pause") {
        drawSoftRoundRect(cx - 56, cy - 72, 40, 144, 16);
        drawSoftRoundRect(cx + 16, cy - 72, 40, 144, 16);
    } else if (type == "stop") {
        drawSoftRoundRect(cx - 64, cy - 64, 128, 128, 24);
    } else if (type == "prev") {
        //  上一首：竖条在右 + 向左三角，整体居�?
        drawSoftRoundRect(cx + 28, cy - 48, 20, 96, 8);
        drawSoftTri({ mkVert(cx - 48, cy), mkVert(cx + 28, cy - 48), mkVert(cx + 28, cy + 48) }, 6);
    } else if (type == "next") {
        //  下一首：竖条在左 + 向右三角，整体居�?
        drawSoftRoundRect(cx - 48, cy - 48, 20, 96, 8);
        drawSoftTri({ mkVert(cx + 48, cy), mkVert(cx - 28, cy - 48), mkVert(cx - 28, cy + 48) }, 6);
    } else if (type == "volume") {
        //  喇叭：左侧窄手柄 + 右侧梯形喇叭�?
        drawSoftRoundRect(cx - 50, cy - 16, 28, 32, 6);
        auto v0 = mkVert(static_cast<float>(cx - 22), static_cast<float>(cy - 20));
        auto v1 = mkVert(static_cast<float>(cx - 22), static_cast<float>(cy + 20));
        auto v2 = mkVert(static_cast<float>(cx + 60), static_cast<float>(cy + 56));
        auto v3 = mkVert(static_cast<float>(cx + 60), static_cast<float>(cy - 56));
        drawSoftTri({ v0, v1, v2 }, 6);
        drawSoftTri({ v0, v2, v3 }, 6);
    } else if (type == "mute") {
        // 喇叭 + 静音斜杠
        drawSoftRoundRect(cx - 50, cy - 16, 28, 32, 6);
        auto v0 = mkVert(static_cast<float>(cx - 22), static_cast<float>(cy - 20));
        auto v1 = mkVert(static_cast<float>(cx - 22), static_cast<float>(cy + 20));
        auto v2 = mkVert(static_cast<float>(cx + 60), static_cast<float>(cy + 56));
        auto v3 = mkVert(static_cast<float>(cx + 60), static_cast<float>(cy - 56));
        drawSoftTri({ v0, v1, v2 }, 6);
        drawSoftTri({ v0, v2, v3 }, 6);
        //  静音斜杠在喇叭开口右�?
        drawSoftRoundRect(cx + 68, cy - 52, 12, 104, 4);
    } else if (type == "playlist") {
        //  汉堡菜单图标：三条横�?
        int lineW = 128;
        int lineH = 20;
        int gap = 28;
        drawSoftRoundRect(cx - lineW/2, cy - gap - lineH/2, lineW, lineH, lineH/2);
        drawSoftRoundRect(cx - lineW/2, cy - lineH/2, lineW, lineH, lineH/2);
        drawSoftRoundRect(cx - lineW/2, cy + gap - lineH/2, lineW, lineH, lineH/2);
    } else {
        fillCircle(cx, cy, 72, 255, 255, 255, 100);
        fillCircle(cx, cy, 64, 255, 255, 255, 255);
    }

    SDL_SetRenderTarget(m_renderer, nullptr);
    return target;
}

SDL_Texture* SDLRenderer::getIconTexture(const std::string& type) {
    auto it = m_iconTextures.find(type);
    if (it != m_iconTextures.end()) {
        return it->second;
    }

    if (!m_renderer) {
        Logger::instance().debug("getIconTexture: renderer not ready for " + type);
        return nullptr;
    }

    SDL_Texture* texture = createIconTexture(type);
    if (texture) {
        m_iconTextures[type] = texture;
        Logger::instance().info("Generated icon texture for: " + type);
    }
    return texture;
}

void SDLRenderer::loadIconTextures() {
    const std::vector<std::string> iconNames = {
        "play", "pause", "stop", "prev", "next", "volume", "mute", "playlist"
    };
    for (const auto& name : iconNames) {
        getIconTexture(name);
    }
}

void SDLRenderer::clearIconTextures() {
    for (auto& pair : m_iconTextures) {
        if (pair.second) {
            SDL_DestroyTexture(pair.second);
        }
    }
    m_iconTextures.clear();
}

void SDLRenderer::drawIcon(int cx, int cy, const std::string& type, bool hovered, float scale) {
    const uint8_t* color = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;

    SDL_Texture* texture = getIconTexture(type);
    if (texture) {
        int drawSize = static_cast<int>(32 * scale);
        SDL_FRect dstRect = { static_cast<float>(cx - drawSize / 2), static_cast<float>(cy - drawSize / 2), static_cast<float>(drawSize), static_cast<float>(drawSize) };
        // 颜色调制：正常灰白，hover 纯白
        SDL_SetTextureColorMod(texture, color[0], color[1], color[2]);
        SDL_RenderTexture(m_renderer, texture, nullptr, &dstRect);
        SDL_SetTextureColorMod(texture, 255, 255, 255); // 重置，避免影响后续渲染
        return;
    }

    // Fallback: primitive drawing when PNG icon unavailable
    int size = 24;
    int r = size / 3;
    SDL_SetRenderDrawColor(m_renderer, color[0], color[1], color[2], color[3]);

    if (type == "play") {
        for (int row = -r; row <= r; row++) {
            int y = cy + row;
            float progress = (row + r) / (float)(2 * r);
            int lineWidth = static_cast<int>(progress * r * 1.5);
            int xStart = cx - r/2;
            for (int i = 0; i < lineWidth; i++) {
                SDL_RenderPoint(m_renderer, static_cast<float>(xStart + i), static_cast<float>(y));
            }
        }
    } else if (type == "pause") {
        int barWidth = r / 2;
        int gap = r / 2;
        fillRect(cx - gap - barWidth, cy - r, barWidth, r * 2, color[0], color[1], color[2], color[3]);
        fillRect(cx + gap, cy - r, barWidth, r * 2, color[0], color[1], color[2], color[3]);
    } else if (type == "stop") {
        int s = r * 3 / 2;
        fillRect(cx - s/2, cy - s/2, s, s, color[0], color[1], color[2], color[3]);
    } else if (type == "prev") {
        fillRect(cx - r, cy - r, r/3, r * 2, color[0], color[1], color[2], color[3]);
        for (int row = -r; row <= r; row++) {
            int y = cy + row;
            float progress = 1.0f - (row + r) / (float)(2 * r);
            int lineWidth = static_cast<int>(progress * r);
            int xStart = cx - r/3;
            for (int i = 0; i < lineWidth; i++) {
                SDL_RenderPoint(m_renderer, static_cast<float>(xStart + i), static_cast<float>(y));
            }
        }
    } else if (type == "next") {
        fillRect(cx + r - r/3, cy - r, r/3, r * 2, color[0], color[1], color[2], color[3]);
        for (int row = -r; row <= r; row++) {
            int y = cy + row;
            float progress = (row + r) / (float)(2 * r);
            int lineWidth = static_cast<int>(progress * r);
            int xStart = cx - r/3 - lineWidth;
            for (int i = 0; i < lineWidth; i++) {
                SDL_RenderPoint(m_renderer, static_cast<float>(xStart + i), static_cast<float>(y));
            }
        }
    } else if (type == "volume") {
        int hw = r / 2;
        int hh = r;
        int mouthW = r;
        int mouthH = r * 2;
        // 手柄
        fillRect(cx - r, cy - hh / 2, hw, hh, color[0], color[1], color[2], color[3]);
        //  喇叭口梯�?
        SDL_Vertex verts[4];
        verts[0] = { {static_cast<float>(cx - r + hw), static_cast<float>(cy - hh / 2)}, {1, 1, 1, 1}, {0, 0} };
        verts[1] = { {static_cast<float>(cx - r + hw), static_cast<float>(cy + hh / 2)}, {1, 1, 1, 1}, {0, 0} };
        verts[2] = { {static_cast<float>(cx + mouthW), static_cast<float>(cy + mouthH / 2)}, {1, 1, 1, 1}, {0, 0} };
        verts[3] = { {static_cast<float>(cx + mouthW), static_cast<float>(cy - mouthH / 2)}, {1, 1, 1, 1}, {0, 0} };
        int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(m_renderer, nullptr, verts, 4, idx, 6);
    } else if (type == "mute") {
        int hw = r / 2;
        int hh = r;
        int mouthW = r;
        int mouthH = r * 2;
        fillRect(cx - r, cy - hh / 2, hw, hh, 150, 150, 150, 255);
        SDL_Vertex verts[4];
        verts[0] = { {static_cast<float>(cx - r + hw), static_cast<float>(cy - hh / 2)}, {1, 1, 1, 1}, {0, 0} };
        verts[1] = { {static_cast<float>(cx - r + hw), static_cast<float>(cy + hh / 2)}, {1, 1, 1, 1}, {0, 0} };
        verts[2] = { {static_cast<float>(cx + mouthW), static_cast<float>(cy + mouthH / 2)}, {1, 1, 1, 1}, {0, 0} };
        verts[3] = { {static_cast<float>(cx + mouthW), static_cast<float>(cy - mouthH / 2)}, {1, 1, 1, 1}, {0, 0} };
        int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(m_renderer, nullptr, verts, 4, idx, 6);
        // 静音斜杠
        SDL_SetRenderDrawColor(m_renderer, 255, 100, 100, 255);
        int sx = cx + mouthW + 2;
        int sy1 = cy - mouthH / 2 - 2;
        int sy2 = cy + mouthH / 2 + 2;
        SDL_RenderLine(m_renderer, static_cast<float>(sx), static_cast<float>(sy1), static_cast<float>(sx + r / 2), static_cast<float>(sy2));
        SDL_RenderLine(m_renderer, static_cast<float>(sx + 1), static_cast<float>(sy1), static_cast<float>(sx + 1 + r / 2), static_cast<float>(sy2));
    }
}

} // namespace VideoPlay
