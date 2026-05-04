#include "app.h"
#include "renderer/sdlrenderer.h"
#include "core/ffmpegplayer.h"
#include "core/settings.h"
#include "utils/logger.h"
#include "subtitles/subtitleparser.h"
#include <SDL3/SDL.h>
#include <iostream>

namespace VideoPlay {


void VideoPlayerApp::handleMenu(int menuId) {
    // 最近文件菜单项 ID 范围 100-109
    if (menuId >= 100 && menuId < 110) {
        auto recent = Settings::instance().recentFiles();
        size_t idx = static_cast<size_t>(menuId - 100);
        if (idx < recent.size()) {
            openFile(recent[idx]);
        }
        return;
    }

    switch (menuId) {
        case 1: // 打开文件
            openFileDialog();
            break;
        case 2: // 打开文件夹
            openFolderDialog();
            break;
        case 4: // 导入字幕
            openSubtitleDialog();
            break;
        case 3: // 退出
            m_running = false;
            break;
        case 10: // 播放/暂停
            togglePlayPause();
            break;
        case 11: // 停止
            stop();
            break;
        case 12: // 上一个
            playPrevious();
            break;
        case 13: // 下一个
            playNext();
            break;
        case 18: // 播放列表
            m_renderer->togglePlaylistPanel();
            break;
        case 19: // 上一集（播放菜单就近入口）
            playPreviousEpisode();
            break;
        case 22: // 下一集（播放菜单就近入口）
            playNextEpisode();
            break;
        case 30: // 上一集
            playPreviousEpisode();
            break;
        case 31: // 下一集
            playNextEpisode();
            break;
        case 32: // 切换选集面板
            m_renderer->toggleEpisodePanel();
            break;
        case 14: // 增加速度
            setSpeed(m_speed * 1.25);
            break;
        case 15: // 降低速度
            setSpeed(m_speed * 0.8);
            break;
        case 16: // 全屏
            if (m_renderer) m_renderer->toggleFullscreen();
            break;
        case 17: // 无边框模式
            if (m_renderer) m_renderer->toggleBorderless();
            break;
        case 50: // 快捷键
            showHelp();
            break;
        case 51: // 关于
            showAbout();
            break;
        case 60: // 不循环
            Settings::instance().setLoopMode(LoopMode::None);
            if (m_renderer) m_renderer->setLoopMode(0);
            break;
        case 61: // 单曲循环
            Settings::instance().setLoopMode(LoopMode::Single);
            if (m_renderer) m_renderer->setLoopMode(1);
            break;
        case 62: // 列表循环
            Settings::instance().setLoopMode(LoopMode::Playlist);
            if (m_renderer) m_renderer->setLoopMode(2);
            break;
        case 70: // 原始比例
            Settings::instance().setAspectMode(AspectMode::Original);
            if (m_renderer) m_renderer->setAspectMode(AspectMode::Original);
            break;
        case 71: // 16:9
            Settings::instance().setAspectMode(AspectMode::R16_9);
            if (m_renderer) m_renderer->setAspectMode(AspectMode::R16_9);
            break;
        case 72: // 4:3
            Settings::instance().setAspectMode(AspectMode::R4_3);
            if (m_renderer) m_renderer->setAspectMode(AspectMode::R4_3);
            break;
        case 73: // 铺满
            Settings::instance().setAspectMode(AspectMode::FillWindow);
            if (m_renderer) m_renderer->setAspectMode(AspectMode::FillWindow);
            break;
        case 80: // 始终置顶
            if (m_renderer) {
                m_renderer->toggleAlwaysOnTop();
                Settings::instance().setAlwaysOnTop(m_renderer->isAlwaysOnTop());
            }
            break;
    }

    // 章节跳转菜单项 ID 范围 200-249
    if (menuId >= 200 && menuId < 250) {
        size_t idx = static_cast<size_t>(menuId - 200);
        auto chapters = m_player->chapters();
        if (idx < chapters.size()) {
            int64_t targetPos = chapters[idx].startTime;
            if (targetPos >= 0 && targetPos < m_duration) {
                seek(targetPos - m_position); // 相对 seek
                Logger::instance().info("Chapter seek: " + chapters[idx].title +
                    " at " + formatTime(targetPos));
            }
        }
    }
}

void VideoPlayerApp::openSubtitleDialog() {
    if (!m_renderer) return;

    Logger::instance().info("Opening subtitle dialog...");

    m_renderer->openSubtitleDialog([this](const std::string& filePath) {
        if (!filePath.empty()) {
            Logger::instance().info("Selected subtitle: " + filePath);
            loadSubtitleFile(filePath);
        } else {
            Logger::instance().info("Subtitle dialog cancelled");
        }
    });
}

void VideoPlayerApp::loadSubtitleFile(const std::string& path) {
    if (!m_subtitleParser) return;

    if (m_subtitleParser->loadFile(path)) {
        m_currentSubtitle = path;
        Logger::instance().info("Loaded subtitle: " + path);
    } else {
        Logger::instance().error("Failed to load subtitle: " + path);
        m_currentSubtitle.clear();
    }
}

void VideoPlayerApp::showHelp() {
    const char* helpText = 
        "快捷键列表：\n\n"
        "空格          - 播放/暂停\n"
        "S             - 停止\n"
        "← / →         - 后退/前进 5秒\n"
        "Shift+← / Shift+→ - 后退/前进 30秒\n"
        "↑ / ↓         - 音量增加/减少\n"
        "M             - 静音切换\n"
        ".             - 切换播放速度\n"
        "[ / ]         - 降低/增加速度\n"
        "F             - 全屏切换\n"
        "N             - 下一个（优先下一集）\n"
        "P             - 上一个（优先上一集）\n"
        "Ctrl+Shift+Right - 下一集\n"
        "Ctrl+Shift+Left  - 上一集\n"
        "L             - 循环模式切换\n"
        "A             - 画面比例切换\n"
        "T             - 窗口置顶\n"
        "G / H         - 字幕提前/延后 0.5秒\n"
        "[             - AB循环: 设置A点\n"
        "]             - AB循环: 设置B点\n"
        "\\             - AB循环: 清除\n"
        "F12           - 截图\n"
        "Ctrl+E        - 切换选集面板\n"
        "Ctrl+L        - 切换播放列表\n"
        "Ctrl+O        - 打开文件\n"
        "F1            - 显示帮助\n"
        "Esc           - 退出全屏/关闭菜单\n\n"
        "鼠标操作：\n"
        "左键点击控制栏按钮\n"
        "拖动进度条跳转\n"
        "滚轮调节音量\n"
        "拖放文件到窗口播放";
    
    if (m_renderer) {
        m_renderer->showMessageBox("帮助", helpText, false);
    }
}

void VideoPlayerApp::showAbout() {
    const char* aboutText = 
        "VideoPlay v" APP_VERSION "\n\n"
        "基于 FFmpeg + SDL3 的视频播放器\n\n"
        "功能特性：\n"
        "- 支持多种视频格式\n"
        "- 变速播放 (0.25x - 4x)\n"
        "- 字幕支持 (SRT/ASS/VTT)\n"
        "- 播放列表管理\n\n"
        "License: GPLv3";
    
    if (m_renderer) {
        m_renderer->showMessageBox("关于", aboutText, false);
    }
}

} // namespace VideoPlay
