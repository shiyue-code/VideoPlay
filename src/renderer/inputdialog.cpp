#include "renderer/inputdialog.h"

#include "renderer/inputfield.h"
#include "utils/logger.h"

#include <utility>
#include <SDL3/SDL.h>

namespace VideoPlay {

static constexpr int INPUT_HEIGHT = 34;
static constexpr int BUTTON_WIDTH = 80;
static constexpr int BUTTON_HEIGHT = 30;
static constexpr int MARGIN = 20;

InputDialog::InputDialog(SDL_Window* parentWindow, TTF_Font* font)
    : ModalDialog(parentWindow, font) {
    m_windowWidth = 520;
    m_windowHeight = 150;
}

void InputDialog::show(const std::string& title, const std::string& placeholder,
                       const std::string& initialValue,
                       std::function<void(const std::string&)> callback) {
    m_callback = std::move(callback);
    m_accepted = false;

    m_input = std::make_unique<InputField>(m_font);
    m_input->setPlaceholder(placeholder);
    m_input->setValue(initialValue);
    m_input->setRect({static_cast<float>(MARGIN),
                      static_cast<float>(kTitleHeight + MARGIN),
                      static_cast<float>(m_windowWidth - MARGIN * 2),
                      static_cast<float>(INPUT_HEIGHT)});
    m_input->setActive(true);

    if (!openWindow(title, true)) {
        return;
    }

    runModal();

    if (m_callback) {
        m_callback(m_accepted ? m_input->getValue() : std::string());
    }
}

void InputDialog::finish(bool accepted) {
    m_accepted = accepted;
    requestClose();
}

bool InputDialog::onEvent(const SDL_Event& event) {
    // 输入框优先消费文本/编辑事件
    if (m_input && m_input->handleEvent(event)) {
        return true;
    }

    switch (event.type) {
        case SDL_EVENT_QUIT:
            finish(false);
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                int mx = static_cast<int>(event.button.x);
                int my = static_cast<int>(event.button.y);

                // 确定 / 取消按钮
                int btnY = m_windowHeight - BUTTON_HEIGHT - 15;
                int okX = m_windowWidth / 2 - BUTTON_WIDTH - 10;
                int cancelX = m_windowWidth / 2 + 10;
                if (my >= btnY && my <= btnY + BUTTON_HEIGHT) {
                    if (mx >= okX && mx <= okX + BUTTON_WIDTH) {
                        finish(!m_input || !m_input->getValue().empty());
                        return true;
                    }
                    if (mx >= cancelX && mx <= cancelX + BUTTON_WIDTH) {
                        finish(false);
                        return true;
                    }
                }
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                finish(!m_input || !m_input->getValue().empty());
                return true;
            }
            break;
        default:
            break;
    }
    return false;
}

void InputDialog::renderContent() {
    float mx, my;
    SDL_GetMouseState(&mx, &my);

    // 输入框
    if (m_input) {
        m_input->render(m_renderer, mx, my);
    }

    // 确定 / 取消按钮
    int btnY = m_windowHeight - BUTTON_HEIGHT - 15;
    int okX = m_windowWidth / 2 - BUTTON_WIDTH - 10;
    int cancelX = m_windowWidth / 2 + 10;

    auto drawButton = [&](int x, const std::string& text) {
        bool hovered = (mx >= x && mx <= x + BUTTON_WIDTH &&
                        my >= btnY && my <= btnY + BUTTON_HEIGHT);
        const uint8_t* c = hovered ? kColorButtonHover : kColorButtonBg;
        SDL_SetRenderDrawColor(m_renderer, c[0], c[1], c[2], c[3]);
        SDL_FRect rect = {static_cast<float>(x), static_cast<float>(btnY),
                          static_cast<float>(BUTTON_WIDTH), static_cast<float>(BUTTON_HEIGHT)};
        SDL_RenderFillRect(m_renderer, &rect);
        int textW = getTextWidth(text);
        drawText(text, x + (BUTTON_WIDTH - textW) / 2,
                 btnY + (BUTTON_HEIGHT - getFontHeight()) / 2,
                 kColorButtonText[0], kColorButtonText[1], kColorButtonText[2], 255);
    };
    drawButton(okX, "确定");
    drawButton(cancelX, "取消");
}

} // namespace VideoPlay
