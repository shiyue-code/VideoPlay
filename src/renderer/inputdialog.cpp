#include "renderer/inputdialog.h"
#include "renderer/inputfield.h"
#include "utils/logger.h"
#include <algorithm>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// DWM 圆角偏好（Win11+），与 custommessagebox.cpp 保持一致的本地常量
constexpr DWORD kDwmwaWindowCornerPreference = 33;
constexpr int kDwmWindowCornerRound = 2;
#endif

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto logger = Logger::get("renderer.dialog");
    return *logger;
}
}

static constexpr uint8_t COLOR_BG[4] = {30, 30, 30, 240};
static constexpr uint8_t COLOR_TITLE_BG[4] = {40, 40, 40, 255};
static constexpr uint8_t COLOR_TITLE_TEXT[4] = {255, 255, 255, 255};
static constexpr uint8_t COLOR_BUTTON_BG[4] = {60, 60, 60, 255};
static constexpr uint8_t COLOR_BUTTON_HOVER[4] = {80, 80, 80, 255};
static constexpr uint8_t COLOR_BUTTON_TEXT[4] = {255, 255, 255, 255};
static constexpr uint8_t COLOR_CLOSE_HOVER[4] = {232, 17, 35, 255};

static constexpr int TITLE_HEIGHT = 32;
static constexpr int INPUT_HEIGHT = 34;
static constexpr int BUTTON_WIDTH = 80;
static constexpr int BUTTON_HEIGHT = 30;
static constexpr int MARGIN = 20;

InputDialog::InputDialog(SDL_Window* parentWindow, TTF_Font* font)
    : m_parentWindow(parentWindow), m_font(font) {
}

InputDialog::~InputDialog() {
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
}

void InputDialog::show(const std::string& title, const std::string& placeholder,
                       const std::string& initialValue,
                       std::function<void(const std::string&)> callback) {
    m_title = title;
    m_callback = std::move(callback);
    m_accepted = false;
    m_dragging = false;

    m_input = std::make_unique<InputField>(m_font);
    m_input->setPlaceholder(placeholder);
    m_input->setValue(initialValue);
    m_input->setRect({static_cast<float>(MARGIN),
                      static_cast<float>(TITLE_HEIGHT + MARGIN),
                      static_cast<float>(m_windowWidth - MARGIN * 2),
                      static_cast<float>(INPUT_HEIGHT)});
    m_input->setActive(true);

    m_window = SDL_CreateWindow(title.c_str(), m_windowWidth, m_windowHeight,
                                SDL_WINDOW_BORDERLESS);
    if (!m_window) {
        logger().error("Failed to create input dialog window: " + std::string(SDL_GetError()));
        return;
    }

    SDL_SetWindowAlwaysOnTop(m_window, true);

    if (m_parentWindow) {
        int parentX, parentY, parentW, parentH;
        SDL_GetWindowPosition(m_parentWindow, &parentX, &parentY);
        SDL_GetWindowSize(m_parentWindow, &parentW, &parentH);
        SDL_SetWindowPosition(m_window,
                              parentX + (parentW - m_windowWidth) / 2,
                              parentY + (parentH - m_windowHeight) / 2);
    }

#ifdef _WIN32
    SDL_PropertiesID props = SDL_GetWindowProperties(m_window);
    HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (hwnd) {
        int corner = kDwmWindowCornerRound;
        DwmSetWindowAttribute(hwnd, kDwmwaWindowCornerPreference, &corner, sizeof(corner));
    }
#endif

    m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (!m_renderer) {
        logger().error("Failed to create input dialog renderer: " + std::string(SDL_GetError()));
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        return;
    }

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_StartTextInput(m_window);
    SDL_RaiseWindow(m_window);

    m_running = true;
    while (m_running) {
        handleEvents();
        render();
        SDL_Delay(m_dragging ? 1 : 16);
    }

    SDL_StopTextInput(m_window);

    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }

    if (m_callback) {
        m_callback(m_accepted ? m_input->getValue() : std::string());
    }
}

void InputDialog::finish(bool accepted) {
    m_accepted = accepted;
    m_running = false;
}

void InputDialog::handleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // 输入框优先消费文本/编辑事件
        if (m_input && m_input->handleEvent(event)) {
            continue;
        }

        switch (event.type) {
            case SDL_EVENT_QUIT:
                finish(false);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int mx = static_cast<int>(event.button.x);
                    int my = static_cast<int>(event.button.y);

                    // 关闭按钮
                    constexpr int closeWidth = 46;
                    int closeX = m_windowWidth - closeWidth;
                    if (mx >= closeX && mx <= closeX + closeWidth &&
                        my >= 0 && my <= TITLE_HEIGHT) {
                        finish(false);
                        break;
                    }

                    // 确定 / 取消按钮
                    int btnY = m_windowHeight - BUTTON_HEIGHT - 15;
                    int okX = m_windowWidth / 2 - BUTTON_WIDTH - 10;
                    int cancelX = m_windowWidth / 2 + 10;
                    if (my >= btnY && my <= btnY + BUTTON_HEIGHT) {
                        if (mx >= okX && mx <= okX + BUTTON_WIDTH) {
                            finish(!m_input || !m_input->getValue().empty());
                            break;
                        }
                        if (mx >= cancelX && mx <= cancelX + BUTTON_WIDTH) {
                            finish(false);
                            break;
                        }
                    }

                    // 标题栏拖动
                    if (my < TITLE_HEIGHT) {
#ifdef _WIN32
                        SDL_PropertiesID props = SDL_GetWindowProperties(m_window);
                        HWND hwnd = (HWND)SDL_GetPointerProperty(
                            props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
                        if (hwnd) {
                            ReleaseCapture();
                            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                        }
#else
                        m_dragging = true;
                        m_dragOffsetX = mx;
                        m_dragOffsetY = my;
#endif
                    }
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    m_dragging = false;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (m_dragging) {
                    int windowX, windowY;
                    SDL_GetWindowPosition(m_window, &windowX, &windowY);
                    SDL_SetWindowPosition(m_window,
                        windowX + static_cast<int>(event.motion.x) - m_dragOffsetX,
                        windowY + static_cast<int>(event.motion.y) - m_dragOffsetY);
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    finish(false);
                } else if (event.key.key == SDLK_RETURN ||
                           event.key.key == SDLK_KP_ENTER) {
                    finish(!m_input || !m_input->getValue().empty());
                }
                break;
            default:
                break;
        }
    }
}

void InputDialog::render() {
    if (!m_renderer) return;

    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
    SDL_RenderClear(m_renderer);

    // 背景与标题栏
    SDL_FRect bgRect = {0, 0, static_cast<float>(m_windowWidth),
                        static_cast<float>(m_windowHeight)};
    SDL_SetRenderDrawColor(m_renderer, COLOR_BG[0], COLOR_BG[1], COLOR_BG[2], COLOR_BG[3]);
    SDL_RenderFillRect(m_renderer, &bgRect);

    SDL_FRect titleBar = {0, 0, static_cast<float>(m_windowWidth),
                          static_cast<float>(TITLE_HEIGHT)};
    SDL_SetRenderDrawColor(m_renderer, COLOR_TITLE_BG[0], COLOR_TITLE_BG[1],
                           COLOR_TITLE_BG[2], COLOR_TITLE_BG[3]);
    SDL_RenderFillRect(m_renderer, &titleBar);

    int titleTextY = (TITLE_HEIGHT - getFontHeight()) / 2;
    drawText(m_title, 15, titleTextY,
             COLOR_TITLE_TEXT[0], COLOR_TITLE_TEXT[1], COLOR_TITLE_TEXT[2], 255);

    // 关闭按钮
    constexpr int closeWidth = 46;
    constexpr int closeIconSize = 10;
    int closeX = m_windowWidth - closeWidth;

    float mx, my;
    SDL_GetMouseState(&mx, &my);
    if (mx >= closeX && mx <= closeX + closeWidth && my >= 0 && my <= TITLE_HEIGHT) {
        SDL_SetRenderDrawColor(m_renderer, COLOR_CLOSE_HOVER[0], COLOR_CLOSE_HOVER[1],
                               COLOR_CLOSE_HOVER[2], COLOR_CLOSE_HOVER[3]);
        SDL_FRect closeBtn = {static_cast<float>(closeX), 0,
                              static_cast<float>(closeWidth), static_cast<float>(TITLE_HEIGHT)};
        SDL_RenderFillRect(m_renderer, &closeBtn);
    }

    SDL_SetRenderDrawColor(m_renderer, COLOR_BUTTON_TEXT[0], COLOR_BUTTON_TEXT[1],
                           COLOR_BUTTON_TEXT[2], COLOR_BUTTON_TEXT[3]);
    int iconX = closeX + (closeWidth - closeIconSize) / 2;
    int iconY = (TITLE_HEIGHT - closeIconSize) / 2;
    SDL_RenderLine(m_renderer, iconX, iconY, iconX + closeIconSize, iconY + closeIconSize);
    SDL_RenderLine(m_renderer, iconX + closeIconSize, iconY, iconX, iconY + closeIconSize);

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
        const uint8_t* c = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON_BG;
        SDL_SetRenderDrawColor(m_renderer, c[0], c[1], c[2], c[3]);
        SDL_FRect rect = {static_cast<float>(x), static_cast<float>(btnY),
                          static_cast<float>(BUTTON_WIDTH), static_cast<float>(BUTTON_HEIGHT)};
        SDL_RenderFillRect(m_renderer, &rect);
        int textW = getTextWidth(text);
        drawText(text, x + (BUTTON_WIDTH - textW) / 2,
                 btnY + (BUTTON_HEIGHT - getFontHeight()) / 2,
                 COLOR_BUTTON_TEXT[0], COLOR_BUTTON_TEXT[1], COLOR_BUTTON_TEXT[2], 255);
    };
    drawButton(okX, "确定");
    drawButton(cancelX, "取消");

    SDL_RenderPresent(m_renderer);
}

void InputDialog::drawText(const std::string& text, int x, int y,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!m_renderer || text.empty() || !m_font) return;

    SDL_Color color = {r, g, b, a};
    SDL_Surface* surface = TTF_RenderText_Blended(m_font, text.c_str(), 0, color);
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

int InputDialog::getTextWidth(const std::string& text) {
    if (text.empty() || !m_font) return 0;
    int w = 0;
    TTF_GetStringSize(m_font, text.c_str(), 0, &w, nullptr);
    return w;
}

int InputDialog::getFontHeight() {
    if (!m_font) return 0;
    return TTF_GetFontHeight(m_font);
}

} // namespace VideoPlay
