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
        case 90: // AB循环: 设置A点
            setLoopPointA();
            break;
        case 91: // AB循环: 设置B点
            setLoopPointB();
            break;
        case 92: // AB循环: 清除
            clearLoop();
            break;
        case 300: // AI 分析当前视频
            startAIAnalysis();
            break;
        case 301: // 显示摘要
            showAISummary();
            break;
        case 302: // 搜索内容
            showSearchPanel();
            break;
        case 303: // 清除 AI 缓存
            clearAICache();
            break;
        case 304: // AI 设置
            showAISettings();
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
        "- 播放列表管理\n"
        "- AI 视频摘要与章节自动划分\n"
        "- 智能内容搜索\n\n"
        "License: GPLv3";
    
    if (m_renderer) {
        m_renderer->showMessageBox("关于", aboutText, false);
    }
}

void VideoPlayerApp::startAIAnalysis() {
    if (m_currentFile.empty()) {
        if (m_renderer) {
            m_renderer->showMessageBox("AI 分析", "请先打开一个视频文件", false);
        }
        return;
    }

    if (!m_aiAnalyzer->isConfigured()) {
        if (m_renderer) {
            m_renderer->showMessageBox("AI 分析", 
                "请先配置 AI API Key\n\n"
                "在设置中配置 API 地址和 Key", false);
        }
        return;
    }

    if (m_aiAnalyzing) {
        Logger::instance().info("AI analysis already in progress");
        return;
    }

    m_aiAnalyzing = true;
    m_aiProgress = 0.0f;
    m_aiStatus = "开始分析...";

    Logger::instance().info("[AI] Starting analysis for: " + m_currentFile);

    m_aiAnalyzer->analyze(m_currentFile,
        [this](const AIAnalysisResult& result) {
            m_aiResult = result;
            m_aiAnalyzing = false;
            m_aiProgress = 1.0f;
            m_aiStatus = "分析完成";

            if (!result.chapters.empty() && m_player) {
                m_player->setChapters(result.chapters);
                m_renderer->setChapters(result.chapters);
            }

            if (m_searchEngine) {
                m_searchEngine->buildIndex(m_currentFile, result.transcript, result.chapters);
            }

            Logger::instance().info("[AI] Analysis complete: " + 
                std::to_string(result.chapters.size()) + " chapters");
        },
        [this](float progress, const std::string& status) {
            m_aiProgress = progress;
            m_aiStatus = status;
        },
        [this](const std::string& error) {
            m_aiAnalyzing = false;
            m_aiStatus = "分析失败: " + error;
            Logger::instance().error("[AI] Analysis failed: " + error);
        }
    );
}

void VideoPlayerApp::showAISummary() {
    if (m_currentFile.empty()) {
        if (m_renderer) {
            m_renderer->showMessageBox("AI 摘要", "请先打开一个视频文件", false);
        }
        return;
    }

    if (!m_aiResult.valid) {
        if (m_aiAnalyzer->hasCache(m_currentFile)) {
            m_aiResult = m_aiAnalyzer->loadCache(m_currentFile);
            if (m_aiResult.valid && m_searchEngine) {
                m_searchEngine->buildIndex(m_currentFile, m_aiResult.transcript, m_aiResult.chapters);
            }
        }
    }

    if (!m_aiResult.valid) {
        if (m_renderer) {
            m_renderer->showMessageBox("AI 摘要", 
                "当前视频尚未进行 AI 分析\n\n"
                "请先执行 \"AI 分析当前视频\"", false);
        }
        return;
    }

    std::string summaryText = "AI 摘要\n\n" + m_aiResult.summary + "\n\n自动生成的章节：\n";
    for (const auto& chapter : m_aiResult.chapters) {
        summaryText += "[" + formatTime(chapter.startTime) + "] " + chapter.title + "\n";
    }

    if (m_renderer) {
        m_renderer->showMessageBox("AI 摘要", summaryText, false);
    }
}

void VideoPlayerApp::showSearchPanel() {
    if (m_currentFile.empty()) {
        if (m_renderer) {
            m_renderer->showMessageBox("搜索", "请先打开一个视频文件", false);
        }
        return;
    }

    if (!m_searchEngine->hasIndex()) {
        if (m_aiResult.valid) {
            m_searchEngine->buildIndex(m_currentFile, m_aiResult.transcript, m_aiResult.chapters);
        } else if (m_aiAnalyzer->hasCache(m_currentFile)) {
            m_aiResult = m_aiAnalyzer->loadCache(m_currentFile);
            if (m_aiResult.valid) {
                m_searchEngine->buildIndex(m_currentFile, m_aiResult.transcript, m_aiResult.chapters);
            }
        }
    }

    if (!m_searchEngine->hasIndex()) {
        if (m_renderer) {
            m_renderer->showMessageBox("搜索", 
                "当前视频没有可搜索的内容\n\n"
                "请先执行 \"AI 分析当前视频\"", false);
        }
        return;
    }

    Logger::instance().info("[Search] Search panel requested");
}

void VideoPlayerApp::clearAICache() {
    if (m_currentFile.empty()) {
        if (m_renderer) {
            m_renderer->showMessageBox("清除缓存", "请先打开一个视频文件", false);
        }
        return;
    }

    m_aiAnalyzer->clearCache(m_currentFile);
    m_aiResult = AIAnalysisResult();
    if (m_searchEngine) {
        m_searchEngine->clearIndex();
    }

    if (m_renderer) {
        m_renderer->showMessageBox("清除缓存", "AI 缓存已清除", false);
    }
    Logger::instance().info("[AI] Cache cleared for: " + m_currentFile);
}

void VideoPlayerApp::showAISettings() {
    if (!m_renderer) return;

    AIConfig currentConfig = Settings::instance().aiConfig();
    
    AISettings settings;
    settings.baseUrl = currentConfig.baseUrl;
    settings.apiKey = currentConfig.apiKey;
    settings.model = currentConfig.model;

    SettingsDialog dialog(m_renderer->getWindow(), m_renderer->getFont());
    dialog.show(settings, [this](const AISettings& newSettings) {
        AIConfig config;
        config.baseUrl = newSettings.baseUrl;
        config.apiKey = newSettings.apiKey;
        config.model = newSettings.model;
        
        // 清理 URL - 移除重复的协议和路径
        std::string& url = config.baseUrl;
        
        // 查找第一个 "https://" 的位置
        size_t firstHttps = url.find("https://");
        if (firstHttps != std::string::npos) {
            // 查找第二个 "https://" 
            size_t secondHttps = url.find("https://", firstHttps + 1);
            if (secondHttps != std::string::npos) {
                // 只保留第一个 URL
                url = url.substr(0, secondHttps);
            }
        }
        
        // 移除末尾的斜杠
        while (!url.empty() && url.back() == '/') {
            url.pop_back();
        }
        
        // 移除末尾的 /v1（如果存在）
        if (url.size() >= 3 && url.substr(url.size() - 3) == "/v1") {
            url = url.substr(0, url.size() - 3);
        }
        
        Settings::instance().setAIConfig(config);
        
        if (m_aiAnalyzer) {
            m_aiAnalyzer->configure(config);
        }
        
        Logger::instance().info("[AI] Settings saved, baseUrl: " + config.baseUrl);
    });
}

void VideoPlayerApp::performSearch(const std::string& query) {
    if (!m_searchEngine || !m_searchEngine->hasIndex() || query.empty()) {
        return;
    }

    auto results = m_searchEngine->search(query, 20);
    Logger::instance().info("[Search] Query: " + query + " Results: " + std::to_string(results.size()));

    if (!results.empty()) {
        seek(results[0].timestamp - m_position);
    }
}

} // namespace VideoPlay
