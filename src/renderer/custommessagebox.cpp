#include "renderer/custommessagebox.h"
#include "renderer/textutils.h"
#include "utils/logger.h"
#include <algorithm>
#include <cstdio>
#include <utility>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// DWM 圆角偏好（Win11+）。新 SDK 已在 dwmapi.h 中定义同名枚举，这里使用
// 本地常量避免在新旧 Windows SDK 之间重复定义类型。
constexpr DWORD kDwmwaWindowCornerPreference = 33;
constexpr int kDwmWindowCornerRound = 2;
#endif

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto logger = Logger::get("renderer.dialog");
    return *logger;
}

bool parseTimestampMs(const std::string& text, int64_t& timestampMs) {
    int first = 0;
    int second = 0;
    int third = 0;
    int colonCount = static_cast<int>(std::count(text.begin(), text.end(), ':'));

    if (colonCount == 1 && sscanf(text.c_str(), "[%d:%d]", &first, &second) == 2) {
        timestampMs = static_cast<int64_t>(first * 60 + second) * 1000;
        return true;
    }

    if (colonCount == 2 && sscanf(text.c_str(), "[%d:%d:%d]", &first, &second, &third) == 3) {
        timestampMs = static_cast<int64_t>(first * 3600 + second * 60 + third) * 1000;
        return true;
    }

    return false;
}
}


// 颜色常量
static constexpr uint8_t COLOR_BG[4] = {30, 30, 30, 240};
static constexpr uint8_t COLOR_TITLE_BG[4] = {40, 40, 40, 255};
static constexpr uint8_t COLOR_TEXT[4] = {220, 220, 220, 255};
static constexpr uint8_t COLOR_TITLE_TEXT[4] = {255, 255, 255, 255};
static constexpr uint8_t COLOR_BUTTON_BG[4] = {60, 60, 60, 255};
static constexpr uint8_t COLOR_BUTTON_HOVER[4] = {80, 80, 80, 255};
static constexpr uint8_t COLOR_BUTTON_TEXT[4] = {255, 255, 255, 255};
static constexpr uint8_t COLOR_CLOSE_HOVER[4] = {232, 17, 35, 255};

static constexpr int TITLE_HEIGHT = 32;

CustomMessageBox::CustomMessageBox(SDL_Window* parentWindow, TTF_Font* font)
    : m_parentWindow(parentWindow), m_font(font) {
}

CustomMessageBox::~CustomMessageBox() {
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
}

void CustomMessageBox::show(const std::string& title, const std::string& message, bool isError,
                            TimestampClickCallback timestampCallback) {
    m_title = title;
    m_message = message;
    m_isError = isError;
    m_timestampClickCallback = std::move(timestampCallback);
    m_timestampRects.clear();
    m_dragging = false;
    m_dragOffsetX = 0;
    m_dragOffsetY = 0;

    calculateSize();

    // 创建无边框窗口
    m_window = SDL_CreateWindow(title.c_str(), m_windowWidth, m_windowHeight, SDL_WINDOW_BORDERLESS);
    if (!m_window) {
        logger().error("Failed to create message box window: " + std::string(SDL_GetError()));
        return;
    }

    // 设置窗口始终置顶
    SDL_SetWindowAlwaysOnTop(m_window, true);

    // 居中显示在父窗口
    if (m_parentWindow) {
        int parentX, parentY, parentW, parentH;
        SDL_GetWindowPosition(m_parentWindow, &parentX, &parentY);
        SDL_GetWindowSize(m_parentWindow, &parentW, &parentH);
        int x = parentX + (parentW - m_windowWidth) / 2;
        int y = parentY + (parentH - m_windowHeight) / 2;
        SDL_SetWindowPosition(m_window, x, y);
    }

#ifdef _WIN32
    // 使用 Win32 API 设置圆角
    SDL_PropertiesID props = SDL_GetWindowProperties(m_window);
    HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (hwnd) {
        // 设置圆角（Win11+）
        int corner = kDwmWindowCornerRound;
        DwmSetWindowAttribute(hwnd, kDwmwaWindowCornerPreference, &corner, sizeof(corner));
    }
#endif

    m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (!m_renderer) {
        logger().error("Failed to create message box renderer: " + std::string(SDL_GetError()));
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        return;
    }

    // 启用 alpha 混合
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    m_running = true;
    while (m_running) {
        handleEvents();
        render();
        SDL_Delay(m_dragging ? 1 : 16);
    }

    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
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
    m_windowHeight = TITLE_HEIGHT + 20 + contentHeight + 20 + 40 + 20;

    m_windowHeight = std::max(m_windowHeight, 200);
    m_windowHeight = std::min(m_windowHeight, 800);
}

std::vector<std::string> CustomMessageBox::wrapText(const std::string& text, int maxWidth) {
    return TextRender::wrapText(m_font, text, maxWidth);
}

void CustomMessageBox::render() {
    if (!m_renderer) return;

    // 清空背景（透明）
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
    SDL_RenderClear(m_renderer);

    // 绘制主背景
    SDL_FRect bgRect = {0, 0, static_cast<float>(m_windowWidth), static_cast<float>(m_windowHeight)};
    SDL_SetRenderDrawColor(m_renderer, COLOR_BG[0], COLOR_BG[1], COLOR_BG[2], COLOR_BG[3]);
    SDL_RenderFillRect(m_renderer, &bgRect);

    // 绘制标题栏背景
    SDL_FRect titleBar = {0, 0, static_cast<float>(m_windowWidth), static_cast<float>(TITLE_HEIGHT)};
    SDL_SetRenderDrawColor(m_renderer, COLOR_TITLE_BG[0], COLOR_TITLE_BG[1], COLOR_TITLE_BG[2], COLOR_TITLE_BG[3]);
    SDL_RenderFillRect(m_renderer, &titleBar);

    // 绘制标题文本
    int titleTextY = (TITLE_HEIGHT - getFontHeight()) / 2;
    drawText(m_title, 15, titleTextY, COLOR_TITLE_TEXT[0], COLOR_TITLE_TEXT[1], COLOR_TITLE_TEXT[2], 255);

    // 关闭按钮
    constexpr int closeWidth = 46;
    constexpr int closeIconSize = 10;
    int closeX = m_windowWidth - closeWidth;
    int closeY = 0;

    float mx, my;
    SDL_GetMouseState(&mx, &my);
    bool closeHovered = (mx >= closeX && mx <= closeX + closeWidth && my >= closeY && my <= closeY + TITLE_HEIGHT);

    if (closeHovered) {
        SDL_SetRenderDrawColor(m_renderer, COLOR_CLOSE_HOVER[0], COLOR_CLOSE_HOVER[1], COLOR_CLOSE_HOVER[2], COLOR_CLOSE_HOVER[3]);
        SDL_FRect closeBtn = {static_cast<float>(closeX), static_cast<float>(closeY),
                              static_cast<float>(closeWidth), static_cast<float>(TITLE_HEIGHT)};
        SDL_RenderFillRect(m_renderer, &closeBtn);
    }

    // 关闭按钮 X
    SDL_SetRenderDrawColor(m_renderer, COLOR_BUTTON_TEXT[0], COLOR_BUTTON_TEXT[1], COLOR_BUTTON_TEXT[2], COLOR_BUTTON_TEXT[3]);
    int iconX = closeX + (closeWidth - closeIconSize) / 2;
    int iconY = (TITLE_HEIGHT - closeIconSize) / 2;
    SDL_RenderLine(m_renderer, iconX, iconY, iconX + closeIconSize, iconY + closeIconSize);
    SDL_RenderLine(m_renderer, iconX + closeIconSize, iconY, iconX, iconY + closeIconSize);

    // 绘制消息文本
    auto lines = wrapText(m_message, m_windowWidth - 60);
    int lineHeight = getFontHeight() + 4;
    int textY = TITLE_HEIGHT + 20;
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
            bool clickable = m_timestampClickCallback && parseTimestampMs(token, timestampMs);
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
        SDL_SetRenderDrawColor(m_renderer, COLOR_BUTTON_HOVER[0], COLOR_BUTTON_HOVER[1], COLOR_BUTTON_HOVER[2], COLOR_BUTTON_HOVER[3]);
    } else {
        SDL_SetRenderDrawColor(m_renderer, COLOR_BUTTON_BG[0], COLOR_BUTTON_BG[1], COLOR_BUTTON_BG[2], COLOR_BUTTON_BG[3]);
    }
    SDL_FRect okBtn = {static_cast<float>(okBtnX), static_cast<float>(okBtnY),
                       static_cast<float>(okBtnWidth), static_cast<float>(okBtnHeight)};
    SDL_RenderFillRect(m_renderer, &okBtn);

    // 确定按钮文本
    std::string okText = "确定";
    int okTextWidth = getTextWidth(okText);
    drawText(okText, okBtnX + (okBtnWidth - okTextWidth) / 2, okBtnY + 7,
             COLOR_BUTTON_TEXT[0], COLOR_BUTTON_TEXT[1], COLOR_BUTTON_TEXT[2], 255);

    SDL_RenderPresent(m_renderer);
}

void CustomMessageBox::handleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                m_running = false;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int mx = static_cast<int>(event.button.x);
                    int my = static_cast<int>(event.button.y);

                    // 关闭按钮点击
                    constexpr int closeWidth = 46;
                    int closeX = m_windowWidth - closeWidth;
                    if (mx >= closeX && mx <= closeX + closeWidth && my >= 0 && my <= TITLE_HEIGHT) {
                        m_running = false;
                        break;
                    }

                    // 确定按钮点击
                    int okBtnWidth = 80;
                    int okBtnHeight = 30;
                    int okBtnX = (m_windowWidth - okBtnWidth) / 2;
                    int okBtnY = m_windowHeight - okBtnHeight - 15;
                    if (mx >= okBtnX && mx <= okBtnX + okBtnWidth && my >= okBtnY && my <= okBtnY + okBtnHeight) {
                        m_running = false;
                        break;
                    }

                    if (m_timestampClickCallback) {
                        for (const auto& timestampRect : m_timestampRects) {
                            const SDL_Rect& rect = timestampRect.rect;
                            if (mx >= rect.x && mx <= rect.x + rect.w &&
                                my >= rect.y && my <= rect.y + rect.h) {
                                m_timestampClickCallback(timestampRect.timestampMs);
                                m_running = false;
                                break;
                            }
                        }
                        if (!m_running) {
                            break;
                        }
                    }

                    // 标题栏拖动
                    if (my < TITLE_HEIGHT) {
#ifdef _WIN32
                        SDL_PropertiesID props = SDL_GetWindowProperties(m_window);
                        HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
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
                    int mx = static_cast<int>(event.motion.x);
                    int my = static_cast<int>(event.motion.y);
                    int windowX, windowY;
                    SDL_GetWindowPosition(m_window, &windowX, &windowY);
                    SDL_SetWindowPosition(m_window,
                        windowX + mx - m_dragOffsetX,
                        windowY + my - m_dragOffsetY);
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_RETURN) {
                    m_running = false;
                }
                break;
        }
    }
}

void CustomMessageBox::drawText(const std::string& text, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    TextRender::drawText(m_renderer, m_font, text, x, y, r, g, b, a);
}

int CustomMessageBox::getTextWidth(const std::string& text) {
    return TextRender::textWidth(m_font, text);
}

int CustomMessageBox::getFontHeight() {
    return TextRender::fontHeight(m_font);
}

} // namespace VideoPlay
