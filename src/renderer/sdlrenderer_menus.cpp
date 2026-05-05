#include "renderer/sdlrenderer.h"
#include "renderer/sdlrenderer_internal.h"
#include "utils/logger.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

namespace VideoPlay {


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
        {0, "", "", true},
        {14, "增加速度", "", false, true},
        {15, "降低速度", "", false, true},
        {0, "", "", true},
        {90, "AB循环: 设置A点", "[", false, true},
        {91, "AB循环: 设置B点", "]", false, true},
        {92, "AB循环: 清除", "\\", false, true},
        {0, "", "", true},
        {60, "循环: 不循环", "", false, true},
        {61, "循环: 单曲循环", "", false, true},
        {62, "循环: 列表循环", "", false, true},
        {0, "", "", true},
        {70, "比例: 原始", "", false, true},
        {71, "比例: 16:9", "", false, true},
        {72, "比例: 4:3", "", false, true},
        {73, "比例: 铺满", "", false, true},
        {0, "", "", true},
        {80, "始终置顶", "T", false, true},
        {0, "", "", true},
        {16, "全屏", "F", false, true}
    };
    m_menus.push_back(playMenu);

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
    m_contextMenu.items = {
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
        {300, "AI 分析当前视频", "", false, true},
        {302, "搜索内容", "Ctrl+F", false, true},
        {0, "", "", true},
        {16, "全屏", "F", false, true},
        {80, "始终置顶", "T", false, true}
    };
}

bool SDLRenderer::handleMenuClick(int x, int y) {
    // 隐藏右键菜单
    hideContextMenu();
    
    int menuX = 10;
    for (int i = 0; i < (int)m_menus.size(); i++) {
        if (m_menus[i].label == "章节" && !m_hasChapters) continue;
        int textW = getTextWidth(m_menus[i].label);
        int menuWidth = textW + 20;
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
        menuX += menuWidth + 10;
    }
    closeAllMenus();
    return false;
}

void SDLRenderer::closeAllMenus(bool animate) {
    if (m_activeMenu < 0 && m_menuAnimAlpha <= 0.0f) return;
    if (!animate) {
        m_activeMenu = -1;
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

void SDLRenderer::updateMenuAnimation() {
    if (!m_menuAnimating) {
        m_menuAnimAlpha = (m_activeMenu >= 0) ? 1.0f : 0.0f;
        return;
    }

    uint64_t elapsed = SDL_GetTicks() - m_menuAnimStartTime;
    float t = std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(MENU_ANIM_DURATION_MS));

    if (m_pendingMenu >= 0) {
        //  打开动画（ease-out�?
        float ease = 1.0f - (1.0f - t) * (1.0f - t);
        m_menuAnimAlpha = ease;
        if (t >= 1.0f) {
            m_menuAnimating = false;
            m_menuAnimAlpha = 1.0f;
        }
    } else {
        //  关闭动画（ease-in�?
        float ease = t * t;
        m_menuAnimAlpha = 1.0f - ease;
        if (t >= 1.0f) {
            m_menuAnimating = false;
            m_menuAnimAlpha = 0.0f;
            m_activeMenu = -1;
        }
    }
}

void SDLRenderer::renderMenuBar() {
    //  菜单栏背�?
    fillRect(0, 0, m_windowWidth, m_menuBarHeight, 
             COLOR_MENU_BG[0], COLOR_MENU_BG[1], COLOR_MENU_BG[2], COLOR_MENU_BG[3]);
    
    //  菜单�?
    int x = 10;
    for (int i = 0; i < (int)m_menus.size(); i++) {
        if (m_menus[i].label == "章节" && !m_hasChapters) continue;
        int textW = getTextWidth(m_menus[i].label);
        int itemWidth = textW + 20;
        
        bool isActive = (i == m_activeMenu);
        bool isHovered = m_menuBarHovered && m_mouseY < m_menuBarHeight && 
                         m_mouseX >= x && m_mouseX <= x + itemWidth;
        
        if (isActive) {
            fillRect(x, 0, itemWidth, m_menuBarHeight,
                     COLOR_MENU_ACTIVE[0], COLOR_MENU_ACTIVE[1], COLOR_MENU_ACTIVE[2], COLOR_MENU_ACTIVE[3]);
        } else if (isHovered) {
            fillRect(x, 0, itemWidth, m_menuBarHeight,
                     COLOR_MENU_HOVER[0], COLOR_MENU_HOVER[1], COLOR_MENU_HOVER[2], COLOR_MENU_HOVER[3]);
        }
        
        // 渲染文字
        int textY = (m_menuBarHeight - getFontHeight()) / 2;
        drawText(m_menus[i].label, x + 10, textY, 
                 COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2]);
        
        x += itemWidth + 10;
    }

    //  无边框模式下的系统按�?
    if (m_borderless) {
        int btnSize = 14;
        int btnGap = 12;
        int rightMargin = 14;
        int btnY = (m_menuBarHeight - btnSize) / 2;
        int startX = m_windowWidth - rightMargin - 3 * btnSize - 2 * btnGap;

        // 最小化按钮
        {
            int bx = startX;
            bool hovered = (m_hoveredControl == ControlType::SysMinButton);
            bool pressed = (m_pressedControl == ControlType::SysMinButton);
            const uint8_t* c = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;
            if (pressed) {
                fillRect(bx - 2, btnY - 2, btnSize + 4, btnSize + 4,
                         COLOR_BUTTON_BG_PRESSED[0], COLOR_BUTTON_BG_PRESSED[1], COLOR_BUTTON_BG_PRESSED[2], COLOR_BUTTON_BG_PRESSED[3]);
            } else if (hovered) {
                fillRect(bx - 2, btnY - 2, btnSize + 4, btnSize + 4,
                         COLOR_BUTTON_BG_HOVER[0], COLOR_BUTTON_BG_HOVER[1], COLOR_BUTTON_BG_HOVER[2], COLOR_BUTTON_BG_HOVER[3]);
            }
            // 横线
            int lineW = btnSize - 4;
            int lineH = 2;
            int lineX = bx + (btnSize - lineW) / 2;
            int lineY = btnY + (btnSize - lineH) / 2;
            fillRect(lineX, lineY, lineW, lineH, c[0], c[1], c[2], c[3]);
            m_controlRects.push_back({bx, btnY, btnSize, btnSize, ControlType::SysMinButton, 0});
        }

        // 最大化/还原按钮
        {
            int bx = startX + btnSize + btnGap;
            bool hovered = (m_hoveredControl == ControlType::SysMaxButton);
            bool pressed = (m_pressedControl == ControlType::SysMaxButton);
            const uint8_t* c = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;
            if (pressed) {
                fillRect(bx - 2, btnY - 2, btnSize + 4, btnSize + 4,
                         COLOR_BUTTON_BG_PRESSED[0], COLOR_BUTTON_BG_PRESSED[1], COLOR_BUTTON_BG_PRESSED[2], COLOR_BUTTON_BG_PRESSED[3]);
            } else if (hovered) {
                fillRect(bx - 2, btnY - 2, btnSize + 4, btnSize + 4,
                         COLOR_BUTTON_BG_HOVER[0], COLOR_BUTTON_BG_HOVER[1], COLOR_BUTTON_BG_HOVER[2], COLOR_BUTTON_BG_HOVER[3]);
            }
            bool isMaximized = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED);
            if (isMaximized) {
                // 还原：两个错位小方框
                int s = btnSize - 6;
                int ox = bx + 2;
                int oy = btnY + 2;
                drawRect(ox, oy + 2, s, s, c[0], c[1], c[2], c[3]);
                drawRect(ox + 3, oy - 1, s, s, c[0], c[1], c[2], c[3]);
            } else {
                //  最大化：空心方�?
                int s = btnSize - 4;
                int ox = bx + 2;
                int oy = btnY + 2;
                drawRect(ox, oy, s, s, c[0], c[1], c[2], c[3]);
            }
            m_controlRects.push_back({bx, btnY, btnSize, btnSize, ControlType::SysMaxButton, 0});
        }

        // 关闭按钮
        {
            int bx = startX + 2 * (btnSize + btnGap);
            bool hovered = (m_hoveredControl == ControlType::SysCloseButton);
            bool pressed = (m_pressedControl == ControlType::SysCloseButton);
            const uint8_t* c = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;
            if (pressed) {
                fillRect(bx - 2, btnY - 2, btnSize + 4, btnSize + 4,
                         COLOR_BUTTON_BG_PRESSED[0], COLOR_BUTTON_BG_PRESSED[1], COLOR_BUTTON_BG_PRESSED[2], COLOR_BUTTON_BG_PRESSED[3]);
            } else if (hovered) {
                //  关闭按钮 hover 用红色背景（现代风格�?
                fillRect(bx - 2, btnY - 2, btnSize + 4, btnSize + 4, 232, 17, 35, 255);
                c = COLOR_BUTTON_HOVER;
            }
            // X
            SDL_SetRenderDrawColor(m_renderer, c[0], c[1], c[2], c[3]);
            int pad = 3;
            SDL_RenderLine(m_renderer, static_cast<float>(bx + pad), static_cast<float>(btnY + pad),
                           static_cast<float>(bx + btnSize - 1 - pad), static_cast<float>(btnY + btnSize - 1 - pad));
            SDL_RenderLine(m_renderer, static_cast<float>(bx + btnSize - 1 - pad), static_cast<float>(btnY + pad),
                           static_cast<float>(bx + pad), static_cast<float>(btnY + btnSize - 1 - pad));
            m_controlRects.push_back({bx, btnY, btnSize, btnSize, ControlType::SysCloseButton, 0});
        }
    }
}

void SDLRenderer::renderMenu(const Menu& menu, int x, int y, float alpha) {
    int itemHeight = 24;
    int labelMaxW = 0;
    int shortcutMaxW = 0;
    const int labelFontSize = 12;
    const int shortcutFontSize = 11;
    const int shortcutGap = 24; // 标签与快捷键之间的最小间距
    const int hPadding = 20;    // 左右总内边距

    // 预先计算所需宽度
    for (const auto& item : menu.items) {
        if (!item.separator && item.enabled) {
            int lw = getTextWidth(item.label, labelFontSize);
            if (lw > labelMaxW) labelMaxW = lw;
            if (!item.shortcut.empty()) {
                int sw = getTextWidth(item.shortcut, shortcutFontSize);
                if (sw > shortcutMaxW) shortcutMaxW = sw;
            }
        }
    }

    int menuWidth = hPadding + labelMaxW;
    if (shortcutMaxW > 0) {
        menuWidth += shortcutGap + shortcutMaxW;
    }
    // 最小宽度保证
    if (menuWidth < 140) menuWidth = 140;

    int menuHeight = (int)menu.items.size() * itemHeight + 8;
    uint8_t baseAlpha = static_cast<uint8_t>(240 * alpha);

    // 菜单背景
    fillRect(x, y, menuWidth, menuHeight,
             COLOR_MENU_BG[0], COLOR_MENU_BG[1], COLOR_MENU_BG[2], baseAlpha);

    //  菜单�?
    int itemY = y + 4;
    for (const auto& item : menu.items) {
        if (item.separator) {
            //  分隔�?
            fillRect(x + 5, itemY + itemHeight/2 - 1, menuWidth - 10, 2, 100, 100, 100, static_cast<uint8_t>(255 * alpha));
        } else {
            //  检测悬�?
            bool hovered = (m_mouseX >= x && m_mouseX <= x + menuWidth &&
                           m_mouseY >= itemY && m_mouseY <= itemY + itemHeight);

            if (hovered && item.enabled) {
                fillRect(x + 2, itemY, menuWidth - 4, itemHeight,
                         COLOR_MENU_ACTIVE[0], COLOR_MENU_ACTIVE[1], COLOR_MENU_ACTIVE[2], static_cast<uint8_t>(COLOR_MENU_ACTIVE[3] * alpha));
            }

            //  渲染菜单项文�?
            if (item.enabled) {
                int labelX = x + 10;
                // 循环模式菜单项：当前选中的前面打勾
                if (item.id >= 60 && item.id <= 62) {
                    int mode = item.id - 60;
                    if (mode == m_loopMode) {
                        drawText("\xE2\x9C\x93 ", labelX, itemY + 4,
                                COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], labelFontSize);
                        labelX += getTextWidth("\xE2\x9C\x93 ", labelFontSize);
                    } else {
                        labelX += getTextWidth("\xE2\x9C\x93 ", labelFontSize);
                    }
                }
                // 画面比例菜单项：当前选中的前面打勾
                if (item.id >= 70 && item.id <= 73) {
                    int mode = item.id - 70;
                    if (mode == static_cast<int>(m_aspectMode)) {
                        drawText("\xE2\x9C\x93 ", labelX, itemY + 4,
                                COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], labelFontSize);
                        labelX += getTextWidth("\xE2\x9C\x93 ", labelFontSize);
                    } else {
                        labelX += getTextWidth("\xE2\x9C\x93 ", labelFontSize);
                    }
                }
                // 始终置顶菜单项：启用时前面打勾
                if (item.id == 80) {
                    if (m_alwaysOnTop) {
                        drawText("\xE2\x9C\x93 ", labelX, itemY + 4,
                                COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], labelFontSize);
                        labelX += getTextWidth("\xE2\x9C\x93 ", labelFontSize);
                    } else {
                        labelX += getTextWidth("\xE2\x9C\x93 ", labelFontSize);
                    }
                }
                drawText(item.label, labelX, itemY + 4,
                        COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], labelFontSize);

                // 渲染快捷键（右对齐）
                if (!item.shortcut.empty()) {
                    int sw = getTextWidth(item.shortcut, shortcutFontSize);
                    drawText(item.shortcut, x + menuWidth - 10 - sw, itemY + 4,
                            150, 150, 150, shortcutFontSize);
                }
            }
        }
        itemY += itemHeight;
    }
}

void SDLRenderer::showContextMenu(int x, int y) {
    m_contextMenuX = x;
    m_contextMenuY = y;
    m_showContextMenu = true;
    // 关闭顶部菜单栏
    closeAllMenus(false);
}

void SDLRenderer::hideContextMenu() {
    m_showContextMenu = false;
}

void SDLRenderer::renderContextMenu() {
    if (!m_showContextMenu) return;

    int itemHeight = 28;
    int hPadding = 20;
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

    int menuWidth = hPadding + labelMaxW;
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
    fillRect(renderX, renderY, menuWidth, menuHeight,
             COLOR_MENU_BG[0], COLOR_MENU_BG[1], COLOR_MENU_BG[2], 240);

    // 菜单项
    int itemY = renderY + 4;
    for (const auto& item : m_contextMenu.items) {
        if (item.separator) {
            fillRect(renderX + 5, itemY + itemHeight/2 - 1, menuWidth - 10, 2, 100, 100, 100, 255);
        } else {
            bool hovered = (m_mouseX >= renderX && m_mouseX <= renderX + menuWidth &&
                           m_mouseY >= itemY && m_mouseY <= itemY + itemHeight);

            if (hovered && item.enabled) {
                fillRect(renderX + 2, itemY, menuWidth - 4, itemHeight,
                         COLOR_MENU_ACTIVE[0], COLOR_MENU_ACTIVE[1], COLOR_MENU_ACTIVE[2], COLOR_MENU_ACTIVE[3]);
            }

            if (item.enabled) {
                int labelX = renderX + 10;
                drawText(item.label, labelX, itemY + 4,
                        COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], labelFontSize);

                if (!item.shortcut.empty()) {
                    int sw = getTextWidth(item.shortcut, shortcutFontSize);
                    drawText(item.shortcut, renderX + menuWidth - 10 - sw, itemY + 4,
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
            if (m_menuCallback) {
                m_menuCallback(item.id);
            }
            return true;
        }
        itemY += itemHeight;
    }

    hideContextMenu();
    return true;
}

} // namespace VideoPlay
