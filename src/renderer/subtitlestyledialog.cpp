#include "renderer/subtitlestyledialog.h"

#include "renderer/ui_draw.h"
#include "utils/string_utils.h"

#include <algorithm>
#include <cstdlib>

namespace VideoPlay {

namespace {

// 常用字幕颜色预设（点击直接填入对应颜色输入框）
const std::vector<std::string>& colorPresets() {
    static const std::vector<std::string> presets = {
        "#FFFFFF", "#FFFF00", "#00FF00", "#00FFFF",
        "#FF0000", "#0000FF", "#000000"
    };
    return presets;
}

bool parseIntValue(const std::string& text, int& value) {
    std::string s = trim(text);
    if (s.empty()) return false;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    value = static_cast<int>(v);
    return true;
}

} // namespace

SubtitleStyleDialog::SubtitleStyleDialog(SDL_Window* parentWindow, TTF_Font* font,
                                         TTF_Font* titleFont)
    : ModalDialog(parentWindow, font, titleFont) {
    m_windowWidth = 480;
    // 标题区 + 表单 4 行 + 间距 + 按钮区(高32，下边距20)
    m_windowHeight = titleHeight() + PADDING +
                     4 * INPUT_HEIGHT + 3 * ROW_GAP +
                     PADDING + UiStyle::kButtonHeight + (UiStyle::kPanelPadding - 4);
}

void SubtitleStyleDialog::show(const SubtitleStyle& current, SaveCallback onSave) {
    m_style = current;
    m_saveCallback = onSave;
    m_hasOutline = current.hasOutline;

    m_fontSizeInput = std::make_unique<InputField>(m_font);
    m_fontColorInput = std::make_unique<InputField>(m_font);
    m_outlineWidthInput = std::make_unique<InputField>(m_font);
    m_outlineColorInput = std::make_unique<InputField>(m_font);

    m_fontSizeInput->setValue(std::to_string(current.fontSize));
    m_fontSizeInput->setPlaceholder("24");
    m_fontColorInput->setValue(current.fontColor);
    m_fontColorInput->setPlaceholder("#FFFFFF");
    m_outlineWidthInput->setValue(std::to_string(current.outlineWidth));
    m_outlineWidthInput->setPlaceholder("2");
    m_outlineColorInput->setValue(current.outlineColor);
    m_outlineColorInput->setPlaceholder("#000000");

    calculateLayout();

    if (!openWindow("字幕样式", true)) {
        return;
    }
    runModal();
}

void SubtitleStyleDialog::calculateLayout() {
    const float fieldX = PADDING + LABEL_W;
    int y = titleHeight() + PADDING;

    // 字号
    m_fontSizeInput->setRect({fieldX, static_cast<float>(y), 96, INPUT_HEIGHT});
    y += INPUT_HEIGHT + ROW_GAP;

    // 字体颜色：输入框 + 预览色块 + 预设色板
    m_fontColorInput->setRect({fieldX, static_cast<float>(y), 96, INPUT_HEIGHT});
    m_fontPreviewRect = {fieldX + 104, static_cast<float>(y) + 1, 28, 28};
    m_fontPresetRects.clear();
    for (size_t i = 0; i < colorPresets().size(); ++i) {
        m_fontPresetRects.push_back({
            fieldX + 144 + static_cast<float>(i) * SWATCH_PITCH,
            static_cast<float>(y) + 4, SWATCH_SIZE, SWATCH_SIZE});
    }
    y += INPUT_HEIGHT + ROW_GAP;

    // 描边：复选框（含「启用描边」文字命中区）+ 宽度
    m_outlineCheckRect = {fieldX, static_cast<float>(y) + 5, 108, 20};
    m_outlineWidthInput->setRect({fieldX + 156, static_cast<float>(y), 64, INPUT_HEIGHT});
    y += INPUT_HEIGHT + ROW_GAP;

    // 描边颜色
    m_outlineColorInput->setRect({fieldX, static_cast<float>(y), 96, INPUT_HEIGHT});
    m_outlinePreviewRect = {fieldX + 104, static_cast<float>(y) + 1, 28, 28};
    m_outlinePresetRects.clear();
    for (size_t i = 0; i < colorPresets().size(); ++i) {
        m_outlinePresetRects.push_back({
            fieldX + 144 + static_cast<float>(i) * SWATCH_PITCH,
            static_cast<float>(y) + 4, SWATCH_SIZE, SWATCH_SIZE});
    }

    // 按钮区：右下角，右边距 padding，下边距 padding-4，间距 10，左取消右保存
    int btnW = UiStyle::kButtonMinWidth;
    int btnY = m_windowHeight - UiStyle::kButtonHeight - (UiStyle::kPanelPadding - 4);
    m_saveBtnRect = {static_cast<float>(m_windowWidth - UiStyle::kPanelPadding - btnW),
                     static_cast<float>(btnY), static_cast<float>(btnW),
                     static_cast<float>(UiStyle::kButtonHeight)};
    m_cancelBtnRect = {m_saveBtnRect.x - 10.0f - btnW, m_saveBtnRect.y,
                       static_cast<float>(btnW), static_cast<float>(UiStyle::kButtonHeight)};
}

void SubtitleStyleDialog::applyAndSave() {
    SubtitleStyle out = m_style;

    int size = 0;
    if (parseIntValue(m_fontSizeInput->getValue(), size)) {
        out.fontSize = std::clamp(size, 10, 96);
    }

    uint8_t r = 0, g = 0, b = 0;
    if (parseHexColor(trim(m_fontColorInput->getValue()), r, g, b)) {
        out.fontColor = formatHexColor(r, g, b);
    }

    out.hasOutline = m_hasOutline;

    int width = 0;
    if (parseIntValue(m_outlineWidthInput->getValue(), width)) {
        out.outlineWidth = std::clamp(width, 0, 6);
    }

    if (parseHexColor(trim(m_outlineColorInput->getValue()), r, g, b)) {
        out.outlineColor = formatHexColor(r, g, b);
    }

    if (m_saveCallback) {
        m_saveCallback(out);
    }
}

bool SubtitleStyleDialog::hitPresets(const std::vector<SDL_FRect>& rects,
                                     int mx, int my, int& index) const {
    for (size_t i = 0; i < rects.size(); ++i) {
        if (isPointInRect(mx, my, rects[i])) {
            index = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

bool SubtitleStyleDialog::onEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_QUIT:
            requestClose();
            return true;

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event.button.button == SDL_BUTTON_LEFT) {
                int mx = static_cast<int>(event.button.x);
                int my = static_cast<int>(event.button.y);

                if (isPointInRect(mx, my, m_saveBtnRect)) {
                    applyAndSave();
                    requestClose();
                    return true;
                }

                if (isPointInRect(mx, my, m_cancelBtnRect)) {
                    requestClose();
                    return true;
                }

                if (isPointInRect(mx, my, m_outlineCheckRect)) {
                    m_hasOutline = !m_hasOutline;
                    return true;
                }

                int presetIdx = -1;
                if (hitPresets(m_fontPresetRects, mx, my, presetIdx)) {
                    m_fontColorInput->setValue(colorPresets()[presetIdx]);
                } else if (hitPresets(m_outlinePresetRects, mx, my, presetIdx)) {
                    m_outlineColorInput->setValue(colorPresets()[presetIdx]);
                }

                // 落到输入框：框内激活、框外取消激活
                m_fontSizeInput->handleEvent(event);
                m_fontColorInput->handleEvent(event);
                m_outlineWidthInput->handleEvent(event);
                m_outlineColorInput->handleEvent(event);
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_MOTION:
            m_fontSizeInput->handleEvent(event);
            m_fontColorInput->handleEvent(event);
            m_outlineWidthInput->handleEvent(event);
            m_outlineColorInput->handleEvent(event);
            break;  // 不消费：基类还要处理拖动/释放

        case SDL_EVENT_KEY_DOWN: {
            if (event.key.key == SDLK_ESCAPE) {
                return false;  // Esc 交给基类关闭
            }

            if (event.key.key == SDLK_RETURN) {
                applyAndSave();
                requestClose();
                return true;
            }

            if (m_fontSizeInput->handleEvent(event)) return true;
            if (m_fontColorInput->handleEvent(event)) return true;
            if (m_outlineWidthInput->handleEvent(event)) return true;
            if (m_outlineColorInput->handleEvent(event)) return true;
            break;
        }

        case SDL_EVENT_TEXT_INPUT:
            m_fontSizeInput->handleEvent(event);
            m_fontColorInput->handleEvent(event);
            m_outlineWidthInput->handleEvent(event);
            m_outlineColorInput->handleEvent(event);
            return true;
    }
    return false;
}

void SubtitleStyleDialog::drawColorPreview(const SDL_FRect& rect,
                                           const std::string& value) const {
    uint8_t r = 0, g = 0, b = 0;
    if (parseHexColor(trim(value), r, g, b)) {
        UiDraw::fillRoundRect(m_renderer, rect, 5.0f, r, g, b, 255);
        SDL_SetRenderDrawColor(m_renderer, UiStyle::kDialogBorder[0],
                               UiStyle::kDialogBorder[1], UiStyle::kDialogBorder[2], 255);
    } else {
        // 非法输入：底色的输入框色 + 警示描边
        UiDraw::fillRoundRect(m_renderer, rect, 5.0f,
                              UiStyle::kInputBg[0], UiStyle::kInputBg[1],
                              UiStyle::kInputBg[2], 255);
        SDL_SetRenderDrawColor(m_renderer, 200, 90, 90, 255);
    }
    SDL_RenderRect(m_renderer, &rect);
}

void SubtitleStyleDialog::drawPresets(const std::vector<SDL_FRect>& rects,
                                      float mouseX, float mouseY) const {
    const auto& presets = colorPresets();
    for (size_t i = 0; i < rects.size() && i < presets.size(); ++i) {
        uint8_t r = 0, g = 0, b = 0;
        if (!parseHexColor(presets[i], r, g, b)) continue;
        UiDraw::fillRoundRect(m_renderer, rects[i], 4.0f, r, g, b, 255);
        bool hovered = isPointInRect(mouseX, mouseY, rects[i]);
        const uint8_t* border = hovered ? UiStyle::kAccentColor : UiStyle::kDialogBorder;
        SDL_SetRenderDrawColor(m_renderer, border[0], border[1], border[2], 255);
        SDL_RenderRect(m_renderer, &rects[i]);
    }
}

void SubtitleStyleDialog::drawCheckbox(float mouseX, float mouseY) const {
    SDL_FRect box = {m_outlineCheckRect.x, m_outlineCheckRect.y, 20, 20};
    bool hovered = isPointInRect(mouseX, mouseY, m_outlineCheckRect);
    const uint8_t* border = hovered ? UiStyle::kAccentColor : UiStyle::kDialogBorder;
    UiDraw::fillRoundRect(m_renderer, box, 4.0f, border[0], border[1], border[2], 255);
    SDL_FRect inner = {box.x + 1, box.y + 1, box.w - 2, box.h - 2};
    UiDraw::fillRoundRect(m_renderer, inner, 3.0f,
                          UiStyle::kInputBg[0], UiStyle::kInputBg[1],
                          UiStyle::kInputBg[2], 255);
    if (m_hasOutline) {
        SDL_SetRenderDrawColor(m_renderer, UiStyle::kAccentColor[0],
                               UiStyle::kAccentColor[1], UiStyle::kAccentColor[2], 255);
        SDL_RenderLine(m_renderer, box.x + 5, box.y + 10, box.x + 8.5f, box.y + 14.5f);
        SDL_RenderLine(m_renderer, box.x + 8.5f, box.y + 14.5f, box.x + 15, box.y + 5.5f);
        SDL_RenderLine(m_renderer, box.x + 5, box.y + 11, box.x + 8.5f, box.y + 15.5f);
        SDL_RenderLine(m_renderer, box.x + 8.5f, box.y + 15.5f, box.x + 15, box.y + 6.5f);
    }
}

void SubtitleStyleDialog::renderContent() {
    float mx, my;
    SDL_GetMouseState(&mx, &my);
    const uint8_t* label = UiStyle::kLabelColor;
    const float fieldX = PADDING + LABEL_W;

    int y = titleHeight() + PADDING;
    drawText("字号:", PADDING, y + 8, label[0], label[1], label[2], 255);
    m_fontSizeInput->render(m_renderer, mx, my);
    drawText("10 - 96", static_cast<int>(fieldX) + 104, y + 8,
             label[0], label[1], label[2], 150);
    y += INPUT_HEIGHT + ROW_GAP;

    drawText("颜色:", PADDING, y + 8, label[0], label[1], label[2], 255);
    m_fontColorInput->render(m_renderer, mx, my);
    drawColorPreview(m_fontPreviewRect, m_fontColorInput->getValue());
    drawPresets(m_fontPresetRects, mx, my);
    y += INPUT_HEIGHT + ROW_GAP;

    drawText("描边:", PADDING, y + 8, label[0], label[1], label[2], 255);
    drawCheckbox(mx, my);
    drawText("启用描边", static_cast<int>(fieldX) + 28, y + 8,
             UiStyle::kValueColor[0], UiStyle::kValueColor[1], UiStyle::kValueColor[2], 255);
    drawText("宽度:", static_cast<int>(fieldX) + 110, y + 8,
             label[0], label[1], label[2], 255);
    m_outlineWidthInput->render(m_renderer, mx, my);
    drawText("0 - 6", static_cast<int>(fieldX) + 228, y + 8,
             label[0], label[1], label[2], 150);
    y += INPUT_HEIGHT + ROW_GAP;

    drawText("描边颜色:", PADDING, y + 8, label[0], label[1], label[2], 255);
    m_outlineColorInput->render(m_renderer, mx, my);
    drawColorPreview(m_outlinePreviewRect, m_outlineColorInput->getValue());
    drawPresets(m_outlinePresetRects, mx, my);

    UiDraw::drawButton(m_renderer, m_font, m_cancelBtnRect, "取消",
                       isPointInRect(mx, my, m_cancelBtnRect), false, false);
    UiDraw::drawButton(m_renderer, m_font, m_saveBtnRect, "保存",
                       isPointInRect(mx, my, m_saveBtnRect), false, true);
}

} // namespace VideoPlay
