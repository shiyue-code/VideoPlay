#include "renderer/sdlrenderer.h"
#include "renderer/sdlrenderer_internal.h"
#include "utils/logger.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto logger = Logger::get("renderer.events");
    return *logger;
}

// event.key.mod 来自 SDL 内部按键状态表：窗口创建/聚焦前已按住的
// 修饰键（先按住 Ctrl 再启动程序）没有对应 keydown，mod 会缺失。
// 用系统实时按键状态补齐（只补不删），Ctrl+← 等才不会退化成裸方向键
SDL_Keymod liveKeyMods(SDL_Keymod eventMod) {
    SDL_Keymod mods = eventMod | SDL_GetModState();
#ifdef _WIN32
    if ((GetAsyncKeyState(VK_LCONTROL) & 0x8000) ||
        (GetAsyncKeyState(VK_RCONTROL) & 0x8000)) {
        mods |= SDL_KMOD_CTRL;
    }
    if ((GetAsyncKeyState(VK_LSHIFT) & 0x8000) ||
        (GetAsyncKeyState(VK_RSHIFT) & 0x8000)) {
        mods |= SDL_KMOD_SHIFT;
    }
    if ((GetAsyncKeyState(VK_LMENU) & 0x8000) ||
        (GetAsyncKeyState(VK_RMENU) & 0x8000)) {
        mods |= SDL_KMOD_ALT;
    }
#endif
    return mods;
}

}


bool SDLRenderer::hasSearchSelection() const
{
    return m_searchSelectionStart != m_searchSelectionEnd;
}

std::pair<size_t, size_t> SDLRenderer::searchSelectionRange() const
{
    size_t start = std::min(m_searchSelectionStart, m_searchSelectionEnd);
    size_t end = std::max(m_searchSelectionStart, m_searchSelectionEnd);
    start = std::min(start, m_searchQuery.size());
    end = std::min(end, m_searchQuery.size());
    return {start, end};
}

void SDLRenderer::clearSearchSelection()
{
    m_searchSelectionStart = m_searchQuery.size();
    m_searchSelectionEnd = m_searchQuery.size();
}

void SDLRenderer::selectAllSearchQuery()
{
    m_searchSelectionStart = 0;
    m_searchSelectionEnd = m_searchQuery.size();
}

void SDLRenderer::deleteSearchSelection()
{
    if (!hasSearchSelection()) {
        return;
    }

    auto [start, end] = searchSelectionRange();
    m_searchQuery.erase(start, end - start);
    m_searchSelectionStart = start;
    m_searchSelectionEnd = start;
}

void SDLRenderer::insertSearchText(const std::string& text)
{
    if (text.empty()) {
        return;
    }

    deleteSearchSelection();
    size_t insertPos = std::min(m_searchSelectionEnd, m_searchQuery.size());
    m_searchQuery.insert(insertPos, text);
    m_searchSelectionStart = insertPos + text.size();
    m_searchSelectionEnd = m_searchSelectionStart;
}

void SDLRenderer::deleteSearchCharBefore()
{
    if (hasSearchSelection()) {
        deleteSearchSelection();
        return;
    }
    if (m_searchQuery.empty()) {
        return;
    }

    size_t erasePos = m_searchQuery.size();
    while (erasePos > 0 && (m_searchQuery[erasePos - 1] & 0xC0) == 0x80) {
        --erasePos;
    }
    if (erasePos > 0) {
        --erasePos;
    }
    m_searchQuery.erase(erasePos);
    m_searchSelectionStart = erasePos;
    m_searchSelectionEnd = erasePos;
}

void SDLRenderer::deleteSearchCharAfter()
{
    if (hasSearchSelection()) {
        deleteSearchSelection();
    }
}

void SDLRenderer::copySearchSelection()
{
    if (!hasSearchSelection()) {
        return;
    }

    auto [start, end] = searchSelectionRange();
    SDL_SetClipboardText(m_searchQuery.substr(start, end - start).c_str());
    showOSD(OSDType::Info, "已复制");
}

void SDLRenderer::cutSearchSelection()
{
    if (!hasSearchSelection()) {
        return;
    }

    copySearchSelection();
    deleteSearchSelection();
}

void SDLRenderer::pasteSearchClipboard()
{
    if (!SDL_HasClipboardText()) {
        return;
    }

    char* text = SDL_GetClipboardText();
    if (text) {
        insertSearchText(text);
        SDL_free(text);
    }
}

void SDLRenderer::copyChatMessage(size_t index)
{
    std::lock_guard<std::mutex> lock(m_chatMutex);
    if (index >= m_chatHistory.size()) {
        return;
    }

    SDL_SetClipboardText(m_chatHistory[index].text.c_str());
    showOSD(OSDType::Info, "已复制回答");
}

void SDLRenderer::copyLastAIChatMessage()
{
    std::lock_guard<std::mutex> lock(m_chatMutex);
    for (auto it = m_chatHistory.rbegin(); it != m_chatHistory.rend(); ++it) {
        if (!it->isUser) {
            SDL_SetClipboardText(it->text.c_str());
            showOSD(OSDType::Info, "已复制回答");
            return;
        }
    }
}



void SDLRenderer::handleEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_TEXT_INPUT:
            if (m_isSearchInputFocused && event.text.text) {
                insertSearchText(event.text.text);
            }
            break;

        case SDL_EVENT_QUIT:
            m_initialized = false;
            break;

        case SDL_EVENT_DROP_FILE: {
            const char* droppedFile = event.drop.data;
            if (droppedFile && m_callbacks.fileDrop) {
                m_callbacks.fileDrop(droppedFile);
            }
            break;
        }

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            // 失焦期间松开的键收不到 keyup，清空防止残留修饰键污染后续按键
            m_heldKeyMods.clear();
            break;

        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            m_windowWidth = event.window.data1;
            m_windowHeight = event.window.data2;
            applySmallWindowPanelGuard();
            break;

        case SDL_EVENT_AUDIO_DEVICE_ADDED:
        case SDL_EVENT_AUDIO_DEVICE_REMOVED:
            refreshAudioOutputDevices();
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
            // 合并系统实时修饰键：启动早期 SDL 内部状态可能漏掉
            // 窗口创建前已按住的 Ctrl/Shift，见 liveKeyMods
            SDL_Keymod mods = liveKeyMods(event.key.mod);
            // 自动重复的按键沿用首次按下时的修饰键：按住 Ctrl+→ 期间
            // 先松 Ctrl，后续 repeat 不能退化成裸 → 触发 +5s seek
            if (pressed) {
                if (event.key.repeat) {
                    auto held = m_heldKeyMods.find(event.key.scancode);
                    if (held != m_heldKeyMods.end()) {
                        mods = static_cast<SDL_Keymod>(mods | held->second);
                    }
                } else {
                    m_heldKeyMods[event.key.scancode] = mods;
                }
            } else {
                m_heldKeyMods.erase(event.key.scancode);
            }

            //  菜单快捷键处理
            if (pressed) {
                // Ctrl+O 打开文件
                if (event.key.key == SDLK_O && (mods & SDL_KMOD_CTRL)) {
                    if (m_callbacks.fileOpen) m_callbacks.fileOpen();
                    break;
                }

                // 处理搜索输入框快捷键
                if (m_isSearchInputFocused) {
                    bool ctrl = (mods & SDL_KMOD_CTRL) != 0;
                    if (ctrl && event.key.key == SDLK_A) {
                        selectAllSearchQuery();
                        return;
                    } else if (ctrl && event.key.key == SDLK_C) {
                        if (hasSearchSelection()) {
                            copySearchSelection();
                        } else {
                            copyLastAIChatMessage();
                        }
                        return;
                    } else if (ctrl && event.key.key == SDLK_X) {
                        cutSearchSelection();
                        return;
                    } else if (ctrl && event.key.key == SDLK_V) {
                        pasteSearchClipboard();
                        return;
                    } else if (event.key.key == SDLK_BACKSPACE) {
                        deleteSearchCharBefore();
                        return; // 拦截按键
                    } else if (event.key.key == SDLK_DELETE) {
                        deleteSearchCharAfter();
                        return; // 拦截按键
                    } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                        if (!m_searchQuery.empty() && m_callbacks.search) {
                            m_callbacks.search(m_searchQuery);
                            m_searchQuery.clear(); // 发送后清空
                            clearSearchSelection();
                        }
                        return; // 拦截按键
                    } else if (event.key.key == SDLK_ESCAPE) {
                        toggleSearchPanel(); // 关闭搜索面板
                        return;
                    }
                    return; // 焦点在输入框时拦截所有按键，防止空格暂停等
                } else if (m_showSearchPanel && event.key.key == SDLK_C &&
                           (mods & SDL_KMOD_CTRL)) {
                    copyLastAIChatMessage();
                    return;
                }
            }
            
            if (m_callbacks.key) {
                m_callbacks.key(event.key.key, pressed);
            }

            //  内置快捷�?
            if (pressed) {
                switch (event.key.key) {
                    case SDLK_SPACE:
                        if (m_callbacks.playPause) m_callbacks.playPause();
                        break;
                    case SDLK_ESCAPE:
                        if (m_showSearchPanel) {
                            toggleSearchPanel();
                        } else if (m_infoPanel != InfoPanelKind::None) {
                            closeInfoPanel();
                        } else if (m_fullscreen) {
                            toggleFullscreen();
                        }
                        m_menuManager->closeAllMenus();
                        m_menuManager->hideContextMenu();
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        if (m_infoPanel == InfoPanelKind::Custom) {
                            closeInfoPanel();
                        }
                        break;
                    case SDLK_TAB:
                        toggleMediaInfoPanel();
                        break;
                    case SDLK_F:
                        toggleFullscreen();
                        break;
                    case SDLK_I:
                        // I 切换右上角 A/V/Diff 与统计信息行
                        if (!(mods & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
                            m_showSyncInfo = !m_showSyncInfo;
                        }
                        break;
                    case SDLK_S:
                        if (m_callbacks.stop) m_callbacks.stop();
                        break;
                    case SDLK_LEFT: {
                        bool ctrlShift = (mods & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) == (SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
                        bool ctrlOnly = (mods & SDL_KMOD_CTRL) && !(mods & SDL_KMOD_SHIFT);
                        bool shiftOnly = (mods & SDL_KMOD_SHIFT) && !(mods & SDL_KMOD_CTRL);
                        if (ctrlShift && m_callbacks.episodePrev) {
                            m_callbacks.episodePrev();
                        } else if (ctrlOnly && m_callbacks.frameStep) {
                            m_callbacks.frameStep(-1); // Ctrl+左: 上一帧
                        } else if (shiftOnly && m_callbacks.seek) {
                            m_callbacks.seek(-30.0); // Shift+左: 后退30秒
                        } else if (!ctrlShift && m_callbacks.seek) {
                            m_callbacks.seek(-5.0); // 左: 后退5秒
                        }
                        break;
                    }
                    case SDLK_RIGHT: {
                        bool ctrlShift = (mods & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) == (SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
                        bool ctrlOnly = (mods & SDL_KMOD_CTRL) && !(mods & SDL_KMOD_SHIFT);
                        bool shiftOnly = (mods & SDL_KMOD_SHIFT) && !(mods & SDL_KMOD_CTRL);
                        if (ctrlShift && m_callbacks.episodeNext) {
                            m_callbacks.episodeNext();
                        } else if (ctrlOnly && m_callbacks.frameStep) {
                            m_callbacks.frameStep(1); // Ctrl+右: 下一帧
                        } else if (shiftOnly && m_callbacks.seek) {
                            m_callbacks.seek(30.0);  // Shift+右: 前进30秒
                        } else if (!ctrlShift && m_callbacks.seek) {
                            m_callbacks.seek(5.0);  // 右: 前进5秒
                        }
                        break;
                    }
                    case SDLK_COMMA:
                        if (m_callbacks.frameStep) m_callbacks.frameStep(-1); // ,: 上一帧
                        break;
                    case SDLK_UP:
                        if ((mods & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) == (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) {
                            adjustContrast(0.1f);
                        } else if (mods & SDL_KMOD_CTRL) {
                            adjustBrightness(0.05f);
                        } else if (m_callbacks.volume) {
                            m_callbacks.volume(5);
                        }
                        break;
                    case SDLK_DOWN:
                        if ((mods & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) == (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) {
                            adjustContrast(-0.1f);
                        } else if (mods & SDL_KMOD_CTRL) {
                            adjustBrightness(-0.05f);
                        } else if (m_callbacks.volume) {
                            m_callbacks.volume(-5);
                        }
                        break;
                    case SDLK_0:
                    case SDLK_KP_0:
                        if (mods & SDL_KMOD_CTRL) {
                            resetVideoFilter();
                        }
                        break;
                    case SDLK_C:
                        if (!(mods & SDL_KMOD_CTRL)) {
                            toggleVideoControlPanel();
                        }
                        break;
                    case SDLK_F6:
                        toggleVideoControlPanel();
                        break;
                    case SDLK_N:
                        if (m_callbacks.next) m_callbacks.next();
                        break;
                    case SDLK_P:
                        if (m_callbacks.prev) m_callbacks.prev();
                        break;
                    case SDLK_PERIOD:
                        if (m_callbacks.speed) m_callbacks.speed(0); // 循环速度
                        break;
                    case SDLK_F12:
                        if (mods & SDL_KMOD_SHIFT) {
                            if (m_callbacks.screenshotClipboard) m_callbacks.screenshotClipboard();
                        } else if (m_callbacks.screenshot) {
                            m_callbacks.screenshot();
                        }
                        break;
                    case SDLK_L:
                        if (!(mods & SDL_KMOD_CTRL)) {
                            // L 键循环切换循环模式
                            m_loopMode = (m_loopMode + 1) % 3;
                            if (m_callbacks.loopMode) m_callbacks.loopMode(m_loopMode);
                        }
                        break;
                    case SDLK_A:
                        if (!(mods & SDL_KMOD_CTRL)) {
                            // A 键循环切换画面比例
                            int next = (static_cast<int>(m_aspectMode) + 1) % 4;
                            m_aspectMode = static_cast<AspectMode>(next);
                        }
                        break;
                    case SDLK_T:
                        toggleAlwaysOnTop();
                        break;
                    case SDLK_B:
                        if (mods & SDL_KMOD_SHIFT) {
                            if (m_callbacks.clearBookmarks) m_callbacks.clearBookmarks();
                        } else if (mods & SDL_KMOD_CTRL) {
                            if (m_callbacks.addBookmark) m_callbacks.addBookmark();
                        }
                        break;
                    case SDLK_G:
                        if (mods & SDL_KMOD_CTRL) {
                            if (m_callbacks.gotoTime) m_callbacks.gotoTime(); // Ctrl+G: 跳转到时间
                        } else if (mods & SDL_KMOD_SHIFT) {
                            if (m_callbacks.audioSync) m_callbacks.audioSync(-500);  // 音频提前 0.5s
                        } else {
                            if (m_callbacks.subtitleSync) m_callbacks.subtitleSync(-500); // 字幕提前 0.5s
                        }
                        break;
                    case SDLK_H:
                        if (mods & SDL_KMOD_SHIFT) {
                            if (m_callbacks.audioSync) m_callbacks.audioSync(500);   // 音频延后 0.5s
                        } else {
                            if (m_callbacks.subtitleSync) m_callbacks.subtitleSync(500);  // 字幕延后 0.5s
                        }
                        break;
                    case SDLK_LEFTBRACKET:
                        if (m_callbacks.abLoop) m_callbacks.abLoop('a');
                        break;
                    case SDLK_RIGHTBRACKET:
                        if (m_callbacks.abLoop) m_callbacks.abLoop('b');
                        break;
                    case SDLK_BACKSLASH:
                        if (m_callbacks.abLoop) m_callbacks.abLoop('c');
                        break;
                    case SDLK_DELETE:
                        if (m_hoveredControl == ControlType::PlaylistItem &&
                            m_callbacks.playlistRemove && m_hoveredControlValue >= 0) {
                            m_callbacks.playlistRemove(static_cast<size_t>(m_hoveredControlValue));
                        }
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
                if (m_menuManager && m_menuManager->isContextMenuOpen()) {
                    m_menuManager->handleContextMenuClick(m_mouseX, m_mouseY);
                    break;
                }
                handleMouseButtonDown(m_mouseX, m_mouseY, event.button.clicks);
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                // 右键菜单
                m_menuManager->showContextMenu(m_mouseX, m_mouseY);
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
            SDL_Keymod mod = SDL_GetModState();
            if (mod & SDL_KMOD_SHIFT || (m_menuManager && m_menuManager->getActiveSubmenuId() == static_cast<int>(MenuId::VideoFilter))) {
                // Shift+滚轮 或 展开画面参数子菜单时：无级调节亮度
                adjustBrightness(event.wheel.y > 0.0f ? 0.05f : -0.05f);
                break;
            }
            // 信息面板模态期间滚轮滚动面板内容，不变音量
            if (m_infoPanel != InfoPanelKind::None) {
                m_infoPanelScroll -= static_cast<int>(event.wheel.y * 40.0f);
                m_infoPanelScroll = std::clamp(m_infoPanelScroll, 0, m_infoPanelScrollMax);
                break;
            }
            int mx = static_cast<int>(event.wheel.mouse_x);
            int my = static_cast<int>(event.wheel.mouse_y);
            // 面板随控制栏隐藏时不再响应滚动
            bool overPlaylist = m_showControls && m_showPlaylistPanel && mx >= m_windowWidth - 284 && mx <= m_windowWidth - 24
                                && my >= m_menuBarHeight + 10 && my <= m_windowHeight - m_controlHeight - 34;
            bool overEpisode = m_showControls && m_showEpisodePanel && mx >= 24 && mx <= 284
                               && my >= m_menuBarHeight + 10 && my <= m_windowHeight - m_controlHeight - 34;
            bool overSearch = m_showControls && m_showSearchPanel && mx >= m_windowWidth - 364 && mx <= m_windowWidth - 24
                               && my >= m_menuBarHeight + 10 && my <= m_windowHeight - m_controlHeight - 34;
            if (overSearch) {
                m_searchScrollOffset -= static_cast<int>(event.wheel.y) * 40;
                if (m_searchScrollOffset < 0) m_searchScrollOffset = 0;
            } else if (overPlaylist) {
                m_playlistScrollOffset -= static_cast<int>(event.wheel.y) * 40;
                if (m_playlistScrollOffset < 0) m_playlistScrollOffset = 0;
            } else if (overEpisode) {
                m_episodeScrollOffset -= static_cast<int>(event.wheel.y) * 40;
                if (m_episodeScrollOffset < 0) m_episodeScrollOffset = 0;
            } else if (m_callbacks.volume) {
                m_callbacks.volume(event.wheel.y > 0.0f ? 5 : -5);
            }
            break;
        }
    }
}

void SDLRenderer::handleMouseMotion(int x, int y) {
    // 检测悬浮的控件
    ControlType lastHovered = m_hoveredControl;
    m_hoveredControl = getControlAt(x, y, &m_hoveredControlValue);

    // 信息面板模态：打开期间遮罩下方控件不可悬停，只保留关闭按钮与正文时间戳链接
    if (m_infoPanel != InfoPanelKind::None) {
        m_hoveredControl = ControlType::None;
        for (const auto& rect : m_controlRects) {
            if ((rect.type == ControlType::InfoPanelClose ||
                 rect.type == ControlType::InfoPanelLink) &&
                x >= rect.x && x <= rect.x + rect.w &&
                y >= rect.y && y <= rect.y + rect.h) {
                m_hoveredControl = rect.type;
                m_hoveredControlValue = rect.value;
                break;
            }
        }
    }

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
            case ControlType::VideoPanelToggle:
                m_tooltip = "画面调节  C";
                break;
            case ControlType::VideoPanelClose:
                m_tooltip = "关闭面板";
                break;
            case ControlType::VideoPanelReset:
                m_tooltip = "恢复默认  Ctrl+0";
                break;
            case ControlType::VideoPanelHdrToggle:
                m_tooltip = "切换 HDR 色调映射";
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
                m_tooltip.clear();
                break;
            case ControlType::ChapterMarker:
                if (m_hoveredControlValue >= 0 && m_hoveredControlValue < (int)m_chapters.size()) {
                    m_tooltip = m_chapters[m_hoveredControlValue].title;
                    if (m_tooltip.empty()) {
                        m_tooltip = "Chapter " + std::to_string(m_hoveredControlValue + 1);
                    }
                }
                break;
            case ControlType::BookmarkMarker:
                if (m_hoveredControlValue >= 0 && m_hoveredControlValue < (int)m_bookmarks.size()) {
                    m_tooltip = m_bookmarks[m_hoveredControlValue].title;
                    if (m_tooltip.empty()) {
                        m_tooltip = "Bookmark " + std::to_string(m_hoveredControlValue + 1);
                    }
                }
                break;
            case ControlType::SearchMessageCopy:
                m_tooltip = "复制回答";
                break;
            case ControlType::InfoPanelClose:
                m_tooltip = "关闭";
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

    if (m_menuManager && m_menuManager->isMenuOpen()) {
        m_menuManager->handleMouseMove(x, y);
    }

    
    if (m_mouseDown && m_pressedControl == ControlType::PlaylistItem && m_playlistDragFrom >= 0) {
        if (!m_playlistDragging && std::abs(y - m_playlistDragStartY) > 6) {
            m_playlistDragging = true;
        }
        if (m_playlistDragging) {
            int hitValue = 0;
            ControlType hit = getControlAt(x, y, &hitValue);
            if (hit == ControlType::PlaylistItem) {
                m_playlistDragTo = hitValue;
            } else if (m_playlistItemCount > 0) {
                bool overPlaylist = m_showControls && m_showPlaylistPanel && x >= m_windowWidth - 284 && x <= m_windowWidth - 24
                    && y >= m_menuBarHeight + 10 && y <= m_windowHeight - m_controlHeight - 34;
                if (overPlaylist) {
                    m_playlistDragTo = (y > m_playlistDragStartY) ? m_playlistItemCount - 1 : 0;
                }
            }
        }
    }

    //  处理进度条拖动（仅更新 UI，不 seek）
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
    if (m_draggingVolume && m_callbacks.volume) {
        for (const auto& rect : m_controlRects) {
            if (rect.type == ControlType::VolumeBar) {
                float ratio = static_cast<float>(x - rect.x) / rect.w;
                ratio = std::max(0.0f, std::min(1.0f, ratio));
                m_callbacks.volume(kVolumeAbsoluteBase + static_cast<int>(ratio * 100));
                break;
            }
        }
    }

    // 处理画面参数滑块拖动
    if (m_draggingBrightness) {
        for (const auto& rect : m_controlRects) {
            if (rect.type == ControlType::VideoPanelBrightnessBar) {
                float ratio = static_cast<float>(x - rect.x) / rect.w;
                ratio = std::clamp(ratio, 0.0f, 1.0f);
                m_videoFilterConfig.brightness = -1.0f + ratio * 2.0f;
                m_videoFilterConfig.enabled = true;
                if (m_callbacks.videoFilterConfig) m_callbacks.videoFilterConfig(m_videoFilterConfig);
                break;
            }
        }
    }
    if (m_draggingContrast) {
        for (const auto& rect : m_controlRects) {
            if (rect.type == ControlType::VideoPanelContrastBar) {
                float ratio = static_cast<float>(x - rect.x) / rect.w;
                ratio = std::clamp(ratio, 0.0f, 1.0f);
                m_videoFilterConfig.contrast = ratio * 2.0f;
                m_videoFilterConfig.enabled = true;
                if (m_callbacks.videoFilterConfig) m_callbacks.videoFilterConfig(m_videoFilterConfig);
                break;
            }
        }
    }
    if (m_draggingSaturation) {
        for (const auto& rect : m_controlRects) {
            if (rect.type == ControlType::VideoPanelSaturationBar) {
                float ratio = static_cast<float>(x - rect.x) / rect.w;
                ratio = std::clamp(ratio, 0.0f, 1.0f);
                m_videoFilterConfig.saturation = ratio * 3.0f;
                m_videoFilterConfig.enabled = true;
                if (m_callbacks.videoFilterConfig) m_callbacks.videoFilterConfig(m_videoFilterConfig);
                break;
            }
        }
    }
    
    // 无边框模式：处理 resize 区域光标�?resize 拖动
    if (m_borderless && m_windowFrame && !m_windowFrame->usesNativeResize()) {
        FrameHitTest hit = m_windowFrame->hitTest(x, y);
        // 如果不在控件上，更新光标�?resize 光标
        if (m_hoveredControl == ControlType::None && !m_mouseDown) {
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

void SDLRenderer::handleMouseButtonDown(int x, int y, int clicks) {
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
    if (m_pressedControl == ControlType::EpisodeItem) {
        logger().info("[Episode] Mouse down on item " + std::to_string(m_pressedControlValue));
    }
    if (m_pressedControl == ControlType::PlaylistItem) {
        m_playlistDragFrom = m_pressedControlValue;
        m_playlistDragTo = m_pressedControlValue;
        m_playlistDragStartY = y;
        m_playlistDragging = false;
        logger().info("[Playlist] Mouse down on item " + std::to_string(m_pressedControlValue));
    }

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
        bool clickedMenu = m_menuManager->handleMenuClick(x, y);
        
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
    
    // 如果菜单已打开，在菜单栏下方点击交给 MenuManager 处理
    if (m_menuManager && m_menuManager->isMenuOpen() && y >= m_menuBarHeight) {
        bool inMenu = m_menuManager->handleMenuClick(x, y);
        if (!inMenu) {
            m_menuManager->closeAllMenus();
            if (m_pressedControl != ControlType::SearchInput &&
                m_pressedControl != ControlType::SearchPanelToggle) {
                m_isSearchInputFocused = false;
                SDL_StopTextInput(m_window);
            }
        }
        m_pressedControl = ControlType::None;
        return;
    }

    // 信息面板模态：× 关闭；正文时间戳链接先回调再关闭；面板内点击只吞掉事件；
    // 面板外点击关闭，不透传给遮罩下的控制栏/进度条（m_pressedControl 置空避免松开时误触发）
    if (m_infoPanel != InfoPanelKind::None) {
        for (const auto& rect : m_controlRects) {
            if ((rect.type != ControlType::InfoPanelClose &&
                 rect.type != ControlType::InfoPanelLink) ||
                x < rect.x || x > rect.x + rect.w ||
                y < rect.y || y > rect.y + rect.h) {
                continue;
            }
            if (rect.type == ControlType::InfoPanelClose) {
                closeInfoPanel();
            } else if (rect.value >= 0 && rect.value < static_cast<int>(m_infoPanelLinks.size()) &&
                       m_infoPanel == InfoPanelKind::Custom &&
                       m_customContent.timestampCallback) {
                auto callback = m_customContent.timestampCallback;
                callback(m_infoPanelLinks[rect.value]);
                closeInfoPanel();
            }
            m_pressedControl = ControlType::None;
            return;
        }
        bool inPanel = x >= m_infoPanelRect.x && x <= m_infoPanelRect.x + m_infoPanelRect.w &&
                       y >= m_infoPanelRect.y && y <= m_infoPanelRect.y + m_infoPanelRect.h;
        if (!inPanel) {
            closeInfoPanel();
        }
        m_pressedControl = ControlType::None;
        return;
    }

    // 视频空白区域双击：切换全屏。
    // 到达此处时已排除菜单栏、打开中的菜单与信息面板；
    // 命中无交互控件（ControlType::None）才算落在画面背景上。
    // 底部悬浮控制栏区域不注册背景矩形，按钮间隙同样返回 None，需按几何排除
    if (clicks >= 2 && m_pressedControl == ControlType::None) {
        const int controlBarY = m_windowHeight - m_controlHeight - 24;
        const bool overControlBar = m_showControls &&
            x >= 24 && x <= m_windowWidth - 24 &&
            y >= controlBarY && y <= controlBarY + m_controlHeight;
        if (!overControlBar) {
            toggleFullscreen();
            return;
        }
    }

    // 处理控件点击
    switch (m_pressedControl) {
        case ControlType::PlayButton:
            if (m_callbacks.playPause) m_callbacks.playPause();
            break;
        case ControlType::StopButton:
            if (m_callbacks.stop) m_callbacks.stop();
            break;
        case ControlType::PrevButton:
            if (m_callbacks.prev) m_callbacks.prev();
            break;
        case ControlType::NextButton:
            if (m_callbacks.next) m_callbacks.next();
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
            if (m_callbacks.seek && m_pressedControlValue >= 0 && m_pressedControlValue < (int)m_chapters.size()) {
                int64_t chapterTime = m_chapters[m_pressedControlValue].startTime;
                logger().info("[Chapter] Clicked chapter " + std::to_string(m_pressedControlValue) + 
                    ", startTime=" + std::to_string(chapterTime) + 
                    ", duration=" + std::to_string(m_lastDuration));
                if (m_lastDuration > 0) {
                    double ratio = static_cast<double>(chapterTime) / static_cast<double>(m_lastDuration);
                    logger().info("[Chapter] ratio=" + std::to_string(ratio) + 
                        ", seekParam=" + std::to_string(kSeekAbsoluteBase + ratio * 1000.0));
                    m_callbacks.seek(kSeekAbsoluteBase + ratio * 1000.0); // 绝对位置编码
                }
            }
            break;
        case ControlType::BookmarkMarker:
            if (m_callbacks.bookmarkClick && m_pressedControlValue >= 0 && m_pressedControlValue < (int)m_bookmarks.size()) {
                m_callbacks.bookmarkClick(m_pressedControlValue);
            }
            break;
        case ControlType::VolumeButton:
            if (m_callbacks.mute) m_callbacks.mute();
            break;
        case ControlType::VolumeBar:
            m_draggingVolume = true;
            // 立即设置音量
            for (const auto& rect : m_controlRects) {
                if (rect.type == ControlType::VolumeBar) {
                    float ratio = static_cast<float>(x - rect.x) / rect.w;
                    ratio = std::max(0.0f, std::min(1.0f, ratio));
                    if (m_callbacks.volume) m_callbacks.volume(kVolumeAbsoluteBase + static_cast<int>(ratio * 100));
                    break;
                }
            }
            break;
        case ControlType::SpeedButton:
            if (m_callbacks.speed) m_callbacks.speed(0);
            break;
        case ControlType::PlaylistButton:
            togglePlaylistPanel();
            break;
        case ControlType::VideoPanelToggle:
            toggleVideoControlPanel();
            break;
        case ControlType::VideoPanelClose:
            m_showVideoControlPanel = false;
            break;
        case ControlType::VideoPanelReset:
            resetVideoFilter();
            break;
        case ControlType::VideoPanelHdrToggle: {
            if (m_videoFilterConfig.hdrToneMap == HdrToneMapMode::Auto) {
                m_videoFilterConfig.hdrToneMap = HdrToneMapMode::On;
            } else if (m_videoFilterConfig.hdrToneMap == HdrToneMapMode::On) {
                m_videoFilterConfig.hdrToneMap = HdrToneMapMode::Off;
            } else {
                m_videoFilterConfig.hdrToneMap = HdrToneMapMode::Auto;
            }
            if (m_callbacks.videoFilterConfig) {
                m_callbacks.videoFilterConfig(m_videoFilterConfig);
            }
            std::string msg = "HDR 色调映射: ";
            if (m_videoFilterConfig.hdrToneMap == HdrToneMapMode::Auto) msg += "自动";
            else if (m_videoFilterConfig.hdrToneMap == HdrToneMapMode::On) msg += "强制开启";
            else msg += "关闭";
            showOSD(OSDType::Info, msg);
            break;
        }
        case ControlType::VideoPanelBrightnessBar:
            m_draggingBrightness = true;
            for (const auto& rect : m_controlRects) {
                if (rect.type == ControlType::VideoPanelBrightnessBar) {
                    float ratio = static_cast<float>(x - rect.x) / rect.w;
                    ratio = std::clamp(ratio, 0.0f, 1.0f);
                    m_videoFilterConfig.brightness = -1.0f + ratio * 2.0f;
                    m_videoFilterConfig.enabled = true;
                    if (m_callbacks.videoFilterConfig) m_callbacks.videoFilterConfig(m_videoFilterConfig);
                    break;
                }
            }
            break;
        case ControlType::VideoPanelContrastBar:
            m_draggingContrast = true;
            for (const auto& rect : m_controlRects) {
                if (rect.type == ControlType::VideoPanelContrastBar) {
                    float ratio = static_cast<float>(x - rect.x) / rect.w;
                    ratio = std::clamp(ratio, 0.0f, 1.0f);
                    m_videoFilterConfig.contrast = ratio * 2.0f;
                    m_videoFilterConfig.enabled = true;
                    if (m_callbacks.videoFilterConfig) m_callbacks.videoFilterConfig(m_videoFilterConfig);
                    break;
                }
            }
            break;
        case ControlType::VideoPanelSaturationBar:
            m_draggingSaturation = true;
            for (const auto& rect : m_controlRects) {
                if (rect.type == ControlType::VideoPanelSaturationBar) {
                    float ratio = static_cast<float>(x - rect.x) / rect.w;
                    ratio = std::clamp(ratio, 0.0f, 1.0f);
                    m_videoFilterConfig.saturation = ratio * 3.0f;
                    m_videoFilterConfig.enabled = true;
                    if (m_callbacks.videoFilterConfig) m_callbacks.videoFilterConfig(m_videoFilterConfig);
                    break;
                }
            }
            break;
        case ControlType::SearchPanelToggle:
            toggleSearchPanel();
            break;
        case ControlType::SearchCloseButton:
            toggleSearchPanel();
            break;
        case ControlType::SearchInput:
            m_isSearchInputFocused = true;
            clearSearchSelection();
            SDL_StartTextInput(m_window);
            break;
        default:
            break;
    }
}

void SDLRenderer::handleMouseButtonUp(int x, int y) {
    // 进度条释放时才执行 seek
    if (m_draggingProgress && m_callbacks.seek) {
        m_callbacks.seek(kSeekAbsoluteBase + m_dragProgressRatio * 1000); // 传回绝对位置 (1000~2000)
    }

    if (m_playlistDragging) {
        if (m_callbacks.playlistReorder && m_playlistDragFrom >= 0 &&
            m_playlistDragTo >= 0 && m_playlistDragFrom != m_playlistDragTo) {
            m_callbacks.playlistReorder(static_cast<size_t>(m_playlistDragFrom),
                                      static_cast<size_t>(m_playlistDragTo));
        }
        m_playlistDragging = false;
        m_playlistDragFrom = -1;
        m_playlistDragTo = -1;
        m_draggingProgress = false;
        m_draggingVolume = false;
        m_pressedControl = ControlType::None;
        return;
    }

    // 信息面板模态期间，释�?不透传给遮罩下的控件（按下在面板打开前的残留状态也清掉�?
    if (m_infoPanel != InfoPanelKind::None) {
        m_pressedControl = ControlType::None;
    }

    // 列表/面板项的单击操作在鼠标释放时触发（按下和释放需在同一个控件上）
    if (!m_draggingProgress && !m_draggingVolume && m_pressedControl != ControlType::None) {
        int releaseValue = 0;
        ControlType releaseControl = getControlAt(x, y, &releaseValue);
        if (releaseControl == m_pressedControl && releaseValue == m_pressedControlValue) {
            switch (m_pressedControl) {
                case ControlType::PlaylistItem:
                    if (m_callbacks.playlistItem) m_callbacks.playlistItem(static_cast<size_t>(m_pressedControlValue));
                    break;
                case ControlType::EpisodeItem:
                    logger().info("[Episode] Clicked episode item " + std::to_string(m_pressedControlValue));
                    if (m_callbacks.episodeItem) m_callbacks.episodeItem(static_cast<size_t>(m_pressedControlValue));
                    break;
                case ControlType::EpisodePrev:
                    if (m_callbacks.episodePrev) m_callbacks.episodePrev();
                    break;
                case ControlType::EpisodeNext:
                    if (m_callbacks.episodeNext) m_callbacks.episodeNext();
                    break;
                case ControlType::SearchTimestamp:
                    if (m_callbacks.seek && m_lastDuration > 0) {
                        double ratio = static_cast<double>(m_pressedControlValue) / static_cast<double>(m_lastDuration);
                        ratio = std::max(0.0, std::min(1.0, ratio));
                        m_callbacks.seek(kSeekAbsoluteBase + ratio * 1000.0); // 绝对位置编码
                    }
                    break;
                case ControlType::SearchMessageCopy:
                    copyChatMessage(static_cast<size_t>(m_pressedControlValue));
                    break;
                default:
                    break;
            }
        }
    }

    m_draggingProgress = false;
    m_draggingVolume = false;
    m_draggingBrightness = false;
    m_draggingContrast = false;
    m_draggingSaturation = false;
    m_playlistDragging = false;
    m_playlistDragFrom = -1;
    m_playlistDragTo = -1;
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

FrameHitTest SDLRenderer::captionHitTestAt(int x, int y) {
    if (!m_borderless) return FrameHitTest::None;
    if (y < 0 || y >= m_menuBarHeight) return FrameHitTest::None;

    // 系统按钮（无边框模式下由 renderMenuBar 注册到 m_controlRects）
    int value = 0;
    ControlType control = getControlAt(x, y, &value);
    switch (control) {
        case ControlType::SysMinButton:   return FrameHitTest::MinButton;
        case ControlType::SysMaxButton:   return FrameHitTest::MaxButton;
        case ControlType::SysCloseButton: return FrameHitTest::CloseButton;
        case ControlType::None:           break;
        default:                          return FrameHitTest::None;  // 其他控件交给客户区
    }

    // 菜单展开期间整条菜单栏都交给客户区，保证 hover 切换与点击关闭正常
    if (m_menuManager && m_menuManager->isMenuOpen()) return FrameHitTest::None;

    // 顶部菜单项区域（几何与 renderMenuBar 保持一致）
    if (m_menuManager) {
        int menuX = kTopMenuX;
        const auto& topMenus = m_menuManager->getTopMenus();
        for (size_t i = 0; i < topMenus.size(); i++) {
            if (!m_menuManager->isTopMenuVisible(i)) continue;
            int itemWidth = getTextWidth(topMenus[i]->getLabel()) + kTopMenuPaddingX * 2;
            if (x >= menuX && x <= menuX + itemWidth) return FrameHitTest::None;
            menuX += itemWidth + kTopMenuGap;
        }
    }

    // 剩下的空白区域可用于拖动窗口
    return FrameHitTest::Caption;
}

void SDLRenderer::handleFrameMouse(FrameHitTest hit, FrameMouseAction action) {
    ControlType control = ControlType::None;
    switch (hit) {
        case FrameHitTest::MinButton:   control = ControlType::SysMinButton;   break;
        case FrameHitTest::MaxButton:   control = ControlType::SysMaxButton;   break;
        case FrameHitTest::CloseButton: control = ControlType::SysCloseButton; break;
        default: break;
    }

    switch (action) {
        case FrameMouseAction::Move:
            // 鼠标位于非客户区，SDL 收不到移动事件，这里手动维护 hover 状态
            m_hoveredControl = control;
            m_hoveredControlValue = 0;
            m_mouseX = -1;  // 避免误判菜单项 hover
            m_mouseY = -1;
            m_lastMouseMove = SDL_GetTicks();
            break;

        case FrameMouseAction::Leave:
            m_hoveredControl = ControlType::None;
            m_pressedControl = ControlType::None;
            m_mouseX = -1;
            m_mouseY = -1;
            break;

        case FrameMouseAction::Click:
            if (control == ControlType::SysMinButton) {
                SDL_MinimizeWindow(m_window);
            } else if (control == ControlType::SysMaxButton) {
                if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) {
                    SDL_RestoreWindow(m_window);
                } else {
                    SDL_MaximizeWindow(m_window);
                }
            } else if (control == ControlType::SysCloseButton) {
                SDL_Event quitEvent;
                quitEvent.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&quitEvent);
            }
            break;
    }
}

ResizeMode SDLRenderer::getResizeModeAt(int x, int y) const {
    if (!m_borderless || !m_windowFrame) {
        return ResizeMode::None;
    }
    // 原生窗口框架自行处理缩放，不需要自绘 resize
    if (m_windowFrame->usesNativeResize()) {
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
