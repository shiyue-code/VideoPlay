#include "renderer/settingsdialog.h"
#include "utils/logger.h"
#include <algorithm>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#ifndef DWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
enum DWM_WINDOW_CORNER_PREFERENCE {
    DWMWCP_DEFAULT = 0,
    DWMWCP_DONOTROUND = 1,
    DWMWCP_ROUND = 2,
    DWMWCP_ROUNDSMALL = 3
};
#endif
#endif

namespace VideoPlay {

// 颜色常量
static constexpr uint8_t COLOR_BG[4] = {30, 30, 30, 240};
static constexpr uint8_t COLOR_TITLE_BG[4] = {40, 40, 40, 255};
static constexpr uint8_t COLOR_TEXT[4] = {220, 220, 220, 255};
static constexpr uint8_t COLOR_TITLE_TEXT[4] = {255, 255, 255, 255};
static constexpr uint8_t COLOR_LABEL[4] = {180, 180, 180, 255};
static constexpr uint8_t COLOR_INPUT_BG[4] = {50, 50, 50, 255};
static constexpr uint8_t COLOR_INPUT_BORDER[4] = {80, 80, 80, 255};
static constexpr uint8_t COLOR_INPUT_ACTIVE[4] = {100, 150, 255, 255};
static constexpr uint8_t COLOR_SELECTION[4] = {50, 100, 200, 128};
static constexpr uint8_t COLOR_BUTTON_BG[4] = {60, 60, 60, 255};
static constexpr uint8_t COLOR_BUTTON_HOVER[4] = {80, 80, 80, 255};
static constexpr uint8_t COLOR_BUTTON_PRIMARY[4] = {50, 120, 200, 255};
static constexpr uint8_t COLOR_BUTTON_PRIMARY_HOVER[4] = {70, 140, 220, 255};
static constexpr uint8_t COLOR_BUTTON_TEXT[4] = {255, 255, 255, 255};
static constexpr uint8_t COLOR_CLOSE_HOVER[4] = {232, 17, 35, 255};

SettingsDialog::SettingsDialog(SDL_Window* parentWindow, TTF_Font* font)
    : m_parentWindow(parentWindow), m_font(font) {
}

SettingsDialog::~SettingsDialog() {
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
}

void SettingsDialog::show(const AISettings& currentSettings, SaveCallback onSave) {
    m_settings = currentSettings;
    m_saveCallback = onSave;
    m_dragging = false;
    m_activeField = InputField::None;
    m_cursorPos = 0;
    m_selectionStart = -1;
    m_selectionEnd = -1;
    m_selecting = false;

    calculateLayout();

    m_window = SDL_CreateWindow("AI 设置", m_windowWidth, m_windowHeight, SDL_WINDOW_BORDERLESS);
    if (!m_window) {
        Logger::instance().error("Failed to create settings dialog: " + std::string(SDL_GetError()));
        return;
    }

    SDL_SetWindowAlwaysOnTop(m_window, true);

    if (m_parentWindow) {
        int parentX, parentY, parentW, parentH;
        SDL_GetWindowPosition(m_parentWindow, &parentX, &parentY);
        SDL_GetWindowSize(m_parentWindow, &parentW, &parentH);
        int x = parentX + (parentW - m_windowWidth) / 2;
        int y = parentY + (parentH - m_windowHeight) / 2;
        SDL_SetWindowPosition(m_window, x, y);
    }

#ifdef _WIN32
    SDL_PropertiesID props = SDL_GetWindowProperties(m_window);
    HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (hwnd) {
        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));
    }
#endif

    m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (!m_renderer) {
        Logger::instance().error("Failed to create settings renderer: " + std::string(SDL_GetError()));
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        return;
    }

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_StartTextInput(m_window);

    m_running = true;
    while (m_running) {
        handleEvents();
        render();
        SDL_Delay(16);
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
}

void SettingsDialog::calculateLayout() {
    int y = TITLE_HEIGHT + PADDING;

    m_baseUrlRect = {PADDING + 120.0f, static_cast<float>(y), 
                     static_cast<float>(m_windowWidth - PADDING * 2 - 120), INPUT_HEIGHT};
    y += INPUT_HEIGHT + PADDING;

    m_apiKeyRect = {PADDING + 120.0f, static_cast<float>(y), 
                    static_cast<float>(m_windowWidth - PADDING * 2 - 120), INPUT_HEIGHT};
    y += INPUT_HEIGHT + PADDING;

    m_whisperModelRect = {PADDING + 120.0f, static_cast<float>(y), 
                          static_cast<float>(m_windowWidth - PADDING * 2 - 120), INPUT_HEIGHT};
    y += INPUT_HEIGHT + PADDING;

    m_gptModelRect = {PADDING + 120.0f, static_cast<float>(y), 
                      static_cast<float>(m_windowWidth - PADDING * 2 - 120), INPUT_HEIGHT};
    y += INPUT_HEIGHT + PADDING * 2;

    int btnWidth = 80;
    int btnHeight = 30;
    int btnY = m_windowHeight - btnHeight - PADDING;
    m_saveBtnRect = {static_cast<float>(m_windowWidth - btnWidth * 2 - PADDING * 2), 
                     static_cast<float>(btnY), static_cast<float>(btnWidth), static_cast<float>(btnHeight)};
    m_cancelBtnRect = {static_cast<float>(m_windowWidth - btnWidth - PADDING), 
                       static_cast<float>(btnY), static_cast<float>(btnWidth), static_cast<float>(btnHeight)};
}

std::string* SettingsDialog::getActiveField() {
    switch (m_activeField) {
        case InputField::BaseUrl: return &m_settings.baseUrl;
        case InputField::ApiKey: return &m_settings.apiKey;
        case InputField::WhisperModel: return &m_settings.whisperModel;
        case InputField::GptModel: return &m_settings.gptModel;
        default: return nullptr;
    }
}

std::string SettingsDialog::getDisplayText(bool isPassword) {
    std::string* field = getActiveField();
    if (!field) return "";
    return isPassword ? maskPassword(*field) : *field;
}

int SettingsDialog::getCursorPosFromMouseX(int mouseX, const std::string& text) {
    if (text.empty()) return 0;
    
    int textStartX = PADDING + 125; // 输入框内文本起始位置
    int relativeX = mouseX - textStartX;
    
    if (relativeX <= 0) return 0;
    
    // 逐字符计算位置，找到最接近的光标位置
    int currentWidth = 0;
    for (size_t i = 0; i < text.length(); i++) {
        std::string charStr = text.substr(i, 1);
        int charWidth = getTextWidth(charStr);
        if (currentWidth + charWidth / 2 > relativeX) {
            return static_cast<int>(i);
        }
        currentWidth += charWidth;
    }
    
    return static_cast<int>(text.length());
}

void SettingsDialog::deleteSelection() {
    if (m_selectionStart < 0 || m_selectionEnd < 0) return;
    
    std::string* field = getActiveField();
    if (!field) return;
    
    int start = std::min(m_selectionStart, m_selectionEnd);
    int end = std::max(m_selectionStart, m_selectionEnd);
    
    if (start >= 0 && end <= static_cast<int>(field->length()) && start < end) {
        field->erase(start, end - start);
        m_cursorPos = start;
    }
    
    m_selectionStart = -1;
    m_selectionEnd = -1;
}

void SettingsDialog::selectAll() {
    std::string* field = getActiveField();
    if (!field || field->empty()) return;
    
    m_selectionStart = 0;
    m_selectionEnd = static_cast<int>(field->length());
    m_cursorPos = m_selectionEnd;
}

void SettingsDialog::moveCursorHome() {
    m_cursorPos = 0;
    m_selectionStart = -1;
    m_selectionEnd = -1;
}

void SettingsDialog::moveCursorEnd() {
    std::string* field = getActiveField();
    if (field) {
        m_cursorPos = static_cast<int>(field->length());
    }
    m_selectionStart = -1;
    m_selectionEnd = -1;
}

void SettingsDialog::moveCursorLeft() {
    if (m_cursorPos > 0) {
        m_cursorPos--;
    }
    m_selectionStart = -1;
    m_selectionEnd = -1;
}

void SettingsDialog::moveCursorRight() {
    std::string* field = getActiveField();
    if (field && m_cursorPos < static_cast<int>(field->length())) {
        m_cursorPos++;
    }
    m_selectionStart = -1;
    m_selectionEnd = -1;
}

void SettingsDialog::deleteCharBefore() {
    if (m_selectionStart >= 0) {
        deleteSelection();
        return;
    }
    
    std::string* field = getActiveField();
    if (field && m_cursorPos > 0) {
        field->erase(m_cursorPos - 1, 1);
        m_cursorPos--;
    }
}

void SettingsDialog::deleteCharAfter() {
    if (m_selectionStart >= 0) {
        deleteSelection();
        return;
    }
    
    std::string* field = getActiveField();
    if (field && m_cursorPos < static_cast<int>(field->length())) {
        field->erase(m_cursorPos, 1);
    }
}

void SettingsDialog::render() {
    if (!m_renderer) return;

    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
    SDL_RenderClear(m_renderer);

    // 主背景
    SDL_FRect bgRect = {0, 0, static_cast<float>(m_windowWidth), static_cast<float>(m_windowHeight)};
    SDL_SetRenderDrawColor(m_renderer, COLOR_BG[0], COLOR_BG[1], COLOR_BG[2], COLOR_BG[3]);
    SDL_RenderFillRect(m_renderer, &bgRect);

    // 标题栏
    SDL_FRect titleBar = {0, 0, static_cast<float>(m_windowWidth), static_cast<float>(TITLE_HEIGHT)};
    SDL_SetRenderDrawColor(m_renderer, COLOR_TITLE_BG[0], COLOR_TITLE_BG[1], COLOR_TITLE_BG[2], COLOR_TITLE_BG[3]);
    SDL_RenderFillRect(m_renderer, &titleBar);

    drawText("AI 设置", 15, 10, COLOR_TITLE_TEXT[0], COLOR_TITLE_TEXT[1], COLOR_TITLE_TEXT[2], 255);

    // 关闭按钮
    int closeX = m_windowWidth - 35;
    int closeY = 5;
    int closeSize = 30;

    float mx, my;
    SDL_GetMouseState(&mx, &my);
    bool closeHovered = (mx >= closeX && mx <= closeX + closeSize && my >= closeY && my <= closeY + closeSize);

    if (closeHovered) {
        SDL_SetRenderDrawColor(m_renderer, COLOR_CLOSE_HOVER[0], COLOR_CLOSE_HOVER[1], COLOR_CLOSE_HOVER[2], COLOR_CLOSE_HOVER[3]);
    } else {
        SDL_SetRenderDrawColor(m_renderer, COLOR_BUTTON_BG[0], COLOR_BUTTON_BG[1], COLOR_BUTTON_BG[2], COLOR_BUTTON_BG[3]);
    }
    SDL_FRect closeBtn = {static_cast<float>(closeX), static_cast<float>(closeY),
                          static_cast<float>(closeSize), static_cast<float>(closeSize)};
    SDL_RenderFillRect(m_renderer, &closeBtn);

    SDL_SetRenderDrawColor(m_renderer, COLOR_BUTTON_TEXT[0], COLOR_BUTTON_TEXT[1], COLOR_BUTTON_TEXT[2], COLOR_BUTTON_TEXT[3]);
    SDL_RenderLine(m_renderer, closeX + 8, closeY + 8, closeX + closeSize - 8, closeY + closeSize - 8);
    SDL_RenderLine(m_renderer, closeX + closeSize - 8, closeY + 8, closeX + 8, closeY + closeSize - 8);

    // 输入框
    drawText("API 地址:", PADDING, TITLE_HEIGHT + PADDING + 8, COLOR_LABEL[0], COLOR_LABEL[1], COLOR_LABEL[2], 255);
    drawInputField(m_settings.baseUrl, m_baseUrlRect, m_activeField == InputField::BaseUrl);
    if (m_settings.baseUrl.empty() && m_activeField != InputField::BaseUrl) {
        int hintY = static_cast<int>(m_baseUrlRect.y) + (static_cast<int>(m_baseUrlRect.h) - getFontHeight()) / 2;
        drawText("https://api.openai.com", static_cast<int>(m_baseUrlRect.x) + 5, hintY, 100, 100, 100, 150);
    }

    drawText("API Key:", PADDING, TITLE_HEIGHT + PADDING * 2 + INPUT_HEIGHT + 8, COLOR_LABEL[0], COLOR_LABEL[1], COLOR_LABEL[2], 255);
    drawInputField(m_settings.apiKey, m_apiKeyRect, m_activeField == InputField::ApiKey, true);
    if (m_settings.apiKey.empty() && m_activeField != InputField::ApiKey) {
        int hintY = static_cast<int>(m_apiKeyRect.y) + (static_cast<int>(m_apiKeyRect.h) - getFontHeight()) / 2;
        drawText("sk-xxxxx", static_cast<int>(m_apiKeyRect.x) + 5, hintY, 100, 100, 100, 150);
    }

    drawText("Whisper 模型:", PADDING, TITLE_HEIGHT + PADDING * 3 + INPUT_HEIGHT * 2 + 8, COLOR_LABEL[0], COLOR_LABEL[1], COLOR_LABEL[2], 255);
    drawInputField(m_settings.whisperModel, m_whisperModelRect, m_activeField == InputField::WhisperModel);
    if (m_settings.whisperModel.empty() && m_activeField != InputField::WhisperModel) {
        int hintY = static_cast<int>(m_whisperModelRect.y) + (static_cast<int>(m_whisperModelRect.h) - getFontHeight()) / 2;
        drawText("whisper-1", static_cast<int>(m_whisperModelRect.x) + 5, hintY, 100, 100, 100, 150);
    }

    drawText("GPT 模型:", PADDING, TITLE_HEIGHT + PADDING * 4 + INPUT_HEIGHT * 3 + 8, COLOR_LABEL[0], COLOR_LABEL[1], COLOR_LABEL[2], 255);
    drawInputField(m_settings.gptModel, m_gptModelRect, m_activeField == InputField::GptModel);
    if (m_settings.gptModel.empty() && m_activeField != InputField::GptModel) {
        int hintY = static_cast<int>(m_gptModelRect.y) + (static_cast<int>(m_gptModelRect.h) - getFontHeight()) / 2;
        drawText("gpt-4o-mini", static_cast<int>(m_gptModelRect.x) + 5, hintY, 100, 100, 100, 150);
    }

    // 按钮
    bool saveHovered = isPointInRect(mx, my, m_saveBtnRect);
    bool cancelHovered = isPointInRect(mx, my, m_cancelBtnRect);

    drawButton("保存", m_saveBtnRect, saveHovered);
    drawButton("取消", m_cancelBtnRect, cancelHovered);

    SDL_RenderPresent(m_renderer);
}

void SettingsDialog::drawInputField(const std::string& value, const SDL_FRect& rect, bool isActive, bool isPassword) {
    // 背景
    SDL_SetRenderDrawColor(m_renderer, COLOR_INPUT_BG[0], COLOR_INPUT_BG[1], COLOR_INPUT_BG[2], COLOR_INPUT_BG[3]);
    SDL_RenderFillRect(m_renderer, &rect);

    // 边框
    if (isActive) {
        SDL_SetRenderDrawColor(m_renderer, COLOR_INPUT_ACTIVE[0], COLOR_INPUT_ACTIVE[1], COLOR_INPUT_ACTIVE[2], COLOR_INPUT_ACTIVE[3]);
    } else {
        SDL_SetRenderDrawColor(m_renderer, COLOR_INPUT_BORDER[0], COLOR_INPUT_BORDER[1], COLOR_INPUT_BORDER[2], COLOR_INPUT_BORDER[3]);
    }
    SDL_RenderRect(m_renderer, &rect);

    // 文本
    std::string displayText = isPassword ? maskPassword(value) : value;
    int textX = static_cast<int>(rect.x) + 5;
    int textY = static_cast<int>(rect.y) + (static_cast<int>(rect.h) - getFontHeight()) / 2;

    // 绘制选区高亮
    if (isActive && m_selectionStart >= 0 && m_selectionEnd >= 0) {
        int selStart = std::min(m_selectionStart, m_selectionEnd);
        int selEnd = std::max(m_selectionStart, m_selectionEnd);
        
        std::string beforeSel = displayText.substr(0, selStart);
        std::string selected = displayText.substr(selStart, selEnd - selStart);
        
        int selX = textX + getTextWidth(beforeSel);
        int selW = getTextWidth(selected);
        
        SDL_SetRenderDrawColor(m_renderer, COLOR_SELECTION[0], COLOR_SELECTION[1], COLOR_SELECTION[2], COLOR_SELECTION[3]);
        SDL_FRect selRect = {static_cast<float>(selX), static_cast<float>(textY),
                            static_cast<float>(selW), static_cast<float>(getFontHeight())};
        SDL_RenderFillRect(m_renderer, &selRect);
    }

    if (!displayText.empty()) {
        drawText(displayText, textX, textY, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 255);
    }

    // 光标
    if (isActive) {
        uint64_t now = SDL_GetTicks();
        if (now - m_cursorBlinkTime >= 500) {
            m_cursorVisible = !m_cursorVisible;
            m_cursorBlinkTime = now;
        }
        if (m_cursorVisible) {
            std::string beforeCursor = displayText.substr(0, m_cursorPos);
            int cursorX = textX + getTextWidth(beforeCursor);
            int cursorY = textY + 2;
            int cursorH = getFontHeight() - 4;
            SDL_SetRenderDrawColor(m_renderer, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], COLOR_TEXT[3]);
            SDL_FRect cursorRect = {static_cast<float>(cursorX), static_cast<float>(cursorY),
                                   2.0f, static_cast<float>(cursorH)};
            SDL_RenderFillRect(m_renderer, &cursorRect);
        }
    }
}

void SettingsDialog::drawButton(const std::string& text, const SDL_FRect& rect, bool isHovered) {
    if (isHovered) {
        SDL_SetRenderDrawColor(m_renderer, COLOR_BUTTON_PRIMARY_HOVER[0], COLOR_BUTTON_PRIMARY_HOVER[1], COLOR_BUTTON_PRIMARY_HOVER[2], COLOR_BUTTON_PRIMARY_HOVER[3]);
    } else {
        SDL_SetRenderDrawColor(m_renderer, COLOR_BUTTON_PRIMARY[0], COLOR_BUTTON_PRIMARY[1], COLOR_BUTTON_PRIMARY[2], COLOR_BUTTON_PRIMARY[3]);
    }
    SDL_RenderFillRect(m_renderer, &rect);

    int textWidth = getTextWidth(text);
    int textX = static_cast<int>(rect.x) + (static_cast<int>(rect.w) - textWidth) / 2;
    int textY = static_cast<int>(rect.y) + (static_cast<int>(rect.h) - getFontHeight()) / 2;
    drawText(text, textX, textY, COLOR_BUTTON_TEXT[0], COLOR_BUTTON_TEXT[1], COLOR_BUTTON_TEXT[2], 255);
}

std::string SettingsDialog::maskPassword(const std::string& password) {
    if (password.empty()) return "";
    return std::string(password.length(), '*');
}

bool SettingsDialog::isPointInRect(float x, float y, const SDL_FRect& rect) {
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

void SettingsDialog::handleEvents() {
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

                    // 关闭按钮
                    int closeX = m_windowWidth - 35;
                    int closeY = 5;
                    int closeSize = 30;
                    if (mx >= closeX && mx <= closeX + closeSize && my >= closeY && my <= closeY + closeSize) {
                        m_running = false;
                        break;
                    }

                    // 输入框点击检测
                    InputField clickedField = InputField::None;
                    if (isPointInRect(mx, my, m_baseUrlRect)) {
                        clickedField = InputField::BaseUrl;
                    } else if (isPointInRect(mx, my, m_apiKeyRect)) {
                        clickedField = InputField::ApiKey;
                    } else if (isPointInRect(mx, my, m_whisperModelRect)) {
                        clickedField = InputField::WhisperModel;
                    } else if (isPointInRect(mx, my, m_gptModelRect)) {
                        clickedField = InputField::GptModel;
                    }

                    if (clickedField != InputField::None) {
                        bool fieldChanged = (m_activeField != clickedField);
                        m_activeField = clickedField;
                        
                        // 获取当前字段文本
                        std::string* field = getActiveField();
                        std::string text = field ? *field : "";
                        
                        // 计算光标位置
                        m_cursorPos = getCursorPosFromMouseX(mx, text);
                        
                        // 开始选择
                        if (!fieldChanged && event.button.clicks == 1) {
                            m_selectionStart = m_cursorPos;
                            m_selectionEnd = m_cursorPos;
                            m_selecting = true;
                        } else {
                            m_selectionStart = -1;
                            m_selectionEnd = -1;
                        }
                        
                        m_cursorVisible = true;
                        m_cursorBlinkTime = SDL_GetTicks();
                    } else {
                        m_activeField = InputField::None;
                        m_selectionStart = -1;
                        m_selectionEnd = -1;
                    }

                    // 保存按钮
                    if (isPointInRect(mx, my, m_saveBtnRect)) {
                        if (m_saveCallback) {
                            m_saveCallback(m_settings);
                        }
                        m_running = false;
                        break;
                    }

                    // 取消按钮
                    if (isPointInRect(mx, my, m_cancelBtnRect)) {
                        m_running = false;
                        break;
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
                m_dragging = false;
                m_selecting = false;
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
                if (m_selecting && m_activeField != InputField::None) {
                    int mx = static_cast<int>(event.motion.x);
                    std::string* field = getActiveField();
                    if (field) {
                        m_cursorPos = getCursorPosFromMouseX(mx, *field);
                        m_selectionEnd = m_cursorPos;
                        m_cursorVisible = true;
                        m_cursorBlinkTime = SDL_GetTicks();
                    }
                }
                break;
                
            case SDL_EVENT_KEY_DOWN: {
                bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
                bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
                
                if (event.key.key == SDLK_ESCAPE) {
                    m_running = false;
                } else if (event.key.key == SDLK_RETURN) {
                    if (m_saveCallback) {
                        m_saveCallback(m_settings);
                    }
                    m_running = false;
                } else if (event.key.key == SDLK_BACKSPACE) {
                    deleteCharBefore();
                    m_cursorVisible = true;
                    m_cursorBlinkTime = SDL_GetTicks();
                } else if (event.key.key == SDLK_DELETE) {
                    deleteCharAfter();
                    m_cursorVisible = true;
                    m_cursorBlinkTime = SDL_GetTicks();
                } else if (event.key.key == SDLK_HOME) {
                    moveCursorHome();
                    m_cursorVisible = true;
                    m_cursorBlinkTime = SDL_GetTicks();
                } else if (event.key.key == SDLK_END) {
                    moveCursorEnd();
                    m_cursorVisible = true;
                    m_cursorBlinkTime = SDL_GetTicks();
                } else if (event.key.key == SDLK_LEFT) {
                    moveCursorLeft();
                    m_cursorVisible = true;
                    m_cursorBlinkTime = SDL_GetTicks();
                } else if (event.key.key == SDLK_RIGHT) {
                    moveCursorRight();
                    m_cursorVisible = true;
                    m_cursorBlinkTime = SDL_GetTicks();
                } else if (ctrl && event.key.key == SDLK_A) {
                    selectAll();
                    m_cursorVisible = true;
                    m_cursorBlinkTime = SDL_GetTicks();
                } else if (ctrl && event.key.key == SDLK_C) {
                    // Ctrl+C 复制
                    if (m_selectionStart >= 0) {
                        std::string* field = getActiveField();
                        if (field) {
                            int start = std::min(m_selectionStart, m_selectionEnd);
                            int end = std::max(m_selectionStart, m_selectionEnd);
                            std::string selected = field->substr(start, end - start);
                            SDL_SetClipboardText(selected.c_str());
                        }
                    }
                } else if (ctrl && event.key.key == SDLK_X) {
                    // Ctrl+X 剪切
                    if (m_selectionStart >= 0) {
                        std::string* field = getActiveField();
                        if (field) {
                            int start = std::min(m_selectionStart, m_selectionEnd);
                            int end = std::max(m_selectionStart, m_selectionEnd);
                            std::string selected = field->substr(start, end - start);
                            SDL_SetClipboardText(selected.c_str());
                            deleteSelection();
                        }
                    }
                } else if (ctrl && event.key.key == SDLK_V) {
                    // Ctrl+V 粘贴
                    if (m_activeField != InputField::None) {
                        deleteSelection();
                        char* clipboard = SDL_GetClipboardText();
                        if (clipboard) {
                            std::string text(clipboard);
                            SDL_free(clipboard);
                            std::string* field = getActiveField();
                            if (field) {
                                field->insert(m_cursorPos, text);
                                m_cursorPos += static_cast<int>(text.length());
                            }
                        }
                        m_cursorVisible = true;
                        m_cursorBlinkTime = SDL_GetTicks();
                    }
                }
                break;
            }
            
            case SDL_EVENT_TEXT_INPUT:
                if (m_activeField != InputField::None) {
                    deleteSelection();
                    std::string text = event.text.text;
                    std::string* field = getActiveField();
                    if (field) {
                        field->insert(m_cursorPos, text);
                        m_cursorPos += static_cast<int>(text.length());
                    }
                    m_cursorVisible = true;
                    m_cursorBlinkTime = SDL_GetTicks();
                }
                break;
        }
    }
}

void SettingsDialog::drawText(const std::string& text, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
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

int SettingsDialog::getTextWidth(const std::string& text) {
    if (text.empty() || !m_font) return 0;

    int w = 0;
    TTF_GetStringSize(m_font, text.c_str(), 0, &w, nullptr);
    return w;
}

int SettingsDialog::getFontHeight() {
    if (!m_font) return 0;
    return TTF_GetFontHeight(m_font);
}

} // namespace VideoPlay
