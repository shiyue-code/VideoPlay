#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <functional>

namespace VideoPlay {

struct AISettings {
    std::string baseUrl;
    std::string apiKey;
    std::string whisperModel;
    std::string gptModel;
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
    int m_windowHeight = 380;
    static constexpr int TITLE_HEIGHT = 40;
    static constexpr int INPUT_HEIGHT = 30;
    static constexpr int LABEL_HEIGHT = 20;
    static constexpr int PADDING = 15;

    // 输入框状态
    enum class InputField {
        None,
        BaseUrl,
        ApiKey,
        WhisperModel,
        GptModel
    };
    InputField m_activeField = InputField::None;

    // 光标和选择状态
    int m_cursorPos = 0;           // 光标位置（字符索引）
    int m_selectionStart = -1;     // 选择起始位置（-1 表示无选择）
    int m_selectionEnd = -1;       // 选择结束位置
    bool m_selecting = false;      // 是否正在鼠标拖动选择

    // 输入框矩形区域
    SDL_FRect m_baseUrlRect;
    SDL_FRect m_apiKeyRect;
    SDL_FRect m_whisperModelRect;
    SDL_FRect m_gptModelRect;
    SDL_FRect m_saveBtnRect;
    SDL_FRect m_cancelBtnRect;

    void render();
    void handleEvents();
    void calculateLayout();
    void drawText(const std::string& text, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void drawInputField(const std::string& value, const SDL_FRect& rect, bool isActive, bool isPassword = false);
    void drawButton(const std::string& text, const SDL_FRect& rect, bool isHovered);
    int getTextWidth(const std::string& text);
    int getFontHeight();
    bool isPointInRect(float x, float y, const SDL_FRect& rect);
    std::string maskPassword(const std::string& password);
    
    // 文本编辑辅助函数
    std::string* getActiveField();
    std::string getDisplayText(bool isPassword);
    int getCursorPosFromMouseX(int mouseX, const std::string& text);
    void deleteSelection();
    void selectAll();
    void moveCursorHome();
    void moveCursorEnd();
    void moveCursorLeft();
    void moveCursorRight();
    void deleteCharBefore();
    void deleteCharAfter();
    
    uint64_t m_cursorBlinkTime = 0;
    bool m_cursorVisible = true;
};

} // namespace VideoPlay

#endif // SETTINGSDIALOG_H
