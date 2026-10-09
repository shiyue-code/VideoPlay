#include "renderer/settingsdialog.h"

#include "utils/string_utils.h"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace VideoPlay {

namespace {
std::string normalizeProvider(std::string provider) {
    provider = toLower(trim(provider));
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

static constexpr uint8_t COLOR_LABEL[4] = {180, 180, 180, 255};
static constexpr uint8_t COLOR_INPUT_BG[4] = {50, 50, 50, 255};
static constexpr uint8_t COLOR_INPUT_BORDER[4] = {80, 80, 80, 255};
static constexpr uint8_t COLOR_BUTTON_PRIMARY[4] = {50, 120, 200, 255};
static constexpr uint8_t COLOR_BUTTON_PRIMARY_HOVER[4] = {70, 140, 220, 255};
static constexpr uint8_t COLOR_BUTTON_TEXT[4] = {255, 255, 255, 255};
static constexpr uint8_t COLOR_DROPDOWN_HOVER[4] = {65, 75, 90, 255};

SettingsDialog::SettingsDialog(SDL_Window* parentWindow, TTF_Font* font)
    : ModalDialog(parentWindow, font) {
    m_windowWidth = 500;
    m_windowHeight = 420;
}

void SettingsDialog::show(const AISettings& currentSettings, SaveCallback onSave) {
    m_settings = currentSettings;
    m_settings.provider = normalizeProvider(m_settings.provider);
    m_saveCallback = onSave;
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

    if (!openWindow("AI 设置", true)) {
        return;
    }
    runModal();
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
            SDL_FRect optionRect = dropdownOptionRect(m_providerDropdownRect, i, m_providerOptions.size());
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
            SDL_FRect optionRect = dropdownOptionRect(m_modelDropdownRect, i, m_modelOptions.size());
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
            SDL_FRect optionRect = dropdownOptionRect(m_detailDropdownRect, i, m_detailOptions.size());
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
    int y = kTitleHeight + PADDING;

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

void SettingsDialog::renderContent() {
    float mx, my;
    SDL_GetMouseState(&mx, &my);

    int formY = kTitleHeight + PADDING;
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
    drawDropdown(m_providerDropdownRect, m_settings.provider, m_providerDropdownOpen, mx, my);
    drawDropdown(m_modelDropdownRect, currentProviderSettings().model, m_modelDropdownOpen, mx, my);
    drawDropdown(m_detailDropdownRect, detailLabel(), m_detailDropdownOpen, mx, my);

    // 下拉选项最后绘制，确保在其它控件之上
    if (m_providerDropdownOpen) {
        drawDropdownOptions(m_providerDropdownRect, m_providerOptions, mx, my);
    } else if (m_modelDropdownOpen) {
        drawDropdownOptions(m_modelDropdownRect, m_modelOptions, mx, my);
    } else if (m_detailDropdownOpen) {
        drawDropdownOptions(m_detailDropdownRect, m_detailOptions, mx, my);
    }
}

bool SettingsDialog::onEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_QUIT:
            requestClose();
            return true;

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event.button.button == SDL_BUTTON_LEFT) {
                int mx = static_cast<int>(event.button.x);
                int my = static_cast<int>(event.button.y);

                if (isPointInRect(mx, my, m_saveBtnRect)) {
                    saveCurrentProviderFields();
                    if (m_saveCallback) {
                        m_saveCallback(m_settings);
                    }
                    requestClose();
                    return true;
                }

                if (isPointInRect(mx, my, m_cancelBtnRect)) {
                    requestClose();
                    return true;
                }

                if (handleDropdownClick(mx, my)) {
                    return true;
                }

                if (isPointInRect(mx, my, m_apiKeyToggleRect)) {
                    m_providerDropdownOpen = false;
                    m_modelDropdownOpen = false;
                    m_detailDropdownOpen = false;
                    m_apiKeyVisible = !m_apiKeyVisible;
                    m_apiKeyInput->setPassword(!m_apiKeyVisible);
                    m_apiKeyInput->setActive(true);
                    return true;
                }

                m_providerDropdownOpen = false;
                m_modelDropdownOpen = false;
                m_detailDropdownOpen = false;

                m_baseUrlInput->handleEvent(event);
                m_apiKeyInput->handleEvent(event);
                // 不消费：标题栏拖动/关闭按钮交给基类处理
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP:
            m_baseUrlInput->handleEvent(event);
            m_apiKeyInput->handleEvent(event);
            break;  // 不消费：基类还要清除拖动状态

        case SDL_EVENT_MOUSE_MOTION:
            m_baseUrlInput->handleEvent(event);
            m_apiKeyInput->handleEvent(event);
            break;  // 不消费：基类还要处理标题栏拖动

        case SDL_EVENT_KEY_DOWN: {
            if (event.key.key == SDLK_ESCAPE) {
                if (m_providerDropdownOpen || m_modelDropdownOpen || m_detailDropdownOpen) {
                    m_providerDropdownOpen = false;
                    m_modelDropdownOpen = false;
                    m_detailDropdownOpen = false;
                    return true;
                }
                return false;  // 无下拉框时 Esc 交给基类关闭
            }

            if (event.key.key == SDLK_RETURN) {
                saveCurrentProviderFields();
                if (m_saveCallback) {
                    m_saveCallback(m_settings);
                }
                requestClose();
                return true;
            }

            if (m_baseUrlInput->handleEvent(event)) return true;
            if (m_apiKeyInput->handleEvent(event)) return true;
            break;
        }

        case SDL_EVENT_TEXT_INPUT:
            m_baseUrlInput->handleEvent(event);
            m_apiKeyInput->handleEvent(event);
            return true;
    }
    return false;
}

void SettingsDialog::drawDropdown(const SDL_FRect& rect,
                                  const std::string& value,
                                  bool open,
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
    if (open) {
        // 展开时箭头朝上
        SDL_RenderLine(m_renderer, arrowX, arrowY + 5, arrowX + 5, arrowY);
        SDL_RenderLine(m_renderer, arrowX + 10, arrowY + 5, arrowX + 5, arrowY);
    } else {
        SDL_RenderLine(m_renderer, arrowX, arrowY, arrowX + 5, arrowY + 5);
        SDL_RenderLine(m_renderer, arrowX + 10, arrowY, arrowX + 5, arrowY + 5);
    }
}

bool SettingsDialog::dropdownShouldOpenUpward(const SDL_FRect& rect, size_t optionCount) const {
    int listBottom = static_cast<int>(rect.y + rect.h * static_cast<float>(optionCount + 1));
    return listBottom > m_windowHeight - 20;
}

SDL_FRect SettingsDialog::dropdownOptionRect(const SDL_FRect& rect,
                                             size_t index,
                                             size_t optionCount) const {
    SDL_FRect optionRect = rect;
    if (dropdownShouldOpenUpward(rect, optionCount)) {
        optionRect.y = rect.y - rect.h * static_cast<float>(optionCount - index);
    } else {
        optionRect.y = rect.y + rect.h * static_cast<float>(index + 1);
    }
    return optionRect;
}

void SettingsDialog::drawDropdownOptions(const SDL_FRect& rect,
                                         const std::vector<std::string>& options,
                                         float mouseX,
                                         float mouseY) {
    for (size_t i = 0; i < options.size(); ++i) {
        SDL_FRect optionRect = dropdownOptionRect(rect, i, options.size());
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

} // namespace VideoPlay
