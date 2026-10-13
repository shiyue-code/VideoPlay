#pragma once

#include <cstdint>

// 信息面板（媒体信息/关于/消息面板共用）与独立窗口对话框的视觉参数
// 与 sdlrenderer_internal.h 的设计令牌同源：冷调深灰 + 青色强调 + 低对比描边
namespace VideoPlay::UiStyle {

// 面板容器（平面设计：无投影、无描边，纯色面）
inline constexpr int kPanelRadius = 14;
inline constexpr int kPanelPadding = 24;
inline constexpr uint8_t kPanelBg[4] = {26, 30, 38, 242};
inline constexpr uint8_t kPanelBorder[4] = {255, 255, 255, 0};
inline constexpr uint8_t kPanelShadow[4] = {0, 0, 0, 0};

// 标题与行文本
inline constexpr int kTitleFontSize = 18;
inline constexpr int kRowFontSize = 12;
inline constexpr int kRowHeight = 24;
inline constexpr uint8_t kTitleColor[4] = {240, 243, 248, 255};
inline constexpr uint8_t kLabelColor[4] = {156, 165, 179, 255};
inline constexpr uint8_t kValueColor[4] = {232, 236, 242, 255};

// 右上角关闭按钮（×）
inline constexpr int kCloseSize = 28;
inline constexpr int kCloseRadius = 6;
inline constexpr int kCloseInset = 9;
inline constexpr int kCloseHoverAlpha = 200;
inline constexpr uint8_t kCloseHoverBg[3] = {70, 76, 88};
inline constexpr uint8_t kClosePressedBg[4] = {82, 88, 100, 220};
inline constexpr uint8_t kCloseLineShade = 230;
inline constexpr uint8_t kCloseLineAlpha = 190;

// 独立窗口对话框（InputDialog/SettingsDialog）与信息面板同一视觉语言
inline constexpr uint8_t kDialogBg[4] = {26, 30, 38, 255};         // 不透明版面板底
inline constexpr uint8_t kDialogBorder[4] = {56, 61, 72, 255};     // 窗口/输入框 1px 边框
inline constexpr uint8_t kInputBg[4] = {18, 21, 27, 255};
inline constexpr uint8_t kAccentColor[4] = {0, 170, 255, 255};     // 聚焦边框（= COLOR_PRIMARY）
inline constexpr uint8_t kSelectionColor[4] = {0, 170, 255, 90};   // 输入框选区
inline constexpr uint8_t kPlaceholderColor[4] = {106, 115, 128, 255};

// 按钮：主按钮（确定/保存）与次要按钮（取消/显示隐藏）
inline constexpr int kButtonRadius = 6;
inline constexpr int kButtonHeight = 32;
inline constexpr int kButtonMinWidth = 84;
inline constexpr uint8_t kBtnPrimaryBg[4] = {0, 150, 230, 255};
inline constexpr uint8_t kBtnPrimaryHover[4] = {0, 170, 255, 255};
inline constexpr uint8_t kBtnPrimaryPressed[4] = {0, 120, 195, 255};
inline constexpr uint8_t kBtnSecondaryBg[4] = {52, 58, 70, 255};
inline constexpr uint8_t kBtnSecondaryHover[4] = {64, 71, 84, 255};
inline constexpr uint8_t kBtnSecondaryPressed[4] = {44, 49, 59, 255};

// 下拉框选项
inline constexpr uint8_t kDropdownHover[4] = {56, 63, 76, 255};

} // namespace VideoPlay::UiStyle
