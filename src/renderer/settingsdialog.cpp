#include "renderer/settingsdialog.h"
#include "utils/logger.h"

#include <algorithm>
#include <cmath>
#include <cctype>

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

namespace {
Logger& logger() {
    static auto logger = Logger::get("renderer.settings");
    return *logger;
}

std::string trimCopy(const std::string& value) {
    auto start = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c);
    });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c);
    }).base();
    return start < end ? std::string(start, end) : std::string();
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string normalizeProvider(std::string provider) {
    provider = lowerCopy(trimCopy(provider));
    if (provider.empty() || provider == "auto") {
        return "mimo";
    }
    if (provider == "google") {
        return "gemini";
    }
    if (provider == "xiaomi" || provider == "xiaomimimo") {
        return "mimo";
    }
    return provider;
}

AIProviderSettings defaultProviderSettings(const std::string& provider) {
    if (provider == "gemini") {
        return {"https://generativelanguage.googleapis.com", std::string(), "gemini-2.5-flash"};
    }
    return {"https://api.xiaomimimo.com", std::string(), "mimo-v2.5"};
}

std::vector<std::string> modelOptionsForProvider(const std::string& provider) {
    if (provider == "gemini") {
        return {"gemini-2.5-flash", "gemini-2.5-pro"};
    }
    return {"mimo-v2.5", "mimo-v2-omni"};
}

bool containsOption(const std::vector<std::string>& options, const std::string& option) {
    return std::find(options.begin(), options.end(), option) != options.end();
}

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

static constexpr uint8_t COLOR_BG[4] = {30, 30, 30, 240};
static constexpr uint8_t COLOR_TITLE_BG[4] = {40, 40, 40, 255};
static constexpr uint8_t COLOR_TITLE_TEXT[4] = {255, 255, 255, 255};
static constexpr uint8_t COLOR_LABEL[4] = {180, 180, 180, 255};
static constexpr uint8_t COLOR_INPUT_BG[4] = {50, 50, 50, 255};
static constexpr uint8_t COLOR_INPUT_BORDER[4] = {80, 80, 80, 255};
static constexpr uint8_t COLOR_BUTTON_PRIMARY[4] = {50, 120, 200, 255};
static constexpr uint8_t COLOR_BUTTON_PRIMARY_HOVER[4] = {70, 140, 220, 255};
static constexpr uint8_t COLOR_BUTTON_TEXT[4] = {255, 255, 255, 255};
static constexpr uint8_t COLOR_CLOSE_HOVER[4] = {232, 17, 35, 255};
static constexpr uint8_t COLOR_DROPDOWN_HOVER[4] = {65, 75, 90, 255};

SettingsDialog::SettingsDialog(SDL_Window* parentWindow, TTF_Font* font)
    : m_parentWindow(parentWindow), m_font(font) {
}

SettingsDialog::~SettingsDialog() {
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
}

void SettingsDialog::show(const AISettings& currentSettings, SaveCallback onSave) {
    m_settings = currentSettings;
    m_settings.provider = normalizeProvider(m_settings.provider);
    m_saveCallback = onSave;
    m_dragging = false;
    m_apiKeyVisible = false;
    m_providerDropdownOpen = false;
    m_modelDropdownOpen = false;
    m_detailDropdownOpen = false;
    m_providerOptions = {"mimo", "gemini"};
    m_detailOptions = {"简略", "标准", "详细"};
    m_settings.analysisDetailLevel = std::clamp(m_settings.analysisDetailLevel, 0, 2);

    ensureProviderSettings();

    m_baseUrlInput = std::make_unique<InputField>(m_font);
    m_apiKeyInput = std::make_unique<InputField>(m_font);

    m_baseUrlInput->setPlaceholder("https://api.xiaomimimo.com");
    m_apiKeyInput->setPassword(!m_apiKeyVisible);
    m_apiKeyInput->setPlaceholder("MiMo 或 Gemini API Key");

    updateModelOptions();
    loadProviderFields(m_settings.provider);
    calculateLayout();

    m_window = SDL_CreateWindow("AI 设置", m_windowWidth, m_windowHeight, SDL_WINDOW_BORDERLESS);
    if (!m_window) {
        logger().error("Failed to create settings dialog: " + std::string(SDL_GetError()));
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
        logger().error("Failed to create settings renderer: " + std::string(SDL_GetError()));
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

AIProviderSettings& SettingsDialog::currentProviderSettings() {
    m_settings.provider = normalizeProvider(m_settings.provider);
    auto it = m_settings.providers.find(m_settings.provider);
    if (it == m_settings.providers.end()) {
        it = m_settings.providers.emplace(m_settings.provider,
                                          defaultProviderSettings(m_settings.provider)).first;
    }
    return it->second;
}

void SettingsDialog::ensureProviderSettings() {
    for (const auto& provider : m_providerOptions) {
        if (m_settings.providers.find(provider) == m_settings.providers.end()) {
            m_settings.providers[provider] = defaultProviderSettings(provider);
        }
    }
    currentProviderSettings();
}

void SettingsDialog::saveCurrentProviderFields() {
    AIProviderSettings& providerSettings = currentProviderSettings();
    if (m_baseUrlInput) {
        providerSettings.baseUrl = m_baseUrlInput->getValue();
    }
    if (m_apiKeyInput) {
        providerSettings.apiKey = m_apiKeyInput->getValue();
    }
    if (providerSettings.model.empty()) {
        updateModelOptions();
        if (!m_modelOptions.empty()) {
            providerSettings.model = m_modelOptions.front();
        }
    }
    m_settings.analysisDetailLevel = std::clamp(m_settings.analysisDetailLevel, 0, 2);
}

void SettingsDialog::loadProviderFields(const std::string& provider) {
    m_settings.provider = normalizeProvider(provider);
    AIProviderSettings& providerSettings = currentProviderSettings();
    updateModelOptions();
    if (providerSettings.model.empty() && !m_modelOptions.empty()) {
        providerSettings.model = m_modelOptions.front();
    }
    if (m_baseUrlInput) {
        m_baseUrlInput->setValue(providerSettings.baseUrl);
        m_baseUrlInput->setActive(false);
    }
    if (m_apiKeyInput) {
        m_apiKeyInput->setValue(providerSettings.apiKey);
        m_apiKeyInput->setActive(false);
    }
}

void SettingsDialog::updateModelOptions() {
    m_modelOptions = modelOptionsForProvider(m_settings.provider);
    std::string currentModel = currentProviderSettings().model;
    if (!currentModel.empty() && !containsOption(m_modelOptions, currentModel)) {
        m_modelOptions.insert(m_modelOptions.begin(), currentModel);
    }
}

std::string SettingsDialog::detailLabel() const {
    int level = std::clamp(m_settings.analysisDetailLevel, 0, 2);
    if (level == 0) return "简略";
    if (level == 2) return "详细";
    return "标准";
}

void SettingsDialog::setDetailByLabel(const std::string& label) {
    if (label == "简略") {
        m_settings.analysisDetailLevel = 0;
    } else if (label == "详细") {
        m_settings.analysisDetailLevel = 2;
    } else {
        m_settings.analysisDetailLevel = 1;
    }
}

bool SettingsDialog::handleDropdownClick(int mx, int my) {
    auto selectProvider = [&](const std::string& provider) {
        saveCurrentProviderFields();
        m_settings.provider = provider;
        m_providerDropdownOpen = false;
        m_modelDropdownOpen = false;
        m_detailDropdownOpen = false;
        loadProviderFields(provider);
    };

    if (isPointInRect(mx, my, m_providerDropdownRect)) {
        m_providerDropdownOpen = !m_providerDropdownOpen;
        m_modelDropdownOpen = false;
        m_detailDropdownOpen = false;
        if (m_baseUrlInput) m_baseUrlInput->setActive(false);
        if (m_apiKeyInput) m_apiKeyInput->setActive(false);
        return true;
    }

    if (m_providerDropdownOpen) {
        for (size_t i = 0; i < m_providerOptions.size(); ++i) {
            SDL_FRect optionRect = m_providerDropdownRect;
            optionRect.y += optionRect.h * static_cast<float>(i + 1);
            if (isPointInRect(mx, my, optionRect)) {
                selectProvider(m_providerOptions[i]);
                return true;
            }
        }
        m_providerDropdownOpen = false;
    }

    if (isPointInRect(mx, my, m_modelDropdownRect)) {
        updateModelOptions();
        m_modelDropdownOpen = !m_modelDropdownOpen;
        m_providerDropdownOpen = false;
        m_detailDropdownOpen = false;
        if (m_baseUrlInput) m_baseUrlInput->setActive(false);
        if (m_apiKeyInput) m_apiKeyInput->setActive(false);
        return true;
    }

    if (m_modelDropdownOpen) {
        for (size_t i = 0; i < m_modelOptions.size(); ++i) {
            SDL_FRect optionRect = m_modelDropdownRect;
            optionRect.y += optionRect.h * static_cast<float>(i + 1);
            if (isPointInRect(mx, my, optionRect)) {
                currentProviderSettings().model = m_modelOptions[i];
                m_modelDropdownOpen = false;
                return true;
            }
        }
        m_modelDropdownOpen = false;
    }

    if (isPointInRect(mx, my, m_detailDropdownRect)) {
        m_detailDropdownOpen = !m_detailDropdownOpen;
        m_providerDropdownOpen = false;
        m_modelDropdownOpen = false;
        if (m_baseUrlInput) m_baseUrlInput->setActive(false);
        if (m_apiKeyInput) m_apiKeyInput->setActive(false);
        return true;
    }

    if (m_detailDropdownOpen) {
        for (size_t i = 0; i < m_detailOptions.size(); ++i) {
            SDL_FRect optionRect = m_detailDropdownRect;
            optionRect.y += optionRect.h * static_cast<float>(i + 1);
            if (isPointInRect(mx, my, optionRect)) {
                setDetailByLabel(m_detailOptions[i]);
                m_detailDropdownOpen = false;
                return true;
            }
        }
        m_detailDropdownOpen = false;
    }

    return false;
}

void SettingsDialog::calculateLayout() {
    int y = TITLE_HEIGHT + PADDING;

    m_providerDropdownRect = {PADDING + 120.0f, static_cast<float>(y),
                              static_cast<float>(m_windowWidth - PADDING * 2 - 120), INPUT_HEIGHT};
    y += INPUT_HEIGHT + PADDING;

    m_baseUrlInput->setRect({PADDING + 120.0f, static_cast<float>(y),
                            static_cast<float>(m_windowWidth - PADDING * 2 - 120), INPUT_HEIGHT});
    y += INPUT_HEIGHT + PADDING;

    int toggleWidth = 54;
    int toggleGap = 8;
    m_apiKeyInput->setRect({PADDING + 120.0f, static_cast<float>(y),
                           static_cast<float>(m_windowWidth - PADDING * 2 - 120 - toggleWidth - toggleGap), INPUT_HEIGHT});
    m_apiKeyToggleRect = {
        static_cast<float>(m_windowWidth - PADDING - toggleWidth),
        static_cast<float>(y),
        static_cast<float>(toggleWidth),
        static_cast<float>(INPUT_HEIGHT)
    };
    y += INPUT_HEIGHT + PADDING;

    m_modelDropdownRect = {PADDING + 120.0f, static_cast<float>(y),
                           static_cast<float>(m_windowWidth - PADDING * 2 - 120), INPUT_HEIGHT};
    y += INPUT_HEIGHT + PADDING;

    m_detailDropdownRect = {PADDING + 120.0f, static_cast<float>(y),
                            static_cast<float>(m_windowWidth - PADDING * 2 - 120), INPUT_HEIGHT};

    int btnWidth = 80;
    int btnHeight = 30;
    int btnY = m_windowHeight - btnHeight - PADDING;
    m_saveBtnRect = {static_cast<float>(m_windowWidth - btnWidth * 2 - PADDING * 2),
                     static_cast<float>(btnY), static_cast<float>(btnWidth), static_cast<float>(btnHeight)};
    m_cancelBtnRect = {static_cast<float>(m_windowWidth - btnWidth - PADDING),
                       static_cast<float>(btnY), static_cast<float>(btnWidth), static_cast<float>(btnHeight)};
}

void SettingsDialog::render() {
    if (!m_renderer) return;

    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
    SDL_RenderClear(m_renderer);

    SDL_FRect bgRect = {0, 0, static_cast<float>(m_windowWidth), static_cast<float>(m_windowHeight)};
    SDL_SetRenderDrawColor(m_renderer, COLOR_BG[0], COLOR_BG[1], COLOR_BG[2], COLOR_BG[3]);
    SDL_RenderFillRect(m_renderer, &bgRect);

    SDL_FRect titleBar = {0, 0, static_cast<float>(m_windowWidth), static_cast<float>(TITLE_HEIGHT)};
    SDL_SetRenderDrawColor(m_renderer, COLOR_TITLE_BG[0], COLOR_TITLE_BG[1], COLOR_TITLE_BG[2], COLOR_TITLE_BG[3]);
    SDL_RenderFillRect(m_renderer, &titleBar);

    int titleTextY = (TITLE_HEIGHT - getFontHeight()) / 2;
    drawText("AI 设置", 15, titleTextY, COLOR_TITLE_TEXT[0], COLOR_TITLE_TEXT[1], COLOR_TITLE_TEXT[2], 255);

    constexpr int closeWidth = 46;
    constexpr int closeIconSize = 10;
    int closeX = m_windowWidth - closeWidth;
    int closeY = 0;

    float mx, my;
    SDL_GetMouseState(&mx, &my);
    bool closeHovered = (mx >= closeX && mx <= closeX + closeWidth && my >= closeY && my <= closeY + TITLE_HEIGHT);

    if (closeHovered) {
        SDL_SetRenderDrawColor(m_renderer, COLOR_CLOSE_HOVER[0], COLOR_CLOSE_HOVER[1],
                               COLOR_CLOSE_HOVER[2], COLOR_CLOSE_HOVER[3]);
        SDL_FRect closeBtn = {static_cast<float>(closeX), static_cast<float>(closeY),
                              static_cast<float>(closeWidth), static_cast<float>(TITLE_HEIGHT)};
        SDL_RenderFillRect(m_renderer, &closeBtn);
    }

    SDL_SetRenderDrawColor(m_renderer, COLOR_BUTTON_TEXT[0], COLOR_BUTTON_TEXT[1],
                           COLOR_BUTTON_TEXT[2], COLOR_BUTTON_TEXT[3]);
    int iconX = closeX + (closeWidth - closeIconSize) / 2;
    int iconY = (TITLE_HEIGHT - closeIconSize) / 2;
    SDL_RenderLine(m_renderer, iconX, iconY, iconX + closeIconSize, iconY + closeIconSize);
    SDL_RenderLine(m_renderer, iconX + closeIconSize, iconY, iconX, iconY + closeIconSize);

    int formY = TITLE_HEIGHT + PADDING;
    drawText("服务商:", PADDING, formY + 8, COLOR_LABEL[0], COLOR_LABEL[1], COLOR_LABEL[2], 255);
    formY += INPUT_HEIGHT + PADDING;

    drawText("API 地址:", PADDING, formY + 8, COLOR_LABEL[0], COLOR_LABEL[1], COLOR_LABEL[2], 255);
    m_baseUrlInput->render(m_renderer, mx, my);
    formY += INPUT_HEIGHT + PADDING;

    drawText("API Key:", PADDING, formY + 8, COLOR_LABEL[0], COLOR_LABEL[1], COLOR_LABEL[2], 255);
    m_apiKeyInput->render(m_renderer, mx, my);
    drawButton(m_apiKeyVisible ? "隐藏" : "显示", m_apiKeyToggleRect, isPointInRect(mx, my, m_apiKeyToggleRect));
    formY += INPUT_HEIGHT + PADDING;

    drawText("模型:", PADDING, formY + 8, COLOR_LABEL[0], COLOR_LABEL[1], COLOR_LABEL[2], 255);
    formY += INPUT_HEIGHT + PADDING;
    drawText("分析详细度:", PADDING, formY + 8, COLOR_LABEL[0], COLOR_LABEL[1], COLOR_LABEL[2], 255);

    drawText("切换服务商会保留各自的地址、Key 和模型", PADDING + 120,
             formY + INPUT_HEIGHT + 4, COLOR_LABEL[0], COLOR_LABEL[1], COLOR_LABEL[2], 150);

    bool saveHovered = isPointInRect(mx, my, m_saveBtnRect);
    bool cancelHovered = isPointInRect(mx, my, m_cancelBtnRect);
    drawButton("保存", m_saveBtnRect, saveHovered);
    drawButton("取消", m_cancelBtnRect, cancelHovered);

    updateModelOptions();
    drawDropdown(m_providerDropdownRect, m_settings.provider, m_providerDropdownOpen,
                 m_providerOptions, mx, my);
    drawDropdown(m_modelDropdownRect, currentProviderSettings().model, m_modelDropdownOpen,
                 m_modelOptions, mx, my);
    drawDropdown(m_detailDropdownRect, detailLabel(), m_detailDropdownOpen,
                 m_detailOptions, mx, my);

    SDL_RenderPresent(m_renderer);
}

void SettingsDialog::handleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                m_running = false;
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int mx = static_cast<int>(event.button.x);
                    int my = static_cast<int>(event.button.y);

                    constexpr int closeWidth = 46;
                    int closeX = m_windowWidth - closeWidth;
                    if (mx >= closeX && mx <= closeX + closeWidth && my >= 0 && my <= TITLE_HEIGHT) {
                        m_running = false;
                        break;
                    }

                    if (isPointInRect(mx, my, m_saveBtnRect)) {
                        saveCurrentProviderFields();
                        if (m_saveCallback) {
                            m_saveCallback(m_settings);
                        }
                        m_running = false;
                        break;
                    }

                    if (isPointInRect(mx, my, m_cancelBtnRect)) {
                        m_running = false;
                        break;
                    }

                    if (handleDropdownClick(mx, my)) {
                        break;
                    }

                    if (isPointInRect(mx, my, m_apiKeyToggleRect)) {
                        m_providerDropdownOpen = false;
                        m_modelDropdownOpen = false;
                        m_detailDropdownOpen = false;
                        m_apiKeyVisible = !m_apiKeyVisible;
                        m_apiKeyInput->setPassword(!m_apiKeyVisible);
                        m_apiKeyInput->setActive(true);
                        break;
                    }

                    m_providerDropdownOpen = false;
                    m_modelDropdownOpen = false;
                    m_detailDropdownOpen = false;

                    m_baseUrlInput->handleEvent(event);
                    m_apiKeyInput->handleEvent(event);

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
            }

            case SDL_EVENT_MOUSE_BUTTON_UP:
                m_dragging = false;
                m_baseUrlInput->handleEvent(event);
                m_apiKeyInput->handleEvent(event);
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
                m_baseUrlInput->handleEvent(event);
                m_apiKeyInput->handleEvent(event);
                break;

            case SDL_EVENT_KEY_DOWN: {
                if (event.key.key == SDLK_ESCAPE) {
                    if (m_providerDropdownOpen || m_modelDropdownOpen || m_detailDropdownOpen) {
                        m_providerDropdownOpen = false;
                        m_modelDropdownOpen = false;
                        m_detailDropdownOpen = false;
                    } else {
                        m_running = false;
                    }
                    break;
                }

                if (event.key.key == SDLK_RETURN) {
                    saveCurrentProviderFields();
                    if (m_saveCallback) {
                        m_saveCallback(m_settings);
                    }
                    m_running = false;
                    break;
                }

                if (m_baseUrlInput->handleEvent(event)) break;
                if (m_apiKeyInput->handleEvent(event)) break;
                break;
            }

            case SDL_EVENT_TEXT_INPUT:
                m_baseUrlInput->handleEvent(event);
                m_apiKeyInput->handleEvent(event);
                break;
        }
    }
}

void SettingsDialog::drawDropdown(const SDL_FRect& rect,
                                  const std::string& value,
                                  bool open,
                                  const std::vector<std::string>& options,
                                  float mouseX,
                                  float mouseY) {
    fillRoundRect(m_renderer, rect, 7,
                  COLOR_INPUT_BORDER[0], COLOR_INPUT_BORDER[1],
                  COLOR_INPUT_BORDER[2], COLOR_INPUT_BORDER[3]);
    SDL_FRect innerRect = {rect.x + 1.0f, rect.y + 1.0f,
                           std::max(0.0f, rect.w - 2.0f),
                           std::max(0.0f, rect.h - 2.0f)};
    fillRoundRect(m_renderer, innerRect, 6,
                  COLOR_INPUT_BG[0], COLOR_INPUT_BG[1], COLOR_INPUT_BG[2], COLOR_INPUT_BG[3]);

    int textY = static_cast<int>(rect.y) + (static_cast<int>(rect.h) - getFontHeight()) / 2;
    drawText(value, static_cast<int>(rect.x) + 8, textY,
             COLOR_BUTTON_TEXT[0], COLOR_BUTTON_TEXT[1], COLOR_BUTTON_TEXT[2], 255);

    int arrowX = static_cast<int>(rect.x + rect.w - 20.0f);
    int arrowY = static_cast<int>(rect.y + rect.h / 2.0f) - 2;
    SDL_SetRenderDrawColor(m_renderer, COLOR_BUTTON_TEXT[0], COLOR_BUTTON_TEXT[1],
                           COLOR_BUTTON_TEXT[2], COLOR_BUTTON_TEXT[3]);
    SDL_RenderLine(m_renderer, arrowX, arrowY, arrowX + 5, arrowY + 5);
    SDL_RenderLine(m_renderer, arrowX + 10, arrowY, arrowX + 5, arrowY + 5);

    if (!open) {
        return;
    }

    for (size_t i = 0; i < options.size(); ++i) {
        SDL_FRect optionRect = rect;
        optionRect.y += rect.h * static_cast<float>(i + 1);
        bool hovered = isPointInRect(mouseX, mouseY, optionRect);
        const uint8_t* bg = hovered ? COLOR_DROPDOWN_HOVER : COLOR_INPUT_BG;
        fillRoundRect(m_renderer, optionRect, 5, bg[0], bg[1], bg[2], bg[3]);

        SDL_SetRenderDrawColor(m_renderer, COLOR_INPUT_BORDER[0], COLOR_INPUT_BORDER[1],
                               COLOR_INPUT_BORDER[2], COLOR_INPUT_BORDER[3]);
        SDL_RenderRect(m_renderer, &optionRect);

        int optionTextY = static_cast<int>(optionRect.y) +
                          (static_cast<int>(optionRect.h) - getFontHeight()) / 2;
        drawText(options[i], static_cast<int>(optionRect.x) + 8, optionTextY,
                 COLOR_BUTTON_TEXT[0], COLOR_BUTTON_TEXT[1], COLOR_BUTTON_TEXT[2], 255);
    }
}

void SettingsDialog::drawText(const std::string& text, int x, int y,
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

void SettingsDialog::drawButton(const std::string& text, const SDL_FRect& rect, bool isHovered) {
    if (isHovered) {
        fillRoundRect(m_renderer, rect, 7,
                      COLOR_BUTTON_PRIMARY_HOVER[0], COLOR_BUTTON_PRIMARY_HOVER[1],
                      COLOR_BUTTON_PRIMARY_HOVER[2], COLOR_BUTTON_PRIMARY_HOVER[3]);
    } else {
        fillRoundRect(m_renderer, rect, 7,
                      COLOR_BUTTON_PRIMARY[0], COLOR_BUTTON_PRIMARY[1],
                      COLOR_BUTTON_PRIMARY[2], COLOR_BUTTON_PRIMARY[3]);
    }

    int textWidth = getTextWidth(text);
    int textX = static_cast<int>(rect.x) + (static_cast<int>(rect.w) - textWidth) / 2;
    int textY = static_cast<int>(rect.y) + (static_cast<int>(rect.h) - getFontHeight()) / 2;
    drawText(text, textX, textY, COLOR_BUTTON_TEXT[0], COLOR_BUTTON_TEXT[1], COLOR_BUTTON_TEXT[2], 255);
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

bool SettingsDialog::isPointInRect(float x, float y, const SDL_FRect& rect) {
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

} // namespace VideoPlay
