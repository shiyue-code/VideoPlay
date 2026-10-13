#ifndef SUBTITLESTYLEDIALOG_H
#define SUBTITLESTYLEDIALOG_H

#include "core/settings.h"
#include "renderer/inputfield.h"
#include "renderer/modaldialog.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace VideoPlay {

// 字幕样式设置对话框：字号、字体颜色、描边开关/宽度/颜色。
// fontFamily 渲染端暂不支持，保存时原样回传不改。
class SubtitleStyleDialog : public ModalDialog {
public:
    SubtitleStyleDialog(SDL_Window* parentWindow, TTF_Font* font, TTF_Font* titleFont);
    ~SubtitleStyleDialog() override = default;

    using SaveCallback = std::function<void(const SubtitleStyle& style)>;

    void show(const SubtitleStyle& current, SaveCallback onSave);

private:
    SubtitleStyle m_style;
    SaveCallback m_saveCallback;
    bool m_hasOutline = false;

    static constexpr int INPUT_HEIGHT = 30;
    static constexpr int ROW_GAP = 16;
    static constexpr int LABEL_W = 96;
    // 表单边距与信息面板一致
    static constexpr int PADDING = UiStyle::kPanelPadding;
    static constexpr int SWATCH_SIZE = 22;
    static constexpr int SWATCH_PITCH = 28;

    std::unique_ptr<InputField> m_fontSizeInput;
    std::unique_ptr<InputField> m_fontColorInput;
    std::unique_ptr<InputField> m_outlineWidthInput;
    std::unique_ptr<InputField> m_outlineColorInput;

    SDL_FRect m_outlineCheckRect;
    SDL_FRect m_fontPreviewRect;
    SDL_FRect m_outlinePreviewRect;
    std::vector<SDL_FRect> m_fontPresetRects;
    std::vector<SDL_FRect> m_outlinePresetRects;
    SDL_FRect m_saveBtnRect;
    SDL_FRect m_cancelBtnRect;

    void calculateLayout();
    bool onEvent(const SDL_Event& event) override;
    void renderContent() override;
    void applyAndSave();
    bool hitPresets(const std::vector<SDL_FRect>& rects, int mx, int my, int& index) const;
    void drawColorPreview(const SDL_FRect& rect, const std::string& value) const;
    void drawPresets(const std::vector<SDL_FRect>& rects, float mouseX, float mouseY) const;
    void drawCheckbox(float mouseX, float mouseY) const;
};

} // namespace VideoPlay

#endif // SUBTITLESTYLEDIALOG_H
