#include "renderer/sdlrenderer.h"
#include "renderer/sdlrenderer_internal.h"
#include "utils/logger.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>

namespace VideoPlay {

namespace {
constexpr int kTopMenuX = 10;
constexpr int kTopMenuPaddingX = 16;
constexpr int kTopMenuGap = 8;

uint8_t scaledAlpha(int base, float alpha)
{
    int value = static_cast<int>(base * std::clamp(alpha, 0.0f, 1.0f));
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

}


void SDLRenderer::initMenus() {
    // 文件菜单
    Menu fileMenu;
    fileMenu.label = "文件";
    fileMenu.items = {
        {1, "打开文件...", "Ctrl+O", false, true},
        {2, "打开文件夹..", "", false, true},
        {4, "导入字幕...", "", false, true},
        {0, "", "", true}, // 分隔线
        {3, "退出", "Alt+F4", false, true},
    };
    m_menus.push_back(fileMenu);

    // 播放菜单
    Menu playMenu;
    playMenu.label = "播放";
    playMenu.items = {
        {10, "播放/暂停", "Space", false, true},
        {11, "停止", "S", false, true},
        {0, "", "", true},
        {12, "上一个", "P", false, true},
        {13, "下一个", "N", false, true},
        {0, "", "", true},
        {19, "上一集", "Ctrl+Shift+Left", false, true},
        {22, "下一集", "Ctrl+Shift+Right", false, true},
        {0, "", "", true},
        {18, "播放列表", "Ctrl+L", false, true},
        {24, "清空播放列表", "", false, true},
        {0, "", "", true},
        {14, "增加速度", "", false, true},
        {15, "降低速度", "", false, true},
        {0, "", "", true},
        {58, "AB 循环", "", false, true},
        {0, "", "", true},
        {89, "音频滤镜", "", false, true},
        {98, "音频输出", "", false, true},
        {0, "", "", true},
        {120, "视频基础参数", "", false, true},
        {141, "去隔行", "", false, true},
        {0, "", "", true},
        {97, "硬件解码", "", false, true},
        {0, "", "", true},
        {59, "循环", "", false, true},
        {0, "", "", true},
        {69, "画面比例", "", false, true},
        {130, "画面变换", "", false, true},
        {0, "", "", true},
        {80, "始终置顶", "T", false, true},
        {0, "", "", true},
        {16, "全屏", "F", false, true}
    };
    m_menus.push_back(playMenu);

    // 音轨菜单（动态填充）
    Menu audioMenu;
    audioMenu.label = "音轨";
    audioMenu.items = {
        {400, "无可用音轨", "", false, false}
    };
    m_menus.push_back(audioMenu);

    // 字幕菜单（动态填充）
    Menu subtitleMenu;
    subtitleMenu.label = "字幕";
    subtitleMenu.items = {
        {450, "无可用字幕", "", false, false}
    };
    m_menus.push_back(subtitleMenu);

    // 章节菜单
    Menu chapterMenu;
    chapterMenu.label = "章节";
    chapterMenu.items = {
        {200, "无可用章节", "", false, false}
    };
    m_menus.push_back(chapterMenu);

    // 剧集菜单
    Menu episodeMenu;
    episodeMenu.label = "剧集";
    episodeMenu.items = {
        {30, "上一集", "Ctrl+Shift+Left", false, true},
        {31, "下一集", "Ctrl+Shift+Right", false, true},
        {0, "", "", true},
        {32, "切换选集面板", "Ctrl+E", false, true}
    };
    m_menus.push_back(episodeMenu);

    // AI 菜单
    Menu aiMenu;
    aiMenu.label = "AI";
    aiMenu.items = {
        {300, "AI 分析当前视频", "", false, true},
        {301, "显示摘要", "", false, true},
        {0, "", "", true},
        {302, "搜索内容", "Ctrl+F", false, true},
        {0, "", "", true},
        {304, "AI 设置...", "", false, true},
        {303, "清除 AI 缓存", "", false, true}
    };
    m_menus.push_back(aiMenu);

    // 帮助菜单
    Menu helpMenu;
    helpMenu.label = "帮助";
    helpMenu.items = {
        {50, "快捷键", "F1", false, true},
        {51, "关于", "", false, true}
    };
    m_menus.push_back(helpMenu);

    // 初始化右键上下文菜单
    m_contextMenu.label = "";
    m_playbackContextMenu.label = "";
    m_playbackContextMenu.items = {
        {10, "播放/暂停", "Space", false, true},
        {11, "停止", "S", false, true},
        {0, "", "", true},
        {1, "打开文件...", "Ctrl+O", false, true},
        {4, "导入字幕...", "", false, true},
        {0, "", "", true},
        {14, "增加速度", "", false, true},
        {15, "降低速度", "", false, true},
        {0, "", "", true},
        {90, "AB循环: 设置A点", "[", false, true},
        {91, "AB循环: 设置B点", "]", false, true},
        {92, "AB循环: 清除", "\\", false, true},
        {0, "", "", true},
        {93, "音频滤镜: 关闭", "", false, true},
        {94, "音频滤镜: 语音增强", "", false, true},
        {95, "音频滤镜: 低音增强", "", false, true},
        {96, "音频滤镜: 夜间模式", "", false, true},
        {0, "", "", true},
        {97, "硬件解码", "", false, true},
        {0, "", "", true},
        {300, "AI 分析当前视频", "", false, true},
        {302, "搜索内容", "Ctrl+F", false, true},
        {0, "", "", true},
        {16, "全屏", "F", false, true},
        {80, "始终置顶", "T", false, true}
    };
    m_contextMenu = m_playbackContextMenu;
}

bool SDLRenderer::handleMenuClick(int x, int y) {
    // 隐藏右键菜单
    hideContextMenu();
    
    int menuX = kTopMenuX;
    for (int i = 0; i < (int)m_menus.size(); i++) {
        if (!isTopMenuVisible(static_cast<size_t>(i))) continue;
        int textW = getTextWidth(m_menus[i].label);
        int menuWidth = textW + kTopMenuPaddingX * 2;
        if (x >= menuX && x <= menuX + menuWidth) {
            if (m_activeMenu == i && !m_menuAnimating) {
                m_pendingMenu = -1;
                m_menuAnimStartTime = SDL_GetTicks();
                m_menuAnimating = true;
            } else {
                m_activeMenu = i;
                m_pendingMenu = i;
                m_menuAnimStartTime = SDL_GetTicks();
                m_menuAnimating = true;
            }
            return true;
        }
        menuX += menuWidth + kTopMenuGap;
    }
    closeAllMenus();
    return false;
}

void SDLRenderer::closeAllMenus(bool animate) {
    if (m_activeMenu < 0 && m_menuAnimAlpha <= 0.0f) return;
    if (!animate) {
        m_activeMenu = -1;
        m_activeSubmenuParent = 0;
        m_pendingMenu = -1;
        m_menuAnimAlpha = 0.0f;
        m_menuAnimating = false;
        return;
    }
    m_pendingMenu = -1;
    m_menuAnimStartTime = SDL_GetTicks();
    m_menuAnimating = true;
}

bool SDLRenderer::isMenuOpen() const {
    return m_activeMenu >= 0;
}

bool SDLRenderer::isTopMenuVisible(size_t index) const {
    if (index >= m_menus.size()) return false;
    const std::string& label = m_menus[index].label;
    if (label == "章节") return m_hasChapters;
    if (label == "音轨") return !m_audioTracks.empty();
    if (label == "字幕") return !m_subtitleTracks.empty();
    return !label.empty();
}

void SDLRenderer::updateMenuAnimation() {
    if (!m_menuAnimating) {
        m_menuAnimAlpha = (m_activeMenu >= 0) ? 1.0f : 0.0f;
        return;
    }

    uint64_t elapsed = SDL_GetTicks() - m_menuAnimStartTime;
    float t = std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(MENU_ANIM_DURATION_MS));

    if (m_pendingMenu >= 0) {
        // 打开动画：更柔和的 ease-out cubic
        float inv = 1.0f - t;
        float ease = 1.0f - inv * inv * inv;
        m_menuAnimAlpha = ease;
        if (t >= 1.0f) {
            m_menuAnimating = false;
            m_menuAnimAlpha = 1.0f;
        }
    } else {
        // 关闭动画：稍快的 ease-in quad
        float ease = t * t;
        m_menuAnimAlpha = 1.0f - ease;
        if (t >= 1.0f) {
            m_menuAnimating = false;
            m_menuAnimAlpha = 0.0f;
            m_activeMenu = -1;
            m_activeSubmenuParent = 0;
        }
    }
}

void SDLRenderer::renderMenuBar() {
    // 菜单栏背景：低对比深色层 + 顶部高光 + 底部描边
    fillRect(0, 0, m_windowWidth, m_menuBarHeight, 34, 35, 39, 245);
    fillRect(0, 0, m_windowWidth, 1, 255, 255, 255, 18);
    fillRect(0, m_menuBarHeight - 1, m_windowWidth, 1, 0, 0, 0, 80);
    
    // 菜单项
    int x = kTopMenuX;
    for (int i = 0; i < (int)m_menus.size(); i++) {
        if (!isTopMenuVisible(static_cast<size_t>(i))) continue;
        int textW = getTextWidth(m_menus[i].label);
        int itemWidth = textW + kTopMenuPaddingX * 2;
        
        bool isActive = (i == m_activeMenu);
        bool isHovered = m_menuBarHovered && m_mouseY < m_menuBarHeight && 
                         m_mouseX >= x && m_mouseX <= x + itemWidth;
        
        if (isActive) {
            fillRoundRect(x, 4, itemWidth, m_menuBarHeight - 8, 5, 52, 82, 105, 220);
            fillRect(x + 8, m_menuBarHeight - 3, itemWidth - 16, 2,
                     COLOR_PROGRESS_FILL[0], COLOR_PROGRESS_FILL[1], COLOR_PROGRESS_FILL[2], 235);
        } else if (isHovered) {
            fillRoundRect(x, 4, itemWidth, m_menuBarHeight - 8, 5, 255, 255, 255, 34);
        }
        
        // 渲染文字
        int textY = (m_menuBarHeight - getFontHeight()) / 2;
        drawText(m_menus[i].label, x + kTopMenuPaddingX, textY, 
                 isActive ? 255 : COLOR_TEXT[0],
                 isActive ? 255 : COLOR_TEXT[1],
                 isActive ? 255 : COLOR_TEXT[2]);
        
        x += itemWidth + kTopMenuGap;
    }

    // 无边框模式下的系统按钮
    if (m_borderless) {
        constexpr int buttonWidth = 46;
        constexpr int iconSize = 10;
        int iconY = (m_menuBarHeight - iconSize) / 2;
        int startX = m_windowWidth - 3 * buttonWidth;

        // 最小化按钮
        {
            int bx = startX;
            bool hovered = (m_hoveredControl == ControlType::SysMinButton);
            bool pressed = (m_pressedControl == ControlType::SysMinButton);
            const uint8_t* c = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;
            if (pressed) {
                fillRect(bx, 0, buttonWidth, m_menuBarHeight,
                         COLOR_BUTTON_BG_PRESSED[0], COLOR_BUTTON_BG_PRESSED[1], COLOR_BUTTON_BG_PRESSED[2], COLOR_BUTTON_BG_PRESSED[3]);
            } else if (hovered) {
                fillRect(bx, 0, buttonWidth, m_menuBarHeight,
                         COLOR_BUTTON_BG_HOVER[0], COLOR_BUTTON_BG_HOVER[1], COLOR_BUTTON_BG_HOVER[2], COLOR_BUTTON_BG_HOVER[3]);
            }
            // 横线
            int lineW = iconSize;
            int lineH = 2;
            int lineX = bx + (buttonWidth - lineW) / 2;
            int lineY = iconY + iconSize - 3;
            fillRect(lineX, lineY, lineW, lineH, c[0], c[1], c[2], c[3]);
            m_controlRects.push_back({bx, 0, buttonWidth, m_menuBarHeight, ControlType::SysMinButton, 0});
        }

        // 最大化/还原按钮
        {
            int bx = startX + buttonWidth;
            bool hovered = (m_hoveredControl == ControlType::SysMaxButton);
            bool pressed = (m_pressedControl == ControlType::SysMaxButton);
            const uint8_t* c = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;
            if (pressed) {
                fillRect(bx, 0, buttonWidth, m_menuBarHeight,
                         COLOR_BUTTON_BG_PRESSED[0], COLOR_BUTTON_BG_PRESSED[1], COLOR_BUTTON_BG_PRESSED[2], COLOR_BUTTON_BG_PRESSED[3]);
            } else if (hovered) {
                fillRect(bx, 0, buttonWidth, m_menuBarHeight,
                         COLOR_BUTTON_BG_HOVER[0], COLOR_BUTTON_BG_HOVER[1], COLOR_BUTTON_BG_HOVER[2], COLOR_BUTTON_BG_HOVER[3]);
            }
            bool isMaximized = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED);
            if (isMaximized) {
                // 还原：两个错位小方框
                int s = 8;
                int ox = bx + (buttonWidth - iconSize) / 2 + 1;
                int oy = iconY + 2;
                drawRect(ox, oy + 2, s, s, c[0], c[1], c[2], c[3]);
                drawRect(ox + 3, oy - 1, s, s, c[0], c[1], c[2], c[3]);
            } else {
                // 最大化：空心方框
                int s = iconSize;
                int ox = bx + (buttonWidth - s) / 2;
                int oy = iconY;
                drawRect(ox, oy, s, s, c[0], c[1], c[2], c[3]);
            }
            m_controlRects.push_back({bx, 0, buttonWidth, m_menuBarHeight, ControlType::SysMaxButton, 0});
        }

        // 关闭按钮
        {
            int bx = startX + 2 * buttonWidth;
            bool hovered = (m_hoveredControl == ControlType::SysCloseButton);
            bool pressed = (m_pressedControl == ControlType::SysCloseButton);
            const uint8_t* c = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;
            if (pressed) {
                fillRect(bx, 0, buttonWidth, m_menuBarHeight,
                         COLOR_BUTTON_BG_PRESSED[0], COLOR_BUTTON_BG_PRESSED[1], COLOR_BUTTON_BG_PRESSED[2], COLOR_BUTTON_BG_PRESSED[3]);
            } else if (hovered) {
                // 关闭按钮 hover 用红色背景
                fillRect(bx, 0, buttonWidth, m_menuBarHeight, 232, 17, 35, 255);
                c = COLOR_BUTTON_HOVER;
            }
            // X
            SDL_SetRenderDrawColor(m_renderer, c[0], c[1], c[2], c[3]);
            int iconX = bx + (buttonWidth - iconSize) / 2;
            SDL_RenderLine(m_renderer, static_cast<float>(iconX), static_cast<float>(iconY),
                           static_cast<float>(iconX + iconSize), static_cast<float>(iconY + iconSize));
            SDL_RenderLine(m_renderer, static_cast<float>(iconX + iconSize), static_cast<float>(iconY),
                           static_cast<float>(iconX), static_cast<float>(iconY + iconSize));
            m_controlRects.push_back({bx, 0, buttonWidth, m_menuBarHeight, ControlType::SysCloseButton, 0});
        }
    }
}

void SDLRenderer::renderMenu(const Menu& menu, int x, int y, float alpha) {
    const int itemHeight = 30;
    const int separatorHeight = 14;
    int labelMaxW = 0;
    int shortcutMaxW = 0;
    const int labelFontSize = 14;
    const int shortcutFontSize = 12;
    const int shortcutGap = 30; // 标签与快捷键之间的最小间距
    const int leftPadding = 16;
    const int rightPadding = 18;
    const int checkColumnWidth = 22;
    const int menuPadY = 5;

    // 预先计算所需宽度
    for (const auto& item : menu.items) {
        if (!item.separator && item.enabled) {
            std::string displayLabel = submenuParentLabel(item, m_loopMode, m_aspectMode, m_audioFilterPreset);
            int lw = getTextWidth(displayLabel, labelFontSize);
            if (lw > labelMaxW) labelMaxW = lw;
            if (!item.shortcut.empty()) {
                int sw = getTextWidth(item.shortcut, shortcutFontSize);
                if (sw > shortcutMaxW) shortcutMaxW = sw;
            }
        }
    }

    int menuWidth = leftPadding + checkColumnWidth + labelMaxW + rightPadding;
    if (shortcutMaxW > 0) {
        menuWidth += shortcutGap + shortcutMaxW;
    }
    // 最小宽度保证
    if (menuWidth < 192) menuWidth = 192;

    int contentHeight = 0;
    for (const auto& item : menu.items) {
        contentHeight += item.separator ? separatorHeight : itemHeight;
    }
    int menuHeight = contentHeight + menuPadY * 2;
    int renderY = y - 6 + static_cast<int>(6.0f * std::clamp(alpha, 0.0f, 1.0f));
    uint8_t baseAlpha = scaledAlpha(244, alpha);
    const int submenuWidth = 180;
    int activeSubmenuX = x + menuWidth - 4;
    int activeSubmenuY = 0;
    int rowUnderMouseParent = 0;
    bool mouseInMainMenu = (m_mouseX >= x && m_mouseX <= x + menuWidth &&
                            m_mouseY >= renderY && m_mouseY <= renderY + menuHeight);

    int scanY = renderY + menuPadY;
    for (const auto& item : menu.items) {
        if (item.separator) {
            scanY += separatorHeight;
            continue;
        }
        if (m_mouseX >= x && m_mouseX <= x + menuWidth &&
            m_mouseY >= scanY && m_mouseY <= scanY + itemHeight) {
            if (isSubmenuParent(item.id)) {
                rowUnderMouseParent = item.id;
            }
            break;
        }
        scanY += itemHeight;
    }

    if (rowUnderMouseParent != 0) {
        m_activeSubmenuParent = rowUnderMouseParent;
    } else if (mouseInMainMenu) {
        m_activeSubmenuParent = 0;
    }

    // 菜单阴影和背景
    fillRoundRect(x + 2, renderY + 5, menuWidth, menuHeight, 8, 0, 0, 0, scaledAlpha(95, alpha));
    fillRoundRect(x + 1, renderY + 2, menuWidth, menuHeight, 8, 0, 0, 0, scaledAlpha(45, alpha));
    fillRoundRect(x, renderY, menuWidth, menuHeight, 8,
                  38, 39, 43, baseAlpha);
    drawRect(x + 1, renderY + 1, menuWidth - 2, menuHeight - 2,
             255, 255, 255, scaledAlpha(26, alpha));

    // 菜单项
    int itemY = renderY + menuPadY;
    for (const auto& item : menu.items) {
        if (item.separator) {
            // 分隔线
            fillRect(x + leftPadding, itemY + separatorHeight / 2,
                     menuWidth - leftPadding - rightPadding, 1,
                     255, 255, 255, scaledAlpha(38, alpha));
            itemY += separatorHeight;
            continue;
        } else {
            // 检测悬浮
            bool hovered = (m_mouseX >= x && m_mouseX <= x + menuWidth &&
                           m_mouseY >= itemY && m_mouseY <= itemY + itemHeight);

            if (isSubmenuParent(item.id)) {
                if (item.id == m_activeSubmenuParent) {
                    activeSubmenuY = itemY;
                } else if (hovered) {
                    m_activeSubmenuParent = item.id;
                    activeSubmenuY = itemY;
                } else if (rowUnderMouseParent == 0 && m_activeSubmenuParent == 0) {
                    int submenuHeight = menuSubmenuItemCount(item.id) * itemHeight + menuPadY * 2;
                    bool submenuHovered = (m_mouseX >= activeSubmenuX && m_mouseX <= activeSubmenuX + submenuWidth &&
                                           m_mouseY >= itemY && m_mouseY <= itemY + submenuHeight);
                    if (submenuHovered) {
                        m_activeSubmenuParent = item.id;
                        activeSubmenuY = itemY;
                    }
                }
                if (rowUnderMouseParent != 0) {
                    hovered = (item.id == rowUnderMouseParent);
                } else if (m_activeSubmenuParent != 0) {
                    hovered = (item.id == m_activeSubmenuParent);
                }
            }

            if (hovered && item.enabled) {
                fillRoundRect(x + 5, itemY + 1, menuWidth - 10, itemHeight - 2, 5,
                              48, 96, 132, scaledAlpha(215, alpha));
                fillRect(x + 8, itemY + 6, 2, itemHeight - 12,
                         COLOR_PROGRESS_FILL[0], COLOR_PROGRESS_FILL[1], COLOR_PROGRESS_FILL[2], scaledAlpha(230, alpha));
            }

            // 渲染菜单项文字
            if (item.enabled) {
                std::string displayLabel = submenuParentLabel(item, m_loopMode, m_aspectMode, m_audioFilterPreset);
                if (item.id == static_cast<int>(MenuId::AudioOutput)) {
                    std::string current = m_audioOutputDeviceName.empty() ? "系统默认" : m_audioOutputDeviceName;
                    displayLabel = item.label + "（" + current + "）";
                }
                if (item.id == static_cast<int>(MenuId::Deinterlace)) {
                    displayLabel = item.label + std::string("（") +
                                   deinterlaceModeName(m_deinterlaceMode) + "）";
                }
                if (item.id == static_cast<int>(MenuId::VideoTransform)) {
                    std::string summary = std::to_string(m_videoTransform.rotation) + "°";
                    if (m_videoTransform.flipHorizontal) summary += " 水平";
                    if (m_videoTransform.flipVertical) summary += " 垂直";
                    if (m_videoTransform.cropPercent > 0) {
                        summary += " 裁" + std::to_string(m_videoTransform.cropPercent) + "%";
                    }
                    displayLabel = item.label + "（" + summary + "）";
                }
                int checkX = x + leftPadding;
                int labelX = checkX + checkColumnWidth;
                int labelY = itemY + (itemHeight - getFontHeight(labelFontSize)) / 2;
                int shortcutY = itemY + (itemHeight - getFontHeight(shortcutFontSize)) / 2;
                bool checked = false;

                // 循环模式菜单项：当前选中的前面打勾
                if (item.id >= 60 && item.id <= 62) {
                    int mode = item.id - 60;
                    checked = (mode == m_loopMode);
                }
                // 画面比例菜单项：当前选中的前面打勾
                if (item.id >= 70 && item.id <= 73) {
                    int mode = item.id - 70;
                    checked = (mode == static_cast<int>(m_aspectMode));
                }
                // 始终置顶菜单项：启用时前面打勾
        if (item.id == 80) {
            checked = m_alwaysOnTop;
        }
        if (item.id >= 93 && item.id <= 96) {
            checked = (item.id - 93) == static_cast<int>(m_audioFilterPreset);
        }
        if (item.id == 97) {
            checked = m_hardwareDecodingEnabled;
        }

                if (checked) {
                    drawText("\xE2\x9C\x93", checkX, labelY,
                             COLOR_PROGRESS_FILL[0], COLOR_PROGRESS_FILL[1], COLOR_PROGRESS_FILL[2], labelFontSize, scaledAlpha(255, alpha));
                }
                drawText(displayLabel, labelX, labelY,
                        COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], labelFontSize, scaledAlpha(255, alpha));

                // 渲染快捷键（右对齐）
                if (!item.shortcut.empty()) {
                    int sw = getTextWidth(item.shortcut, shortcutFontSize);
                    drawText(item.shortcut, x + menuWidth - rightPadding - sw, shortcutY,
                            168, 172, 178, shortcutFontSize, scaledAlpha(230, alpha));
                }
                if (isSubmenuParent(item.id)) {
                    drawText(">", x + menuWidth - rightPadding - getTextWidth(">", shortcutFontSize), shortcutY,
                             168, 172, 178, shortcutFontSize, scaledAlpha(230, alpha));
                }
            } else {
                int labelX = x + leftPadding + checkColumnWidth;
                int labelY = itemY + (itemHeight - getFontHeight(labelFontSize)) / 2;
                drawText(item.label, labelX, labelY, 125, 128, 134, labelFontSize, scaledAlpha(180, alpha));
            }
        }
        itemY += itemHeight;
    }

    if (m_activeSubmenuParent != 0 && activeSubmenuY > 0) {
        int itemCount = menuSubmenuItemCount(m_activeSubmenuParent);
        int submenuHeight = itemCount * itemHeight + menuPadY * 2;
        fillRoundRect(activeSubmenuX + 2, activeSubmenuY + 5, submenuWidth, submenuHeight, 8,
                      0, 0, 0, scaledAlpha(95, alpha));
        fillRoundRect(activeSubmenuX, activeSubmenuY, submenuWidth, submenuHeight, 8,
                      38, 39, 43, baseAlpha);
        drawRect(activeSubmenuX + 1, activeSubmenuY + 1, submenuWidth - 2, submenuHeight - 2,
                 255, 255, 255, scaledAlpha(26, alpha));

        int submenuItemY = activeSubmenuY + menuPadY;
        for (int index = 0; index < itemCount; ++index) {
            int entryId = menuSubmenuItemId(m_activeSubmenuParent, index);
            bool hovered = (m_mouseX >= activeSubmenuX && m_mouseX <= activeSubmenuX + submenuWidth &&
                           m_mouseY >= submenuItemY && m_mouseY <= submenuItemY + itemHeight);
            if (hovered) {
                fillRoundRect(activeSubmenuX + 5, submenuItemY + 1, submenuWidth - 10, itemHeight - 2, 5,
                              48, 96, 132, scaledAlpha(215, alpha));
                fillRect(activeSubmenuX + 8, submenuItemY + 6, 2, itemHeight - 12,
                         COLOR_PROGRESS_FILL[0], COLOR_PROGRESS_FILL[1], COLOR_PROGRESS_FILL[2], scaledAlpha(230, alpha));
            }

            bool checked = false;
            if (entryId >= 60 && entryId <= 62) {
                checked = (entryId - 60) == m_loopMode;
            } else if (entryId >= 70 && entryId <= 73) {
                checked = (entryId - 70) == static_cast<int>(m_aspectMode);
            } else if (entryId >= 93 && entryId <= 96) {
                checked = (entryId - 93) == static_cast<int>(m_audioFilterPreset);
            } else if (entryId == static_cast<int>(MenuId::DeinterlaceOff)) {
                checked = (m_deinterlaceMode == DeinterlaceMode::Off);
            } else if (entryId == static_cast<int>(MenuId::DeinterlaceAuto)) {
                checked = (m_deinterlaceMode == DeinterlaceMode::Auto);
            } else if (entryId == static_cast<int>(MenuId::DeinterlaceYadif)) {
                checked = (m_deinterlaceMode == DeinterlaceMode::Yadif);
            } else if (entryId == static_cast<int>(MenuId::DeinterlaceBwdif)) {
                checked = (m_deinterlaceMode == DeinterlaceMode::Bwdif);
            } else if (entryId == static_cast<int>(MenuId::Rotate0)) {
                checked = (m_videoTransform.rotation == 0);
            } else if (entryId == static_cast<int>(MenuId::Rotate90)) {
                checked = (m_videoTransform.rotation == 90);
            } else if (entryId == static_cast<int>(MenuId::Rotate180)) {
                checked = (m_videoTransform.rotation == 180);
            } else if (entryId == static_cast<int>(MenuId::Rotate270)) {
                checked = (m_videoTransform.rotation == 270);
            } else if (entryId == static_cast<int>(MenuId::FlipHorizontal)) {
                checked = m_videoTransform.flipHorizontal;
            } else if (entryId == static_cast<int>(MenuId::FlipVertical)) {
                checked = m_videoTransform.flipVertical;
            } else if (entryId == static_cast<int>(MenuId::CropOff)) {
                checked = (m_videoTransform.cropPercent == 0);
            } else if (entryId == static_cast<int>(MenuId::Crop10)) {
                checked = (m_videoTransform.cropPercent == 10);
            } else if (entryId == static_cast<int>(MenuId::Crop20)) {
                checked = (m_videoTransform.cropPercent == 20);
            } else if (m_activeSubmenuParent == static_cast<int>(MenuId::AudioOutput)) {
                if (index >= 0 && index < static_cast<int>(m_audioOutputDevices.size())) {
                    const auto& device = m_audioOutputDevices[static_cast<size_t>(index)];
                    if (device.isDefault) {
                        checked = m_audioOutputDeviceName.empty();
                    } else {
                        checked = (device.name == m_audioOutputDeviceName);
                    }
                }
            }
            int checkX = activeSubmenuX + leftPadding;
            int labelX = checkX + checkColumnWidth;
            int labelY = submenuItemY + (itemHeight - getFontHeight(labelFontSize)) / 2;
            if (checked) {
                drawText("\xE2\x9C\x93", checkX, labelY,
                         COLOR_PROGRESS_FILL[0], COLOR_PROGRESS_FILL[1], COLOR_PROGRESS_FILL[2], labelFontSize, scaledAlpha(255, alpha));
            }
            drawText(menuSubmenuItemLabel(m_activeSubmenuParent, index), labelX, labelY,
                     COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], labelFontSize, scaledAlpha(255, alpha));
            submenuItemY += itemHeight;
        }
    }
}

void SDLRenderer::showContextMenu(int x, int y) {
    int hitValue = 0;
    ControlType hit = getControlAt(x, y, &hitValue);
    if (hit == ControlType::PlaylistItem) {
        m_contextPlaylistIndex = hitValue;
        m_contextMenu.label = "";
        m_contextMenu.items = {
            {25, "播放此项", "", false, true},
            {23, "从列表删除", "Delete", false, true},
            {0, "", "", true},
            {24, "清空播放列表", "", false, true}
        };
    } else {
        m_contextPlaylistIndex = -1;
        m_contextMenu = m_playbackContextMenu;
    }

    m_contextMenuX = x;
    m_contextMenuY = y;
    m_showContextMenu = true;
    closeAllMenus(false);
}

void SDLRenderer::hideContextMenu() {
    m_showContextMenu = false;
}

void SDLRenderer::renderContextMenu() {
    if (!m_showContextMenu) return;

    int itemHeight = 28;
    int leftPadding = 18;
    int rightPadding = 18;
    int labelFontSize = 14;
    int shortcutFontSize = 12;
    int labelMaxW = 0;
    int shortcutMaxW = 0;
    int shortcutGap = 30;

    for (const auto& item : m_contextMenu.items) {
        if (item.separator) continue;
        int lw = getTextWidth(item.label, labelFontSize);
        if (lw > labelMaxW) labelMaxW = lw;
        if (!item.shortcut.empty()) {
            int sw = getTextWidth(item.shortcut, shortcutFontSize);
            if (sw > shortcutMaxW) shortcutMaxW = sw;
        }
    }

    int menuWidth = leftPadding + labelMaxW + rightPadding;
    if (shortcutMaxW > 0) {
        menuWidth += shortcutGap + shortcutMaxW;
    }
    if (menuWidth < 160) menuWidth = 160;

    int menuHeight = (int)m_contextMenu.items.size() * itemHeight + 8;

    // 确保菜单不超出窗口右边界和下边界
    int renderX = m_contextMenuX;
    int renderY = m_contextMenuY;
    if (renderX + menuWidth > m_windowWidth) {
        renderX = m_windowWidth - menuWidth - 5;
    }
    if (renderY + menuHeight > m_windowHeight) {
        renderY = m_windowHeight - menuHeight - 5;
    }
    if (renderX < 0) renderX = 0;
    if (renderY < 0) renderY = 0;

    // 菜单背景
    fillRoundRect(renderX, renderY, menuWidth, menuHeight, 8,
                  COLOR_MENU_BG[0], COLOR_MENU_BG[1], COLOR_MENU_BG[2], 240);

    // 菜单项
    int itemY = renderY + 4;
    for (const auto& item : m_contextMenu.items) {
        if (item.separator) {
            fillRect(renderX + leftPadding, itemY + itemHeight / 2 - 1,
                     menuWidth - leftPadding - rightPadding, 1, 100, 100, 100, 180);
        } else {
            bool hovered = (m_mouseX >= renderX && m_mouseX <= renderX + menuWidth &&
                           m_mouseY >= itemY && m_mouseY <= itemY + itemHeight);

            if (hovered && item.enabled) {
                fillRoundRect(renderX + 4, itemY + 2, menuWidth - 8, itemHeight - 4, 5,
                              COLOR_MENU_ACTIVE[0], COLOR_MENU_ACTIVE[1], COLOR_MENU_ACTIVE[2], COLOR_MENU_ACTIVE[3]);
            }

            if (item.enabled) {
                int labelX = renderX + leftPadding;
                int labelY = itemY + (itemHeight - getFontHeight(labelFontSize)) / 2;
                int shortcutY = itemY + (itemHeight - getFontHeight(shortcutFontSize)) / 2;
                drawText(item.label, labelX, labelY,
                        COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], labelFontSize);

                if (!item.shortcut.empty()) {
                    int sw = getTextWidth(item.shortcut, shortcutFontSize);
                    drawText(item.shortcut, renderX + menuWidth - rightPadding - sw, shortcutY,
                            150, 150, 150, shortcutFontSize);
                }
            }
        }
        itemY += itemHeight;
    }
}

bool SDLRenderer::handleContextMenuClick(int x, int y) {
    if (!m_showContextMenu) return false;

    int itemHeight = 28;
    int menuWidth = 200; // 估算值，与 renderContextMenu 保持一致

    // 计算实际渲染位置
    int renderX = m_contextMenuX;
    int renderY = m_contextMenuY;
    if (renderX + menuWidth > m_windowWidth) {
        renderX = m_windowWidth - menuWidth - 5;
    }

    int itemY = renderY + 4;
    for (const auto& item : m_contextMenu.items) {
        if (item.separator) {
            itemY += itemHeight;
            continue;
        }

        if (x >= renderX && x <= renderX + menuWidth &&
            y >= itemY && y <= itemY + itemHeight && item.enabled) {
            hideContextMenu();
            MenuId id = static_cast<MenuId>(item.id);
            if (id == MenuId::PlaylistRemove && m_playlistRemoveCallback &&
                m_contextPlaylistIndex >= 0) {
                m_playlistRemoveCallback(static_cast<size_t>(m_contextPlaylistIndex));
            } else if (id == MenuId::PlaylistClear && m_playlistClearCallback) {
                m_playlistClearCallback();
            } else if (id == MenuId::PlaylistPlayItem && m_playlistItemCallback &&
                       m_contextPlaylistIndex >= 0) {
                m_playlistItemCallback(static_cast<size_t>(m_contextPlaylistIndex));
            } else if (m_menuCallback) {
                m_menuCallback(id);
            }
            return true;
        }
        itemY += itemHeight;
    }

    hideContextMenu();
    return true;
}

void SDLRenderer::updateTrackMenus() {
    // 音轨菜单 index = 2, 字幕菜单 index = 3
    if (m_menus.size() < 4) return;

    // 音轨菜单
    Menu& audioMenu = m_menus[2];
    audioMenu.items.clear();
    if (m_audioTracks.empty()) {
        audioMenu.items.push_back({400, "无可用音轨", "", false, false});
    } else {
        for (size_t i = 0; i < m_audioTracks.size(); i++) {
            MenuItem item;
            item.id = 400 + static_cast<int>(i);
            item.label = trackLabel(m_audioTracks[i], static_cast<int>(i + 1));
            item.enabled = true;
            // 标记当前选中
            if (static_cast<int>(i) == m_currentAudioTrack) {
                item.label = "✓ " + item.label;
            }
            audioMenu.items.push_back(item);
        }
    }

    // 字幕菜单
    Menu& subtitleMenu = m_menus[3];
    subtitleMenu.items.clear();
    // 第一项：关闭内封字幕
    {
        MenuItem off;
        off.id = 450;
        off.label = (m_currentSubtitleTrack == -1) ? "✓ 关闭内封字幕" : "关闭内封字幕";
        off.enabled = true;
        subtitleMenu.items.push_back(off);
    }
    if (m_subtitleTracks.empty()) {
        // 只保留"关闭"项
    } else {
        for (size_t i = 0; i < m_subtitleTracks.size(); i++) {
            MenuItem item;
            item.id = 451 + static_cast<int>(i);
            item.label = trackLabel(m_subtitleTracks[i], static_cast<int>(i + 1));
            item.enabled = true;
            if (static_cast<int>(i) == m_currentSubtitleTrack) {
                item.label = "✓ " + item.label;
            }
            subtitleMenu.items.push_back(item);
        }
    }
}

} // namespace VideoPlay
