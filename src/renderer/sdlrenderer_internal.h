#pragma once

#include <cstdint>
#include <string>

namespace VideoPlay {

// ============================================================================
// 设计令牌：现代化深色主题（Fluent 风格）
// 冷调深灰底 + 单一青色强调色 + 低对比白描边 + 白色叠加态
// 分三层表面：BG(应用底) < MENU_BG(弹层面) < CONTROL_BG(悬浮玻璃面)
// ============================================================================

// 颜色定义 (RGBA)
inline constexpr uint8_t COLOR_BG[] = { 13, 15, 19, 255 };          // 应用底色
inline constexpr uint8_t COLOR_CONTROL_BG[] = { 22, 25, 32, 235 };  // 悬浮面：控制栏/侧栏
inline constexpr uint8_t COLOR_BUTTON_BG[] = { 60, 60, 60, 0 };     // 图标按钮常态（透明）
inline constexpr uint8_t COLOR_BUTTON_BG_HOVER[] = { 255, 255, 255, 26 };
inline constexpr uint8_t COLOR_BUTTON_BG_PRESSED[] = { 255, 255, 255, 50 };
inline constexpr uint8_t COLOR_PROGRESS_BG[] = { 255, 255, 255, 36 };
inline constexpr uint8_t COLOR_PROGRESS_FILL[] = { 0, 170, 255, 255 };   // 强调色
inline constexpr uint8_t COLOR_PRIMARY[] = { 0, 170, 255, 255 };
inline constexpr uint8_t COLOR_PROGRESS_HOVER[] = { 80, 210, 255, 255 }; // 强调色（亮）
inline constexpr uint8_t COLOR_BUTTON[] = { 198, 205, 216, 255 };        // 图标常态
inline constexpr uint8_t COLOR_BUTTON_HOVER[] = { 255, 255, 255, 255 };
inline constexpr uint8_t COLOR_TEXT[] = { 240, 243, 248, 255 };          // 主文本
inline constexpr uint8_t COLOR_TEXT_DIM[] = { 156, 165, 179, 255 };      // 次文本
inline constexpr uint8_t COLOR_TEXT_FAINT[] = { 106, 115, 128, 255 };    // 禁用/占位
inline constexpr uint8_t COLOR_MENU_BG[] = { 28, 32, 40, 255 };          // 弹层面：菜单/面板
inline constexpr uint8_t COLOR_MENU_HOVER[] = { 58, 64, 76, 255 };       // 条目悬浮底
inline constexpr uint8_t COLOR_MENU_ACTIVE[] = { 0, 170, 255, 255 };     // 选中态（强调色）
inline constexpr uint8_t COLOR_BORDER_SUBTLE[] = { 255, 255, 255, 28 };  // 1px 描边
inline constexpr uint8_t COLOR_SURFACE_HIGH[] = { 40, 46, 57, 255 };     // 抬升面：卡片/气泡

// 速度选项
inline constexpr double SPEED_OPTIONS[] = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 4.0 };
inline constexpr int SPEED_COUNT = sizeof(SPEED_OPTIONS) / sizeof(SPEED_OPTIONS[0]);

// 子菜单统一宽度
inline constexpr int kSubmenuWidth = 220;

// 顶部菜单栏布局常量
inline constexpr int kTopMenuX = 10;
inline constexpr int kTopMenuPaddingX = 14;
inline constexpr int kTopMenuGap = 6;

} // namespace VideoPlay
