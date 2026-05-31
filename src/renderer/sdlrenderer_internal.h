#pragma once

#include <cstdint>
#include <string>

namespace VideoPlay {

// 颜色定义 (RGBA)
inline constexpr uint8_t COLOR_BG[] = { 20, 20, 20, 255 };
inline constexpr uint8_t COLOR_CONTROL_BG[] = { 32, 34, 38, 175 };
inline constexpr uint8_t COLOR_BUTTON_BG[] = { 60, 60, 60, 0 };
inline constexpr uint8_t COLOR_BUTTON_BG_HOVER[] = { 255, 255, 255, 45 };
inline constexpr uint8_t COLOR_BUTTON_BG_PRESSED[] = { 255, 255, 255, 75 };
inline constexpr uint8_t COLOR_PROGRESS_BG[] = { 255, 255, 255, 60 };
inline constexpr uint8_t COLOR_PROGRESS_FILL[] = { 0, 170, 255, 255 };
inline constexpr uint8_t COLOR_PROGRESS_HOVER[] = { 80, 210, 255, 255 };
inline constexpr uint8_t COLOR_BUTTON[] = { 220, 220, 220, 255 };
inline constexpr uint8_t COLOR_BUTTON_HOVER[] = { 255, 255, 255, 255 };
inline constexpr uint8_t COLOR_TEXT[] = { 245, 245, 245, 255 };
inline constexpr uint8_t COLOR_MENU_BG[] = { 45, 45, 45, 255 };
inline constexpr uint8_t COLOR_MENU_HOVER[] = { 60, 60, 60, 255 };
inline constexpr uint8_t COLOR_MENU_ACTIVE[] = { 0, 120, 200, 255 };

// 速度选项
inline constexpr double SPEED_OPTIONS[] = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 4.0 };
inline constexpr int SPEED_COUNT = sizeof(SPEED_OPTIONS) / sizeof(SPEED_OPTIONS[0]);

} // namespace VideoPlay
