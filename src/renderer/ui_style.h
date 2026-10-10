#pragma once

#include <cstdint>

// 信息面板（媒体信息/关于/消息面板共用）的视觉参数
namespace VideoPlay::UiStyle {

// 面板容器
inline constexpr int kPanelRadius = 12;
inline constexpr int kPanelPadding = 24;
inline constexpr uint8_t kPanelBg[4] = {24, 26, 32, 238};
inline constexpr uint8_t kPanelBorder[4] = {255, 255, 255, 60};
inline constexpr uint8_t kPanelShadow[4] = {0, 0, 0, 100};

// 标题与行文本
inline constexpr int kTitleFontSize = 18;
inline constexpr int kRowFontSize = 12;
inline constexpr int kRowHeight = 24;
inline constexpr uint8_t kTitleColor[4] = {255, 255, 255, 255};
inline constexpr uint8_t kLabelColor[4] = {160, 168, 178, 255};
inline constexpr uint8_t kValueColor[4] = {235, 238, 242, 255};

// 右上角关闭按钮（×）
inline constexpr int kCloseSize = 28;
inline constexpr int kCloseRadius = 6;
inline constexpr int kCloseInset = 9;
inline constexpr int kCloseHoverAlpha = 200;
inline constexpr uint8_t kCloseHoverBg[3] = {70, 75, 85};
inline constexpr uint8_t kClosePressedBg[4] = {80, 85, 95, 220};
inline constexpr uint8_t kCloseLineShade = 230;
inline constexpr uint8_t kCloseLineAlpha = 190;

// 独立窗口对话框（InputDialog/SettingsDialog）与信息面板同一视觉语言
inline constexpr uint8_t kDialogBg[4] = {24, 26, 32, 255};       // 不透明版面板底
inline constexpr uint8_t kDialogBorder[4] = {60, 63, 70, 255};   // 窗口/输入框 1px 边框
inline constexpr uint8_t kInputBg[4] = {16, 18, 22, 255};
inline constexpr uint8_t kAccentColor[4] = {0, 170, 255, 255};   // 聚焦边框（= COLOR_PRIMARY）
inline constexpr uint8_t kSelectionColor[4] = {0, 170, 255, 90}; // 输入框选区
inline constexpr uint8_t kPlaceholderColor[4] = {110, 116, 126, 255};

// 按钮：主按钮（确定/保存）与次要按钮（取消/显示隐藏）
inline constexpr int kButtonRadius = 6;
inline constexpr int kButtonHeight = 32;
inline constexpr int kButtonMinWidth = 84;
inline constexpr uint8_t kBtnPrimaryBg[4] = {0, 120, 200, 255};
inline constexpr uint8_t kBtnPrimaryHover[4] = {0, 140, 225, 255};
inline constexpr uint8_t kBtnPrimaryPressed[4] = {0, 100, 170, 255};
inline constexpr uint8_t kBtnSecondaryBg[4] = {52, 55, 62, 255};
inline constexpr uint8_t kBtnSecondaryHover[4] = {66, 70, 78, 255};
inline constexpr uint8_t kBtnSecondaryPressed[4] = {44, 47, 53, 255};

// 下拉框选项
inline constexpr uint8_t kDropdownHover[4] = {44, 48, 56, 255};

} // namespace VideoPlay::UiStyle
