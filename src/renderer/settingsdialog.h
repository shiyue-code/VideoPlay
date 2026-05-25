#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "renderer/inputfield.h"

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
    std::unordered_map<std::string, AIProviderSettings> providers;
};

class SettingsDialog {
public:
    SettingsDialog(SDL_Window* parentWindow, TTF_Font* font);
    ~SettingsDialog();

    using SaveCallback = std::function<void(const AISettings& settings)>;

    void show(const AISettings& currentSettings, SaveCallback onSave);

private:
    SDL_Window* m_parentWindow;
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    TTF_Font* m_font;

    AISettings m_settings;
    SaveCallback m_saveCallback;

    bool m_running = false;
    bool m_dragging = false;
    bool m_apiKeyVisible = false;
    int m_dragOffsetX = 0;
    int m_dragOffsetY = 0;

    int m_windowWidth = 500;
    int m_windowHeight = 370;
    static constexpr int TITLE_HEIGHT = 32;
    static constexpr int INPUT_HEIGHT = 30;
    static constexpr int PADDING = 15;

    std::vector<std::string> m_providerOptions;
    std::vector<std::string> m_modelOptions;
    bool m_providerDropdownOpen = false;
    bool m_modelDropdownOpen = false;
    SDL_FRect m_providerDropdownRect;
    SDL_FRect m_modelDropdownRect;

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
    bool handleDropdownClick(int mx, int my);
    void drawDropdown(const SDL_FRect& rect,
                      const std::string& value,
                      bool open,
                      const std::vector<std::string>& options,
                      float mouseX,
                      float mouseY);
    void calculateLayout();
    void render();
    void handleEvents();
    void drawText(const std::string& text,
                  int x,
                  int y,
                  uint8_t r,
                  uint8_t g,
                  uint8_t b,
                  uint8_t a = 255);
    void drawButton(const std::string& text, const SDL_FRect& rect, bool isHovered);
    int getTextWidth(const std::string& text);
    int getFontHeight();
    bool isPointInRect(float x, float y, const SDL_FRect& rect);
};

} // namespace VideoPlay

#endif // SETTINGSDIALOG_H
