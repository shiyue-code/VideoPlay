#include "app.h"
#include "renderer/sdlrenderer.h"
#include "core/ffmpegplayer.h"
#include "core/settings.h"
#include "utils/logger.h"
#include "utils/string_utils.h"
#include "subtitles/subtitleparser.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

namespace VideoPlay {

namespace {

static inline int menuItemOffset(int rawId, MenuId base, int count) {
    int b = static_cast<int>(base);
    int idx = rawId - b;
    if (idx >= 0 && idx < count) return idx;
    return -1;
}

Logger& logger() {
    static auto logger = Logger::get("app.events");
    return *logger;
}


}



void VideoPlayerApp::handleMenu(MenuId menuId) {
    int rawId = static_cast<int>(menuId);
    // 最近文件菜单项
    int recentIdx = menuItemOffset(rawId, MenuId::RecentFileBase, kRecentFileCount);
    if (recentIdx != -1) {
        auto recent = Settings::instance().recentFiles();
        size_t idx = static_cast<size_t>(recentIdx);
        if (idx < recent.size()) {
            openFile(recent[idx]);
        }
        return;
    }

    switch (menuId) {
        case MenuId::OpenFile: // 打开文件
            openFileDialog();
            break;
        case MenuId::OpenFolder: // 打开文件夹
            openFolderDialog();
            break;
        case MenuId::ImportSubtitle: // 导入字幕
            openSubtitleDialog();
            break;
        case MenuId::Exit: // 退出
            m_running = false;
            break;
        case MenuId::PlayPause: // 播放/暂停
            togglePlayPause();
            break;
        case MenuId::Stop: // 停止
            stop();
            break;
        case MenuId::Prev: // 上一个
            playPrevious();
            break;
        case MenuId::Next: // 下一个
            playNext();
            break;
        case MenuId::Playlist: // 播放列表
            m_renderer->togglePlaylistPanel();
            break;
        case MenuId::PlaylistRemove:
            if (m_renderer) {
                // 由列表右键菜单的回调处理；此处给播放菜单兜底
            }
            break;
        case MenuId::PlaylistClear:
            clearPlaylist();
            break;
        case MenuId::PlaylistPlayItem:
            break;
        case MenuId::PrevEpisodePlayMenu: // 上一集（播放菜单就近入口）
            playPreviousEpisode();
            break;
        case MenuId::NextEpisodePlayMenu: // 下一集（播放菜单就近入口）
            playNextEpisode();
            break;
        case MenuId::PrevEpisode: // 上一集
            playPreviousEpisode();
            break;
        case MenuId::NextEpisode: // 下一集
            playNextEpisode();
            break;
        case MenuId::EpisodePanel: // 切换选集面板
            m_renderer->toggleEpisodePanel();
            break;
        case MenuId::SpeedUp: // 增加速度
            setSpeed(m_speed * 1.25);
            break;
        case MenuId::SpeedDown: // 降低速度
            setSpeed(m_speed * 0.8);
            break;
        case MenuId::Fullscreen: // 全屏
            if (m_renderer) m_renderer->toggleFullscreen();
            break;
        case MenuId::Borderless: // 无边框模式
            if (m_renderer) m_renderer->toggleBorderless();
            break;
        case MenuId::Help: // 快捷键
            showHelp();
            break;
        case MenuId::About: // 关于
            showAbout();
            break;
        case MenuId::LoopNone: // 不循环
            Settings::instance().setLoopMode(LoopMode::None);
            if (m_renderer) m_renderer->setLoopMode(0);
            break;
        case MenuId::LoopSingle: // 单曲循环
            Settings::instance().setLoopMode(LoopMode::Single);
            if (m_renderer) m_renderer->setLoopMode(1);
            break;
        case MenuId::LoopPlaylist: // 列表循环
            Settings::instance().setLoopMode(LoopMode::Playlist);
            if (m_renderer) m_renderer->setLoopMode(2);
            break;
        case MenuId::AspectOriginal: // 原始比例
            Settings::instance().setAspectMode(AspectMode::Original);
            if (m_renderer) m_renderer->setAspectMode(AspectMode::Original);
            break;
        case MenuId::Aspect16_9: // 16:9
            Settings::instance().setAspectMode(AspectMode::R16_9);
            if (m_renderer) m_renderer->setAspectMode(AspectMode::R16_9);
            break;
        case MenuId::Aspect4_3: // 4:3
            Settings::instance().setAspectMode(AspectMode::R4_3);
            if (m_renderer) m_renderer->setAspectMode(AspectMode::R4_3);
            break;
        case MenuId::AspectFill: // 铺满
            Settings::instance().setAspectMode(AspectMode::FillWindow);
            if (m_renderer) m_renderer->setAspectMode(AspectMode::FillWindow);
            break;
        case MenuId::AlwaysOnTop: // 始终置顶
            if (m_renderer) {
                m_renderer->toggleAlwaysOnTop();
                Settings::instance().setAlwaysOnTop(m_renderer->isAlwaysOnTop());
            }
            break;
        case MenuId::ABLoopSetA: // AB循环: 设置A点
            setLoopPointA();
            break;
        case MenuId::ABLoopSetB: // AB循环: 设置B点
            setLoopPointB();
            break;
        case MenuId::ABLoopClear: // AB循环: 清除
            clearLoop();
            break;
        case MenuId::AudioFilterOff:
        case MenuId::AudioFilterVoice:
        case MenuId::AudioFilterBass:
        case MenuId::AudioFilterNight: {
            auto preset = static_cast<AudioFilterPreset>(rawId - 93);
            auto config = audioFilterConfigForPreset(preset);
            Settings::instance().setAudioFilterConfig(config);
            Settings::instance().save();
            if (m_player) {
                m_player->setAudioFilterConfig(config);
            }
            if (m_renderer) {
                m_renderer->setAudioFilterPreset(preset);
                m_renderer->showOSD(OSDType::Info,
                                    std::string("音频滤镜 ") + audioFilterPresetName(preset));
            }
            break;
        }
        case MenuId::HardwareDecoding: {
            bool enabled = !Settings::instance().hardwareDecodingEnabled();
            Settings::instance().setHardwareDecodingEnabled(enabled);
            Settings::instance().save();
            if (m_player) {
                m_player->setHardwareDecodingEnabled(enabled);
            }
            if (m_renderer) {
                m_renderer->setHardwareDecodingEnabled(enabled);
                std::string text = enabled ? "硬件解码 已开启" : "硬件解码 已关闭";
                if (!m_currentFile.empty()) {
                    text += "，重新打开文件后生效";
                }
                m_renderer->showOSD(OSDType::Info, text);
            }
            break;
        }
        case MenuId::VideoFilterBrightnessUp:
        case MenuId::VideoFilterBrightnessDown:
        case MenuId::VideoFilterContrastUp:
        case MenuId::VideoFilterContrastDown:
        case MenuId::VideoFilterSaturationUp:
        case MenuId::VideoFilterReset: {
            if (!m_player) break;
            auto config = m_player->videoFilterConfig();
            config.enabled = true;
            switch (menuId) {
                case MenuId::VideoFilterBrightnessUp: config.brightness = std::clamp(config.brightness + 0.05f, -1.0f, 1.0f); break;
                case MenuId::VideoFilterBrightnessDown: config.brightness = std::clamp(config.brightness - 0.05f, -1.0f, 1.0f); break;
                case MenuId::VideoFilterContrastUp: config.contrast = std::clamp(config.contrast + 0.1f, 0.0f, 2.0f); break;
                case MenuId::VideoFilterContrastDown: config.contrast = std::clamp(config.contrast - 0.1f, 0.0f, 2.0f); break;
                case MenuId::VideoFilterSaturationUp: config.saturation = std::clamp(config.saturation + 0.1f, 0.0f, 3.0f); break;
                case MenuId::VideoFilterReset: config = VideoFilterConfig{}; break;  // 重置
                default: break;
            }
            Settings::instance().setVideoFilterConfig(config);
            Settings::instance().save();
            m_player->setVideoFilterConfig(config);
            if (m_renderer) {
                if (config.isDefault()) {
                    m_renderer->showOSD(OSDType::Info, "视频基础参数 已重置");
                } else {
                    m_renderer->showOSD(OSDType::Info,
                        "亮度 " + std::to_string(config.brightness).substr(0, 5) +
                        " 对比 " + std::to_string(config.contrast).substr(0, 4) +
                        " 饱和 " + std::to_string(config.saturation).substr(0, 4));
                }
            }
            break;
        }
        case MenuId::AIAnalyze: // AI 分析当前视频
            startAIAnalysis();
            break;
        case MenuId::AISummary: // 显示摘要
            showAISummary();
            break;
        case MenuId::Search: // 搜索内容
            showSearchPanel();
            break;
        case MenuId::ClearAICache: // 清除 AI 缓存
            clearAICache();
            break;
        case MenuId::AISettings: // AI 设置
            showAISettings();
            break;
        default:
            break;
    }

    // 章节跳转菜单项
    int chapterIdx = menuItemOffset(rawId, MenuId::ChapterBase, kChapterCount);
    if (chapterIdx != -1) {
        auto chapters = m_player->chapters();
        size_t idx = static_cast<size_t>(chapterIdx);
        if (idx < chapters.size()) {
            int64_t targetPos = chapters[idx].startTime;
            if (targetPos >= 0 && targetPos < m_duration) {
                seek(targetPos - m_position); // 相对 seek
                logger().info("Chapter seek: " + chapters[idx].title +
                    " at " + formatTime(targetPos));
            }
        }
        return;
    }

    // 音轨切换菜单项
    int trackIndex = menuItemOffset(rawId, MenuId::AudioTrackBase, kAudioTrackCount);
    if (trackIndex != -1) {
        if (m_player && m_renderer) {
            if (m_player->setAudioTrack(trackIndex)) {
                m_renderer->setAudioTracks(m_player->audioTracks(),
                                           m_player->currentAudioTrack());
                m_renderer->showOSD(OSDType::Info, "音轨已切换");
            }
        }
        return;
    }

    // 字幕切换菜单项
    int subtitleIdx = menuItemOffset(rawId, MenuId::SubtitleTrackBase, kSubtitleTrackCount);
    if (subtitleIdx != -1) {
        int trackIndex = (subtitleIdx == 0) ? -1 : (subtitleIdx - 1);
        if (m_player && m_renderer) {
            if (m_player->setSubtitleTrack(trackIndex)) {
                m_renderer->clearSubtitleBitmap();
                m_renderer->setSubtitleTracks(m_player->subtitleTracks(),
                                              m_player->currentSubtitleTrack());
                if (trackIndex == -1) {
                    m_renderer->showOSD(OSDType::Info, "内封字幕已关闭");
                } else {
                    m_renderer->showOSD(OSDType::Info, "字幕轨已切换");
                }
            }
        }
        return;
    }
}

void VideoPlayerApp::openSubtitleDialog() {
    if (!m_renderer) return;

    logger().info("Opening subtitle dialog...");

    m_renderer->openSubtitleDialog([this](const std::string& filePath) {
        if (!filePath.empty()) {
            logger().info("Selected subtitle: " + filePath);
            loadSubtitleFile(filePath);
        } else {
            logger().info("Subtitle dialog cancelled");
        }
    });
}

void VideoPlayerApp::loadSubtitleFile(const std::string& path) {
    if (!m_subtitleParser) return;

    if (m_subtitleParser->loadFile(path)) {
        m_currentSubtitle = path;
        logger().info("Loaded subtitle: " + path);
        if (m_searchEngine) {
            m_searchEngine->addSubtitleEntries(m_subtitleParser->entries());
        }
    } else {
        logger().error("Failed to load subtitle: " + path);
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
        "播放菜单      - 切换音频滤镜预设\n"
        "播放菜单      - 切换硬件解码\n"
        "Tab           - 媒体信息\n"
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

    {
        std::lock_guard<std::mutex> lock(m_aiStateMutex);
        if (m_aiAnalyzing) {
            logger().info("AI analysis already in progress");
            if (m_renderer) {
                m_renderer->showOSD(OSDType::Info, "AI 分析正在进行");
            }
            return;
        }

        m_aiAnalyzing = true;
        m_aiDirectSearchActive = false;
        m_aiProgress = 0.0f;
        m_aiStatus = "准备分析视频...";
        m_aiRequestStartTimeMs = 0;
    }

    logger().info("[AI] Starting analysis for: " + m_currentFile);

    m_aiAnalyzer->analyze(m_currentFile,
        [this](const AIAnalysisResult& result) {
            m_aiResult = result;
            {
                std::lock_guard<std::mutex> lock(m_aiStateMutex);
                m_aiAnalyzing = false;
                m_aiDirectSearchActive = false;
                m_aiProgress = 1.0f;
                m_aiStatus = "分析完成，生成 " + std::to_string(result.chapters.size()) + " 个章节";
                m_aiRequestStartTimeMs = 0;
            }

            if (!result.chapters.empty() && m_player) {
                m_player->setChapters(result.chapters);
                if (m_renderer) {
                    m_renderer->setChapters(result.chapters);
                }
            }

            if (m_searchEngine) {
                m_searchEngine->buildIndex(m_currentFile, result.transcript, result.chapters);
            }

            logger().info("[AI] Analysis complete: " + 
                std::to_string(result.chapters.size()) + " chapters");
            processNextPendingSearch();
        },
        [this](float progress, const std::string& status) {
            std::lock_guard<std::mutex> lock(m_aiStateMutex);
            m_aiProgress = progress;
            m_aiDirectSearchActive = false;
            m_aiStatus = status.empty() ? "正在分析视频内容..." : status;
        },
        [this](const std::string& error) {
            {
                std::lock_guard<std::mutex> lock(m_aiStateMutex);
                m_aiAnalyzing = false;
                m_aiDirectSearchActive = false;
                m_aiStatus = "分析失败: " + error;
                m_aiRequestStartTimeMs = 0;
            }
            logger().error("[AI] Analysis failed: " + error);
            processNextPendingSearch();
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
        m_renderer->showMessageBox("AI 摘要", summaryText, false,
            [this](int64_t timestampMs) {
                if (m_duration <= 0) {
                    return;
                }

                double ratio = static_cast<double>(timestampMs) / static_cast<double>(m_duration);
                seekTo(std::max(0.0, std::min(1.0, ratio)));
            });
    }
}

void VideoPlayerApp::showSearchPanel() {
    if (m_currentFile.empty()) {
        if (m_renderer) {
            m_renderer->showMessageBox("搜索", "请先打开一个视频文件", false);
        }
        return;
    }

    if (m_renderer) {
        m_renderer->showSearchPanel();
    }

    logger().info("[Search] Search panel requested");
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
    logger().info("[AI] Cache cleared for: " + m_currentFile);
}

void VideoPlayerApp::showAISettings() {
    if (!m_renderer) return;

    AIConfig currentConfig = Settings::instance().aiConfig();
    
    AISettings settings;
    settings.provider = currentConfig.provider;
    settings.analysisDetailLevel = currentConfig.analysisDetailLevel;
    for (const auto& entry : currentConfig.providers) {
        settings.providers[entry.first] = {
            entry.second.baseUrl,
            entry.second.apiKey,
            entry.second.model
        };
    }
    if (settings.providers.find(settings.provider) == settings.providers.end()) {
        settings.providers[settings.provider] = {
            currentConfig.baseUrl,
            currentConfig.apiKey,
            currentConfig.model
        };
    }

    SettingsDialog dialog(m_renderer->getWindow(), m_renderer->getFont());
    dialog.show(settings, [this](const AISettings& newSettings) {
        AIConfig config = Settings::instance().aiConfig();
        config.provider = toLower(trim(newSettings.provider));
        config.analysisDetailLevel = std::clamp(newSettings.analysisDetailLevel, 0, 2);
        if (config.provider.empty() || config.provider == "auto") {
            config.provider = "mimo";
        } else if (config.provider == "google") {
            config.provider = "gemini";
        } else if (config.provider == "xiaomi" || config.provider == "xiaomimimo") {
            config.provider = "mimo";
        }

        config.providers.clear();
        for (const auto& entry : newSettings.providers) {
            std::string providerId = toLower(trim(entry.first));
            if (providerId.empty() || providerId == "auto") {
                providerId = "mimo";
            } else if (providerId == "google") {
                providerId = "gemini";
            } else if (providerId == "xiaomi" || providerId == "xiaomimimo") {
                providerId = "mimo";
            }

            config.providers[providerId] = {
                trim(entry.second.baseUrl),
                entry.second.apiKey,
                trim(entry.second.model)
            };
        }

        auto activeIt = config.providers.find(config.provider);
        if (activeIt != config.providers.end()) {
            config.baseUrl = activeIt->second.baseUrl;
            config.apiKey = activeIt->second.apiKey;
            config.model = activeIt->second.model;
        } else {
            config.baseUrl.clear();
            config.apiKey.clear();
            config.model.clear();
        }
        
        Settings::instance().setAIConfig(config);
        config = Settings::instance().aiConfig();
        
        if (m_aiAnalyzer) {
            m_aiAnalyzer->configure(config);
        }
        
        logger().info("[AI] Settings saved, provider: " + config.provider +
                      ", baseUrl: " + config.baseUrl +
                      ", model: " + config.model);
    });
}

std::vector<SearchResult> VideoPlayerApp::performSearch(const std::string& query) {
    std::vector<SearchResult> results;
    if (!m_searchEngine || !m_searchEngine->hasIndex() || query.empty()) {
        if (m_renderer) {
            m_renderer->setSearchHighlights({});
        }
        return results;
    }

    results = m_searchEngine->search(query, 20);
    logger().info("[Search] Query: " + query + " Results: " + std::to_string(results.size()));

    if (m_renderer) {
        std::vector<int64_t> highlights;
        for (const auto& r : results) {
            highlights.push_back(r.timestamp);
        }
        m_renderer->setSearchHighlights(highlights);
    }
    return results;
}

void VideoPlayerApp::handleSearch(const std::string& query) {
    handleSearchInternal(query, true);
}

void VideoPlayerApp::handleSearchInternal(const std::string& query, bool addUserMessage) {
    if (query.empty() || !m_renderer) return;

    if (addUserMessage) {
        m_renderer->addChatMessage(true, query);
    }

    if (!m_aiResult.valid && m_aiAnalyzer && m_aiAnalyzer->hasCache(m_currentFile)) {
        AIAnalysisResult cachedResult = m_aiAnalyzer->loadCache(m_currentFile);
        if (cachedResult.valid && !cachedResult.transcript.empty()) {
            m_aiResult = cachedResult;
        }
        if (m_aiResult.valid && !m_aiResult.transcript.empty() && m_searchEngine) {
            m_searchEngine->buildIndex(m_currentFile, m_aiResult.transcript, m_aiResult.chapters);
        }
    }

    auto localResults = performSearch(query);
    if (!localResults.empty()) {
        std::string message = "找到 " + std::to_string(localResults.size()) + " 处相关内容：\n";
        size_t count = std::min<size_t>(localResults.size(), 8);
        for (size_t i = 0; i < count; ++i) {
            const auto& result = localResults[i];
            message += "[" + formatTime(result.timestamp) + "] " + result.text + "\n";
        }
        m_renderer->addChatMessage(false, message);
    }

    auto isQuestionLike = [](const std::string& text) {
        const std::vector<std::string> markers = {
            "?", "？", "为什么", "怎么", "如何", "是什么", "吗", "呢",
            "谁", "哪里", "哪儿", "多少", "解释", "总结", "讲了"
        };
        for (const auto& marker : markers) {
            if (text.find(marker) != std::string::npos) {
                return true;
            }
        }
        return false;
    };

    bool questionLike = isQuestionLike(query);

    bool hasCompleteAnalysis = m_aiResult.valid && !m_aiResult.transcript.empty();
    if (!hasCompleteAnalysis) {
        if (!m_aiAnalyzer || !m_aiAnalyzer->isConfigured()) {
            m_renderer->addChatMessage(false,
                "当前视频还没有 AI 分析结果，且 API 尚未配置。请先在 AI 设置中配置服务商和 API Key。");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_aiStateMutex);
            if (m_aiAnalyzing) {
                m_pendingSearchQueries.push_back(query);
                m_renderer->addChatMessage(false, "AI 正在处理当前视频，已把这条问题加入队列。");
                return;
            }
            m_aiAnalyzing = true;
            m_aiDirectSearchActive = true;
            m_aiProgress = 0.0f;
            m_aiStatus = "正在调用 API 搜索当前视频...";
            m_aiRequestStartTimeMs = SDL_GetTicks();
        }

        uint64_t requestId = ++m_aiRequestSeq;
        std::string requestedFile = m_currentFile;
        m_renderer->setAIAnalysisState(true, 0.0f, "正在调用 API 搜索当前视频...");

        m_aiAnalyzer->askVideoDirect(requestedFile, query,
            [this, requestedFile, requestId](const std::string& answer) {
                if (requestId != m_aiRequestSeq.load()) {
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(m_aiStateMutex);
                    m_aiAnalyzing = false;
                    m_aiDirectSearchActive = false;
                    m_aiProgress = 1.0f;
                    m_aiStatus = "搜索完成";
                    m_aiRequestStartTimeMs = 0;
                }

                if (m_renderer) {
                    std::string displayAnswer = answer;
                    if (requestedFile != m_currentFile) {
                        std::string fileName = std::filesystem::path(requestedFile).filename().u8string();
                        displayAnswer = "（以下回答来自提问时的视频：" + fileName + "）\n" + answer;
                    }
                    m_renderer->addChatMessage(false, displayAnswer);
                }
                processNextPendingSearch();
            },
            [this, requestId](float progress, const std::string& status) {
                if (requestId != m_aiRequestSeq.load()) {
                    return;
                }
                std::lock_guard<std::mutex> lock(m_aiStateMutex);
                m_aiProgress = progress;
                m_aiDirectSearchActive = true;
                m_aiStatus = status.empty() ? "正在调用 API 搜索视频..." : status;
            },
            [this, requestId](const std::string& error) {
                if (requestId != m_aiRequestSeq.load()) {
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(m_aiStateMutex);
                    m_aiAnalyzing = false;
                    m_aiDirectSearchActive = false;
                    m_aiStatus = "搜索失败: " + error;
                    m_aiRequestStartTimeMs = 0;
                }
                if (m_renderer) {
                    m_renderer->addChatMessage(false, "API 搜索失败: " + error);
                }
                processNextPendingSearch();
            });
        return;
    }

    if (!questionLike) {
        if (localResults.empty()) {
            m_renderer->addChatMessage(false, "没有找到匹配内容。");
        }
        return;
    }

    if (!m_aiAnalyzer) return;

    m_aiAnalyzer->askQuestion(query, m_aiResult,
        [this](const std::string& answer) {
            if (m_renderer) {
                m_renderer->addChatMessage(false, answer);
            }
        },
        [this](const std::string& error) {
            if (m_renderer) {
                m_renderer->addChatMessage(false, "API 请求失败: " + error);
            }
        });
}

void VideoPlayerApp::processNextPendingSearch() {
    std::string nextQuery;
    {
        std::lock_guard<std::mutex> lock(m_aiStateMutex);
        if (m_aiAnalyzing || m_pendingSearchQueries.empty()) {
            return;
        }
        nextQuery = m_pendingSearchQueries.front();
        m_pendingSearchQueries.pop_front();
    }

    if (!nextQuery.empty()) {
        handleSearchInternal(nextQuery, false);
    }
}

void VideoPlayerApp::checkAISearchTimeout() {
    constexpr uint64_t kDirectSearchTimeoutMs = 240000;

    bool timedOut = false;
    std::string message;
    {
        std::lock_guard<std::mutex> lock(m_aiStateMutex);
        if (!m_aiAnalyzing || !m_aiDirectSearchActive || m_aiRequestStartTimeMs == 0) {
            return;
        }

        uint64_t now = SDL_GetTicks();
        if (now - m_aiRequestStartTimeMs < kDirectSearchTimeoutMs) {
            return;
        }

        m_aiAnalyzing = false;
        m_aiDirectSearchActive = false;
        m_aiProgress = 1.0f;
        m_aiStatus = "搜索超时，请稍后重试或检查 API 服务状态";
        m_aiRequestStartTimeMs = 0;
        ++m_aiRequestSeq;
        message = "API 搜索超时：视频已转码，但服务端长时间没有返回答案。请稍后重试，或检查当前模型/API 服务是否支持视频输入。";
        timedOut = true;
    }

    if (!timedOut) {
        return;
    }

    logger().warning("[AI] Direct video search timed out");
    if (m_aiAnalyzer) {
        m_aiAnalyzer->cancel();
    }
    if (m_renderer) {
        m_renderer->addChatMessage(false, message);
    }
    processNextPendingSearch();
}

} // namespace VideoPlay
