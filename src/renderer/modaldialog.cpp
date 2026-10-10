#include "renderer/modaldialog.h"

#include "renderer/ui_draw.h"
#include "utils/logger.h"

#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// DWM 圆角偏好（Win11+）
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

ModalDialog::ModalDialog(SDL_Window* parentWindow, TTF_Font* font, TTF_Font* titleFont)
    : m_parentWindow(parentWindow), m_font(font),
      m_titleFont(titleFont ? titleFont : font) {
}

int ModalDialog::titleHeight() const {
    return UiStyle::kPanelPadding + TextRender::fontHeight(m_titleFont) + 14;
}

ModalDialog::~ModalDialog() {
    destroyWindow();
}

void ModalDialog::destroyWindow() {
    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

bool ModalDialog::openWindow(const std::string& title, bool enableTextInput) {
    m_title = title;
    m_dragging = false;
    m_dragOffsetX = 0;
    m_dragOffsetY = 0;

    m_window = SDL_CreateWindow(title.c_str(), m_windowWidth, m_windowHeight,
                                SDL_WINDOW_BORDERLESS);
    if (!m_window) {
        logger().error("Failed to create dialog window: " + std::string(SDL_GetError()));
        return false;
    }

    SDL_SetWindowAlwaysOnTop(m_window, true);

    // 居中显示在父窗口
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
        logger().error("Failed to create dialog renderer: " + std::string(SDL_GetError()));
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        return false;
    }

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    m_textInputEnabled = enableTextInput;
    if (m_textInputEnabled) {
        SDL_StartTextInput(m_window);
    }
    SDL_RaiseWindow(m_window);
    return true;
}

void ModalDialog::runModal() {
    m_running = true;
    while (m_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (!onEvent(event)) {
                switch (event.type) {
                    case SDL_EVENT_QUIT:
                        m_running = false;
                        break;
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                        if (event.button.button == SDL_BUTTON_LEFT) {
                            int mx = static_cast<int>(event.button.x);
                            int my = static_cast<int>(event.button.y);
                            if (hitCloseButton(mx, my)) {
                                m_running = false;
                            } else if (my < titleHeight()) {
                                // 标题栏拖动
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
                            m_running = false;
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        if (m_renderer) {
            SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
            SDL_RenderClear(m_renderer);
            renderChrome();
            renderContent();
            SDL_RenderPresent(m_renderer);
        }
        SDL_Delay(m_dragging ? 1 : 16);
    }

    if (m_textInputEnabled && m_window) {
        SDL_StopTextInput(m_window);
        m_textInputEnabled = false;
    }
    destroyWindow();
}

void ModalDialog::requestClose() {
    m_running = false;
}

bool ModalDialog::onEvent(const SDL_Event& event) {
    (void)event;
    return false;
}

void ModalDialog::renderContent() {
}

void ModalDialog::renderChrome() {
    // 主背景（不透明面板底色）+ 整窗 1px 边框
    SDL_FRect bgRect = {0, 0, static_cast<float>(m_windowWidth),
                        static_cast<float>(m_windowHeight)};
    SDL_SetRenderDrawColor(m_renderer, UiStyle::kDialogBg[0], UiStyle::kDialogBg[1],
                           UiStyle::kDialogBg[2], UiStyle::kDialogBg[3]);
    SDL_RenderFillRect(m_renderer, &bgRect);
    SDL_SetRenderDrawColor(m_renderer, UiStyle::kDialogBorder[0], UiStyle::kDialogBorder[1],
                           UiStyle::kDialogBorder[2], UiStyle::kDialogBorder[3]);
    SDL_RenderRect(m_renderer, &bgRect);

    // 标题：18px 白字，与信息面板相同的布局（x/顶部对齐到内边距）
    TextRender::drawText(m_renderer, m_titleFont, m_title,
                         UiStyle::kPanelPadding, UiStyle::kPanelPadding, 255, 255, 255);

    // 关闭按钮：与信息面板一致（28px，距右边 16px，与标题垂直居中）
    float mx, my;
    SDL_GetMouseState(&mx, &my);
    int closeX = m_windowWidth - 16 - UiStyle::kCloseSize;
    int closeY = UiStyle::kPanelPadding +
                 (TextRender::fontHeight(m_titleFont) - UiStyle::kCloseSize) / 2;
    bool hovered = hitCloseButton(static_cast<int>(mx), static_cast<int>(my));
    UiDraw::drawCloseButton(m_renderer, closeX, closeY, UiStyle::kCloseSize,
                            hovered, false,
                            UiStyle::kCloseRadius, UiStyle::kCloseInset,
                            UiStyle::kCloseHoverAlpha,
                            UiStyle::kCloseLineShade, UiStyle::kCloseLineAlpha);
}

bool ModalDialog::hitCloseButton(int mx, int my) const {
    int closeX = m_windowWidth - 16 - UiStyle::kCloseSize;
    int closeY = UiStyle::kPanelPadding +
                 (TextRender::fontHeight(m_titleFont) - UiStyle::kCloseSize) / 2;
    return mx >= closeX && mx <= closeX + UiStyle::kCloseSize &&
           my >= closeY && my <= closeY + UiStyle::kCloseSize;
}

bool ModalDialog::hitTitleBar(int mx, int my) const {
    return mx >= 0 && mx <= m_windowWidth && my >= 0 && my < titleHeight();
}

bool ModalDialog::isPointInRect(float x, float y, const SDL_FRect& rect) const {
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

void ModalDialog::drawText(const std::string& text, int x, int y,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    TextRender::drawText(m_renderer, m_font, text, x, y, r, g, b, a);
}

int ModalDialog::getTextWidth(const std::string& text) const {
    return TextRender::textWidth(m_font, text);
}

int ModalDialog::getFontHeight() const {
    return TextRender::fontHeight(m_font);
}

std::vector<std::string> ModalDialog::wrapText(const std::string& text, int maxWidth) const {
    return TextRender::wrapText(m_font, text, maxWidth);
}

} // namespace VideoPlay
