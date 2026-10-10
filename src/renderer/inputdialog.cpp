#include "renderer/inputdialog.h"

#include "renderer/inputfield.h"
#include "renderer/ui_draw.h"
#include "utils/logger.h"

#include <utility>
#include <SDL3/SDL.h>

namespace VideoPlay {

static constexpr int INPUT_HEIGHT = 34;

InputDialog::InputDialog(SDL_Window* parentWindow, TTF_Font* font, TTF_Font* titleFont)
    : ModalDialog(parentWindow, font, titleFont) {
    m_windowWidth = 520;
    // 标题区 + 34px 输入框 + 底部按钮区（下边距 padding-4）
    m_windowHeight = titleHeight() + 4 + INPUT_HEIGHT + 16 +
                     UiStyle::kButtonHeight + UiStyle::kPanelPadding - 4;
}

void InputDialog::show(const std::string& title, const std::string& placeholder,
                       const std::string& initialValue,
                       std::function<void(const std::string&)> callback) {
    m_callback = std::move(callback);
    m_accepted = false;

    m_input = std::make_unique<InputField>(m_font);
    m_input->setPlaceholder(placeholder);
    m_input->setValue(initialValue);
    m_input->setRect({static_cast<float>(UiStyle::kPanelPadding),
                      static_cast<float>(titleHeight() + 4),
                      static_cast<float>(m_windowWidth - UiStyle::kPanelPadding * 2),
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

                // 取消 / 确定按钮（右下角，右到左排布）
                int btnW = UiStyle::kButtonMinWidth;
                int btnY = m_windowHeight - UiStyle::kButtonHeight - (UiStyle::kPanelPadding - 4);
                int okX = m_windowWidth - UiStyle::kPanelPadding - btnW;
                int cancelX = okX - 10 - btnW;
                if (my >= btnY && my <= btnY + UiStyle::kButtonHeight) {
                    if (mx >= okX && mx <= okX + btnW) {
                        finish(!m_input || !m_input->getValue().empty());
                        return true;
                    }
                    if (mx >= cancelX && mx <= cancelX + btnW) {
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

    // 取消 / 确定按钮（右下角，右到左排布：确定最右）
    int btnW = UiStyle::kButtonMinWidth;
    int btnY = m_windowHeight - UiStyle::kButtonHeight - (UiStyle::kPanelPadding - 4);
    SDL_FRect okRect = {static_cast<float>(m_windowWidth - UiStyle::kPanelPadding - btnW),
                        static_cast<float>(btnY), static_cast<float>(btnW),
                        static_cast<float>(UiStyle::kButtonHeight)};
    SDL_FRect cancelRect = {okRect.x - 10.0f - btnW, okRect.y,
                            static_cast<float>(btnW), static_cast<float>(UiStyle::kButtonHeight)};

    UiDraw::drawButton(m_renderer, m_font, cancelRect, "取消",
                       isPointInRect(mx, my, cancelRect), false, false);
    UiDraw::drawButton(m_renderer, m_font, okRect, "确定",
                       isPointInRect(mx, my, okRect), false, true);
}

} // namespace VideoPlay
