#include "renderer/inputfield.h"
#include "utils/logger.h"
#include <algorithm>
#include <cmath>

namespace VideoPlay {

namespace {
void fillRoundRect(SDL_Renderer* renderer, const SDL_FRect& rect, float radius,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (!renderer) return;

    const int x = static_cast<int>(rect.x);
    const int y = static_cast<int>(rect.y);
    const int w = static_cast<int>(rect.w);
    const int h = static_cast<int>(rect.h);
    const int rr = std::max(0, std::min(static_cast<int>(radius), std::min(w, h) / 2));

    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_FRect center = {static_cast<float>(x + rr), static_cast<float>(y),
                        static_cast<float>(w - 2 * rr), static_cast<float>(h)};
    SDL_RenderFillRect(renderer, &center);
    SDL_FRect middle = {static_cast<float>(x), static_cast<float>(y + rr),
                        static_cast<float>(w), static_cast<float>(h - 2 * rr)};
    SDL_RenderFillRect(renderer, &middle);

    auto drawCornerPixel = [&](int px, int py, float coverage) {
        if (coverage <= 0.0f) return;
        uint8_t alpha = static_cast<uint8_t>(std::clamp(coverage, 0.0f, 1.0f) * a);
        SDL_SetRenderDrawColor(renderer, r, g, b, alpha);
        SDL_RenderPoint(renderer, static_cast<float>(px), static_cast<float>(py));
    };

    for (int dy = 0; dy < rr; ++dy) {
        for (int dx = 0; dx < rr; ++dx) {
            float cx = static_cast<float>(rr - dx) - 0.5f;
            float cy = static_cast<float>(rr - dy) - 0.5f;
            float distance = std::sqrt(cx * cx + cy * cy);
            float coverage = static_cast<float>(rr) + 0.5f - distance;

            drawCornerPixel(x + dx, y + dy, coverage);
            drawCornerPixel(x + w - 1 - dx, y + dy, coverage);
            drawCornerPixel(x + dx, y + h - 1 - dy, coverage);
            drawCornerPixel(x + w - 1 - dx, y + h - 1 - dy, coverage);
        }
    }
}
}

// 颜色常量
static constexpr uint8_t COLOR_INPUT_BG[4] = {50, 50, 50, 255};
static constexpr uint8_t COLOR_INPUT_BORDER[4] = {80, 80, 80, 255};
static constexpr uint8_t COLOR_INPUT_ACTIVE[4] = {100, 150, 255, 255};
static constexpr uint8_t COLOR_TEXT[4] = {220, 220, 220, 255};
static constexpr uint8_t COLOR_PLACEHOLDER[4] = {100, 100, 100, 150};
static constexpr uint8_t COLOR_SELECTION[4] = {50, 100, 200, 128};

InputField::InputField(TTF_Font* font)
    : m_font(font) {
}

InputField::~InputField() = default;

void InputField::setRect(const SDL_FRect& rect) {
    m_rect = rect;
}

void InputField::setValue(const std::string& value) {
    m_value = value;
    m_cursorPos = std::min(m_cursorPos, static_cast<int>(m_value.length()));
    m_selectionStart = -1;
    m_selectionEnd = -1;
}

void InputField::setPassword(bool isPassword) {
    m_isPassword = isPassword;
}

void InputField::setPlaceholder(const std::string& placeholder) {
    m_placeholder = placeholder;
}

std::string InputField::getDisplayValue() const {
    return m_isPassword ? maskPassword(m_value) : m_value;
}

void InputField::setActive(bool active) {
    m_active = active;
    if (active) {
        m_cursorVisible = true;
        m_cursorBlinkTime = SDL_GetTicks();
    } else {
        m_selectionStart = -1;
        m_selectionEnd = -1;
    }
}

int InputField::getCursorPosFromMouseX(int mouseX) const {
    std::string display = getDisplayValue();
    if (display.empty()) return 0;

    int textStartX = static_cast<int>(m_rect.x) + 5;
    int relativeX = mouseX - textStartX + m_textOffsetX;

    if (relativeX <= 0) return 0;

    int currentWidth = 0;
    for (size_t i = 0; i < display.length(); i++) {
        std::string charStr = display.substr(i, 1);
        int charWidth = getTextWidth(charStr);
        if (currentWidth + charWidth / 2 > relativeX) {
            return static_cast<int>(i);
        }
        currentWidth += charWidth;
    }

    return static_cast<int>(display.length());
}

void InputField::updateTextOffset(int contentWidth) {
    const int padding = 5;
    const int visibleWidth = std::max(0, static_cast<int>(m_rect.w) - padding * 2);
    const int maxOffset = std::max(0, contentWidth - visibleWidth);

    if (!m_active) {
        m_textOffsetX = 0;
        return;
    }

    std::string display = getDisplayValue();
    int cursorX = getTextWidth(display.substr(0, static_cast<size_t>(m_cursorPos)));

    if (cursorX - m_textOffsetX > visibleWidth) {
        m_textOffsetX = cursorX - visibleWidth;
    } else if (cursorX - m_textOffsetX < 0) {
        m_textOffsetX = cursorX;
    }

    m_textOffsetX = std::clamp(m_textOffsetX, 0, maxOffset);
}

void InputField::deleteSelection() {
    if (m_selectionStart < 0 || m_selectionEnd < 0) return;

    int start = std::min(m_selectionStart, m_selectionEnd);
    int end = std::max(m_selectionStart, m_selectionEnd);

    if (start >= 0 && end <= static_cast<int>(m_value.length()) && start < end) {
        m_value.erase(start, end - start);
        m_cursorPos = start;
    }

    m_selectionStart = -1;
    m_selectionEnd = -1;
}

void InputField::selectAll() {
    if (m_value.empty()) return;
    m_selectionStart = 0;
    m_selectionEnd = static_cast<int>(m_value.length());
    m_cursorPos = m_selectionEnd;
}

void InputField::moveCursorHome() {
    m_cursorPos = 0;
    m_selectionStart = -1;
    m_selectionEnd = -1;
}

void InputField::moveCursorEnd() {
    m_cursorPos = static_cast<int>(m_value.length());
    m_selectionStart = -1;
    m_selectionEnd = -1;
}

void InputField::moveCursorLeft() {
    if (m_cursorPos > 0) {
        m_cursorPos--;
    }
    m_selectionStart = -1;
    m_selectionEnd = -1;
}

void InputField::moveCursorRight() {
    if (m_cursorPos < static_cast<int>(m_value.length())) {
        m_cursorPos++;
    }
    m_selectionStart = -1;
    m_selectionEnd = -1;
}

void InputField::deleteCharBefore() {
    if (m_selectionStart >= 0) {
        deleteSelection();
        return;
    }

    if (m_cursorPos > 0) {
        m_value.erase(m_cursorPos - 1, 1);
        m_cursorPos--;
    }
}

void InputField::deleteCharAfter() {
    if (m_selectionStart >= 0) {
        deleteSelection();
        return;
    }

    if (m_cursorPos < static_cast<int>(m_value.length())) {
        m_value.erase(m_cursorPos, 1);
    }
}

bool InputField::handleEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event.button.button != SDL_BUTTON_LEFT) return false;

            int mx = static_cast<int>(event.button.x);
            int my = static_cast<int>(event.button.y);

            bool inRect = (mx >= m_rect.x && mx <= m_rect.x + m_rect.w &&
                          my >= m_rect.y && my <= m_rect.y + m_rect.h);

            if (!inRect) {
                if (m_active) {
                    setActive(false);
                    return true;
                }
                return false;
            }

            if (!m_active) {
                setActive(true);
            }

            m_cursorPos = getCursorPosFromMouseX(mx);
            m_selectionStart = m_cursorPos;
            m_selectionEnd = m_cursorPos;
            m_selecting = true;
            m_cursorVisible = true;
            m_cursorBlinkTime = SDL_GetTicks();
            return true;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_selecting = false;
            }
            break;

        case SDL_EVENT_MOUSE_MOTION:
            if (m_selecting && m_active) {
                int mx = static_cast<int>(event.motion.x);
                m_cursorPos = getCursorPosFromMouseX(mx);
                m_selectionEnd = m_cursorPos;
                m_cursorVisible = true;
                m_cursorBlinkTime = SDL_GetTicks();
            }
            break;

        case SDL_EVENT_KEY_DOWN: {
            if (!m_active) return false;

            bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;

            m_cursorVisible = true;
            m_cursorBlinkTime = SDL_GetTicks();

            if (event.key.key == SDLK_BACKSPACE) {
                deleteCharBefore();
                return true;
            } else if (event.key.key == SDLK_DELETE) {
                deleteCharAfter();
                return true;
            } else if (event.key.key == SDLK_HOME) {
                moveCursorHome();
                return true;
            } else if (event.key.key == SDLK_END) {
                moveCursorEnd();
                return true;
            } else if (event.key.key == SDLK_LEFT) {
                moveCursorLeft();
                return true;
            } else if (event.key.key == SDLK_RIGHT) {
                moveCursorRight();
                return true;
            } else if (ctrl && event.key.key == SDLK_A) {
                selectAll();
                return true;
            } else if (ctrl && event.key.key == SDLK_C) {
                if (m_selectionStart >= 0) {
                    int start = std::min(m_selectionStart, m_selectionEnd);
                    int end = std::max(m_selectionStart, m_selectionEnd);
                    std::string selected = m_value.substr(start, end - start);
                    SDL_SetClipboardText(selected.c_str());
                }
                return true;
            } else if (ctrl && event.key.key == SDLK_X) {
                if (m_selectionStart >= 0) {
                    int start = std::min(m_selectionStart, m_selectionEnd);
                    int end = std::max(m_selectionStart, m_selectionEnd);
                    std::string selected = m_value.substr(start, end - start);
                    SDL_SetClipboardText(selected.c_str());
                    deleteSelection();
                }
                return true;
            } else if (ctrl && event.key.key == SDLK_V) {
                deleteSelection();
                char* clipboard = SDL_GetClipboardText();
                if (clipboard) {
                    std::string text(clipboard);
                    SDL_free(clipboard);
                    m_value.insert(m_cursorPos, text);
                    m_cursorPos += static_cast<int>(text.length());
                }
                return true;
            }
            break;
        }

        case SDL_EVENT_TEXT_INPUT: {
            if (!m_active) return false;

            deleteSelection();
            std::string text = event.text.text;
            m_value.insert(m_cursorPos, text);
            m_cursorPos += static_cast<int>(text.length());
            m_cursorVisible = true;
            m_cursorBlinkTime = SDL_GetTicks();
            return true;
        }
    }

    return false;
}

void InputField::render(SDL_Renderer* renderer, float mouseX, float mouseY) {
    if (!renderer) return;

    // 背景
    fillRoundRect(renderer, m_rect, 7,
                  COLOR_INPUT_BG[0], COLOR_INPUT_BG[1], COLOR_INPUT_BG[2], COLOR_INPUT_BG[3]);

    // 边框
    SDL_FRect borderRect = m_rect;
    SDL_FRect innerRect = {
        m_rect.x + 1.0f,
        m_rect.y + 1.0f,
        std::max(0.0f, m_rect.w - 2.0f),
        std::max(0.0f, m_rect.h - 2.0f)
    };
    if (m_active) {
        fillRoundRect(renderer, borderRect, 7,
                      COLOR_INPUT_ACTIVE[0], COLOR_INPUT_ACTIVE[1],
                      COLOR_INPUT_ACTIVE[2], COLOR_INPUT_ACTIVE[3]);
    } else {
        fillRoundRect(renderer, borderRect, 7,
                      COLOR_INPUT_BORDER[0], COLOR_INPUT_BORDER[1],
                      COLOR_INPUT_BORDER[2], COLOR_INPUT_BORDER[3]);
    }
    fillRoundRect(renderer, innerRect, 6,
                  COLOR_INPUT_BG[0], COLOR_INPUT_BG[1], COLOR_INPUT_BG[2], COLOR_INPUT_BG[3]);

    std::string display = getDisplayValue();
    std::string visibleText = display.empty() && !m_active ? m_placeholder : display;
    updateTextOffset(getTextWidth(visibleText));

    int textX = static_cast<int>(m_rect.x) + 5 - m_textOffsetX;
    int textY = static_cast<int>(m_rect.y) + (static_cast<int>(m_rect.h) - getFontHeight()) / 2;
    SDL_Rect clipRect = {
        static_cast<int>(m_rect.x) + 5,
        static_cast<int>(m_rect.y) + 1,
        std::max(0, static_cast<int>(m_rect.w) - 10),
        std::max(0, static_cast<int>(m_rect.h) - 2)
    };
    SDL_SetRenderClipRect(renderer, &clipRect);

    // 选区高亮
    if (m_active && m_selectionStart >= 0 && m_selectionEnd >= 0 && m_selectionStart != m_selectionEnd) {
        int selStart = std::min(m_selectionStart, m_selectionEnd);
        int selEnd = std::max(m_selectionStart, m_selectionEnd);

        std::string beforeSel = display.substr(0, selStart);
        std::string selected = display.substr(selStart, selEnd - selStart);

        int selX = textX + getTextWidth(beforeSel);
        int selW = getTextWidth(selected);

        SDL_SetRenderDrawColor(renderer, COLOR_SELECTION[0], COLOR_SELECTION[1], COLOR_SELECTION[2], COLOR_SELECTION[3]);
        SDL_FRect selRect = {static_cast<float>(selX), static_cast<float>(textY),
                            static_cast<float>(selW), static_cast<float>(getFontHeight())};
        SDL_RenderFillRect(renderer, &selRect);
    }

    // 文本或占位符
    if (!display.empty()) {
        drawText(renderer, display, textX, textY, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 255);
    } else if (!m_active && !m_placeholder.empty()) {
        drawText(renderer, m_placeholder, textX, textY, COLOR_PLACEHOLDER[0], COLOR_PLACEHOLDER[1], COLOR_PLACEHOLDER[2], COLOR_PLACEHOLDER[3]);
    }

    // 光标
    if (m_active) {
        uint64_t now = SDL_GetTicks();
        if (now - m_cursorBlinkTime >= 500) {
            m_cursorVisible = !m_cursorVisible;
            m_cursorBlinkTime = now;
        }
        if (m_cursorVisible) {
            std::string beforeCursor = display.substr(0, m_cursorPos);
            int cursorX = textX + getTextWidth(beforeCursor);
            int cursorY = textY + 2;
            int cursorH = getFontHeight() - 4;
            SDL_SetRenderDrawColor(renderer, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], COLOR_TEXT[3]);
            SDL_FRect cursorRect = {static_cast<float>(cursorX), static_cast<float>(cursorY),
                                   2.0f, static_cast<float>(cursorH)};
            SDL_RenderFillRect(renderer, &cursorRect);
        }
    }

    SDL_SetRenderClipRect(renderer, nullptr);
}

void InputField::drawText(SDL_Renderer* renderer, const std::string& text, int x, int y,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!renderer || text.empty() || !m_font) return;

    SDL_Color color = {r, g, b, a};
    SDL_Surface* surface = TTF_RenderText_Blended(m_font, text.c_str(), 0, color);
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

int InputField::getTextWidth(const std::string& text) const {
    if (text.empty() || !m_font) return 0;

    int w = 0;
    TTF_GetStringSize(m_font, text.c_str(), 0, &w, nullptr);
    return w;
}

int InputField::getFontHeight() const {
    if (!m_font) return 0;
    return TTF_GetFontHeight(m_font);
}

std::string InputField::maskPassword(const std::string& text) const {
    if (text.empty()) return "";
    return std::string(text.length(), '*');
}

} // namespace VideoPlay
