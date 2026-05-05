#include "renderer/sdlrenderer.h"
#include "renderer/sdlrenderer_internal.h"
#include "utils/logger.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace VideoPlay {


void SDLRenderer::handleEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_QUIT:
            m_initialized = false;
            break;

        case SDL_EVENT_DROP_FILE: {
            const char* droppedFile = event.drop.data;
            if (droppedFile && m_fileDropCallback) {
                m_fileDropCallback(droppedFile);
            }
            break;
        }

        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            m_windowWidth = event.window.data1;
            m_windowHeight = event.window.data2;
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
            
            //  菜单快捷键处�?
            if (pressed) {
                // Ctrl+O 打开文件
                if (event.key.key == SDLK_O && (event.key.mod & SDL_KMOD_CTRL)) {
                    if (m_fileOpenCallback) m_fileOpenCallback();
                    break;
                }
            }
            
            if (m_keyCallback) {
                m_keyCallback(event.key.key, pressed);
            }

            //  内置快捷�?
            if (pressed) {
                switch (event.key.key) {
                    case SDLK_SPACE:
                        if (m_playPauseCallback) m_playPauseCallback();
                        break;
                    case SDLK_ESCAPE:
                        if (m_fullscreen) toggleFullscreen();
                        closeAllMenus();
                        hideContextMenu();
                        break;
                    case SDLK_F:
                        toggleFullscreen();
                        break;
                    case SDLK_S:
                        if (m_stopCallback) m_stopCallback();
                        break;
                    case SDLK_LEFT: {
                        bool ctrlShift = (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) == (SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
                        bool shiftOnly = (event.key.mod & SDL_KMOD_SHIFT) && !(event.key.mod & SDL_KMOD_CTRL);
                        if (ctrlShift && m_episodePrevCallback) {
                            m_episodePrevCallback();
                        } else if (shiftOnly && m_seekCallback) {
                            m_seekCallback(-30.0); // Shift+左: 后退30秒
                        } else if (!ctrlShift && m_seekCallback) {
                            m_seekCallback(-5.0); // 左: 后退5秒
                        }
                        break;
                    }
                    case SDLK_RIGHT: {
                        bool ctrlShift = (event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) == (SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
                        bool shiftOnly = (event.key.mod & SDL_KMOD_SHIFT) && !(event.key.mod & SDL_KMOD_CTRL);
                        if (ctrlShift && m_episodeNextCallback) {
                            m_episodeNextCallback();
                        } else if (shiftOnly && m_seekCallback) {
                            m_seekCallback(30.0);  // Shift+右: 前进30秒
                        } else if (!ctrlShift && m_seekCallback) {
                            m_seekCallback(5.0);  // 右: 前进5秒
                        }
                        break;
                    }
                    case SDLK_UP:
                        if (m_volumeCallback) m_volumeCallback(5);
                        break;
                    case SDLK_DOWN:
                        if (m_volumeCallback) m_volumeCallback(-5);
                        break;
                    case SDLK_N:
                        if (m_nextCallback) m_nextCallback();
                        break;
                    case SDLK_P:
                        if (m_prevCallback) m_prevCallback();
                        break;
                    case SDLK_PERIOD:
                        if (m_speedCallback) m_speedCallback(0); // 循环速度
                        break;
                    case SDLK_F12:
                        takeScreenshot();
                        break;
                    case SDLK_L:
                        if (!(event.key.mod & SDL_KMOD_CTRL)) {
                            // L 键循环切换循环模式
                            m_loopMode = (m_loopMode + 1) % 3;
                            if (m_loopModeCallback) m_loopModeCallback(m_loopMode);
                        }
                        break;
                    case SDLK_A:
                        if (!(event.key.mod & SDL_KMOD_CTRL)) {
                            // A 键循环切换画面比例
                            int next = (static_cast<int>(m_aspectMode) + 1) % 4;
                            m_aspectMode = static_cast<AspectMode>(next);
                        }
                        break;
                    case SDLK_T:
                        toggleAlwaysOnTop();
                        break;
                    case SDLK_G:
                        if (m_subtitleSyncCallback) m_subtitleSyncCallback(-500); // 提前 0.5s
                        break;
                    case SDLK_H:
                        if (m_subtitleSyncCallback) m_subtitleSyncCallback(500);  // 延后 0.5s
                        break;
                    case SDLK_LEFTBRACKET:
                        if (m_abLoopCallback) m_abLoopCallback('a');
                        break;
                    case SDLK_RIGHTBRACKET:
                        if (m_abLoopCallback) m_abLoopCallback('b');
                        break;
                    case SDLK_BACKSLASH:
                        if (m_abLoopCallback) m_abLoopCallback('c');
                        break;
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_MOTION:
            m_mouseX = static_cast<int>(event.motion.x);
            m_mouseY = static_cast<int>(event.motion.y);
            m_lastMouseMove = SDL_GetTicks();
            m_showControls = true;
            handleMouseMotion(m_mouseX, m_mouseY);
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            m_mouseX = static_cast<int>(event.button.x);
            m_mouseY = static_cast<int>(event.button.y);
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_mouseDown = true;
                // 先检查右键菜单点击
                if (m_showContextMenu) {
                    handleContextMenuClick(m_mouseX, m_mouseY);
                    break;
                }
                handleMouseButtonDown(m_mouseX, m_mouseY);
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                // 右键菜单
                showContextMenu(m_mouseX, m_mouseY);
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            m_mouseX = static_cast<int>(event.button.x);
            m_mouseY = static_cast<int>(event.button.y);
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_mouseDown = false;
                handleMouseButtonUp(m_mouseX, m_mouseY);
            }
            break;

        case SDL_EVENT_MOUSE_WHEEL: {
            int mx = static_cast<int>(event.wheel.mouse_x);
            int my = static_cast<int>(event.wheel.mouse_y);
            bool overPlaylist = m_showPlaylistPanel && mx >= m_windowWidth - 284 && mx <= m_windowWidth - 24
                                && my >= m_menuBarHeight + 10 && my <= m_windowHeight - m_controlHeight - 34;
            bool overEpisode = m_showEpisodePanel && mx >= 24 && mx <= 284
                               && my >= m_menuBarHeight + 10 && my <= m_windowHeight - m_controlHeight - 34;
            if (overPlaylist) {
                m_playlistScrollOffset -= static_cast<int>(event.wheel.y);
                if (m_playlistScrollOffset < 0) m_playlistScrollOffset = 0;
            } else if (overEpisode) {
                m_episodeScrollOffset -= static_cast<int>(event.wheel.y);
                if (m_episodeScrollOffset < 0) m_episodeScrollOffset = 0;
            } else if (m_volumeCallback) {
                m_volumeCallback(event.wheel.y > 0.0f ? 5 : -5);
            }
            break;
        }
    }
}

void SDLRenderer::handleMouseMotion(int x, int y) {
    // 检测悬浮的控件
    ControlType lastHovered = m_hoveredControl;
    m_hoveredControl = getControlAt(x, y, &m_hoveredControlValue);

    // Tooltip 更新（所有可交互控件）
    if (lastHovered != m_hoveredControl) {
        m_tooltipTime = SDL_GetTicks();
        m_tooltipShowTime = 0;
        switch (m_hoveredControl) {
            case ControlType::PlayButton:
                m_tooltip = m_isPlaying ? "暂停  Space" : "播放  Space";
                break;
            case ControlType::StopButton:
                m_tooltip = "停止  S";
                break;
            case ControlType::PrevButton:
                m_tooltip = m_tooltipPrev.empty() ? "上一个 P" : m_tooltipPrev;
                break;
            case ControlType::NextButton:
                m_tooltip = m_tooltipNext.empty() ? "下一个 N" : m_tooltipNext;
                break;
            case ControlType::SpeedButton:
                m_tooltip = "切换速度  .";
                break;
            case ControlType::VolumeButton:
                m_tooltip = "静音  M";
                break;
            case ControlType::VolumeBar:
                m_tooltip = "拖动调节音量";
                break;
            case ControlType::PlaylistButton:
                m_tooltip = "播放列表  Ctrl+L";
                break;
            case ControlType::EpisodePrev:
                m_tooltip = "上一集";
                break;
            case ControlType::EpisodeNext:
                m_tooltip = "下一集";
                break;
            case ControlType::ProgressBar:
                m_tooltip = "拖动跳转";
                break;
            case ControlType::ChapterMarker:
                if (m_hoveredControlValue >= 0 && m_hoveredControlValue < (int)m_chapters.size()) {
                    m_tooltip = m_chapters[m_hoveredControlValue].title;
                    if (m_tooltip.empty()) {
                        m_tooltip = "Chapter " + std::to_string(m_hoveredControlValue + 1);
                    }
                }
                break;
            default:
                m_tooltip.clear();
                m_tooltipShowTime = 0;
                break;
        }
    } else if (m_hoveredControl == ControlType::None) {
        m_tooltip.clear();
        m_tooltipShowTime = 0;
    }

    // 检测菜单栏悬浮
    m_menuBarHovered = (y < m_menuBarHeight);
    
    // 菜单�?hover 自动切换（当已有菜单打开时）
    if (m_menuBarHovered && (m_activeMenu >= 0 || m_menuAnimating)) {
        int menuX = 10;
        for (int i = 0; i < (int)m_menus.size(); i++) {
            if (m_menus[i].label == "章节" && !m_hasChapters) continue;
            int textW = getTextWidth(m_menus[i].label);
            int itemWidth = textW + 20;
            if (m_mouseX >= menuX && m_mouseX <= menuX + itemWidth) {
                if (m_activeMenu != i) {
                    m_activeMenu = i;
                    m_pendingMenu = i;
                    m_menuAnimStartTime = SDL_GetTicks();
                    m_menuAnimating = true;
                }
                break;
            }
            menuX += itemWidth + 10;
        }
    }
    
    //  处理进度条拖动（仅更�?UI，不 seek�?
    if (m_draggingProgress) {
        for (const auto& rect : m_controlRects) {
            if (rect.type == ControlType::ProgressBar) {
                m_dragProgressRatio = static_cast<float>(x - rect.x) / rect.w;
                m_dragProgressRatio = std::max(0.0f, std::min(1.0f, m_dragProgressRatio));
                break;
            }
        }
    }
    
    // 处理音量拖动
    if (m_draggingVolume && m_volumeCallback) {
        for (const auto& rect : m_controlRects) {
            if (rect.type == ControlType::VolumeBar) {
                float ratio = static_cast<float>(x - rect.x) / rect.w;
                ratio = std::max(0.0f, std::min(1.0f, ratio));
                m_volumeCallback(static_cast<int>(ratio * 100) + 1000);
                break;
            }
        }
    }
    
    // 无边框模式：处理 resize 区域光标�?resize 拖动
    if (m_borderless && m_windowFrame) {
        FrameHitTest hit = m_windowFrame->hitTest(x, y);
        // 如果不在控件上，更新光标�?resize 光标
        if (m_hoveredControl == ControlType::None) {
            switch (hit) {
                case FrameHitTest::ResizeLeft:
                case FrameHitTest::ResizeRight:
                    SDL_SetCursor(m_cursorSizeWE);
                    break;
                case FrameHitTest::ResizeTop:
                case FrameHitTest::ResizeBottom:
                    SDL_SetCursor(m_cursorSizeNS);
                    break;
                case FrameHitTest::ResizeTopLeft:
                case FrameHitTest::ResizeBottomRight:
                    SDL_SetCursor(m_cursorSizeNWSE);
                    break;
                case FrameHitTest::ResizeTopRight:
                case FrameHitTest::ResizeBottomLeft:
                    SDL_SetCursor(m_cursorSizeNESW);
                    break;
                case FrameHitTest::Caption:
                    SDL_SetCursor(m_cursorDefault);
                    break;
                default:
                    break;
            }
        }

        // 处理 resize 拖动
        if (m_resizingWindow && m_window) {
            int dx = x - m_resizeStartMouseX;
            int dy = y - m_resizeStartMouseY;
            int newX = m_resizeStartWindowX;
            int newY = m_resizeStartWindowY;
            int newW = m_resizeStartWindowW;
            int newH = m_resizeStartWindowH;

            switch (m_resizeMode) {
                case ResizeMode::Left:
                    newX += dx;
                    newW -= dx;
                    break;
                case ResizeMode::Right:
                    newW += dx;
                    break;
                case ResizeMode::Top:
                    newY += dy;
                    newH -= dy;
                    break;
                case ResizeMode::Bottom:
                    newH += dy;
                    break;
                case ResizeMode::TopLeft:
                    newX += dx;
                    newY += dy;
                    newW -= dx;
                    newH -= dy;
                    break;
                case ResizeMode::TopRight:
                    newY += dy;
                    newW += dx;
                    newH -= dy;
                    break;
                case ResizeMode::BottomLeft:
                    newX += dx;
                    newW -= dx;
                    newH += dy;
                    break;
                case ResizeMode::BottomRight:
                    newW += dx;
                    newH += dy;
                    break;
                default:
                    break;
            }

            //  最小窗口尺寸限�?
            const int MIN_W = 400;
            const int MIN_H = 300;
            if (newW < MIN_W) {
                if (m_resizeMode == ResizeMode::Left ||
                    m_resizeMode == ResizeMode::TopLeft ||
                    m_resizeMode == ResizeMode::BottomLeft) {
                    newX = m_resizeStartWindowX + m_resizeStartWindowW - MIN_W;
                }
                newW = MIN_W;
            }
            if (newH < MIN_H) {
                if (m_resizeMode == ResizeMode::Top ||
                    m_resizeMode == ResizeMode::TopLeft ||
                    m_resizeMode == ResizeMode::TopRight) {
                    newY = m_resizeStartWindowY + m_resizeStartWindowH - MIN_H;
                }
                newH = MIN_H;
            }

            SDL_SetWindowPosition(m_window, newX, newY);
            SDL_SetWindowSize(m_window, newW, newH);
        }
    }
}

void SDLRenderer::handleMouseButtonDown(int x, int y) {
    // 无边框模式下优先检�?resize 区域（避免被控件检测拦截）
    if (m_borderless && m_windowFrame && !(SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED)) {
        ResizeMode mode = getResizeModeAt(x, y);
        if (mode != ResizeMode::None) {
            m_resizingWindow = true;
            m_resizeMode = mode;
            m_resizeStartMouseX = x;
            m_resizeStartMouseY = y;
            SDL_GetWindowPosition(m_window, &m_resizeStartWindowX, &m_resizeStartWindowY);
            SDL_GetWindowSize(m_window, &m_resizeStartWindowW, &m_resizeStartWindowH);
            return;
        }
    }

    m_pressedControl = getControlAt(x, y, &m_pressedControlValue);

    //  处理系统按钮（无边框模式下位于菜单栏区域�?
    if (m_pressedControl == ControlType::SysMinButton) {
        SDL_MinimizeWindow(m_window);
        return;
    }
    if (m_pressedControl == ControlType::SysMaxButton) {
        if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) {
            SDL_RestoreWindow(m_window);
        } else {
            SDL_MaximizeWindow(m_window);
        }
        return;
    }
    if (m_pressedControl == ControlType::SysCloseButton) {
        SDL_Event quitEvent;
        quitEvent.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quitEvent);
        return;
    }
    
    // 处理菜单栏区域的点击
    if (y < m_menuBarHeight) {
        bool clickedMenu = handleMenuClick(x, y);
        
        //  无边框模式下点击菜单栏空白处：双击最大化/还原，单击拖�?
        if (m_borderless && !clickedMenu && m_windowFrame) {
            uint64_t now = SDL_GetTicks();
            if (now - m_lastClickTime < 300 &&
                std::abs(x - m_lastClickX) < 5 && std::abs(y - m_lastClickY) < 5) {
                // 双击：最大化/还原
                if (m_windowFrame->isMaximized()) {
                    m_windowFrame->restoreWindow();
                } else {
                    m_windowFrame->maximizeWindow();
                }
                m_lastClickTime = 0;
            } else {
                //  单击：记录时间并开始拖�?
                m_lastClickTime = now;
                m_lastClickX = x;
                m_lastClickY = y;
                FrameHitTest hit = m_windowFrame->hitTest(x, y);
                if (hit == FrameHitTest::Caption) {
                    m_windowFrame->startDrag();
                }
            }
        }
        return;
    }
    
    // 如果点击了菜单区域之外，关闭菜单
    if (m_activeMenu >= 0 && y >= m_menuBarHeight) {
        bool inMenu = false;
        // 检查是否在打开的菜单内（使用与 renderMenuBar/renderMenu 一致的位置和尺寸）
        int menuX = 10;
        for (int i = 0; i < m_activeMenu && i < (int)m_menus.size(); i++) {
            if (m_menus[i].label == "章节" && !m_hasChapters) continue;
            menuX += getTextWidth(m_menus[i].label) + 20 + 10;
        }
        // 计算实际菜单宽度（与 renderMenu 一致）
        const Menu& activeMenu = m_menus[m_activeMenu];
        int labelMaxW = 0;
        int shortcutMaxW = 0;
        for (const auto& item : activeMenu.items) {
            if (!item.separator && item.enabled) {
                int lw = getTextWidth(item.label, 12);
                if (lw > labelMaxW) labelMaxW = lw;
                if (!item.shortcut.empty()) {
                    int sw = getTextWidth(item.shortcut, 11);
                    if (sw > shortcutMaxW) shortcutMaxW = sw;
                }
            }
        }
        int menuWidth = 20 + labelMaxW;
        if (shortcutMaxW > 0) menuWidth += 24 + shortcutMaxW;
        if (menuWidth < 140) menuWidth = 140;
        int menuHeight = (int)activeMenu.items.size() * 24 + 8;
        if (x >= menuX && x <= menuX + menuWidth && y >= m_menuBarHeight && y <= m_menuBarHeight + menuHeight) {
            inMenu = true;
            // 处理菜单项点击
            int itemIndex = (y - m_menuBarHeight - 4) / 24;
            if (itemIndex >= 0 && itemIndex < (int)activeMenu.items.size()) {
                const auto& item = activeMenu.items[itemIndex];
                if (!item.separator && item.enabled && m_menuCallback) {
                    m_menuCallback(item.id);
                    closeAllMenus();
                    // 防止 handleMouseButtonUp 触发菜单下方的控件点击
                    m_pressedControl = ControlType::None;
                }
            }
        }
        if (!inMenu) {
            closeAllMenus();
        }
        return;
    }
    
    // 处理控件点击
    switch (m_pressedControl) {
        case ControlType::PlayButton:
            if (m_playPauseCallback) m_playPauseCallback();
            break;
        case ControlType::StopButton:
            if (m_stopCallback) m_stopCallback();
            break;
        case ControlType::PrevButton:
            if (m_prevCallback) m_prevCallback();
            break;
        case ControlType::NextButton:
            if (m_nextCallback) m_nextCallback();
            break;
        case ControlType::ProgressBar:
            m_draggingProgress = true;
            // 记录拖动位置用于实时渲染，释放时�?seek
            for (const auto& rect : m_controlRects) {
                if (rect.type == ControlType::ProgressBar) {
                    m_dragProgressRatio = static_cast<float>(x - rect.x) / rect.w;
                    m_dragProgressRatio = std::max(0.0f, std::min(1.0f, m_dragProgressRatio));
                    break;
                }
            }
            break;
        case ControlType::ChapterMarker:
            if (m_seekCallback && m_pressedControlValue >= 0 && m_pressedControlValue < (int)m_chapters.size()) {
                int64_t chapterTime = m_chapters[m_pressedControlValue].startTime;
                Logger::instance().info("[Chapter] Clicked chapter " + std::to_string(m_pressedControlValue) + 
                    ", startTime=" + std::to_string(chapterTime) + 
                    ", duration=" + std::to_string(m_lastDuration));
                if (m_lastDuration > 0) {
                    double ratio = static_cast<double>(chapterTime) / static_cast<double>(m_lastDuration);
                    Logger::instance().info("[Chapter] ratio=" + std::to_string(ratio) + 
                        ", seekParam=" + std::to_string(1000.0 + ratio * 1000.0));
                    m_seekCallback(1000.0 + ratio * 1000.0); // 绝对位置编码
                }
            }
            break;
        case ControlType::VolumeButton:
            if (m_muteCallback) m_muteCallback();
            break;
        case ControlType::VolumeBar:
            m_draggingVolume = true;
            // 立即设置音量
            for (const auto& rect : m_controlRects) {
                if (rect.type == ControlType::VolumeBar) {
                    float ratio = static_cast<float>(x - rect.x) / rect.w;
                    ratio = std::max(0.0f, std::min(1.0f, ratio));
                    if (m_volumeCallback) m_volumeCallback(static_cast<int>(ratio * 100) + 1000);
                    break;
                }
            }
            break;
        case ControlType::SpeedButton:
            if (m_speedCallback) m_speedCallback(0);
            break;
        case ControlType::PlaylistButton:
            m_showPlaylistPanel = !m_showPlaylistPanel;
            break;
        default:
            break;
    }
}

void SDLRenderer::handleMouseButtonUp(int x, int y) {
    // 进度条释放时才执�?seek
    if (m_draggingProgress && m_seekCallback) {
        m_seekCallback(m_dragProgressRatio * 1000 + 1000); // 传回绝对位置 (1000~2000)
    }

    //  列表/面板项的单击操作在鼠标释放时触发（按下和释放需在同一个控件上�?
    if (!m_draggingProgress && !m_draggingVolume && m_pressedControl != ControlType::None) {
        int releaseValue = 0;
        ControlType releaseControl = getControlAt(x, y, &releaseValue);
        if (releaseControl == m_pressedControl && releaseValue == m_pressedControlValue) {
            switch (m_pressedControl) {
                case ControlType::PlaylistItem:
                    if (m_playlistItemCallback) m_playlistItemCallback(static_cast<size_t>(m_pressedControlValue));
                    break;
                case ControlType::EpisodeItem:
                    if (m_episodeItemCallback) m_episodeItemCallback(static_cast<size_t>(m_pressedControlValue));
                    break;
                case ControlType::EpisodePrev:
                    if (m_episodePrevCallback) m_episodePrevCallback();
                    break;
                case ControlType::EpisodeNext:
                    if (m_episodeNextCallback) m_episodeNextCallback();
                    break;
                default:
                    break;
            }
        }
    }

    m_draggingProgress = false;
    m_draggingVolume = false;
    m_resizingWindow = false;
    m_resizeMode = ResizeMode::None;
    m_pressedControl = ControlType::None;
}

ControlType SDLRenderer::getControlAt(int x, int y, int* outValue) {
    for (const auto& rect : m_controlRects) {
        if (x >= rect.x && x <= rect.x + rect.w &&
            y >= rect.y && y <= rect.y + rect.h) {
            if (outValue) *outValue = rect.value;
            return rect.type;
        }
    }
    return ControlType::None;
}

ResizeMode SDLRenderer::getResizeModeAt(int x, int y) const {
    if (!m_borderless || !m_windowFrame) {
        return ResizeMode::None;
    }
    if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) {
        return ResizeMode::None;
    }

    FrameHitTest hit = m_windowFrame->hitTest(x, y);
    switch (hit) {
        case FrameHitTest::ResizeLeft:        return ResizeMode::Left;
        case FrameHitTest::ResizeRight:       return ResizeMode::Right;
        case FrameHitTest::ResizeTop:         return ResizeMode::Top;
        case FrameHitTest::ResizeBottom:      return ResizeMode::Bottom;
        case FrameHitTest::ResizeTopLeft:     return ResizeMode::TopLeft;
        case FrameHitTest::ResizeTopRight:    return ResizeMode::TopRight;
        case FrameHitTest::ResizeBottomLeft:  return ResizeMode::BottomLeft;
        case FrameHitTest::ResizeBottomRight: return ResizeMode::BottomRight;
        default:                              return ResizeMode::None;
    }
}

} // namespace VideoPlay
