#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "renderer/inputfield.h"
#include "renderer/modaldialog.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace VideoPlay {

struct AIProviderSettings {
    std::string baseUrl;
    std::string apiKey;
    std::string model;
};

struct AISettings {
    std::string provider;
    int analysisDetailLevel = 1;
    std::unordered_map<std::string, AIProviderSettings> providers;
};

class SettingsDialog : public ModalDialog {
public:
    SettingsDialog(SDL_Window* parentWindow, TTF_Font* font);
    ~SettingsDialog() override = default;

    using SaveCallback = std::function<void(const AISettings& settings)>;

    void show(const AISettings& currentSettings, SaveCallback onSave);

private:
    AISettings m_settings;
    SaveCallback m_saveCallback;

    bool m_apiKeyVisible = false;

    static constexpr int INPUT_HEIGHT = 30;
    static constexpr int PADDING = 15;

    std::vector<std::string> m_providerOptions;
    std::vector<std::string> m_modelOptions;
    std::vector<std::string> m_detailOptions;
    bool m_providerDropdownOpen = false;
    bool m_modelDropdownOpen = false;
    bool m_detailDropdownOpen = false;
    SDL_FRect m_providerDropdownRect;
    SDL_FRect m_modelDropdownRect;
    SDL_FRect m_detailDropdownRect;

    std::unique_ptr<InputField> m_baseUrlInput;
    std::unique_ptr<InputField> m_apiKeyInput;

    SDL_FRect m_saveBtnRect;
    SDL_FRect m_cancelBtnRect;
    SDL_FRect m_apiKeyToggleRect;

    AIProviderSettings& currentProviderSettings();
    void ensureProviderSettings();
    void saveCurrentProviderFields();
    void loadProviderFields(const std::string& provider);
    void updateModelOptions();
    std::string detailLabel() const;
    void setDetailByLabel(const std::string& label);
    bool handleDropdownClick(int mx, int my);
    void drawDropdown(const SDL_FRect& rect,
                      const std::string& value,
                      bool open,
                      float mouseX,
                      float mouseY);
    void drawDropdownOptions(const SDL_FRect& rect,
                             const std::vector<std::string>& options,
                             float mouseX,
                             float mouseY);
    bool dropdownShouldOpenUpward(const SDL_FRect& rect, size_t optionCount) const;
    SDL_FRect dropdownOptionRect(const SDL_FRect& rect, size_t index, size_t optionCount) const;
    void calculateLayout();
    bool onEvent(const SDL_Event& event) override;
    void renderContent() override;
    void drawButton(const std::string& text, const SDL_FRect& rect, bool isHovered);
};

} // namespace VideoPlay

#endif // SETTINGSDIALOG_H
