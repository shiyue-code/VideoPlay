#include "renderer/sdlrenderer.h"
#include "renderer/sdlrenderer_internal.h"
#include "utils/logger.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <utility>

namespace {

std::string formatBitrate(int64_t bitrate) {
    if (bitrate <= 0) {
        return "N/A";
    }
    std::ostringstream oss;
    if (bitrate >= 1000 * 1000) {
        oss << std::fixed << std::setprecision(2)
            << (static_cast<double>(bitrate) / 1000.0 / 1000.0) << " Mbps";
    } else {
        oss << std::fixed << std::setprecision(0)
            << (static_cast<double>(bitrate) / 1000.0) << " Kbps";
    }
    return oss.str();
}

std::string formatFps(double fps) {
    if (fps <= 0.0) {
        return "N/A";
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << fps;
    return oss.str();
}

size_t utf8CharLength(const std::string& text, size_t pos) {
    if (pos >= text.size()) {
        return 0;
    }

    unsigned char ch = static_cast<unsigned char>(text[pos]);
    if (ch < 0x80) {
        return 1;
    }
    if ((ch & 0xE0) == 0xC0) {
        return (pos + 1 < text.size()) ? 2 : 1;
    }
    if ((ch & 0xF0) == 0xE0) {
        return (pos + 2 < text.size()) ? 3 : 1;
    }
    if ((ch & 0xF8) == 0xF0) {
        return (pos + 3 < text.size()) ? 4 : 1;
    }
    return 1;
}

bool isAsciiDigit(char ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
}

size_t timestampTokenLength(const std::string& text, size_t pos) {
    size_t i = pos;
    if (i + 2 <= text.size() && text.compare(i, 2, "**") == 0) {
        i += 2;
    }

    if (i < text.size() && text[i] == '[') {
        ++i;
    }

    size_t timeStart = i;
    int colonCount = 0;
    while (i < text.size()) {
        if (isAsciiDigit(text[i])) {
            ++i;
            continue;
        }
        if (text[i] == ':') {
            ++colonCount;
            ++i;
            continue;
        }
        break;
    }

    if (colonCount < 1 || colonCount > 2 || i == timeStart) {
        return 0;
    }

    if (i < text.size() && text[i] == ']') {
        ++i;
    }
    if (i + 2 <= text.size() && text.compare(i, 2, "**") == 0) {
        i += 2;
    }

    return i - pos;
}

bool parseTimestampMs(std::string text, int& valueMs) {
    text.erase(std::remove(text.begin(), text.end(), '*'), text.end());
    while (!text.empty() && !isAsciiDigit(text.front())) {
        text.erase(text.begin());
    }
    while (!text.empty() && !isAsciiDigit(text.back())) {
        text.pop_back();
    }

    int h = 0;
    int m = 0;
    int s = 0;
    char tail = '\0';
    if (std::sscanf(text.c_str(), "%d:%d:%d%c", &h, &m, &s, &tail) == 3) {
        valueMs = (h * 3600 + m * 60 + s) * 1000;
        return true;
    }
    if (std::sscanf(text.c_str(), "%d:%d%c", &m, &s, &tail) == 2) {
        valueMs = (m * 60 + s) * 1000;
        return true;
    }
    return false;
}

} // namespace

namespace VideoPlay {


void SDLRenderer::renderUIImpl(int64_t position, int64_t duration, int volume, bool isMuted,
                           bool isPlaying, double speed, const std::string& filename,
                           const std::string& subtitle,
                           const std::vector<std::string>& playlist, size_t currentPlaylistIndex,
                           int64_t audioPts, int64_t videoPts, double avDiff,
                           bool isPreloading) {
    // 清空控件区域
    m_controlRects.clear();
    m_isPlaying = isPlaying;

    // 更新进度条宽度动画（无论控制栏是否显示都更新，保证平滑）
    {
        uint64_t now = SDL_GetTicks();
        float dt = 0.0f;
        if (m_lastProgressAnimTime > 0) {
            dt = std::min((now - m_lastProgressAnimTime) / 1000.0f, 0.05f);
        }
        m_lastProgressAnimTime = now;

        bool isStopped = !isPlaying && !isPreloading && position == 0;
        float targetScale = (duration > 0 && !isStopped) ? 1.0f : 0.0f;
        float diff = targetScale - m_progressBarScale;
        if (std::abs(diff) > 0.0005f) {
            // 指数衰减插值：越接近目标速度越慢，天�?ease-out 效果
            m_progressBarScale += diff * std::min(1.0f, PROGRESS_BAR_ANIM_SPEED * dt);
        } else {
            m_progressBarScale = targetScale;
        }
    }

    // 自动隐藏控制栏（仅在播放状态下，3秒无鼠标操作）
    if (m_showControls && isPlaying && !m_showSearchPanel &&
        SDL_GetTicks() - m_lastMouseMove > 3000) {
        m_showControls = false;
    }

    if (m_showControls) {
        // 菜单栏随控制栏一起显隐
        m_menuManager->renderMenuBar();
        // 底部渐变遮罩，让控制栏自然融入视频
        drawGradientVignette();
        renderControls(position, duration, volume, isMuted, isPlaying, speed, isPreloading);
        // 渲染文件名（左上角，随控制栏自动隐藏）
        if (!filename.empty()) {
            renderFilename(filename);
        }
        // 渲染音视频同步调试信息（右上角，随控制栏自动隐藏）
        renderSyncInfo(audioPts, videoPts, avDiff, m_showPlaylistPanel && !playlist.empty());

        if (m_showSearchPanel) {
            renderSearchPanel();
        }
    } else {
        // 控制栏隐藏时关闭已打开的菜单
        closeAllMenus(false);
    }

    // 侧边面板独立生命周期，不受控制栏自动隐藏影响
    if (m_showPlaylistPanel && !playlist.empty()) {
        renderPlaylistPanel(playlist, currentPlaylistIndex);
    }
    if (m_showEpisodePanel && m_episodeData && !m_episodeData->empty()) {
        renderEpisodePanel();
    }

    // 渲染字幕（始终显示，不受控制栏影响）
    if (!subtitle.empty()) {
        renderSubtitle(subtitle);
    }
    renderSubtitleBitmap(position);

    renderNetworkState();
    renderAIAnalysisOverlay();

    if (m_showMediaInfoPanel) {
        renderMediaInfoPanel();
    }

    renderOSD();

    // 预缓冲加载动画已集成到进度条�?    
    //  渲染打开的菜�?
    m_menuManager->updateMenuAnimation();
    if ((m_activeMenu >= 0 && m_activeMenu < (int)m_menus.size()) || m_menuAnimating) {
        if (m_menuAnimAlpha > 0.01f) {
            constexpr int kTopMenuX = 10;
            constexpr int kTopMenuPaddingX = 16;
            constexpr int kTopMenuGap = 8;
            int menuX = kTopMenuX;
            for (int i = 0; i < m_activeMenu; i++) {
                if (!isTopMenuVisible(static_cast<size_t>(i))) continue;
                int textW = getTextWidth(m_menus[i].label);
                menuX += textW + kTopMenuPaddingX * 2 + kTopMenuGap;
            }
            renderMenu(m_menus[m_activeMenu], menuX, m_menuBarHeight, m_menuAnimAlpha);
        }
    }

    // 渲染 Tooltip
    renderTooltip();

    // 渲染右键上下文菜单
    m_menuManager->renderContextMenu();

    // 自绘 1px 边框，确�?Win10 �?Win11 显示效果完全一�?    // （Win11 �?DWMWA_BORDER_COLOR 是独占特性，Win10 不支持，因此统一�?SDL 自绘�?    // 圆角窗口下不绘制四边直边框，�?DWM 圆角自然呈现
    if (m_borderless && !(SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED)) {
        uint8_t br = COLOR_MENU_BG[0];
        uint8_t bg = COLOR_MENU_BG[1];
        uint8_t bb = COLOR_MENU_BG[2];
        const int r = 8; // 圆角半径
        //  上边框（避开圆角区域�?
        fillRect(r, 0, m_windowWidth - r * 2, 1, br, bg, bb, 255);
        //  下边�?
        fillRect(r, m_windowHeight - 1, m_windowWidth - r * 2, 1, br, bg, bb, 255);
        //  左边框（避开圆角区域�?
        fillRect(0, r, 1, m_windowHeight - r * 2, br, bg, bb, 255);
        //  右边框（避开圆角区域�?
        fillRect(m_windowWidth - 1, r, 1, m_windowHeight - r * 2, br, bg, bb, 255);
    }
}

void SDLRenderer::renderControls(int64_t position, int64_t duration, int volume, bool isMuted,
                                 bool isPlaying, double speed, bool isPreloading) {
    int marginX = 24;
    int marginBottom = 24;
    int controlY = m_windowHeight - m_controlHeight - marginBottom;
    int controlW = m_windowWidth - marginX * 2;
    int radius = 20;

    //  底部投影（增加悬浮感�?
    fillRoundRect(marginX + 2, controlY + 4, controlW, m_controlHeight, radius,
                          0, 0, 0, 80);

    // 外层细白边框（玻璃拟态边框效果）
    fillRoundRect(marginX - 1, controlY - 1, controlW + 2, m_controlHeight + 2, radius + 1,
                          255, 255, 255, 55);

    //  内层主背�?
    fillRoundRect(marginX, controlY, controlW, m_controlHeight, radius,
                          COLOR_CONTROL_BG[0], COLOR_CONTROL_BG[1], COLOR_CONTROL_BG[2], COLOR_CONTROL_BG[3]);
    
    //  进度条（在最上面�?
    renderProgressBar(position, duration, controlY, isPreloading, isPlaying);
    
    // 播放控制按钮
    renderPlaybackControls(isPlaying, controlY);
    
    // 速度按钮
    renderSpeedButton(speed, controlY);
    
    // 时间显示
    renderTimeDisplay(position, duration, controlY);

    // 播放列表切换按钮
    {
        int btnX = m_windowWidth - 44 - m_volumeWidth - 40 - 42;
        int btnY = controlY + 32;
        bool hovered = (m_hoveredControl == ControlType::PlaylistButton);
        bool pressed = (m_pressedControl == ControlType::PlaylistButton);
        drawButton(btnX, btnY, m_buttonSize, m_buttonSize, "playlist", hovered, pressed);
        m_controlRects.push_back({btnX, btnY, m_buttonSize, m_buttonSize, ControlType::PlaylistButton, 0});
    }


    
    // 音量控制
    renderVolumeControl(volume, isMuted, controlY);
}

void SDLRenderer::renderProgressBar(int64_t position, int64_t duration, int controlY, bool isPreloading, bool isPlaying) {
    int barY = controlY + 14;
    int barHeight = 6;
    bool isLiveNetwork = m_mediaInfo.sourceType == SourceType::NetworkStream && duration <= 0;

    //  根据动画状态计算进度条宽度和边�?
    int fullBarWidth = m_windowWidth - 44 * 2;
    int compactBarWidth = m_windowWidth / 3;
    int barWidth = static_cast<int>(fullBarWidth * m_progressBarScale + compactBarWidth * (1.0f - m_progressBarScale));
    int margin = (m_windowWidth - barWidth) / 2;

    //  检测悬�?
    bool hovered = (m_mouseY >= barY && m_mouseY <= barY + barHeight &&
                   m_mouseX >= margin && m_mouseX <= margin + barWidth);
    bool pressed = m_draggingProgress;

    //  完全停止状态：刚启动或停止播放后（position=0，不播放，不预载�?
    bool isCompletelyStopped = !isPlaying && !isPreloading && position == 0;

    // 背景（圆角）
    uint8_t bgAlpha = isPreloading ? 180 : (duration > 0 ? COLOR_PROGRESS_BG[3] : 120);
    fillRoundRect(margin, barY, barWidth, barHeight, barHeight / 2,
                          COLOR_PROGRESS_BG[0], COLOR_PROGRESS_BG[1], COLOR_PROGRESS_BG[2], bgAlpha);

    // 存储进度条几何用于事件处理
    m_lastDuration = duration;
    m_lastBarX = margin;
    m_lastBarW = barWidth;

    if (isLiveNetwork) {
        int liveW = getTextWidth("LIVE", 11) + 18;
        int liveX = margin + barWidth - liveW;
        int liveY = barY - 8;
        fillRoundRect(liveX, liveY, liveW, 20, 10, 210, 40, 60, 220);
        drawText("LIVE", liveX + 9, liveY + 3, 255, 255, 255, 11);
        return;
    }

    // 章节标记（进度条上方书签形状）
    if (duration > 0 && !m_chapters.empty()) {
        const int markerW = 8;
        const int markerBodyH = 12;
        const int markerTipH = 4;
        const int markerTotalH = markerBodyH + markerTipH;
        for (size_t i = 0; i < m_chapters.size(); ++i) {
            int64_t chapterTime = m_chapters[i].startTime;
            if (chapterTime < 0 || chapterTime > duration) continue;
            float ratio = static_cast<float>(chapterTime) / static_cast<float>(duration);
            int markerCenterX = margin + static_cast<int>(barWidth * ratio);
            int markerX = markerCenterX - markerW / 2;
            int markerY = barY - markerTotalH + 1; // 底部略插入进度条 1px

            bool hovered = (m_hoveredControl == ControlType::ChapterMarker &&
                            m_hoveredControlValue == static_cast<int>(i));

            // 基础颜色：青色主题
            uint8_t mr = hovered ? 60 : 0;
            uint8_t mg = hovered ? 220 : 180;
            uint8_t mb = 255;
            uint8_t alpha = hovered ? 255 : 200;

            // 1. 外发光/阴影层
            fillRect(markerX - 2, markerY - 1, markerW + 4, markerTotalH + 3,
                     mr, mg, mb, hovered ? 50 : 25);

            // 2. 帽檐（略宽于主体，顶部圆角效果）
            fillRect(markerX - 1, markerY, markerW + 2, 3, mr, mg, mb, alpha);
            // 帽檐圆角
            fillCircle(markerX, markerY + 1, 1, mr, mg, mb, alpha);
            fillCircle(markerX + markerW, markerY + 1, 1, mr, mg, mb, alpha);

            // 3. 主体
            fillRect(markerX, markerY + 3, markerW, markerBodyH - 3, mr, mg, mb, alpha);

            // 4. 顶部高光条
            fillRect(markerX + 2, markerY + 1, markerW - 4, 1, 255, 255, 255, 120);

            // 5. 底部尖角
            SDL_Vertex triVerts[3];
            SDL_FColor c{mr / 255.0f, mg / 255.0f, mb / 255.0f, alpha / 255.0f};
            triVerts[0] = {{static_cast<float>(markerX), static_cast<float>(markerY + markerBodyH)}, c, {0, 0}};
            triVerts[1] = {{static_cast<float>(markerX + markerW), static_cast<float>(markerY + markerBodyH)}, c, {0, 0}};
            triVerts[2] = {{static_cast<float>(markerCenterX), static_cast<float>(markerY + markerTotalH)}, c, {0, 0}};
            int triIdx[3] = {0, 1, 2};
            SDL_RenderGeometry(m_renderer, nullptr, triVerts, 3, triIdx, 3);

            // 记录章节标记交互区域（先于 ProgressBar，优先级更高）
            int hitX = markerCenterX - 8;
            int hitY = markerY - 4;
            int hitW = 16;
            int hitH = markerTotalH + 8 + barHeight;
            m_controlRects.push_back({hitX, hitY, hitW, hitH,
                                      ControlType::ChapterMarker, static_cast<int>(i)});
        }
    }

    // 搜索热力图标记
    if (duration > 0 && !m_searchHighlights.empty()) {
        for (int64_t timestamp : m_searchHighlights) {
            if (timestamp < 0 || timestamp > duration) continue;
            float ratio = static_cast<float>(timestamp) / static_cast<float>(duration);
            int markerX = margin + static_cast<int>(barWidth * ratio);
            // 绘制半透明亮黄色细线
            fillRect(markerX, barY, 2, barHeight, 255, 200, 50, 150);
        }
    }

    // 进度填充 + thumb（停止状态下只保留背景条，和刚启动时一致）
    if (duration > 0 && !isCompletelyStopped) {
        float progress = m_draggingProgress ? m_dragProgressRatio
                                            : static_cast<float>(position) / duration;
        progress = std::max(0.0f, std::min(1.0f, progress));

        const uint8_t* fillColor = pressed ? COLOR_PROGRESS_HOVER :
                                   (hovered ? COLOR_PROGRESS_HOVER : COLOR_PROGRESS_FILL);

        int fillW = static_cast<int>(barWidth * progress);
        if (fillW < barHeight) fillW = barHeight;

        uint8_t fillAlpha = isPreloading ? 70 : fillColor[3];
        fillRoundRect(margin, barY, fillW, barHeight, barHeight / 2,
                              fillColor[0], fillColor[1], fillColor[2], fillAlpha);

        //  圆形进度 thumb（预载期间用蓝色，播放时白色，避免半透明发灰�?
        int knobX = margin + static_cast<int>(barWidth * progress);
        int knobY = barY + barHeight / 2;
        int thumbRadius = (hovered || pressed) ? 7 : 5;
        if (isPreloading) {
            fillCircle(knobX, knobY, thumbRadius,
                               COLOR_PROGRESS_FILL[0], COLOR_PROGRESS_FILL[1], COLOR_PROGRESS_FILL[2], 255);
        } else {
            fillCircle(knobX, knobY, thumbRadius, 255, 255, 255, 255);
        }
    }

    // 记录控件位置
    m_controlRects.push_back({margin, barY, barWidth, barHeight,
                              ControlType::ProgressBar, 0});
}

void SDLRenderer::renderPlaybackControls(bool isPlaying, int controlY) {
    int buttonY = controlY + 26;
    int x = 44;
    
    //  上一首按�?
    drawButton(x, buttonY, m_buttonSize, m_buttonSize, "prev",
               m_hoveredControl == ControlType::PrevButton, false);
    m_controlRects.push_back({x, buttonY, m_buttonSize, m_buttonSize, ControlType::PrevButton, 0});
    x += m_buttonSize + 10;
    
    // 播放/暂停按钮
    drawButton(x, buttonY, m_buttonSize, m_buttonSize, isPlaying ? "pause" : "play",
               m_hoveredControl == ControlType::PlayButton, m_pressedControl == ControlType::PlayButton);
    m_controlRects.push_back({x, buttonY, m_buttonSize, m_buttonSize, ControlType::PlayButton, 0});
    x += m_buttonSize + 10;
    
    // 停止按钮
    drawButton(x, buttonY, m_buttonSize, m_buttonSize, "stop",
               m_hoveredControl == ControlType::StopButton, m_pressedControl == ControlType::StopButton);
    m_controlRects.push_back({x, buttonY, m_buttonSize, m_buttonSize, ControlType::StopButton, 0});
    x += m_buttonSize + 10;
    
    //  下一首按�?
    drawButton(x, buttonY, m_buttonSize, m_buttonSize, "next",
               m_hoveredControl == ControlType::NextButton, false);
    m_controlRects.push_back({x, buttonY, m_buttonSize, m_buttonSize, ControlType::NextButton, 0});
}

void SDLRenderer::renderSpeedButton(double speed, int controlY) {
    int buttonY = controlY + 32;
    int x = 240;
    
    bool hovered = (m_hoveredControl == ControlType::SpeedButton);
    
    // 圆角按钮背景
    const uint8_t* bgColor = hovered ? COLOR_BUTTON_BG_HOVER : COLOR_BUTTON_BG;
    fillRoundRect(x, buttonY, 50, m_buttonSize, 8, bgColor[0], bgColor[1], bgColor[2], bgColor[3]);
    
    // 速度文字
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << speed << "x";
    std::string speedText = oss.str();
    int textW = getTextWidth(speedText, 11);
    int textX = x + (50 - textW) / 2;
    int textY = buttonY + (m_buttonSize - getFontHeight(11)) / 2;
    drawText(speedText, textX, textY, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 11);
    
    m_controlRects.push_back({x, buttonY, 50, m_buttonSize, ControlType::SpeedButton, 0});
}

void SDLRenderer::renderVolumeControl(int volume, bool isMuted, int controlY) {
    int buttonY = controlY + 32;
    int x = m_windowWidth - 44 - m_volumeWidth - 40;
    
    // 音量图标按钮
    bool volHovered = (m_hoveredControl == ControlType::VolumeButton);
    drawButton(x, buttonY, m_buttonSize, m_buttonSize, isMuted ? "mute" : "volume", volHovered, false);
    m_controlRects.push_back({x, buttonY, m_buttonSize, m_buttonSize, ControlType::VolumeButton, 0});
    x += m_buttonSize + 5;
    
    //  音量�?
    int volBarWidth = m_volumeWidth;
    int volBarHeight = 6;
    int volBarY = buttonY + (m_buttonSize - volBarHeight) / 2;
    
    fillRoundRect(x, volBarY, volBarWidth, volBarHeight, volBarHeight / 2,
                          COLOR_PROGRESS_BG[0], COLOR_PROGRESS_BG[1], COLOR_PROGRESS_BG[2], COLOR_PROGRESS_BG[3]);

    if (!isMuted) {
        float vol = std::max(0, std::min(100, volume)) / 100.0f;
        int fillW = static_cast<int>(volBarWidth * vol);
        if (fillW < volBarHeight) fillW = volBarHeight;
        fillRoundRect(x, volBarY, fillW, volBarHeight, volBarHeight / 2,
                              COLOR_PROGRESS_FILL[0], COLOR_PROGRESS_FILL[1], COLOR_PROGRESS_FILL[2], COLOR_PROGRESS_FILL[3]);
    }

    // 记录控件位置
    m_controlRects.push_back({x, volBarY, volBarWidth, volBarHeight, ControlType::VolumeBar, 0});
}

void SDLRenderer::renderTimeDisplay(int64_t position, int64_t duration, int controlY) {
    int x = 310;
    int y = controlY + 32;
    
    //  格式化时�?
    std::string timeText;
    if (m_mediaInfo.sourceType == SourceType::NetworkStream && duration <= 0) {
        timeText = "直播流";
    } else {
        timeText = VideoPlay::formatTime(position) + " / " + VideoPlay::formatTime(duration);
    }
    
    // 渲染时间文字
    drawText(timeText, x, y + (m_buttonSize - getFontHeight()) / 2,
             COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2]);
}

void SDLRenderer::renderFilename(const std::string& filename) {
    if (filename.empty()) return;
    
    // 在视频区域顶部显示文件名
    int y = m_menuBarHeight + 5;
    
    //  获取文件名（不含路径�?
    std::string name = filename;
    size_t pos = filename.find_last_of("/\\");
    if (pos != std::string::npos) {
        name = filename.substr(pos + 1);
    }
    
    //  计算文字宽度，限制为不与左右侧面板重�?
    int textW = getTextWidth(name);
    int maxWidth = m_windowWidth - 340; // �?20px 安全间隙
    if (m_showEpisodePanel && m_episodeData && !m_episodeData->empty()) {
        // 左侧选集面板�?260+24=284px
        maxWidth -= 284;
    }
    if (m_showPlaylistPanel) {
        // 右侧播放列表面板�?260+24=284px
        maxWidth -= 284;
    }
    if (maxWidth < 120) maxWidth = 120;
    
    // 截断过长的文件名
    if (textW > maxWidth) {
        while (textW > maxWidth - getTextWidth("...") && name.length() > 3) {
            name = name.substr(0, name.length() - 1);
            textW = getTextWidth(name + "...");
        }
        name += "...";
        textW = getTextWidth(name);
    }
    
    //  背景�?
    int bgWidth = textW + 20;
    fillRect(10, y, bgWidth, 25, 0, 0, 0, 150);
    
    //  渲染文件�?
    drawText(name, 20, y + 5, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2]);
}

void SDLRenderer::renderSubtitle(const std::string& subtitle) {
#ifdef HAS_SDL_TTF
    if (!m_font || subtitle.empty()) return;

    // 按换行符分割字幕文本
    std::vector<std::string> lines;
    std::string current;
    for (char c : subtitle) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    if (lines.empty()) return;

    int fontSize = 16;
    int maxWidth = 0;
    int lineHeight = getFontHeight(fontSize);
    for (const auto& line : lines) {
        int w = getTextWidth(line, fontSize);
        if (w > maxWidth) maxWidth = w;
    }

    int paddingX = 24;
    int paddingY = 12;
    int totalWidth = maxWidth + paddingX * 2;
    int totalHeight = lines.size() * lineHeight + paddingY * 2;

    //  底部边距：控制栏显示时在其上方，否则留一定边�?
    int bottomMargin = m_showControls ? m_controlHeight + 20 : 60;
    int x = (m_windowWidth - totalWidth) / 2;
    int y = m_windowHeight - bottomMargin - totalHeight;
    if (y < m_menuBarHeight + 10) y = m_menuBarHeight + 10;

    // 绘制半透明背景
    fillRoundRect(x, y, totalWidth, totalHeight, 8, 0, 0, 0, 180);

    //  绘制字幕文字（白色带轻微描边效果通过背景实现�?
    int textY = y + paddingY;
    for (const auto& line : lines) {
        int textW = getTextWidth(line, fontSize);
        int textX = x + (totalWidth - textW) / 2;
        drawText(line, textX, textY, 255, 255, 255, fontSize);
        textY += lineHeight;
    }
#endif
}

void SDLRenderer::renderSubtitleBitmap(int64_t positionMs) {
    std::lock_guard<std::mutex> lock(m_subtitleBitmapMutex);
    if (!m_subtitleTexture || m_currentBitmap.width <= 0 || m_currentBitmap.height <= 0) {
        return;
    }
    if (positionMs < m_currentBitmap.startMs || positionMs >= m_currentBitmap.endMs) {
        return;
    }

    if (m_videoWidth <= 0 || m_videoHeight <= 0 || m_windowWidth <= 0 || m_windowHeight <= 0) {
        return;
    }

    float targetAspect = static_cast<float>(m_videoWidth) / static_cast<float>(m_videoHeight);
    float windowAspect = static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight);
    SDL_FRect videoDst;
    if (m_aspectMode == AspectMode::FillWindow) {
        videoDst.x = 0;
        videoDst.y = 0;
        videoDst.w = static_cast<float>(m_windowWidth);
        videoDst.h = static_cast<float>(m_windowHeight);
    } else if (windowAspect > targetAspect) {
        videoDst.h = static_cast<float>(m_windowHeight);
        videoDst.w = videoDst.h * targetAspect;
        videoDst.x = (m_windowWidth - videoDst.w) / 2.0f;
        videoDst.y = 0;
    } else {
        videoDst.w = static_cast<float>(m_windowWidth);
        videoDst.h = videoDst.w / targetAspect;
        videoDst.x = 0;
        videoDst.y = (m_windowHeight - videoDst.h) / 2.0f;
    }

    float scaleX = videoDst.w / static_cast<float>(m_videoWidth);
    float scaleY = videoDst.h / static_cast<float>(m_videoHeight);

    SDL_FRect dst;
    dst.x = videoDst.x + static_cast<float>(m_currentBitmap.x) * scaleX;
    dst.y = videoDst.y + static_cast<float>(m_currentBitmap.y) * scaleY;
    dst.w = static_cast<float>(m_currentBitmap.width) * scaleX;
    dst.h = static_cast<float>(m_currentBitmap.height) * scaleY;

    SDL_RenderTexture(m_renderer, m_subtitleTexture, nullptr, &dst);
}

void SDLRenderer::renderOSD() {
    if (m_osdText.empty() || m_osdStartTime == 0) {
        return;
    }

    uint64_t now = SDL_GetTicks();
    uint64_t elapsed = now - m_osdStartTime;
    if (elapsed >= OSD_DURATION_MS) {
        m_osdText.clear();
        m_osdStartTime = 0;
        return;
    }

    float fadeStart = OSD_DURATION_MS * 0.65f;
    float alphaRatio = 1.0f;
    if (elapsed > static_cast<uint64_t>(fadeStart)) {
        alphaRatio = 1.0f - (elapsed - fadeStart) / (OSD_DURATION_MS - fadeStart);
    }
    alphaRatio = std::max(0.0f, std::min(1.0f, alphaRatio));

    const int fontSize = 22;
    const int paddingX = 28;
    const int paddingY = 16;
    const int iconSize = 36;
    auto iconType = [this]() -> std::string {
        switch (m_osdType) {
            case OSDType::Play:         return "play";
            case OSDType::Pause:        return "pause";
            case OSDType::Volume:       return "volume";
            case OSDType::Mute:         return "mute";
            case OSDType::SeekBackward: return "prev";
            case OSDType::SeekForward:  return "next";
            case OSDType::Info:         return "playlist";
            case OSDType::Speed:
            case OSDType::Message:
            default:                    return {};
        }
    }();
    bool hasIcon = !iconType.empty();
    bool hasSpeedBadge = m_osdType == OSDType::Speed;
    int textW = getTextWidth(m_osdText, fontSize);
    int textH = getFontHeight(fontSize);
    bool hasProgress = m_osdProgress >= 0.0f && m_osdProgress <= 1.0f;
    int progressH = hasProgress ? 14 : 0;
    int contentW = textW + (hasIcon ? iconSize + 14 : (hasSpeedBadge ? 58 : 0));
    int boxW = contentW + paddingX * 2;
    int boxH = std::max(textH, hasIcon ? iconSize : 0) + paddingY * 2 + progressH;
    int x = (m_windowWidth - boxW) / 2;
    int y = (m_windowHeight - boxH) / 2;

    uint8_t bgAlpha = static_cast<uint8_t>(210 * alphaRatio);
    uint8_t borderAlpha = static_cast<uint8_t>(80 * alphaRatio);
    uint8_t textAlpha = static_cast<uint8_t>(255 * alphaRatio);

    fillRoundRect(x - 1, y - 1, boxW + 2, boxH + 2, 12, 255, 255, 255, borderAlpha);
    fillRoundRect(x, y, boxW, boxH, 12, 18, 20, 24, bgAlpha);

    int contentX = x + paddingX;
    int centerY = y + paddingY + std::max(textH, hasIcon ? iconSize : 0) / 2;
    if (hasIcon) {
        drawIcon(contentX + iconSize / 2, centerY, iconType, false, 1.0f);
        contentX += iconSize + 14;
    } else if (hasSpeedBadge) {
        fillRoundRect(contentX, centerY - 15, 44, 30, 8, 0, 170, 255, static_cast<uint8_t>(180 * alphaRatio));
        drawText("x", contentX + 17, centerY - getFontHeight(18) / 2, 255, 255, 255, 18, textAlpha);
        contentX += 58;
    }

    drawText(m_osdText, contentX, centerY - textH / 2, 255, 255, 255, fontSize, textAlpha);

    if (hasProgress) {
        int barX = x + paddingX;
        int barY = y + boxH - paddingY - 6;
        int barW = boxW - paddingX * 2;
        int fillW = static_cast<int>(barW * std::clamp(m_osdProgress, 0.0f, 1.0f));
        fillRoundRect(barX, barY, barW, 6, 3, 255, 255, 255, static_cast<uint8_t>(45 * alphaRatio));
        fillRoundRect(barX, barY, std::max(6, fillW), 6, 3, 0, 170, 255, static_cast<uint8_t>(220 * alphaRatio));
    }
}

void SDLRenderer::renderNetworkState() {
    if (m_mediaInfo.sourceType != SourceType::NetworkStream) {
        return;
    }

    const char* text = networkStateText(m_networkState);
    if (!text || text[0] == '\0') {
        return;
    }

    int fontSize = 13;
    int textW = getTextWidth(text, fontSize);
    int textH = getFontHeight(fontSize);
    int paddingX = 14;
    int paddingY = 8;
    int w = textW + paddingX * 2 + 16;
    int h = textH + paddingY * 2;
    int x = (m_windowWidth - w) / 2;
    int y = m_menuBarHeight + 16;

    uint8_t r = 0;
    uint8_t g = 170;
    uint8_t b = 255;
    if (m_networkState == NetworkState::Failed) {
        r = 230;
        g = 70;
        b = 70;
    } else if (m_networkState == NetworkState::Reconnecting) {
        r = 255;
        g = 170;
        b = 50;
    }

    fillRoundRect(x - 1, y - 1, w + 2, h + 2, 10, 255, 255, 255, 45);
    fillRoundRect(x, y, w, h, 10, 20, 22, 28, 220);
    fillCircle(x + paddingX - 2, y + h / 2, 4, r, g, b, 255);
    drawText(text, x + paddingX + 10, y + paddingY, 255, 255, 255, fontSize);
}

void SDLRenderer::renderAIAnalysisOverlay() {
    if (!m_renderer) return;
    if (m_showSearchPanel) {
        return;
    }

    uint64_t now = SDL_GetTicks();
    bool showNotice = !m_aiAnalysisNoticeText.empty() &&
        m_aiAnalysisNoticeStartTime > 0 &&
        now - m_aiAnalysisNoticeStartTime < AI_ANALYSIS_NOTICE_DURATION_MS;
    if (!m_aiAnalysisActive && !showNotice) {
        return;
    }

    bool failed = !m_aiAnalysisActive &&
        m_aiAnalysisNoticeText.find("失败") != std::string::npos;
    std::string title = m_aiAnalysisActive ? "AI 分析中" :
        (failed ? "AI 分析失败" : "AI 分析完成");
    std::string status = m_aiAnalysisActive ? m_aiAnalysisStatus : m_aiAnalysisNoticeText;
    if (status.empty()) {
        status = "正在分析视频内容...";
    }

    auto fitText = [this](std::string text, int maxWidth, int fontSize) {
        if (getTextWidth(text, fontSize) <= maxWidth) {
            return text;
        }

        const std::string suffix = "...";
        int suffixW = getTextWidth(suffix, fontSize);
        while (!text.empty() && getTextWidth(text, fontSize) + suffixW > maxWidth) {
            text.pop_back();
        }
        return text + suffix;
    };

    int maxPanelW = std::max(260, m_windowWidth - 48);
    int minPanelW = std::min(360, maxPanelW);
    int panelW = std::clamp(getTextWidth(status, 12) + 136, minPanelW, maxPanelW);
    int panelH = 78;
    int panelX = (m_windowWidth - panelW) / 2;
    int panelY = m_windowHeight < 260 ? 12 : m_menuBarHeight + 18;
    int radius = 18;

    fillRoundRect(panelX + 2, panelY + 4, panelW, panelH, radius, 0, 0, 0, 80);
    fillRoundRect(panelX - 1, panelY - 1, panelW + 2, panelH + 2, radius + 1,
                  255, 255, 255, 45);
    fillRoundRect(panelX, panelY, panelW, panelH, radius, 24, 27, 32, 230);

    const int spinnerCx = panelX + 36;
    const int spinnerCy = panelY + 31;
    const int dotCount = 10;
    const float pi = 3.14159265f;
    float rotation = (now % 1100) / 1100.0f * 2.0f * pi;
    for (int i = 0; i < dotCount; ++i) {
        float angle = rotation + i * (2.0f * pi / dotCount);
        int dotX = spinnerCx + static_cast<int>(std::cos(angle) * 12.0f);
        int dotY = spinnerCy + static_cast<int>(std::sin(angle) * 12.0f);
        float strength = (i + 1) / static_cast<float>(dotCount);
        uint8_t alpha = static_cast<uint8_t>(55 + 180 * strength);
        int dotR = 2 + static_cast<int>(strength * 2.0f);
        uint8_t r = failed ? 255 : COLOR_PROGRESS_FILL[0];
        uint8_t g = failed ? 95 : COLOR_PROGRESS_FILL[1];
        uint8_t b = failed ? 95 : COLOR_PROGRESS_FILL[2];
        if (!m_aiAnalysisActive) {
            alpha = static_cast<uint8_t>(std::min<int>(255, alpha + 30));
        }
        fillCircle(dotX, dotY, dotR, r, g, b, alpha);
    }

    int textX = panelX + 64;
    int textRight = panelX + panelW - 18;
    int titleY = panelY + 14;
    int statusY = panelY + 37;
    float progress = showNotice ? m_aiAnalysisNoticeProgress : m_aiAnalysisProgress;
    progress = std::clamp(progress, 0.0f, 1.0f);

    std::string percentText;
    if (progress > 0.0f || !m_aiAnalysisActive) {
        percentText = std::to_string(static_cast<int>(std::round(progress * 100.0f))) + "%";
    }
    int percentW = percentText.empty() ? 0 : getTextWidth(percentText, 11);
    int titleMaxW = textRight - textX - percentW - 12;
    drawText(fitText(title, titleMaxW, 13), textX, titleY, 255, 255, 255, 13);
    if (!percentText.empty()) {
        drawText(percentText, textRight - percentW, titleY + 1, 190, 220, 235, 11);
    }

    int elapsedSeconds = 0;
    if (m_aiAnalysisActive && m_aiAnalysisStartTime > 0) {
        elapsedSeconds = static_cast<int>((now - m_aiAnalysisStartTime) / 1000);
    }
    std::string detail = status;
    if (m_aiAnalysisActive && elapsedSeconds >= 3) {
        detail += " / " + std::to_string(elapsedSeconds) + "s";
    }
    drawText(fitText(detail, textRight - textX, 12), textX, statusY,
             205, 212, 220, 12);

    int barX = panelX + 18;
    int barY = panelY + panelH - 17;
    int barW = panelW - 36;
    int barH = 5;
    fillRoundRect(barX, barY, barW, barH, barH / 2, 255, 255, 255, 45);

    if (m_aiAnalysisActive && progress <= 0.01f) {
        int sweepW = std::max(48, barW / 3);
        int sweepTravel = barW + sweepW;
        int sweepX = barX + static_cast<int>(((now % 1400) / 1400.0f) * sweepTravel) - sweepW;
        int clippedX = std::max(barX, sweepX);
        int clippedRight = std::min(barX + barW, sweepX + sweepW);
        if (clippedRight > clippedX) {
            fillRoundRect(clippedX, barY, clippedRight - clippedX, barH, barH / 2,
                          COLOR_PROGRESS_FILL[0], COLOR_PROGRESS_FILL[1],
                          COLOR_PROGRESS_FILL[2], 210);
        }
    } else {
        int fillW = std::max(barH, static_cast<int>(barW * progress));
        uint8_t r = failed ? 255 : COLOR_PROGRESS_FILL[0];
        uint8_t g = failed ? 95 : COLOR_PROGRESS_FILL[1];
        uint8_t b = failed ? 95 : COLOR_PROGRESS_FILL[2];
        fillRoundRect(barX, barY, fillW, barH, barH / 2, r, g, b, 230);
    }

    if (m_aiAnalysisActive) {
        int glowX = barX + static_cast<int>(((now % 1200) / 1200.0f) * barW);
        renderGlowBar(glowX, barY + barH / 2, 72, barH + 7,
                      COLOR_PROGRESS_FILL[0], COLOR_PROGRESS_FILL[1],
                      COLOR_PROGRESS_FILL[2], 70, barX, barX + barW);
    }
}

void SDLRenderer::renderMediaInfoPanel() {
    fillRect(0, 0, m_windowWidth, m_windowHeight, 0, 0, 0, 95);

    int panelW = std::min(560, std::max(320, m_windowWidth - 80));
    int panelX = (m_windowWidth - panelW) / 2;
    int panelY = std::max(m_menuBarHeight + 30, (m_windowHeight - 360) / 2);
    int radius = 12;

    std::vector<std::pair<std::string, std::string>> rows;
    rows.push_back({"来源", m_mediaInfo.source.empty() ? "N/A" : m_mediaInfo.source});
    rows.push_back({"类型", m_mediaInfo.sourceType == SourceType::NetworkStream ? "网络流" : "本地文件"});
    rows.push_back({"容器", m_mediaInfo.container.empty() ? "N/A" : m_mediaInfo.container});
    rows.push_back({"时长", formatTime(m_mediaInfo.durationMs)});
    rows.push_back({"总码率", formatBitrate(m_mediaInfo.bitrate)});

    if (m_mediaInfo.hasVideo) {
        rows.push_back({"视频编码", m_mediaInfo.videoCodec.empty() ? "N/A" : m_mediaInfo.videoCodec});
        rows.push_back({"分辨率", std::to_string(m_mediaInfo.width) + " x " +
                                  std::to_string(m_mediaInfo.height)});
        rows.push_back({"帧率", formatFps(m_mediaInfo.fps)});
        rows.push_back({"视频码率", formatBitrate(m_mediaInfo.videoBitrate)});
        std::string hardwareStatus = "未启用";
        if (!m_mediaInfo.hardwareDecoderEnabled) {
            hardwareStatus = "已关闭";
        } else if (m_mediaInfo.hardwareDecoder) {
            hardwareStatus = "已启用 " + m_mediaInfo.hardwareDevice;
        }
        rows.push_back({"硬件解码", hardwareStatus});
    }

    if (m_mediaInfo.hasAudio) {
        rows.push_back({"音频编码", m_mediaInfo.audioCodec.empty() ? "N/A" : m_mediaInfo.audioCodec});
        rows.push_back({"采样率", m_mediaInfo.sampleRate > 0 ?
                                  std::to_string(m_mediaInfo.sampleRate) + " Hz" : "N/A"});
        rows.push_back({"声道", m_mediaInfo.channels > 0 ?
                                std::to_string(m_mediaInfo.channels) : "N/A"});
        rows.push_back({"音频码率", formatBitrate(m_mediaInfo.audioBitrate)});
    }

    const int titleFont = 18;
    const int rowFont = 12;
    const int padding = 24;
    const int rowH = 24;
    int panelH = padding * 2 + getFontHeight(titleFont) + 14 +
                 static_cast<int>(rows.size()) * rowH;
    if (panelY + panelH > m_windowHeight - 24) {
        panelY = std::max(m_menuBarHeight + 12, m_windowHeight - panelH - 24);
    }

    fillRoundRect(panelX + 2, panelY + 4, panelW, panelH, radius, 0, 0, 0, 100);
    fillRoundRect(panelX - 1, panelY - 1, panelW + 2, panelH + 2, radius + 1,
                  255, 255, 255, 60);
    fillRoundRect(panelX, panelY, panelW, panelH, radius, 24, 26, 32, 238);

    drawText("媒体信息", panelX + padding, panelY + padding,
             255, 255, 255, titleFont);

    int y = panelY + padding + getFontHeight(titleFont) + 14;
    int labelW = 78;
    int valueX = panelX + padding + labelW + 18;
    int valueMaxW = panelW - padding * 2 - labelW - 18;
    for (const auto& row : rows) {
        drawText(row.first, panelX + padding, y, 160, 168, 178, rowFont);

        std::string value = row.second;
        int valueW = getTextWidth(value, rowFont);
        if (valueW > valueMaxW) {
            const std::string ellipsis = "...";
            while (!value.empty() &&
                   getTextWidth(value + ellipsis, rowFont) > valueMaxW) {
                value.pop_back();
            }
            value += ellipsis;
        }
        drawText(value, valueX, y, 235, 238, 242, rowFont);
        y += rowH;
    }
}

void SDLRenderer::renderPlaylistPanel(const std::vector<std::string>& playlist, size_t currentIndex) {
    int panelW = 260;
    int panelX = m_windowWidth - 24 - panelW;
    int panelY = m_menuBarHeight + 10; // 上边�?10px，与下方控制栏间距一�?    // 控制栏底部有 24px 边距，其顶部�?m_windowHeight - m_controlHeight - 24
    // 播放列表面板底部需位于控制栏上方，�?10px 安全间隙
    int panelBottomMargin = m_windowHeight - m_controlHeight - 24 - 10;
    int panelH = panelBottomMargin - panelY;
    if (panelH < 60) return;
    int radius = 20;

    // 投影
    fillRoundRect(panelX + 2, panelY + 4, panelW, panelH, radius, 0, 0, 0, 80);
    // 白边
    fillRoundRect(panelX - 1, panelY - 1, panelW + 2, panelH + 2, radius + 1, 255, 255, 255, 55);
    // 背景
    fillRoundRect(panelX, panelY, panelW, panelH, radius,
                          COLOR_CONTROL_BG[0], COLOR_CONTROL_BG[1], COLOR_CONTROL_BG[2], COLOR_CONTROL_BG[3]);

    // 标题
    int titleY = panelY + 16;
    drawText("播放列表", panelX + 16, titleY, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 13);

    int itemStartY = titleY + 28;
    int itemH = 28;
    int maxVisible = (panelY + panelH - 16 - itemStartY) / itemH;
    if (maxVisible < 1) maxVisible = 1;

    // 计算起始索引：优先使用手动滚动偏移，同时确保当前项在可视范围内
    size_t startIndex = static_cast<size_t>(m_playlistScrollOffset);
    size_t endIndex = startIndex + maxVisible;
    if (endIndex > playlist.size()) {
        endIndex = playlist.size();
        if (playlist.size() > static_cast<size_t>(maxVisible)) {
            startIndex = playlist.size() - maxVisible;
        } else {
            startIndex = 0;
        }
    }
    // 确保当前项在可视范围内
    if (currentIndex < startIndex) {
        startIndex = currentIndex;
    } else if (currentIndex >= endIndex) {
        startIndex = currentIndex - maxVisible + 1;
    }
    m_playlistScrollOffset = static_cast<int>(startIndex);
    endIndex = startIndex + maxVisible;
    if (endIndex > playlist.size()) endIndex = playlist.size();

    for (size_t i = startIndex; i < endIndex; ++i) {
        int itemY = itemStartY + static_cast<int>(i - startIndex) * itemH;
        bool isCurrent = (i == currentIndex);
        bool hovered = (m_mouseX >= panelX + 12 && m_mouseX <= panelX + panelW - 12 &&
                        m_mouseY >= itemY && m_mouseY <= itemY + itemH);

        if (isCurrent) {
            fillRoundRect(panelX + 12, itemY, panelW - 24, itemH, 8,
                                  COLOR_MENU_ACTIVE[0], COLOR_MENU_ACTIVE[1], COLOR_MENU_ACTIVE[2], 200);
            // 当前播放项左侧 3px 竖条指示器
            fillRoundRect(panelX + 12, itemY + 6, 3, itemH - 12, 2,
                          255, 255, 255, 220);
        } else if (hovered) {
            fillRoundRect(panelX + 12, itemY, panelW - 24, itemH, 8,
                                  COLOR_MENU_HOVER[0], COLOR_MENU_HOVER[1], COLOR_MENU_HOVER[2], 160);
        }

        // 记录控件位置用于点击检测
        m_controlRects.push_back({panelX + 12, itemY, panelW - 24, itemH, ControlType::PlaylistItem, static_cast<int>(i)});

        //  文件�?
        std::string name = playlist[i];
        size_t pos = name.find_last_of("/\\");
        if (pos != std::string::npos) {
            name = name.substr(pos + 1);
        }

        // 悬浮时显示进度百分比
        bool showProgress = hovered && i < m_playlistProgress.size() && m_playlistProgress[i] > 0.0f;
        std::string progressText;
        int progressTextW = 0;
        if (showProgress) {
            std::ostringstream pss;
            pss << static_cast<int>(m_playlistProgress[i] * 100.0f) << "%";
            progressText = pss.str();
            progressTextW = getTextWidth(progressText, 10) + 8;
        }

        // 截断
        int maxTextW = panelW - 48 - progressTextW;
        int textW = getTextWidth(name, 12);
        if (textW > maxTextW) {
            while (textW > maxTextW - getTextWidth("...", 12) && name.length() > 3) {
                name = name.substr(0, name.length() - 1);
                textW = getTextWidth(name + "...", 12);
            }
            name += "...";
        }

        int textColorR = isCurrent ? 255 : COLOR_TEXT[0];
        int textColorG = isCurrent ? 255 : COLOR_TEXT[1];
        int textColorB = isCurrent ? 255 : COLOR_TEXT[2];
        drawText(name, panelX + 20, itemY + (itemH - getFontHeight(12)) / 2, textColorR, textColorG, textColorB, 12);

        // 悬浮时右侧显示进度百分比
        if (showProgress) {
            int ptY = itemY + (itemH - getFontHeight(10)) / 2;
            drawText(progressText, panelX + panelW - 24 - progressTextW + 4, ptY, 150, 150, 150, 10);
        }
    }
}

void SDLRenderer::renderLoadingAnimation() {
    if (!m_renderer) return;

    int cx = m_windowWidth / 2;
    int cy = m_windowHeight / 2;
    int radius = 36;
    int dotCount = 8;
    
    // 基于时间计算旋转角度
    uint64_t now = SDL_GetTicks();
    float rotation = (now % 1500) / 1500.0f * 2.0f * 3.14159265f;
    
    for (int i = 0; i < dotCount; ++i) {
        float angle = rotation + i * (2.0f * 3.14159265f / dotCount);
        int dx = static_cast<int>(cx + std::cos(angle) * radius);
        int dy = static_cast<int>(cy + std::sin(angle) * radius);
        
        // 渐隐效果：前面的点更透明
        uint8_t alpha = static_cast<uint8_t>(255 * (1.0f - i / static_cast<float>(dotCount)));
        int dotRadius = 5 - i / 3;
        if (dotRadius < 2) dotRadius = 2;
        
        fillCircle(dx, dy, dotRadius, 0, 170, 255, alpha);
    }
    
    // 绘制 "加载中.." 文字
    std::string text = "加载中..";
    int textW = getTextWidth(text, 14);
    drawText(text, cx - textW / 2, cy + radius + 20, 200, 200, 200, 14);
}

void SDLRenderer::renderEpisodePanel() {
    if (!m_episodeData || m_episodeData->empty()) return;

    int panelW = 260;
    int panelX = 24; // 左侧，与右侧播放列表面板对称
    int panelY = m_menuBarHeight + 10;
    int panelBottomMargin = m_windowHeight - m_controlHeight - 24 - 10;
    int panelH = panelBottomMargin - panelY;
    if (panelH < 60) return;
    int radius = 20;

    // 投影
    fillRoundRect(panelX + 2, panelY + 4, panelW, panelH, radius, 0, 0, 0, 80);
    // 白边
    fillRoundRect(panelX - 1, panelY - 1, panelW + 2, panelH + 2, radius + 1, 255, 255, 255, 55);
    // 背景
    fillRoundRect(panelX, panelY, panelW, panelH, radius,
                          COLOR_CONTROL_BG[0], COLOR_CONTROL_BG[1], COLOR_CONTROL_BG[2], COLOR_CONTROL_BG[3]);

    // 标题
    int titleY = panelY + 16;
    std::string panelTitle = "选集";
    if (!m_episodeSeriesName.empty()) {
        if (m_episodeSeasonNumber > 0) {
            panelTitle = m_episodeSeriesName + " S" + std::to_string(m_episodeSeasonNumber);
        } else {
            panelTitle = m_episodeSeriesName;
        }
    }
    drawText(panelTitle, panelX + 16, titleY, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 13);

    // 底部按钮区域
    const int buttonAreaH = 44;
    const int btnW = 90;
    const int btnH = 30;
    const int btnGap = 16;
    int buttonAreaY = panelY + panelH - buttonAreaH;

    int itemStartY = titleY + 28;
    int itemH = 28;
    int listBottomY = buttonAreaY - 8;
    int maxVisible = (listBottomY - itemStartY) / itemH;
    if (maxVisible < 1) maxVisible = 1;

    size_t currentIndex = m_currentEpisodeIndex;
    size_t startIndex = static_cast<size_t>(m_episodeScrollOffset);
    size_t endIndex = startIndex + maxVisible;
    if (endIndex > m_episodeData->size()) {
        endIndex = m_episodeData->size();
        if (m_episodeData->size() > static_cast<size_t>(maxVisible)) {
            startIndex = m_episodeData->size() - maxVisible;
        } else {
            startIndex = 0;
        }
    }
    // 确保当前项在可视范围内
    if (currentIndex < startIndex) {
        startIndex = currentIndex;
    } else if (currentIndex >= endIndex) {
        startIndex = currentIndex - maxVisible + 1;
    }
    m_episodeScrollOffset = static_cast<int>(startIndex);
    endIndex = startIndex + maxVisible;
    if (endIndex > m_episodeData->size()) endIndex = m_episodeData->size();

    for (size_t i = startIndex; i < endIndex; ++i) {
        int itemY = itemStartY + static_cast<int>(i - startIndex) * itemH;
        bool isCurrent = (i == currentIndex);
        bool hovered = (m_mouseX >= panelX + 12 && m_mouseX <= panelX + panelW - 12 &&
                        m_mouseY >= itemY && m_mouseY <= itemY + itemH);

        if (isCurrent) {
            fillRoundRect(panelX + 12, itemY, panelW - 24, itemH, 8,
                                  COLOR_MENU_ACTIVE[0], COLOR_MENU_ACTIVE[1], COLOR_MENU_ACTIVE[2], 200);
            //  当前集左�?3px 竖条指示�?
            fillRoundRect(panelX + 12, itemY + 6, 3, itemH - 12, 2,
                          255, 255, 255, 220);
        } else if (hovered) {
            fillRoundRect(panelX + 12, itemY, panelW - 24, itemH, 8,
                                  COLOR_MENU_HOVER[0], COLOR_MENU_HOVER[1], COLOR_MENU_HOVER[2], 160);
        }

        m_controlRects.push_back({panelX + 12, itemY, panelW - 24, itemH, ControlType::EpisodeItem, static_cast<int>(i)});

        const auto& ep = (*m_episodeData)[i];
        std::string label = ep.title;

        // 播放进度：底部细进度条 + 右侧小圆点
        bool hasProgress = (i < m_episodeProgress.size() && m_episodeProgress[i] > 0.0f);
        float progress = hasProgress ? m_episodeProgress[i] : 0.0f;

        // 底部进度条（2px 高）
        if (hasProgress) {
            int barY = itemY + itemH - 2;
            int barX = panelX + 12;
            int barW = panelW - 24;
            int fillW = static_cast<int>(barW * progress);
            if (fillW < 1) fillW = 1;
            // 背景
            fillRect(barX, barY, barW, 2, 255, 255, 255, 30);
            // 已播放部分
            fillRect(barX, barY, fillW, 2, 0, 170, 255, 200);
        }

        // 已播放小圆点（右侧）
        int dotX = panelX + panelW - 28;
        int dotY = itemY + itemH / 2;
        if (hasProgress) {
            fillCircle(dotX, dotY, 3, 0, 170, 255, 200);
        }

        int maxTextW = panelW - (hasProgress ? 56 : 48);
        int textW = getTextWidth(label, 12);
        if (textW > maxTextW) {
            while (textW > maxTextW - getTextWidth("...", 12) && label.length() > 3) {
                label = label.substr(0, label.length() - 1);
                textW = getTextWidth(label + "...", 12);
            }
            label += "...";
        }

        int textColorR = isCurrent ? 255 : COLOR_TEXT[0];
        int textColorG = isCurrent ? 255 : COLOR_TEXT[1];
        int textColorB = isCurrent ? 255 : COLOR_TEXT[2];
        drawText(label, panelX + 20, itemY + (itemH - getFontHeight(12)) / 2, textColorR, textColorG, textColorB, 12);
    }

    //  绘制分隔�?
    fillRect(panelX + 16, buttonAreaY - 4, panelW - 32, 1, 255, 255, 255, 40);

    //  上一集按�?
    bool canPrev = (currentIndex > 0);
    int prevBtnX = panelX + (panelW - btnW * 2 - btnGap) / 2;
    int prevBtnY = buttonAreaY + (buttonAreaH - btnH) / 2;
    {
        bool hovered = canPrev && (m_hoveredControl == ControlType::EpisodePrev);
        bool pressed = canPrev && (m_pressedControl == ControlType::EpisodePrev);
        uint8_t bgAlpha = canPrev ? (hovered ? 160 : 100) : 50;
        uint8_t textAlpha = canPrev ? 255 : 120;
        fillRoundRect(prevBtnX, prevBtnY, btnW, btnH, 6,
                              COLOR_MENU_HOVER[0], COLOR_MENU_HOVER[1], COLOR_MENU_HOVER[2], bgAlpha);
        std::string txt = "上一集";
        int tw = getTextWidth(txt, 11);
        drawText(txt, prevBtnX + (btnW - tw) / 2, prevBtnY + (btnH - getFontHeight(11)) / 2,
                 COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 11, textAlpha);
        if (canPrev) {
            m_controlRects.push_back({prevBtnX, prevBtnY, btnW, btnH, ControlType::EpisodePrev, 0});
        }
    }

    // 下一集按钮
    bool canNext = (currentIndex + 1 < m_episodeData->size());
    int nextBtnX = prevBtnX + btnW + btnGap;
    int nextBtnY = prevBtnY;
    {
        bool hovered = canNext && (m_hoveredControl == ControlType::EpisodeNext);
        bool pressed = canNext && (m_pressedControl == ControlType::EpisodeNext);
        uint8_t bgAlpha = canNext ? (hovered ? 160 : 100) : 50;
        uint8_t textAlpha = canNext ? 255 : 120;
        fillRoundRect(nextBtnX, nextBtnY, btnW, btnH, 6,
                              COLOR_MENU_HOVER[0], COLOR_MENU_HOVER[1], COLOR_MENU_HOVER[2], bgAlpha);
        std::string txt = "下一集";
        int tw = getTextWidth(txt, 11);
        drawText(txt, nextBtnX + (btnW - tw) / 2, nextBtnY + (btnH - getFontHeight(11)) / 2,
                 COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 11, textAlpha);
        if (canNext) {
            m_controlRects.push_back({nextBtnX, nextBtnY, btnW, btnH, ControlType::EpisodeNext, 0});
        }
    }
}

void SDLRenderer::renderTooltip() {
    if (m_tooltip.empty() || m_tooltipTime == 0) return;

    // 延迟 400ms 显示，避免鼠标快速滑过时闪烁
    uint64_t elapsed = SDL_GetTicks() - m_tooltipTime;
    if (elapsed < 400) return;

    int fontSize = 11;
    int textW = getTextWidth(m_tooltip, fontSize);
    int textH = getFontHeight(fontSize);
    int paddingX = 10;
    int paddingY = 6;
    int bgW = textW + paddingX * 2;
    int bgH = textH + paddingY * 2 + 2; // +2 给底部蓝色条

    // Tooltip 位于鼠标上方，带 10px 间距
    int tx = m_mouseX - bgW / 2;
    int ty = m_mouseY - bgH - 10;
    if (tx < 6) tx = 6;
    if (tx + bgW > m_windowWidth - 6) tx = m_windowWidth - 6 - bgW;
    if (ty < 6) ty = m_mouseY + 18; // 如果上方空间不足则显示在下方

    //  1px 圆角边框�?
    fillRoundRect(tx, ty, bgW, bgH, 6, 120, 120, 130, 255);
    // 背景：更深更实心的底色，缩进 1px 形成圆角边框效果
    fillRoundRect(tx + 1, ty + 1, bgW - 2, bgH - 2, 5, 28, 30, 36, 250);
    // 底部蓝色胶囊装饰（主题色），用圆+矩形拼接确保圆角绝对可见
    {
        int barH = 4;
        int barW = std::max(20, static_cast<int>(bgW * 0.30f));
        int barX = tx + (bgW - barW) / 2;
        int barY = ty + bgH - barH - 2;
        int r = barH / 2; // 2
        int cy = barY + r;
        //  左半�?
        fillCircle(barX + r, cy, r, 0, 170, 255, 255);
        //  右半�?
        fillCircle(barX + barW - r - 1, cy, r, 0, 170, 255, 255);
        // 中间矩形连接
        fillRect(barX + r, barY, barW - barH, barH, 0, 170, 255, 255);
    }
    //  文字：纯白色，高对比�?
    drawText(m_tooltip, tx + paddingX, ty + paddingY, 255, 255, 255, fontSize);
}

void SDLRenderer::renderSyncInfo(int64_t audioPts, int64_t videoPts, double avDiff, bool playlistVisible) {
    // 分三列渲染，每列独立圆角背景，列间留白自然分隔，不使用线�?    // A/V/Diff 固定在右上角，播放列表面板在 y=44 开始，自然垂直错开
    const int COL_A_WIDTH = 65;
    const int COL_V_WIDTH = 65;
    const int COL_DIFF_WIDTH = 80;
    const int COL_GAP = 10;
    const int PADDING = 8;
    const int RIGHT_MARGIN = 15;
    const int fontSize = 11;
    
    std::string aTime = VideoPlay::formatTime(audioPts);
    std::string vTime = VideoPlay::formatTime(videoPts);
    std::ostringstream diffOss;
    diffOss << std::fixed << std::setprecision(0) << (avDiff * 1000.0);
    std::string diffVal = diffOss.str() + "ms";
    
    int aTimeW = getTextWidth(aTime, fontSize);
    int vTimeW = getTextWidth(vTime, fontSize);
    int diffValW = getTextWidth(diffVal, fontSize);
    int aLabelW = getTextWidth("A:", fontSize);
    int vLabelW = getTextWidth("V:", fontSize);
    int diffLabelW = getTextWidth("Diff:", fontSize);
    
    int totalWidth = COL_A_WIDTH + COL_GAP + COL_V_WIDTH + COL_GAP + COL_DIFF_WIDTH + PADDING * 2;
    
    //  播放列表面板或选集面板显示时水平避�?
    int panelRight = m_windowWidth - RIGHT_MARGIN;
    if (playlistVisible) {
        int playlistPanelX = m_windowWidth - 24 - 260;
        panelRight = std::min(panelRight, playlistPanelX - 20);
    }
    if (m_showSearchPanel) {
        int searchPanelX = m_windowWidth - 24 - 340;
        panelRight = std::min(panelRight, searchPanelX - 20);
    }
    if (m_showEpisodePanel && m_episodeData && !m_episodeData->empty()) {
        int episodePanelRight = 24 + 260;
        // sync info 在右上角，选集面板在左上角，不冲突，不需要避�?        (void)episodePanelRight;
    }
    
    int x = panelRight - totalWidth;
    // 固定在菜单栏下方，与播放列表面板顶部对齐，上边距 10px
    int y = m_menuBarHeight + 10;
    
    // 根据 diff 大小改变颜色
    uint8_t r = 0, g = 255, b = 0;
    double absDiff = std::abs(avDiff);
    if (absDiff > 0.080) {
        r = 255; g = 0; b = 0;
    } else if (absDiff > 0.040) {
        r = 255; g = 255; b = 0;
    }
    
    //  每列左边缘固定（从右向左推导，确保整体右对齐�?
    int diffColX = panelRight - PADDING - COL_DIFF_WIDTH;
    int vColX = diffColX - COL_GAP - COL_V_WIDTH;
    int aColX = vColX - COL_GAP - COL_A_WIDTH;
    
    //  每列独立圆角高亮背景，自然分隔三�?
    fillRoundRect(aColX, y, COL_A_WIDTH, 22, 4, 40, 40, 40, 160);
    fillRoundRect(vColX, y, COL_V_WIDTH, 22, 4, 40, 40, 40, 160);
    fillRoundRect(diffColX, y, COL_DIFF_WIDTH, 22, 4, 40, 40, 40, 160);
    
    // A 列：标签左对齐，时间右对齐（与标签保持最小间距）
    drawText("A:", aColX + 4, y + 3, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], fontSize);
    drawText(aTime, std::max(aColX + aLabelW + 8, aColX + COL_A_WIDTH - aTimeW - 4), y + 3,
             COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], fontSize);
    
    //  V 列：标签左对齐，时间右对�?
    drawText("V:", vColX + 4, y + 3, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], fontSize);
    drawText(vTime, std::max(vColX + vLabelW + 8, vColX + COL_V_WIDTH - vTimeW - 4), y + 3,
             COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], fontSize);
    
    // Diff 列：标签左对齐，数值右对齐
    drawText("Diff:", diffColX + 4, y + 3, COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], fontSize);
    drawText(diffVal, std::max(diffColX + diffLabelW + 8, diffColX + COL_DIFF_WIDTH - diffValW - 4), y + 3,
             r, g, b, fontSize);
}

void SDLRenderer::renderSearchPanel() {
    int panelW = 340;
    int panelX = m_windowWidth - panelW - 24;
    int panelY = m_menuBarHeight + 10;
    int panelH = m_windowHeight - m_menuBarHeight - m_controlHeight - 34;
    int radius = 12;

    // 半透明背景
    fillRoundRect(panelX + 2, panelY + 4, panelW, panelH, radius, 0, 0, 0, 80);
    fillRoundRect(panelX - 1, panelY - 1, panelW + 2, panelH + 2, radius + 1, 255, 255, 255, 30);
    fillRoundRect(panelX, panelY, panelW, panelH, radius, COLOR_MENU_BG[0], COLOR_MENU_BG[1], COLOR_MENU_BG[2], 240);

    // 标题栏
    int titleH = 48;
    drawText("AI 搜索与问答", panelX + 20, panelY + 14, 255, 255, 255, 14);
    int closeSize = 28;
    int closeX = panelX + panelW - closeSize - 12;
    int closeY = panelY + 10;
    bool closeHovered = (m_mouseX >= closeX && m_mouseX <= closeX + closeSize &&
                         m_mouseY >= closeY && m_mouseY <= closeY + closeSize);
    bool closePressed = (m_pressedControl == ControlType::SearchCloseButton);
    if (closePressed) {
        fillRoundRect(closeX, closeY, closeSize, closeSize, 6, 80, 85, 95, 220);
    } else if (closeHovered) {
        fillRoundRect(closeX, closeY, closeSize, closeSize, 6, 70, 75, 85, 200);
    }
    SDL_SetRenderDrawColor(m_renderer, 230, 230, 230, closeHovered ? 255 : 190);
    SDL_RenderLine(m_renderer,
                   static_cast<float>(closeX + 9), static_cast<float>(closeY + 9),
                   static_cast<float>(closeX + closeSize - 9), static_cast<float>(closeY + closeSize - 9));
    SDL_RenderLine(m_renderer,
                   static_cast<float>(closeX + closeSize - 9), static_cast<float>(closeY + 9),
                   static_cast<float>(closeX + 9), static_cast<float>(closeY + closeSize - 9));
    m_controlRects.push_back({closeX, closeY, closeSize, closeSize, ControlType::SearchCloseButton, 0});

    // 历史消息列表渲染区 (支持滚动)
    int inputH = 46;
    int listY = panelY + titleH;
    int listH = panelH - titleH - inputH - 20;

    SDL_Rect clipRect = { panelX, listY, panelW, listH };
    SDL_SetRenderClipRect(m_renderer, &clipRect);

    int contentY = listY - m_searchScrollOffset;
    {
        std::lock_guard<std::mutex> lock(m_chatMutex);
        for (size_t i = 0; i < m_chatHistory.size(); ++i) {
            const auto& msg = m_chatHistory[i];
            // 解析和换行
            std::vector<std::pair<std::string, bool>> tokens;
            tokens.push_back({msg.text, false});

            int bubbleMaxW = panelW - 60;
            int maxLineWidth = 0;
            int currentX = 0;
            int currentY = 0;
            int lineHeight = 18;

            struct DrawCmd {
                std::string text;
                bool isTimestamp;
                int x, y;
                int width;
            };
            std::vector<DrawCmd> drawCmds;

            auto appendDrawCmd = [&](const std::string& text, bool isTimestamp) {
                if (text.empty()) {
                    return;
                }

                int w = getTextWidth(text, 12);
                if (currentX + w > bubbleMaxW && currentX > 0) {
                    currentX = 0;
                    currentY += lineHeight;
                }

                drawCmds.push_back({text, isTimestamp, currentX, currentY, w});
                currentX += w;
                maxLineWidth = std::max(maxLineWidth, currentX);
            };

            for (const auto& token : tokens) {
                if (token.second) {
                    appendDrawCmd(token.first, true);
                    continue;
                }

                for (size_t pos = 0; pos < token.first.size();) {
                    char ch = token.first[pos];
                    if (ch == '\r') {
                        ++pos;
                        continue;
                    }
                    if (ch == '\n') {
                        currentX = 0;
                        currentY += lineHeight;
                        ++pos;
                        continue;
                    }

                    size_t timeLen = timestampTokenLength(token.first, pos);
                    if (timeLen > 0) {
                        appendDrawCmd(token.first.substr(pos, timeLen), true);
                        pos += timeLen;
                        continue;
                    }

                    size_t charLen = utf8CharLength(token.first, pos);
                    appendDrawCmd(token.first.substr(pos, charLen), false);
                    pos += charLen;
                }
            }

            int bubbleW = maxLineWidth + 24;
            int bubbleH = currentY + lineHeight + 20;
            if (!msg.isUser) {
                bubbleW = std::max(bubbleW, 56);
            }

            int bubbleX = msg.isUser ? (panelX + panelW - bubbleW - 20) : (panelX + 20);
            int bubbleY = contentY;

            if (bubbleY + bubbleH > listY && bubbleY < listY + listH) {
                if (msg.isUser) {
                    fillRoundRect(bubbleX, bubbleY, bubbleW, bubbleH, 8, 40, 120, 255, 220); // 用户蓝色
                } else {
                    fillRoundRect(bubbleX, bubbleY, bubbleW, bubbleH, 8, 50, 55, 60, 220); // AI 深灰
                }

                if (!msg.isUser) {
                    int copySize = 22;
                    int copyX = std::min(panelX + panelW - copySize - 12, bubbleX + bubbleW + 5);
                    int copyY = bubbleY + 4;
                    bool copyHovered = (m_hoveredControl == ControlType::SearchMessageCopy &&
                                        m_hoveredControlValue == static_cast<int>(i));
                    bool copyPressed = (m_pressedControl == ControlType::SearchMessageCopy &&
                                        m_pressedControlValue == static_cast<int>(i));
                    fillRoundRect(copyX, copyY, copySize, copySize, 5,
                                  copyPressed ? 74 : (copyHovered ? 64 : 48),
                                  copyPressed ? 82 : (copyHovered ? 72 : 56),
                                  copyPressed ? 96 : (copyHovered ? 88 : 70),
                                  copyHovered || copyPressed ? 230 : 170);

                    SDL_SetRenderDrawColor(m_renderer, 222, 226, 235,
                                           copyHovered || copyPressed ? 245 : 190);
                    SDL_FRect backRect = {
                        static_cast<float>(copyX + 7),
                        static_cast<float>(copyY + 5),
                        8.0f,
                        10.0f
                    };
                    SDL_FRect frontRect = {
                        static_cast<float>(copyX + 5),
                        static_cast<float>(copyY + 8),
                        8.0f,
                        10.0f
                    };
                    SDL_RenderRect(m_renderer, &backRect);
                    SDL_RenderRect(m_renderer, &frontRect);
                    m_controlRects.push_back({copyX, copyY, copySize, copySize,
                                              ControlType::SearchMessageCopy,
                                              static_cast<int>(i)});
                }

                for (const auto& cmd : drawCmds) {
                    int dx = bubbleX + 12 + cmd.x;
                    int dy = bubbleY + 10 + cmd.y;
                    if (cmd.isTimestamp) {
                        int parsedValue = 0;
                        if (parseTimestampMs(cmd.text, parsedValue)) {
                            drawText(cmd.text, dx, dy, 255, 200, 50, 12);
                            m_controlRects.push_back({dx, dy, cmd.width, lineHeight, ControlType::SearchTimestamp, parsedValue});
                            continue;
                        }
                        drawText(cmd.text, dx, dy, 255, 200, 50, 12); // 黄色时间戳
                        int mins = 0, secs = 0;
                        if (sscanf(cmd.text.c_str(), "[%d:%d]", &mins, &secs) == 2) {
                            int value = (mins * 60 + secs) * 1000;
                            m_controlRects.push_back({dx, dy, cmd.width, lineHeight, ControlType::SearchTimestamp, value});
                        } else {
                            int h=0, m=0, s=0;
                            if (sscanf(cmd.text.c_str(), "[%d:%d:%d]", &h, &m, &s) == 3) {
                                int value = (h * 3600 + m * 60 + s) * 1000;
                                m_controlRects.push_back({dx, dy, cmd.width, lineHeight, ControlType::SearchTimestamp, value});
                            } else {
                                drawText(cmd.text, dx, dy, 255, 255, 255, 12); // 解析失败，白色
                            }
                        }
                    } else {
                        drawText(cmd.text, dx, dy, 255, 255, 255, 12);
                    }
                }
            }
            contentY += bubbleH + 10;
        }
    }

    // 更新滚动偏移范围
    uint64_t now = SDL_GetTicks();
    bool showProgressNotice = !m_aiAnalysisNoticeText.empty() &&
        m_aiAnalysisNoticeStartTime > 0 &&
        now - m_aiAnalysisNoticeStartTime < AI_ANALYSIS_NOTICE_DURATION_MS;
    if (m_aiAnalysisActive || showProgressNotice) {
        bool failed = !m_aiAnalysisActive &&
            m_aiAnalysisNoticeText.find("失败") != std::string::npos;
        std::string title = m_aiAnalysisActive ? "AI 搜索中" :
            (failed ? "AI 搜索失败" : "AI 搜索完成");
        std::string status = m_aiAnalysisActive ? m_aiAnalysisStatus : m_aiAnalysisNoticeText;
        float progress = showProgressNotice ? m_aiAnalysisNoticeProgress : m_aiAnalysisProgress;
        progress = std::clamp(progress, 0.0f, 1.0f);

        int cardX = panelX + 20;
        int cardY = contentY;
        int cardW = panelW - 40;
        int cardH = 86;

        if (cardY + cardH > listY && cardY < listY + listH) {
            uint8_t accentR = failed ? 230 : 60;
            uint8_t accentG = failed ? 80 : 150;
            uint8_t accentB = failed ? 80 : 255;
            fillRoundRect(cardX, cardY, cardW, cardH, 8, 45, 50, 56, 230);
            fillRoundRect(cardX, cardY, 4, cardH, 2, accentR, accentG, accentB, 230);
            drawText(title, cardX + 14, cardY + 12, 255, 255, 255, 13);
            if (!status.empty()) {
                drawText(status, cardX + 14, cardY + 34, 205, 212, 222, 12);
            }

            int barX = cardX + 14;
            int barY = cardY + cardH - 20;
            int barW = cardW - 28;
            int barH = 7;
            int fillW = static_cast<int>(barW * progress);
            fillRoundRect(barX, barY, barW, barH, 4, 255, 255, 255, 42);
            if (m_aiAnalysisActive && fillW <= 0) {
                fillW = std::max(14, barW / 8);
            }
            if (fillW > 0) {
                fillRoundRect(barX, barY, std::min(barW, fillW), barH, 4, accentR, accentG, accentB, 235);
            }

            std::ostringstream percent;
            percent << std::fixed << std::setprecision(0) << (progress * 100.0f) << "%";
            drawText(percent.str(), cardX + cardW - 48, cardY + 12,
                     accentR, accentG, accentB, 11);
        }
        contentY += cardH + 10;
    }

    int totalContentH = std::max(0, contentY + m_searchScrollOffset - listY);
    if (m_searchScrollOffset > std::max(0, totalContentH - listH)) {
        m_searchScrollOffset = std::max(0, totalContentH - listH);
    }

    SDL_SetRenderClipRect(m_renderer, nullptr);

    // 输入框区域
    int inputY = panelY + panelH - inputH - 10;
    int inputX = panelX + 16;
    int inputW = panelW - 32;

    bool inputHovered = (m_mouseX >= inputX && m_mouseX <= inputX + inputW && m_mouseY >= inputY && m_mouseY <= inputY + inputH);
    uint8_t borderAlpha = m_isSearchInputFocused ? 200 : (inputHovered ? 120 : 60);

    fillRoundRect(inputX, inputY, inputW, inputH, 8, 20, 22, 28, 255);
    // 边框
    fillRoundRect(inputX - 1, inputY - 1, inputW + 2, 1, borderAlpha, borderAlpha, borderAlpha, 255); // Top
    fillRoundRect(inputX - 1, inputY + inputH, inputW + 2, 1, borderAlpha, borderAlpha, borderAlpha, 255); // Bottom
    fillRoundRect(inputX - 1, inputY, 1, inputH, borderAlpha, borderAlpha, borderAlpha, 255); // Left
    fillRoundRect(inputX + inputW, inputY, 1, inputH, borderAlpha, borderAlpha, borderAlpha, 255); // Right

    // 绘制输入文本
    std::string displayText = m_searchQuery;
    if (m_isSearchInputFocused && !hasSearchSelection() && (SDL_GetTicks() % 1000) < 500) {
        displayText += "|"; // 简单的光标
    }
    if (m_isSearchInputFocused && hasSearchSelection()) {
        auto [selectionStart, selectionEnd] = searchSelectionRange();
        std::string prefix = m_searchQuery.substr(0, selectionStart);
        std::string selected = m_searchQuery.substr(selectionStart, selectionEnd - selectionStart);
        int highlightX = inputX + 12 + getTextWidth(prefix, 12);
        int highlightW = std::max(2, getTextWidth(selected, 12));
        fillRoundRect(highlightX - 2, inputY + 10, highlightW + 4, 22, 4,
                      70, 130, 230, 170);
    }
    if (displayText.empty() && !m_isSearchInputFocused) {
        drawText("输入搜索内容或问题...", inputX + 12, inputY + 14, 150, 150, 150, 12);
    } else {
        drawText(displayText, inputX + 12, inputY + 14, 255, 255, 255, 12);
    }

    m_controlRects.push_back({inputX, inputY, inputW, inputH, ControlType::SearchInput, 0});
    // 面板背景命中检测
    m_controlRects.push_back({panelX, panelY, panelW, panelH, ControlType::PanelBackground, 0});
}

} // namespace VideoPlay
