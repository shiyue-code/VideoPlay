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

} // namespace VideoPlay::UiStyle
