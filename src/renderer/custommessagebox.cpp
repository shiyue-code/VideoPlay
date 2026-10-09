#include "renderer/custommessagebox.h"

#include "utils/string_utils.h"

#include <algorithm>
#include <utility>
#include <SDL3/SDL.h>

namespace VideoPlay {

// 颜色常量
static constexpr uint8_t COLOR_TEXT[4] = {220, 220, 220, 255};

CustomMessageBox::CustomMessageBox(SDL_Window* parentWindow, TTF_Font* font)
    : ModalDialog(parentWindow, font) {
}

void CustomMessageBox::show(const std::string& title, const std::string& message, bool isError,
                            TimestampClickCallback timestampCallback) {
    m_message = message;
    m_isError = isError;
    m_timestampClickCallback = std::move(timestampCallback);
    m_timestampRects.clear();

    calculateSize();

    if (!openWindow(title, false)) {
        return;
    }
    runModal();
}

void CustomMessageBox::calculateSize() {
    int titleWidth = getTextWidth(m_title) + 80;
    int minTitleWidth = 300;
    m_windowWidth = std::max(minTitleWidth, titleWidth);

    // 先按自然换行拆出消息中的每一行，取最长行的宽度作为窗口宽度参考
    auto naturalLines = wrapText(m_message, 100000);
    int maxContentLineWidth = 0;
    for (const auto& line : naturalLines) {
        int lineW = getTextWidth(line);
        if (lineW > maxContentLineWidth) {
            maxContentLineWidth = lineW;
        }
    }

    m_windowWidth = std::max({m_windowWidth, maxContentLineWidth + 60, 400});
    m_windowWidth = std::min(m_windowWidth, 900);

    auto lines = wrapText(m_message, m_windowWidth - 60);
    int lineHeight = getFontHeight() + 4;
    int contentHeight = static_cast<int>(lines.size()) * lineHeight;
    m_windowHeight = kTitleHeight + 20 + contentHeight + 20 + 40 + 20;

    m_windowHeight = std::max(m_windowHeight, 200);
    m_windowHeight = std::min(m_windowHeight, 800);
}

void CustomMessageBox::renderContent() {
    float mx, my;
    SDL_GetMouseState(&mx, &my);

    // 绘制消息文本
    auto lines = wrapText(m_message, m_windowWidth - 60);
    int lineHeight = getFontHeight() + 4;
    int textY = kTitleHeight + 20;
    m_timestampRects.clear();
    for (const auto& line : lines) {
        int textX = 30;
        size_t pos = 0;
        while (pos < line.size()) {
            size_t start = line.find('[', pos);
            if (start == std::string::npos) {
                std::string text = line.substr(pos);
                drawText(text, textX, textY, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 255);
                break;
            }

            if (start > pos) {
                std::string text = line.substr(pos, start - pos);
                drawText(text, textX, textY, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 255);
                textX += getTextWidth(text);
            }

            size_t end = line.find(']', start);
            if (end == std::string::npos) {
                std::string text = line.substr(start);
                drawText(text, textX, textY, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 255);
                break;
            }

            std::string token = line.substr(start, end - start + 1);
            int64_t timestampMs = 0;
            bool clickable = m_timestampClickCallback && parseBracketTimestampMs(token, timestampMs);
            int tokenW = getTextWidth(token);
            if (clickable) {
                drawText(token, textX, textY, 255, 200, 50, 255);
                m_timestampRects.push_back({
                    SDL_Rect{textX, textY, tokenW, lineHeight},
                    timestampMs
                });
            } else {
                drawText(token, textX, textY, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 255);
            }

            textX += tokenW;
            pos = end + 1;
        }
        textY += lineHeight;
    }

    // 确定按钮
    int okBtnWidth = 80;
    int okBtnHeight = 30;
    int okBtnX = (m_windowWidth - okBtnWidth) / 2;
    int okBtnY = m_windowHeight - okBtnHeight - 15;
    bool okHovered = (mx >= okBtnX && mx <= okBtnX + okBtnWidth && my >= okBtnY && my <= okBtnY + okBtnHeight);

    if (okHovered) {
        SDL_SetRenderDrawColor(m_renderer, kColorButtonHover[0], kColorButtonHover[1], kColorButtonHover[2], kColorButtonHover[3]);
    } else {
        SDL_SetRenderDrawColor(m_renderer, kColorButtonBg[0], kColorButtonBg[1], kColorButtonBg[2], kColorButtonBg[3]);
    }
    SDL_FRect okBtn = {static_cast<float>(okBtnX), static_cast<float>(okBtnY),
                       static_cast<float>(okBtnWidth), static_cast<float>(okBtnHeight)};
    SDL_RenderFillRect(m_renderer, &okBtn);

    // 确定按钮文本
    std::string okText = "确定";
    int okTextWidth = getTextWidth(okText);
    drawText(okText, okBtnX + (okBtnWidth - okTextWidth) / 2, okBtnY + 7,
             kColorButtonText[0], kColorButtonText[1], kColorButtonText[2], 255);
}

bool CustomMessageBox::onEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                int mx = static_cast<int>(event.button.x);
                int my = static_cast<int>(event.button.y);

                // 确定按钮点击
                int okBtnWidth = 80;
                int okBtnHeight = 30;
                int okBtnX = (m_windowWidth - okBtnWidth) / 2;
                int okBtnY = m_windowHeight - okBtnHeight - 15;
                if (mx >= okBtnX && mx <= okBtnX + okBtnWidth && my >= okBtnY && my <= okBtnY + okBtnHeight) {
                    requestClose();
                    return true;
                }

                if (m_timestampClickCallback) {
                    for (const auto& timestampRect : m_timestampRects) {
                        const SDL_Rect& rect = timestampRect.rect;
                        if (mx >= rect.x && mx <= rect.x + rect.w &&
                            my >= rect.y && my <= rect.y + rect.h) {
                            m_timestampClickCallback(timestampRect.timestampMs);
                            requestClose();
                            return true;
                        }
                    }
                }
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.key == SDLK_RETURN) {
                requestClose();
                return true;
            }
            break;
        default:
            break;
    }
    return false;
}

} // namespace VideoPlay
