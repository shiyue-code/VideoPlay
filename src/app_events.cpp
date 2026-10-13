#include "app.h"
#include "renderer/sdlrenderer.h"
#include "renderer/subtitlestyledialog.h"
#include "core/ffmpegplayer.h"
#include "core/fileassoc.h"
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
        case MenuId::OpenUrl: // 打开 URL
            openUrlDialog();
            break;
        case MenuId::ImportSubtitle: // 导入字幕
            openSubtitleDialog();
            break;
        case MenuId::SubtitleStyle: // 字幕样式
            showSubtitleStyleDialog();
            break;
        case MenuId::SetDefaultPlayer: // 设为默认播放器
            registerAsDefaultPlayer();
            break;
        case MenuId::RemoveFileAssoc: // 取消文件关联
            removeFileAssociations();
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
            // 删除项由 menu_manager 的 m_playlistRemoveCallback 直接处理
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
        case MenuId::FramePrev: // 上一帧
            stepFrame(-1);
            break;
        case MenuId::FrameNext: // 下一帧
            stepFrame(1);
            break;
        case MenuId::GotoTime: // 跳转到时间
            showGotoTimeDialog();
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
        case MenuId::Rotate0:
        case MenuId::Rotate90:
        case MenuId::Rotate180:
        case MenuId::Rotate270:
        case MenuId::FlipHorizontal:
        case MenuId::FlipVertical:
        case MenuId::CropOff:
        case MenuId::Crop10:
        case MenuId::Crop20:
        case MenuId::TransformReset: {
            if (!m_renderer) break;
            VideoTransform transform = m_renderer->videoTransform();
            switch (menuId) {
                case MenuId::Rotate0: transform.rotation = 0; break;
                case MenuId::Rotate90: transform.rotation = 90; break;
                case MenuId::Rotate180: transform.rotation = 180; break;
                case MenuId::Rotate270: transform.rotation = 270; break;
                case MenuId::FlipHorizontal: transform.flipHorizontal = !transform.flipHorizontal; break;
                case MenuId::FlipVertical: transform.flipVertical = !transform.flipVertical; break;
                case MenuId::CropOff: transform.cropPercent = 0; break;
                case MenuId::Crop10: transform.cropPercent = 10; break;
                case MenuId::Crop20: transform.cropPercent = 20; break;
                case MenuId::TransformReset: transform = VideoTransform{}; break;
                default: break;
            }
            m_renderer->setVideoTransform(transform);
            Settings::instance().setVideoTransform(transform);
            Settings::instance().save();
            std::string osd = "画面 " + std::to_string(transform.rotation) + "°";
            if (transform.flipHorizontal) osd += " 水平翻转";
            if (transform.flipVertical) osd += " 垂直翻转";
            if (transform.cropPercent > 0) {
                osd += " 裁剪" + std::to_string(transform.cropPercent) + "%";
            }
            if (transform.isDefault()) {
                osd = "画面变换 已重置";
            }
            m_renderer->showOSD(OSDType::Info, osd);
            break;
        }
        case MenuId::AlwaysOnTop: // 始终置顶
            if (m_renderer) {
                m_renderer->toggleAlwaysOnTop();
                Settings::instance().setAlwaysOnTop(m_renderer->isAlwaysOnTop());
            }
            break;
        case MenuId::Screenshot: // 截图
            takeScreenshot();
            break;
        case MenuId::ScreenshotToClipboard: // 复制截图到剪贴板
            copyScreenshotToClipboard();
            break;
        case MenuId::GifExport: // 导出 GIF 片段
            exportGifClip();
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
        case MenuId::ChannelMapOriginal:
        case MenuId::ChannelMapMono:
        case MenuId::ChannelMapSwapLR: {
            auto mode = static_cast<ChannelMapMode>(
                rawId - static_cast<int>(MenuId::ChannelMapOriginal));
            Settings::instance().setChannelMapMode(mode);
            Settings::instance().save();
            if (m_player) {
                m_player->setChannelMapMode(mode);
            }
            if (m_renderer) {
                m_renderer->setChannelMapMode(mode);
                m_renderer->showOSD(OSDType::Info,
                    std::string("声道映射 ") + channelMapModeName(mode));
            }
            break;
        }
        case MenuId::DeinterlaceOff:
        case MenuId::DeinterlaceAuto:
        case MenuId::DeinterlaceYadif:
        case MenuId::DeinterlaceBwdif: {
            if (!m_player) break;
            auto config = m_player->videoFilterConfig();
            switch (menuId) {
                case MenuId::DeinterlaceOff: config.deinterlace = DeinterlaceMode::Off; break;
                case MenuId::DeinterlaceAuto: config.deinterlace = DeinterlaceMode::Auto; break;
                case MenuId::DeinterlaceYadif: config.deinterlace = DeinterlaceMode::Yadif; break;
                case MenuId::DeinterlaceBwdif: config.deinterlace = DeinterlaceMode::Bwdif; break;
                default: break;
            }
            Settings::instance().setVideoFilterConfig(config);
            Settings::instance().save();
            m_player->setVideoFilterConfig(config);
            if (m_renderer) {
                m_renderer->setDeinterlaceMode(config.deinterlace);
                m_renderer->showOSD(OSDType::Info,
                    std::string("去隔行 ") + deinterlaceModeName(config.deinterlace));
            }
            break;
        }
        case MenuId::HdrToneMapAuto:
        case MenuId::HdrToneMapOn:
        case MenuId::HdrToneMapOff:
        case MenuId::HdrAlgoHable:
        case MenuId::HdrAlgoMobius:
        case MenuId::HdrAlgoReinhard: {
            if (!m_player) break;
            auto config = m_player->videoFilterConfig();
            std::string osdMsg;
            switch (menuId) {
                case MenuId::HdrToneMapAuto:
                    config.hdrToneMap = HdrToneMapMode::Auto;
                    osdMsg = "HDR 色调映射: 自动";
                    break;
                case MenuId::HdrToneMapOn:
                    config.hdrToneMap = HdrToneMapMode::On;
                    osdMsg = "HDR 色调映射: 开启";
                    break;
                case MenuId::HdrToneMapOff:
                    config.hdrToneMap = HdrToneMapMode::Off;
                    osdMsg = "HDR 色调映射: 关闭";
                    break;
                case MenuId::HdrAlgoHable:
                    config.hdrAlgorithm = HdrToneMapAlgorithm::Hable;
                    osdMsg = "HDR 映射算法: Hable";
                    break;
                case MenuId::HdrAlgoMobius:
                    config.hdrAlgorithm = HdrToneMapAlgorithm::Mobius;
                    osdMsg = "HDR 映射算法: Mobius";
                    break;
                case MenuId::HdrAlgoReinhard:
                    config.hdrAlgorithm = HdrToneMapAlgorithm::Reinhard;
                    osdMsg = "HDR 映射算法: Reinhard";
                    break;
                default: break;
            }
            Settings::instance().setVideoFilterConfig(config);
            Settings::instance().save();
            m_player->setVideoFilterConfig(config);
            if (m_renderer) {
                m_renderer->setHdrToneMapMode(config.hdrToneMap);
                m_renderer->setHdrToneMapAlgorithm(config.hdrAlgorithm);
                m_renderer->showOSD(OSDType::Info, osdMsg);
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
        case MenuId::VideoFilterPanel: {
            if (m_renderer) {
                m_renderer->toggleVideoControlPanel();
            }
            break;
        }
        case MenuId::VideoFilterBrightnessUp:
        case MenuId::VideoFilterBrightnessDown:
        case MenuId::VideoFilterContrastUp:
        case MenuId::VideoFilterContrastDown:
        case MenuId::VideoFilterSaturationUp:
        case MenuId::VideoFilterSaturationDown:
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
                case MenuId::VideoFilterSaturationDown: config.saturation = std::clamp(config.saturation - 0.1f, 0.0f, 3.0f); break;
                case MenuId::VideoFilterReset: {
                    auto deint = config.deinterlace;
                    auto hdrMode = config.hdrToneMap;
                    auto hdrAlgo = config.hdrAlgorithm;
                    config = VideoFilterConfig{};
                    config.deinterlace = deint;
                    config.hdrToneMap = hdrMode;
                    config.hdrAlgorithm = hdrAlgo;
                    break;
                }
                default: break;
            }
            Settings::instance().setVideoFilterConfig(config);
            Settings::instance().save();
            m_player->setVideoFilterConfig(config);
            if (m_renderer) {
                m_renderer->setVideoFilterConfig(config);
                if (config.eqIsIdentity()) {
                    m_renderer->showOSD(OSDType::Info, "视频基础参数 已重置");
                } else {
                    int bPct = static_cast<int>(std::round(config.brightness * 100.0f));
                    int cPct = static_cast<int>(std::round(config.contrast * 100.0f));
                    int sPct = static_cast<int>(std::round(config.saturation * 100.0f));
                    std::string bSign = bPct > 0 ? "+" : "";
                    m_renderer->showOSD(OSDType::Info,
                        "亮度 " + bSign + std::to_string(bPct) + "%" +
                        " 对比 " + std::to_string(cPct) + "%" +
                        " 饱和 " + std::to_string(sPct) + "%");
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

    // 音频输出设备
    int deviceIdx = menuItemOffset(rawId, MenuId::AudioDeviceBase, kAudioDeviceCount);
    if (deviceIdx != -1) {
        if (m_player && m_renderer) {
            const auto& devices = m_renderer->audioOutputDevices();
            if (deviceIdx >= 0 && static_cast<size_t>(deviceIdx) < devices.size()) {
                const auto& device = devices[static_cast<size_t>(deviceIdx)];
                m_player->setAudioOutputDevice(device.id);
                const std::string persistName = device.isDefault ? std::string() : device.name;
                Settings::instance().setAudioOutputDeviceName(persistName);
                Settings::instance().save();
                m_renderer->setAudioOutputDeviceName(persistName);
                m_renderer->showOSD(OSDType::Info, "音频输出 " + device.name);
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

void VideoPlayerApp::registerAsDefaultPlayer() {
    if (!m_renderer) return;

    if (registerFileAssociations()) {
        logger().info("File associations registered");
        m_renderer->showMessageBox("设为默认播放器",
            "已注册 VideoPlay 为支持的媒体格式的默认播放器候选。\n"
            "如果双击媒体文件仍未用 VideoPlay 打开，\n"
            "请在打开的「默认应用」设置中选择 VideoPlay。");
        openDefaultAppsSettings();
    } else {
        logger().error("Failed to register file associations");
        m_renderer->showMessageBox("设为默认播放器",
            "文件关联注册失败，详见日志。", true);
    }
}

void VideoPlayerApp::removeFileAssociations() {
    if (!m_renderer) return;

    if (unregisterFileAssociations()) {
        logger().info("File associations removed");
        m_renderer->showOSD(OSDType::Info, "已取消文件关联");
    } else {
        logger().error("Failed to unregister file associations");
        m_renderer->showMessageBox("取消文件关联",
            "部分文件关联移除失败，详见日志。", true);
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
        if (m_renderer) {
            m_renderer->showOSD(OSDType::Info, "已加载字幕", -1.0f);
        }
        if (m_searchEngine) {
            m_searchEngine->addSubtitleEntries(m_subtitleParser->entries());
        }
    } else {
        logger().error("Failed to load subtitle: " + path);
        m_currentSubtitle.clear();
        if (m_renderer) {
            m_renderer->showMessageBox("加载字幕失败", "无法解析字幕文件:\n" + path, true);
        }
    }
}

void VideoPlayerApp::showHelp() {
    if (!m_renderer) return;

    // 行模式：{标签, 说明}；标签非空且值为空的行是小节标题
    InfoPanelContent content;
    content.title = "帮助";
    auto section = [&content](const char* name) {
        content.rows.push_back({name, ""});
    };
    auto row = [&content](const char* key, const char* desc) {
        content.rows.push_back({key, desc});
    };

    section("播放控制");
    row("空格", "播放/暂停");
    row("S", "停止");
    row("← / →", "后退/前进 5秒");
    row("Shift+← / Shift+→", "后退/前进 30秒");
    row("Ctrl+← / Ctrl+→", "上一帧/下一帧（逐帧步进）");
    row(",", "上一帧");
    row("Ctrl+G", "跳转到时间");
    row(".", "切换播放速度");
    row("F", "全屏切换");
    row("N", "下一个（优先下一集）");
    row("P", "上一个（优先上一集）");
    row("L", "循环模式切换");
    row("[", "AB循环: 设置A点");
    row("]", "AB循环: 设置B点");
    row("\\", "AB循环: 清除");
    row("Ctrl+B", "添加书签");
    row("Shift+B", "清除书签");

    section("音量与音频");
    row("↑ / ↓", "音量增加/减少");
    row("M", "静音切换");
    row("Shift+G / Shift+H", "音频提前/延后 0.5秒");
    row("播放菜单", "切换音频滤镜预设");

    section("画面");
    row("A", "画面比例切换");
    row("播放菜单", "切换硬件解码");
    row("C / F6", "画面调节面板");
    row("Ctrl+↑ / Ctrl+↓", "亮度 +/- 5%");
    row("Ctrl+Shift+↑ / Ctrl+Shift+↓", "对比度 +/- 10%");
    row("Ctrl+0", "重置画面参数");

    section("字幕");
    row("G / H", "字幕提前/延后 0.5秒");

    section("列表与剧集");
    row("Ctrl+Shift+Right", "下一集");
    row("Ctrl+Shift+Left", "上一集");
    row("Ctrl+E", "切换选集面板");
    row("Ctrl+L", "切换播放列表");
    row("Delete", "删除选中的播放列表项");

    section("其它");
    row("T", "窗口置顶");
    row("Tab", "媒体信息");
    row("I", "显隐右上角同步/统计信息");
    row("F12", "截图");
    row("Shift+F12", "复制截图到剪贴板");
    row("Ctrl+F", "搜索内容");
    row("Ctrl+O", "打开文件");
    row("Ctrl+U", "打开 URL");
    row("F1", "显示帮助");
    row("Esc", "退出全屏/关闭菜单");

    section("鼠标操作");
    row("左键", "点击控制栏按钮");
    row("双击", "视频画面切换全屏");
    row("拖动", "进度条跳转");
    row("滚轮", "调节音量");
    row("拖放", "拖放文件到窗口播放");

    m_renderer->showInfoPanel(std::move(content));
}

void VideoPlayerApp::showAbout() {
    if (!m_renderer) {
        return;
    }

    int sdlVersion = SDL_GetVersion();
    std::string sdlVersionStr = std::to_string(SDL_VERSIONNUM_MAJOR(sdlVersion)) + "." +
                                std::to_string(SDL_VERSIONNUM_MINOR(sdlVersion)) + "." +
                                std::to_string(SDL_VERSIONNUM_MICRO(sdlVersion));

    // 窗口最小宽 480 时面板宽 400、值列约 256px，各值需单行放得下
    InfoPanelContent content;
    content.title = "关于";
    content.rows.push_back({"版本", "VideoPlay v" APP_VERSION});
    content.rows.push_back({"简介", "基于 FFmpeg + SDL3 的视频播放器"});
    content.rows.push_back({"FFmpeg", FFmpegPlayer::ffmpegVersion()});
    content.rows.push_back({"SDL", sdlVersionStr});
    content.rows.push_back({"功能", "多格式解码、硬件解码"});
    content.rows.push_back({"", "变速 0.25x–4x（保持音高）"});
    content.rows.push_back({"", "字幕 SRT/ASS/VTT/PGS"});
    content.rows.push_back({"", "播放列表与剧集连播"});
    content.rows.push_back({"", "AI 视频摘要、章节划分与内容搜索"});
    content.rows.push_back({"许可证", "GPLv3"});

    m_renderer->showInfoPanel(std::move(content));
}

void VideoPlayerApp::startAIAnalysis() {
    if (m_currentFile.empty()) {
        if (m_renderer) {
            m_renderer->showOSD(OSDType::Info, "请先打开一个视频文件");
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
            m_renderer->showOSD(OSDType::Info, "请先打开一个视频文件");
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
            m_renderer->showOSD(OSDType::Info,
                "当前视频尚未进行 AI 分析，请先执行「AI 分析当前视频」");
        }
        return;
    }

    // 面板标题已是「AI 摘要」，正文不再重复标题行
    std::string summaryText = m_aiResult.summary + "\n\n自动生成的章节：\n";
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
            m_renderer->showOSD(OSDType::Info, "请先打开一个视频文件");
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
            m_renderer->showOSD(OSDType::Info, "请先打开一个视频文件");
        }
        return;
    }

    m_aiAnalyzer->clearCache(m_currentFile);
    m_aiResult = AIAnalysisResult();
    if (m_searchEngine) {
        m_searchEngine->clearIndex();
    }

    if (m_renderer) {
        m_renderer->showOSD(OSDType::Info, "AI 缓存已清除");
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

    SettingsDialog dialog(m_renderer->getWindow(), m_renderer->getFont(),
                          m_renderer->getTitleFont());
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

void VideoPlayerApp::showSubtitleStyleDialog() {
    if (!m_renderer) return;

    SubtitleStyleDialog dialog(m_renderer->getWindow(), m_renderer->getFont(),
                               m_renderer->getTitleFont());
    dialog.show(Settings::instance().subtitleStyle(), [this](const SubtitleStyle& style) {
        Settings::instance().setSubtitleStyle(style);
        Settings::instance().save();
        if (m_renderer) {
            m_renderer->setSubtitleStyle(style);
            m_renderer->showOSD(OSDType::Info, "字幕样式已保存");
        }
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
