#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "renderer/inputfield.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <functional>
#include <memory>

namespace VideoPlay {

struct AISettings {
    std::string baseUrl;
    std::string apiKey;
    std::string model;
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
    int m_dragOffsetX = 0;
    int m_dragOffsetY = 0;

    int m_windowWidth = 500;
    int m_windowHeight = 320;
    static constexpr int TITLE_HEIGHT = 40;
    static constexpr int INPUT_HEIGHT = 30;
    static constexpr int PADDING = 15;

    // 输入框控件
    std::unique_ptr<InputField> m_baseUrlInput;
    std::unique_ptr<InputField> m_apiKeyInput;
    std::unique_ptr<InputField> m_modelInput;

    // 按钮区域
    SDL_FRect m_saveBtnRect;
    SDL_FRect m_cancelBtnRect;

    void calculateLayout();
    void render();
    void handleEvents();
    void drawText(const std::string& text, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void drawButton(const std::string& text, const SDL_FRect& rect, bool isHovered);
    int getTextWidth(const std::string& text);
    int getFontHeight();
    bool isPointInRect(float x, float y, const SDL_FRect& rect);
};

} // namespace VideoPlay

#endif // SETTINGSDIALOG_H
