#include "renderer/menu_manager.h"
#include "renderer/sdlrenderer.h"
#include "renderer/sdlrenderer_internal.h"
#include "core/settings.h"
#include "core/common.h"
#include "utils/logger.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

#define NOMINMAX

namespace VideoPlay {

namespace {

uint8_t scaledAlpha(int base, float alpha) {
    int value = static_cast<int>(base * std::clamp(alpha, 0.0f, 1.0f));
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

} // namespace

MenuManager::MenuManager(SDLRenderer* renderer) : m_renderer(renderer) {
}

void MenuManager::initMenus() {
    m_topMenus.clear();

    // 1. 文件菜单
    auto fileMenu = MenuItem::create("文件");
    fileMenu->addChild(MenuItem::create(MenuId::OpenFile, "打开文件...", nullptr, "Ctrl+O"));
    fileMenu->addChild(MenuItem::create(MenuId::OpenFolder, "打开文件夹.."));
    fileMenu->addChild(MenuItem::create(MenuId::RecentFiles, "最近播放"));
    fileMenu->addChild(MenuItem::separator());
    fileMenu->addChild(MenuItem::create(MenuId::ImportSubtitle, "导入字幕..."));
    fileMenu->addChild(MenuItem::create(MenuId::MediaInfo, "视频信息", [this]{
        if (m_renderer) m_renderer->toggleMediaInfoPanel();
    }));
    fileMenu->addChild(MenuItem::separator());
    fileMenu->addChild(MenuItem::create(MenuId::Exit, "退出", nullptr, "Alt+F4"));
    m_topMenus.push_back(fileMenu);

    // 2. 播放菜单
    auto playMenu = MenuItem::create("播放");
    playMenu->addChild(MenuItem::create(MenuId::PlayPause, "播放/暂停", nullptr, "Space"));
    playMenu->addChild(MenuItem::create(MenuId::Stop, "停止", nullptr, "S"));
    playMenu->addChild(MenuItem::separator());
    playMenu->addChild(MenuItem::create(MenuId::Prev, "上一个", nullptr, "P"));
    playMenu->addChild(MenuItem::create(MenuId::Next, "下一个", nullptr, "N"));
    playMenu->addChild(MenuItem::separator());
    playMenu->addChild(MenuItem::create(MenuId::PrevEpisode, "上一集", nullptr, "Ctrl+Shift+Left"));
    playMenu->addChild(MenuItem::create(MenuId::NextEpisode, "下一集", nullptr, "Ctrl+Shift+Right"));
    playMenu->addChild(MenuItem::separator());
    playMenu->addChild(MenuItem::create(MenuId::Playlist, "播放列表", nullptr, "Ctrl+L"));
    playMenu->addChild(MenuItem::create(MenuId::PlaylistClear, "清空播放列表"));
    playMenu->addChild(MenuItem::separator());
    playMenu->addChild(MenuItem::create(MenuId::SpeedUp, "增加速度"));
    playMenu->addChild(MenuItem::create(MenuId::SpeedDown, "降低速度"));
    playMenu->addChild(MenuItem::separator());

    // 循环模式子菜单
    auto loopSub = MenuItem::create(MenuId::Loop, "循环模式");
    loopSub->addChild(MenuItem::create(MenuId::LoopNone, "不循环")
        ->setChecked([this]{ return m_renderer && m_renderer->m_loopMode == 0; }));
    loopSub->addChild(MenuItem::create(MenuId::LoopSingle, "单曲循环")
        ->setChecked([this]{ return m_renderer && m_renderer->m_loopMode == 1; }));
    loopSub->addChild(MenuItem::create(MenuId::LoopPlaylist, "列表循环")
        ->setChecked([this]{ return m_renderer && m_renderer->m_loopMode == 2; }));
    playMenu->addChild(loopSub);

    // 音频滤镜子菜单
    auto audioFilterSub = MenuItem::create(MenuId::AudioFilter, "音频滤镜");
    audioFilterSub->addChild(MenuItem::create(MenuId::AudioFilterOff, "关闭")
        ->setChecked([this]{ return m_renderer && m_renderer->m_audioFilterPreset == AudioFilterPreset::Off; }));
    audioFilterSub->addChild(MenuItem::create(MenuId::AudioFilterVoice, "语音增强")
        ->setChecked([this]{ return m_renderer && m_renderer->m_audioFilterPreset == AudioFilterPreset::Voice; }));
    audioFilterSub->addChild(MenuItem::create(MenuId::AudioFilterBass, "低音增强")
        ->setChecked([this]{ return m_renderer && m_renderer->m_audioFilterPreset == AudioFilterPreset::Bass; }));
    audioFilterSub->addChild(MenuItem::create(MenuId::AudioFilterNight, "夜间模式")
        ->setChecked([this]{ return m_renderer && m_renderer->m_audioFilterPreset == AudioFilterPreset::Night; }));
    playMenu->addChild(audioFilterSub);

    // 音频输出子菜单（动态构建）
    auto audioOutSub = MenuItem::create(MenuId::AudioOutput, "音频输出");
    playMenu->addChild(audioOutSub);

    m_topMenus.push_back(playMenu);

    // 3. 画面菜单
    auto videoMenu = MenuItem::create("画面");
    videoMenu->addChild(MenuItem::create(MenuId::VideoFilterPanel, "画面调节面板...", nullptr, "C"));
    videoMenu->addChild(MenuItem::separator());

    // 画面比例子菜单
    auto aspectSub = MenuItem::create(MenuId::Aspect, "画面比例");
    aspectSub->addChild(MenuItem::create(MenuId::AspectOriginal, "原始")
        ->setChecked([this]{ return m_renderer && m_renderer->m_aspectMode == AspectMode::Original; }));
    aspectSub->addChild(MenuItem::create(MenuId::Aspect16_9, "16:9")
        ->setChecked([this]{ return m_renderer && m_renderer->m_aspectMode == AspectMode::R16_9; }));
    aspectSub->addChild(MenuItem::create(MenuId::Aspect4_3, "4:3")
        ->setChecked([this]{ return m_renderer && m_renderer->m_aspectMode == AspectMode::R4_3; }));
    aspectSub->addChild(MenuItem::create(MenuId::AspectFill, "铺满")
        ->setChecked([this]{ return m_renderer && m_renderer->m_aspectMode == AspectMode::FillWindow; }));
    videoMenu->addChild(aspectSub);

    // 画面变换子菜单
    auto transformSub = MenuItem::create(MenuId::VideoTransform, "画面变换");
    transformSub->addChild(MenuItem::create(MenuId::Rotate0, "旋转 0°")
        ->setChecked([this]{ return m_renderer && m_renderer->m_videoTransform.rotation == 0; }));
    transformSub->addChild(MenuItem::create(MenuId::Rotate90, "旋转 90°")
        ->setChecked([this]{ return m_renderer && m_renderer->m_videoTransform.rotation == 90; }));
    transformSub->addChild(MenuItem::create(MenuId::Rotate180, "旋转 180°")
        ->setChecked([this]{ return m_renderer && m_renderer->m_videoTransform.rotation == 180; }));
    transformSub->addChild(MenuItem::create(MenuId::Rotate270, "旋转 270°")
        ->setChecked([this]{ return m_renderer && m_renderer->m_videoTransform.rotation == 270; }));
    transformSub->addChild(MenuItem::create(MenuId::FlipHorizontal, "水平翻转")
        ->setChecked([this]{ return m_renderer && m_renderer->m_videoTransform.flipHorizontal; }));
    transformSub->addChild(MenuItem::create(MenuId::FlipVertical, "垂直翻转")
        ->setChecked([this]{ return m_renderer && m_renderer->m_videoTransform.flipVertical; }));
    transformSub->addChild(MenuItem::create(MenuId::CropOff, "裁剪关闭")
        ->setChecked([this]{ return m_renderer && m_renderer->m_videoTransform.cropPercent == 0; }));
    transformSub->addChild(MenuItem::create(MenuId::Crop10, "裁剪 10%")
        ->setChecked([this]{ return m_renderer && m_renderer->m_videoTransform.cropPercent == 10; }));
    transformSub->addChild(MenuItem::create(MenuId::Crop20, "裁剪 20%")
        ->setChecked([this]{ return m_renderer && m_renderer->m_videoTransform.cropPercent == 20; }));
    transformSub->addChild(MenuItem::create(MenuId::TransformReset, "重置变换"));
    videoMenu->addChild(transformSub);

    // 画面参数子菜单
    auto filterSub = MenuItem::create(MenuId::VideoFilter, "画面参数");
    filterSub->addChild(MenuItem::createDynamic(0, [this]{
        if (!m_renderer) return std::string("参数仪表");
        int bPct = static_cast<int>(std::round(m_renderer->m_videoFilterConfig.brightness * 100.0f));
        int cPct = static_cast<int>(std::round(m_renderer->m_videoFilterConfig.contrast * 100.0f));
        int sPct = static_cast<int>(std::round(m_renderer->m_videoFilterConfig.saturation * 100.0f));
        std::string bSign = bPct > 0 ? "+" : "";
        return "亮 " + bSign + std::to_string(bPct) + "% · 对 " + std::to_string(cPct) + "% · 饱 " + std::to_string(sPct) + "%";
    }));
    filterSub->addChild(MenuItem::create(MenuId::VideoFilterBrightnessUp, "亮度 +5%", nullptr, "Ctrl+Up"));
    filterSub->addChild(MenuItem::create(MenuId::VideoFilterBrightnessDown, "亮度 -5%", nullptr, "Ctrl+Down"));
    filterSub->addChild(MenuItem::create(MenuId::VideoFilterContrastUp, "对比度 +10%", nullptr, "Ctrl+Shift+Up"));
    filterSub->addChild(MenuItem::create(MenuId::VideoFilterContrastDown, "对比度 -10%", nullptr, "Ctrl+Shift+Down"));
    filterSub->addChild(MenuItem::create(MenuId::VideoFilterSaturationUp, "饱和度 +10%"));
    filterSub->addChild(MenuItem::create(MenuId::VideoFilterSaturationDown, "饱和度 -10%"));
    filterSub->addChild(MenuItem::create(MenuId::VideoFilterReset, "重置默认参数", nullptr, "Ctrl+0"));
    videoMenu->addChild(filterSub);

    // HDR 色调映射子菜单（包含模式选择 + 三级算法入口）
    auto hdrSub = MenuItem::createDynamic(MenuId::HdrToneMap, [this]{
        if (!m_renderer) return std::string("HDR 色调映射");
        if (m_renderer->m_hdrToneMapMode == HdrToneMapMode::Off) return std::string("HDR 色调映射（关闭）");
        if (m_renderer->m_hdrToneMapMode == HdrToneMapMode::On) return std::string("HDR 色调映射（开启）");
        return std::string("HDR 色调映射（自动）");
    });
    hdrSub->addChild(MenuItem::create(MenuId::HdrToneMapAuto, "自动 (推荐)")
        ->setChecked([this]{ return m_renderer && m_renderer->m_hdrToneMapMode == HdrToneMapMode::Auto; }));
    hdrSub->addChild(MenuItem::create(MenuId::HdrToneMapOn, "始终开启")
        ->setChecked([this]{ return m_renderer && m_renderer->m_hdrToneMapMode == HdrToneMapMode::On; }));
    hdrSub->addChild(MenuItem::create(MenuId::HdrToneMapOff, "关闭")
        ->setChecked([this]{ return m_renderer && m_renderer->m_hdrToneMapMode == HdrToneMapMode::Off; }));
    hdrSub->addChild(MenuItem::separator());

    // 三级：调色算法选择
    auto algoSub = MenuItem::createDynamic(MenuId::HdrAlgorithmMenu, [this]{
        if (!m_renderer) return std::string("调色算法");
        return std::string("调色算法（") + hdrToneMapAlgorithmName(m_renderer->m_hdrToneMapAlgorithm) + "）";
    });
    algoSub->addChild(MenuItem::create(MenuId::HdrAlgoHable, "Hable (推荐)")
        ->setChecked([this]{ return m_renderer && m_renderer->m_hdrToneMapAlgorithm == HdrToneMapAlgorithm::Hable; }));
    algoSub->addChild(MenuItem::create(MenuId::HdrAlgoMobius, "Mobius")
        ->setChecked([this]{ return m_renderer && m_renderer->m_hdrToneMapAlgorithm == HdrToneMapAlgorithm::Mobius; }));
    algoSub->addChild(MenuItem::create(MenuId::HdrAlgoReinhard, "Reinhard")
        ->setChecked([this]{ return m_renderer && m_renderer->m_hdrToneMapAlgorithm == HdrToneMapAlgorithm::Reinhard; }));
    hdrSub->addChild(algoSub);

    videoMenu->addChild(hdrSub);

    // 去隔行子菜单
    auto deintSub = MenuItem::createDynamic(MenuId::Deinterlace, [this]{
        if (!m_renderer) return std::string("去隔行");
        return std::string("去隔行（") + deinterlaceModeName(m_renderer->m_deinterlaceMode) + "）";
    });
    deintSub->addChild(MenuItem::create(MenuId::DeinterlaceOff, "关闭")
        ->setChecked([this]{ return m_renderer && m_renderer->m_deinterlaceMode == DeinterlaceMode::Off; }));
    deintSub->addChild(MenuItem::create(MenuId::DeinterlaceAuto, "自动")
        ->setChecked([this]{ return m_renderer && m_renderer->m_deinterlaceMode == DeinterlaceMode::Auto; }));
    deintSub->addChild(MenuItem::create(MenuId::DeinterlaceYadif, "线性")
        ->setChecked([this]{ return m_renderer && m_renderer->m_deinterlaceMode == DeinterlaceMode::Yadif; }));
    deintSub->addChild(MenuItem::create(MenuId::DeinterlaceBwdif, "Bob")
        ->setChecked([this]{ return m_renderer && m_renderer->m_deinterlaceMode == DeinterlaceMode::Bwdif; }));
    videoMenu->addChild(deintSub);

    videoMenu->addChild(MenuItem::separator());
    videoMenu->addChild(MenuItem::create(MenuId::HardwareDecoding, "硬件解码")
        ->setChecked([this]{ return m_renderer && m_renderer->m_hardwareDecodingEnabled; }));
    videoMenu->addChild(MenuItem::separator());
    videoMenu->addChild(MenuItem::create(MenuId::Screenshot, "截图", nullptr, "F12"));
    videoMenu->addChild(MenuItem::create(MenuId::AlwaysOnTop, "始终置顶", nullptr, "T")
        ->setChecked([this]{ return m_renderer && m_renderer->m_alwaysOnTop; }));
    videoMenu->addChild(MenuItem::create(MenuId::Fullscreen, "全屏", nullptr, "F")
        ->setChecked([this]{ return m_renderer && m_renderer->m_fullscreen; }));
    m_topMenus.push_back(videoMenu);

    // 4. 音轨菜单
    auto audioMenu = MenuItem::create("音轨");
    audioMenu->addChild(MenuItem::create(400, "无可用音轨")->setEnabled([]{ return false; }));
    m_topMenus.push_back(audioMenu);

    // 5. 字幕菜单
    auto subtitleMenu = MenuItem::create("字幕");
    subtitleMenu->addChild(MenuItem::create(450, "无可用字幕")->setEnabled([]{ return false; }));
    m_topMenus.push_back(subtitleMenu);

    // 6. 章节菜单
    auto chapterMenu = MenuItem::create("章节");
    chapterMenu->addChild(MenuItem::create(200, "无可用章节")->setEnabled([]{ return false; }));
    m_topMenus.push_back(chapterMenu);

    // 7. 剧集菜单
    auto episodeMenu = MenuItem::create("剧集");
    episodeMenu->addChild(MenuItem::create(30, "上一集", nullptr, "Ctrl+Shift+Left"));
    episodeMenu->addChild(MenuItem::create(31, "下一集", nullptr, "Ctrl+Shift+Right"));
    episodeMenu->addChild(MenuItem::separator());
    episodeMenu->addChild(MenuItem::create(32, "切换选集面板", nullptr, "Ctrl+E"));
    m_topMenus.push_back(episodeMenu);

    // 8. AI 菜单
    auto aiMenu = MenuItem::create("AI");
    aiMenu->addChild(MenuItem::create(300, "AI 分析当前视频"));
    aiMenu->addChild(MenuItem::create(301, "显示摘要"));
    aiMenu->addChild(MenuItem::separator());
    aiMenu->addChild(MenuItem::create(302, "搜索内容", nullptr, "Ctrl+F"));
    aiMenu->addChild(MenuItem::separator());
    aiMenu->addChild(MenuItem::create(304, "AI 设置..."));
    aiMenu->addChild(MenuItem::create(303, "清除 AI 缓存"));
    m_topMenus.push_back(aiMenu);

    // 9. 帮助菜单
    auto helpMenu = MenuItem::create("帮助");
    helpMenu->addChild(MenuItem::create(50, "快捷键", nullptr, "F1"));
    helpMenu->addChild(MenuItem::create(51, "关于"));
    m_topMenus.push_back(helpMenu);

    // 初始化右键上下文菜单模板
    m_playbackContextMenu = MenuItem::create("Context");
    m_playbackContextMenu->addChild(MenuItem::create(MenuId::PlayPause, "播放/暂停", nullptr, "Space"));
    m_playbackContextMenu->addChild(MenuItem::create(MenuId::Stop, "停止", nullptr, "S"));
    m_playbackContextMenu->addChild(MenuItem::separator());
    m_playbackContextMenu->addChild(MenuItem::create(MenuId::OpenFile, "打开文件...", nullptr, "Ctrl+O"));
    m_playbackContextMenu->addChild(MenuItem::create(MenuId::ImportSubtitle, "导入字幕..."));
    m_playbackContextMenu->addChild(MenuItem::separator());
    m_playbackContextMenu->addChild(MenuItem::create(MenuId::VideoFilterPanel, "画面调节面板...", nullptr, "C"));
    m_playbackContextMenu->addChild(aspectSub);
    m_playbackContextMenu->addChild(hdrSub);
    m_playbackContextMenu->addChild(MenuItem::separator());
    m_playbackContextMenu->addChild(MenuItem::create(MenuId::SpeedUp, "增加速度"));
    m_playbackContextMenu->addChild(MenuItem::create(MenuId::SpeedDown, "降低速度"));
    m_playbackContextMenu->addChild(MenuItem::create(MenuId::Playlist, "播放列表", nullptr, "Ctrl+L"));
    m_playbackContextMenu->addChild(MenuItem::separator());
    m_playbackContextMenu->addChild(MenuItem::create(MenuId::AIAnalyze, "AI 分析当前视频"));
    m_playbackContextMenu->addChild(MenuItem::create(MenuId::Search, "搜索内容", nullptr, "Ctrl+F"));
    m_playbackContextMenu->addChild(MenuItem::separator());
    m_playbackContextMenu->addChild(MenuItem::create(MenuId::Screenshot, "截图", nullptr, "F12"));
    m_playbackContextMenu->addChild(MenuItem::create(MenuId::AlwaysOnTop, "始终置顶", nullptr, "T")
        ->setChecked([this]{ return m_renderer && m_renderer->m_alwaysOnTop; }));
    m_playbackContextMenu->addChild(MenuItem::create(MenuId::Fullscreen, "全屏", nullptr, "F")
        ->setChecked([this]{ return m_renderer && m_renderer->m_fullscreen; }));

    m_playlistContextMenu = MenuItem::create("PlaylistContext");
    m_playlistContextMenu->addChild(MenuItem::create(25, "播放此项"));
    m_playlistContextMenu->addChild(MenuItem::create(23, "从列表删除", nullptr, "Delete"));
    m_playlistContextMenu->addChild(MenuItem::separator());
    m_playlistContextMenu->addChild(MenuItem::create(24, "清空播放列表"));

    m_activeContextMenu = m_playbackContextMenu;
}

void MenuManager::updateTrackMenus() {
    auto findTop = [this](const std::string& label) -> MenuItemPtr {
        for (auto& top : m_topMenus) {
            if (top->getLabel() == label) return top;
        }
        return nullptr;
    };

    auto audioMenu = findTop("音轨");
    if (audioMenu && m_renderer) {
        audioMenu->children.clear();
        if (m_renderer->m_audioTracks.empty()) {
            audioMenu->addChild(MenuItem::create(400, "无可用音轨")->setEnabled([]{ return false; }));
        } else {
            for (size_t i = 0; i < m_renderer->m_audioTracks.size(); ++i) {
                int trackIdx = static_cast<int>(i);
                std::string label = trackLabel(m_renderer->m_audioTracks[i], trackIdx + 1);
                auto item = MenuItem::create(400 + trackIdx, label);
                item->setChecked([this, trackIdx]{
                    return m_renderer && trackIdx == m_renderer->m_currentAudioTrack;
                });
                audioMenu->addChild(item);
            }
        }
    }

    auto subMenu = findTop("字幕");
    if (subMenu && m_renderer) {
        subMenu->children.clear();
        auto offItem = MenuItem::create(450, "关闭内封字幕");
        offItem->setChecked([this]{
            return m_renderer && m_renderer->m_currentSubtitleTrack == -1;
        });
        subMenu->addChild(offItem);

        for (size_t i = 0; i < m_renderer->m_subtitleTracks.size(); ++i) {
            int trackIdx = static_cast<int>(i);
            std::string label = trackLabel(m_renderer->m_subtitleTracks[i], trackIdx + 1);
            auto item = MenuItem::create(451 + trackIdx, label);
            item->setChecked([this, trackIdx]{
                return m_renderer && trackIdx == m_renderer->m_currentSubtitleTrack;
            });
            subMenu->addChild(item);
        }
    }
}

void MenuManager::updateChapterMenuItems() {
    if (!m_renderer) return;
    for (auto& top : m_topMenus) {
        if (top->getLabel() == "章节") {
            top->children.clear();
            if (m_renderer->m_chapters.empty()) {
                top->addChild(MenuItem::create(200, "无可用章节")->setEnabled([]{ return false; }));
            } else {
                for (size_t i = 0; i < m_renderer->m_chapters.size() && i < 50; ++i) {
                    std::string label = m_renderer->m_chapters[i].title;
                    if (label.empty()) {
                        label = "Chapter " + std::to_string(i + 1);
                    }
                    if (label.length() > 30) {
                        label = label.substr(0, 27) + "...";
                    }
                    top->addChild(MenuItem::create(static_cast<int>(200 + i), label));
                }
            }
            break;
        }
    }
}

void MenuManager::updateRecentFilesMenu() {
    if (m_topMenus.empty()) return;
    auto fileMenu = m_topMenus[0];
    if (!fileMenu) return;

    // 查找最近播放项
    MenuItemPtr recentItem = nullptr;
    for (auto& child : fileMenu->children) {
        if (child->id == static_cast<int>(MenuId::RecentFiles)) {
            recentItem = child;
            break;
        }
    }
    if (!recentItem) return;

    recentItem->children.clear();
    auto recentFiles = Settings::instance().recentFiles();
    if (recentFiles.empty()) {
        recentItem->addChild(MenuItem::create(100, "无最近记录")->setEnabled([]{ return false; }));
    } else {
        for (size_t i = 0; i < recentFiles.size() && i < 10; ++i) {
            std::string path = recentFiles[i];
            std::string filename = std::filesystem::u8path(path).filename().u8string();
            if (filename.length() > 35) {
                filename = filename.substr(0, 32) + "...";
            }
            recentItem->addChild(MenuItem::create(static_cast<int>(100 + i), filename));
        }
    }
}

bool MenuManager::isMenuOpen() const {
    return (m_activeTopMenu >= 0 || m_showContextMenu || !m_cascadeStack.empty());
}

bool MenuManager::isTopMenuVisible(size_t index) const {
    if (index >= m_topMenus.size() || !m_renderer) return false;
    const std::string& label = m_topMenus[index]->getLabel();
    if (label == "章节") return m_renderer->m_hasChapters;
    if (label == "音轨") return !m_renderer->m_audioTracks.empty();
    if (label == "字幕") return !m_renderer->m_subtitleTracks.empty();
    return !label.empty();
}

bool MenuManager::isMouseOverMenu(int x, int y) const {
    if (!isMenuOpen()) return false;
    for (const auto& level : m_cascadeStack) {
        if (x >= level.x && x <= level.x + level.width &&
            y >= level.y && y <= level.y + level.height) {
            return true;
        }
    }
    if (m_activeTopMenu >= 0 && m_renderer && y >= 0 && y <= m_renderer->m_menuBarHeight) {
        return true;
    }
    return false;
}

int MenuManager::getActiveSubmenuId() const {
    if (m_cascadeStack.size() > 1 && m_cascadeStack.back().parent) {
        return m_cascadeStack.back().parent->id;
    }
    return 0;
}

void MenuManager::closeAllMenus(bool animate) {
    if (m_activeTopMenu < 0 && !m_showContextMenu && m_menuAnimAlpha <= 0.0f) return;
    if (!animate) {
        m_activeTopMenu = -1;
        m_pendingTopMenu = -1;
        m_showContextMenu = false;
        m_cascadeStack.clear();
        m_menuAnimAlpha = 0.0f;
        m_menuAnimating = false;
        return;
    }
    m_pendingTopMenu = -1;
    m_menuAnimStartTime = SDL_GetTicks();
    m_menuAnimating = true;
}

void MenuManager::updateMenuAnimation() {
    if (!m_menuAnimating) {
        m_menuAnimAlpha = (m_activeTopMenu >= 0 || m_showContextMenu) ? 1.0f : 0.0f;
        return;
    }

    uint64_t elapsed = SDL_GetTicks() - m_menuAnimStartTime;
    float t = std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(MENU_ANIM_DURATION_MS));

    if (m_pendingTopMenu >= 0) {
        float inv = 1.0f - t;
        m_menuAnimAlpha = 1.0f - inv * inv * inv;
        if (t >= 1.0f) {
            m_menuAnimating = false;
            m_menuAnimAlpha = 1.0f;
        }
    } else {
        float ease = t * t;
        m_menuAnimAlpha = 1.0f - ease;
        if (t >= 1.0f) {
            m_menuAnimating = false;
            m_menuAnimAlpha = 0.0f;
            m_activeTopMenu = -1;
            m_showContextMenu = false;
            m_cascadeStack.clear();
        }
    }
}

int MenuManager::calculateMenuWidth(const MenuItemPtr& parent) {
    if (!parent || parent->children.empty() || !m_renderer) return 180;
    int labelMaxW = 0;
    int shortcutMaxW = 0;
    bool hasChildArrow = false;
    constexpr int labelFontSize = 14;
    constexpr int shortcutFontSize = 12;

    for (const auto& child : parent->children) {
        if (!child || child->isSeparator) continue;
        int lw = m_renderer->getTextWidth(child->getLabel(), labelFontSize);
        if (lw > labelMaxW) labelMaxW = lw;

        if (!child->shortcut.empty()) {
            int sw = m_renderer->getTextWidth(child->shortcut, shortcutFontSize);
            if (sw > shortcutMaxW) shortcutMaxW = sw;
        }
        if (child->hasChildren()) {
            hasChildArrow = true;
        }
    }

    constexpr int leftPadding = 16;
    constexpr int checkColumnWidth = 20;
    constexpr int rightPadding = 18;
    constexpr int shortcutGap = 24;
    int width = leftPadding + checkColumnWidth + labelMaxW + rightPadding;
    if (shortcutMaxW > 0) {
        width += shortcutGap + shortcutMaxW;
    }
    if (hasChildArrow) {
        width += 24;
    }
    return std::max(width, 180);
}

void MenuManager::renderCascadeLevel(const MenuCascadeLevel& level, float alpha) {
    if (!level.parent || !m_renderer) return;

    const int itemHeight = 30;
    const int separatorHeight = 12;
    const int menuPadY = 5;
    const int leftPadding = 16;
    const int checkColumnWidth = 20;
    const int labelFontSize = 14;
    const int shortcutFontSize = 12;
    uint8_t baseAlpha = scaledAlpha(244, alpha);

    // 菜单阴影和背景
    m_renderer->fillRoundRect(level.x + 2, level.y + 5, level.width, level.height, 8,
                              0, 0, 0, scaledAlpha(95, alpha));
    m_renderer->fillRoundRect(level.x, level.y, level.width, level.height, 8,
                              38, 39, 43, baseAlpha);
    m_renderer->drawRect(level.x + 1, level.y + 1, level.width - 2, level.height - 2,
                         255, 255, 255, scaledAlpha(26, alpha));

    int itemY = level.y + menuPadY;
    for (size_t i = 0; i < level.parent->children.size(); ++i) {
        const auto& child = level.parent->children[i];
        if (!child) continue;

        if (child->isSeparator) {
            m_renderer->fillRect(level.x + 12, itemY + separatorHeight / 2,
                                 level.width - 24, 1, 255, 255, 255, scaledAlpha(35, alpha));
            itemY += separatorHeight;
            continue;
        }

        bool hovered = (static_cast<int>(i) == level.hoveredIndex);
        bool enabled = child->isEnabled();

        if (hovered && enabled) {
            m_renderer->fillRoundRect(level.x + 5, itemY + 1, level.width - 10, itemHeight - 2, 5,
                                      48, 96, 132, scaledAlpha(215, alpha));
            m_renderer->fillRect(level.x + 8, itemY + 6, 2, itemHeight - 12,
                                 COLOR_PROGRESS_FILL[0], COLOR_PROGRESS_FILL[1], COLOR_PROGRESS_FILL[2], scaledAlpha(230, alpha));
        }

        int checkX = level.x + leftPadding;
        int labelX = checkX + checkColumnWidth;
        int labelY = itemY + (itemHeight - m_renderer->getFontHeight(labelFontSize)) / 2;

        if (child->isChecked()) {
            m_renderer->drawText("\xE2\x9C\x93", checkX, labelY,
                                 COLOR_PROGRESS_FILL[0], COLOR_PROGRESS_FILL[1], COLOR_PROGRESS_FILL[2],
                                 labelFontSize, scaledAlpha(255, alpha));
        }

        if (enabled) {
            m_renderer->drawText(child->getLabel(), labelX, labelY,
                                 COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2],
                                 labelFontSize, scaledAlpha(255, alpha));
        } else {
            m_renderer->drawText(child->getLabel(), labelX, labelY,
                                 130, 130, 135,
                                 labelFontSize, scaledAlpha(180, alpha));
        }

        if (child->hasChildren()) {
            int arrowX = level.x + level.width - 16;
            m_renderer->drawText(">", arrowX, labelY,
                                 160, 160, 160, 12, scaledAlpha(200, alpha));
        } else if (!child->shortcut.empty()) {
            int sw = m_renderer->getTextWidth(child->shortcut, shortcutFontSize);
            int shortcutX = level.x + level.width - 14 - sw;
            m_renderer->drawText(child->shortcut, shortcutX, labelY,
                                 160, 160, 160, shortcutFontSize, scaledAlpha(200, alpha));
        }

        itemY += itemHeight;
    }
}

void MenuManager::renderMenuBar() {
    if (!m_renderer) return;

    // 绘制菜单栏底色
    m_renderer->fillRect(0, 0, m_renderer->m_windowWidth, m_renderer->m_menuBarHeight, 34, 35, 39, 245);
    m_renderer->fillRect(0, 0, m_renderer->m_windowWidth, 1, 255, 255, 255, 18);

    // 绘制顶级菜单项
    int x = kTopMenuX;
    for (size_t i = 0; i < m_topMenus.size(); ++i) {
        if (!isTopMenuVisible(i)) continue;
        const auto& item = m_topMenus[i];
        int textW = m_renderer->getTextWidth(item->getLabel());
        int itemWidth = textW + kTopMenuPaddingX * 2;
        int itemHeight = m_renderer->m_menuBarHeight;

        bool active = (m_activeTopMenu == static_cast<int>(i));
        bool hovered = (!active && m_renderer->m_mouseX >= x && m_renderer->m_mouseX <= x + itemWidth &&
                        m_renderer->m_mouseY >= 0 && m_renderer->m_mouseY <= itemHeight);

        if (active) {
            m_renderer->fillRoundRect(x, 2, itemWidth, itemHeight - 4, 4,
                                      48, 96, 132, 230);
        } else if (hovered) {
            m_renderer->fillRoundRect(x, 2, itemWidth, itemHeight - 4, 4,
                                      255, 255, 255, 30);
        }

        int textY = (itemHeight - m_renderer->getFontHeight(14)) / 2;
        m_renderer->drawText(item->getLabel(), x + kTopMenuPaddingX, textY,
                             COLOR_TEXT[0], COLOR_TEXT[1], COLOR_TEXT[2], 14, 255);

        x += itemWidth + kTopMenuGap;
    }

    // 无边框模式下绘制系统按钮（最小化、最大化/还原、关闭）
    if (m_renderer->m_borderless) {
        const int buttonWidth = 46;
        int startX = m_renderer->m_windowWidth - 3 * buttonWidth;
        const int iconSize = 10;
        int iconY = (m_renderer->m_menuBarHeight - iconSize) / 2;

        // 最小化按钮
        {
            int bx = startX;
            bool hovered = (m_renderer->m_hoveredControl == ControlType::SysMinButton);
            bool pressed = (m_renderer->m_pressedControl == ControlType::SysMinButton);
            const uint8_t* c = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;
            if (pressed) {
                m_renderer->fillRect(bx, 0, buttonWidth, m_renderer->m_menuBarHeight,
                                     COLOR_BUTTON_BG_PRESSED[0], COLOR_BUTTON_BG_PRESSED[1], COLOR_BUTTON_BG_PRESSED[2], COLOR_BUTTON_BG_PRESSED[3]);
            } else if (hovered) {
                m_renderer->fillRect(bx, 0, buttonWidth, m_renderer->m_menuBarHeight,
                                     COLOR_BUTTON_BG_HOVER[0], COLOR_BUTTON_BG_HOVER[1], COLOR_BUTTON_BG_HOVER[2], COLOR_BUTTON_BG_HOVER[3]);
            }
            SDL_SetRenderDrawColor(m_renderer->m_renderer, c[0], c[1], c[2], c[3]);
            int lineY = m_renderer->m_menuBarHeight / 2;
            int lineX = bx + (buttonWidth - iconSize) / 2;
            SDL_RenderLine(m_renderer->m_renderer, static_cast<float>(lineX), static_cast<float>(lineY),
                           static_cast<float>(lineX + iconSize), static_cast<float>(lineY));
            m_renderer->m_controlRects.push_back({bx, 0, buttonWidth, m_renderer->m_menuBarHeight, ControlType::SysMinButton, 0});
        }

        // 最大化/还原按钮
        {
            int bx = startX + buttonWidth;
            bool hovered = (m_renderer->m_hoveredControl == ControlType::SysMaxButton);
            bool pressed = (m_renderer->m_pressedControl == ControlType::SysMaxButton);
            const uint8_t* c = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;
            if (pressed) {
                m_renderer->fillRect(bx, 0, buttonWidth, m_renderer->m_menuBarHeight,
                                     COLOR_BUTTON_BG_PRESSED[0], COLOR_BUTTON_BG_PRESSED[1], COLOR_BUTTON_BG_PRESSED[2], COLOR_BUTTON_BG_PRESSED[3]);
            } else if (hovered) {
                m_renderer->fillRect(bx, 0, buttonWidth, m_renderer->m_menuBarHeight,
                                     COLOR_BUTTON_BG_HOVER[0], COLOR_BUTTON_BG_HOVER[1], COLOR_BUTTON_BG_HOVER[2], COLOR_BUTTON_BG_HOVER[3]);
            }
            bool isMaximized = (SDL_GetWindowFlags(m_renderer->m_window) & SDL_WINDOW_MAXIMIZED);
            if (isMaximized) {
                // 还原：两个错位小方框
                int s = 8;
                int ox = bx + (buttonWidth - iconSize) / 2 + 1;
                int oy = iconY + 2;
                m_renderer->drawRect(ox, oy + 2, s, s, c[0], c[1], c[2], c[3]);
                m_renderer->drawRect(ox + 3, oy - 1, s, s, c[0], c[1], c[2], c[3]);
            } else {
                // 最大化：空心方框
                int s = iconSize;
                int ox = bx + (buttonWidth - s) / 2;
                int oy = iconY;
                m_renderer->drawRect(ox, oy, s, s, c[0], c[1], c[2], c[3]);
            }
            m_renderer->m_controlRects.push_back({bx, 0, buttonWidth, m_renderer->m_menuBarHeight, ControlType::SysMaxButton, 0});
        }

        // 关闭按钮
        {
            int bx = startX + 2 * buttonWidth;
            bool hovered = (m_renderer->m_hoveredControl == ControlType::SysCloseButton);
            bool pressed = (m_renderer->m_pressedControl == ControlType::SysCloseButton);
            const uint8_t* c = hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON;
            if (pressed) {
                m_renderer->fillRect(bx, 0, buttonWidth, m_renderer->m_menuBarHeight,
                                     COLOR_BUTTON_BG_PRESSED[0], COLOR_BUTTON_BG_PRESSED[1], COLOR_BUTTON_BG_PRESSED[2], COLOR_BUTTON_BG_PRESSED[3]);
            } else if (hovered) {
                // 关闭按钮 hover 用红色背景
                m_renderer->fillRect(bx, 0, buttonWidth, m_renderer->m_menuBarHeight, 232, 17, 35, 255);
                c = COLOR_BUTTON_HOVER;
            }
            SDL_SetRenderDrawColor(m_renderer->m_renderer, c[0], c[1], c[2], c[3]);
            int iconX = bx + (buttonWidth - iconSize) / 2;
            SDL_RenderLine(m_renderer->m_renderer, static_cast<float>(iconX), static_cast<float>(iconY),
                           static_cast<float>(iconX + iconSize), static_cast<float>(iconY + iconSize));
            SDL_RenderLine(m_renderer->m_renderer, static_cast<float>(iconX + iconSize), static_cast<float>(iconY),
                           static_cast<float>(iconX), static_cast<float>(iconY + iconSize));
            m_renderer->m_controlRects.push_back({bx, 0, buttonWidth, m_renderer->m_menuBarHeight, ControlType::SysCloseButton, 0});
        }
    }
}

void MenuManager::renderCascadeMenus() {
    if (!m_renderer) return;
    if (m_activeTopMenu >= 0 && m_menuAnimAlpha > 0.01f) {
        for (const auto& level : m_cascadeStack) {
            renderCascadeLevel(level, m_menuAnimAlpha);
        }
    }
}

void MenuManager::renderContextMenu() {
    if (!m_showContextMenu || m_cascadeStack.empty() || !m_renderer) return;
    for (const auto& level : m_cascadeStack) {
        renderCascadeLevel(level, 1.0f);
    }
}

void MenuManager::openTopMenu(int index) {
    if (index < 0 || index >= static_cast<int>(m_topMenus.size()) || !m_renderer) return;
    m_activeTopMenu = index;
    m_showContextMenu = false;
    m_pendingTopMenu = index;
    m_menuAnimStartTime = SDL_GetTicks();
    m_menuAnimating = true;

    // 计算顶级菜单下拉坐标
    int x = kTopMenuX;
    for (int i = 0; i < index; ++i) {
        if (!isTopMenuVisible(static_cast<size_t>(i))) continue;
        x += m_renderer->getTextWidth(m_topMenus[i]->getLabel()) + kTopMenuPaddingX * 2 + kTopMenuGap;
    }

    auto rootMenu = m_topMenus[index];
    int menuW = calculateMenuWidth(rootMenu);
    int itemH = 30;
    int sepH = 12;
    int contentH = 0;
    for (const auto& child : rootMenu->children) {
        contentH += child->isSeparator ? sepH : itemH;
    }
    int menuH = contentH + 10;
    int renderX = x;
    if (renderX + menuW > m_renderer->m_windowWidth) {
        renderX = std::max(0, m_renderer->m_windowWidth - menuW - 8);
    }

    m_cascadeStack.clear();
    m_cascadeStack.push_back({ rootMenu, renderX, m_renderer->m_menuBarHeight, menuW, menuH, -1 });
}

bool MenuManager::updateCascadeHover(int mouseX, int mouseY) {
    if (m_cascadeStack.empty() || !m_renderer) return false;

    const int itemHeight = 30;
    const int separatorHeight = 12;
    const int menuPadY = 5;

    // 从最深层级向外检测
    for (int lvl = static_cast<int>(m_cascadeStack.size()) - 1; lvl >= 0; --lvl) {
        auto& level = m_cascadeStack[lvl];
        if (mouseX >= level.x && mouseX <= level.x + level.width &&
            mouseY >= level.y && mouseY <= level.y + level.height) {

            int scanY = level.y + menuPadY;
            int hoveredRow = -1;
            int hoveredRowY = scanY;

            for (size_t i = 0; i < level.parent->children.size(); ++i) {
                const auto& child = level.parent->children[i];
                int rowH = child->isSeparator ? separatorHeight : itemHeight;
                if (!child->isSeparator && mouseX >= level.x && mouseX <= level.x + level.width &&
                    mouseY >= scanY && mouseY <= scanY + itemHeight) {
                    hoveredRow = static_cast<int>(i);
                    hoveredRowY = scanY;
                    break;
                }
                scanY += rowH;
            }

            // 检查下一级 (lvl + 1) 是否已经由此悬停项展开
            bool nextLevelBelongsToCurrentRow = false;
            if (lvl + 1 < static_cast<int>(m_cascadeStack.size()) && hoveredRow >= 0 &&
                hoveredRow < static_cast<int>(level.parent->children.size())) {
                if (level.parent->children[hoveredRow] == m_cascadeStack[lvl + 1].parent) {
                    nextLevelBelongsToCurrentRow = true;
                }
            }

            if (nextLevelBelongsToCurrentRow) {
                // 当前项正是下一级的展开源，保留下一级展开，仅收起更深层级（lvl + 2 及以后）
                if (m_cascadeStack.size() > static_cast<size_t>(lvl + 2)) {
                    m_cascadeStack.resize(lvl + 2);
                }
                level.hoveredIndex = hoveredRow;
                return true;
            }

            // 如果当前项不是展开下一级的源，或者悬停项发生切换
            if (level.hoveredIndex != hoveredRow) {
                level.hoveredIndex = hoveredRow;
                // 收起当前层级之后的所有更深层级
                m_cascadeStack.resize(lvl + 1);

                if (hoveredRow >= 0 && hoveredRow < static_cast<int>(level.parent->children.size())) {
                    const auto& child = level.parent->children[hoveredRow];
                    if (child && child->hasChildren() && child->isEnabled()) {
                        // 展开下一级子菜单
                        int subW = calculateMenuWidth(child);
                        int subContentH = 0;
                        for (const auto& subChild : child->children) {
                            subContentH += subChild->isSeparator ? separatorHeight : itemHeight;
                        }
                        int subH = subContentH + menuPadY * 2;

                        int subX = level.x + level.width - 4;
                        if (subX + subW > m_renderer->m_windowWidth) {
                            subX = level.x - subW + 4;
                        }
                        int subY = hoveredRowY;
                        if (subY + subH > m_renderer->m_windowHeight) {
                            subY = std::max(0, m_renderer->m_windowHeight - subH - 8);
                        }

                        m_cascadeStack.push_back({ child, subX, subY, subW, subH, -1 });
                    }
                }
            }
            return true;
        }
    }
    return false;
}

bool MenuManager::handleMouseMove(int x, int y) {
    if (!isMenuOpen()) return false;

    // 1. 检查是否在级联菜单内部
    if (updateCascadeHover(x, y)) {
        return true;
    }

    // 2. 检查是否在顶部菜单栏内切换
    if (m_activeTopMenu >= 0 && y >= 0 && y <= m_renderer->m_menuBarHeight) {
        int menuX = kTopMenuX;
        for (size_t i = 0; i < m_topMenus.size(); ++i) {
            if (!isTopMenuVisible(i)) continue;
            int textW = m_renderer->getTextWidth(m_topMenus[i]->getLabel());
            int itemWidth = textW + kTopMenuPaddingX * 2;
            if (x >= menuX && x <= menuX + itemWidth) {
                if (m_activeTopMenu != static_cast<int>(i)) {
                    openTopMenu(static_cast<int>(i));
                }
                return true;
            }
            menuX += itemWidth + kTopMenuGap;
        }
    }

    return false;
}

bool MenuManager::handleMenuClick(int x, int y) {
    if (!m_renderer) return false;

    // 1. 顶部菜单栏点击
    if (y >= 0 && y <= m_renderer->m_menuBarHeight) {
        int menuX = kTopMenuX;
        for (size_t i = 0; i < m_topMenus.size(); ++i) {
            if (!isTopMenuVisible(i)) continue;
            int textW = m_renderer->getTextWidth(m_topMenus[i]->getLabel());
            int itemWidth = textW + kTopMenuPaddingX * 2;
            if (x >= menuX && x <= menuX + itemWidth) {
                if (m_activeTopMenu == static_cast<int>(i) && !m_menuAnimating) {
                    closeAllMenus(true);
                } else {
                    openTopMenu(static_cast<int>(i));
                }
                return true;
            }
            menuX += itemWidth + kTopMenuGap;
        }
        closeAllMenus(true);
        return false;
    }

    // 2. 级联菜单点击
    if (m_activeTopMenu >= 0 && !m_cascadeStack.empty()) {
        const int itemHeight = 30;
        const int separatorHeight = 12;
        const int menuPadY = 5;

        for (int lvl = static_cast<int>(m_cascadeStack.size()) - 1; lvl >= 0; --lvl) {
            auto& level = m_cascadeStack[lvl];
            if (x >= level.x && x <= level.x + level.width &&
                y >= level.y && y <= level.y + level.height) {

                int scanY = level.y + menuPadY;
                for (size_t i = 0; i < level.parent->children.size(); ++i) {
                    const auto& child = level.parent->children[i];
                    int rowH = child->isSeparator ? separatorHeight : itemHeight;
                    if (!child->isSeparator && x >= level.x && x <= level.x + level.width &&
                        y >= scanY && y <= scanY + itemHeight) {
                        if (child->hasChildren()) {
                            // 点击拥有子菜单项：保持展开
                            return true;
                        }
                        if (child->isEnabled()) {
                            executeItemAction(child);
                            // 画面微调类项保持菜单展开，其余正常关闭
                            bool keepOpen = (child->id == static_cast<int>(MenuId::VideoFilterBrightnessUp) ||
                                             child->id == static_cast<int>(MenuId::VideoFilterBrightnessDown) ||
                                             child->id == static_cast<int>(MenuId::VideoFilterContrastUp) ||
                                             child->id == static_cast<int>(MenuId::VideoFilterContrastDown) ||
                                             child->id == static_cast<int>(MenuId::VideoFilterSaturationUp) ||
                                             child->id == static_cast<int>(MenuId::VideoFilterSaturationDown));
                            if (!keepOpen) {
                                closeAllMenus(true);
                            }
                            return true;
                        }
                    }
                    scanY += rowH;
                }
                return true;
            }
        }
        closeAllMenus(true);
    }
    return false;
}

bool MenuManager::handleContextMenuClick(int x, int y) {
    if (!m_showContextMenu || m_cascadeStack.empty() || !m_renderer) return false;

    const int itemHeight = 30;
    const int separatorHeight = 12;
    const int menuPadY = 5;

    for (int lvl = static_cast<int>(m_cascadeStack.size()) - 1; lvl >= 0; --lvl) {
        auto& level = m_cascadeStack[lvl];
        if (x >= level.x && x <= level.x + level.width &&
            y >= level.y && y <= level.y + level.height) {

            int scanY = level.y + menuPadY;
            for (size_t i = 0; i < level.parent->children.size(); ++i) {
                const auto& child = level.parent->children[i];
                int rowH = child->isSeparator ? separatorHeight : itemHeight;
                if (!child->isSeparator && x >= level.x && x <= level.x + level.width &&
                    y >= scanY && y <= scanY + itemHeight) {
                    if (child->hasChildren()) {
                        return true;
                    }
                    if (child->isEnabled()) {
                        executeItemAction(child);
                        closeAllMenus(false);
                        return true;
                    }
                }
                scanY += rowH;
            }
            return true;
        }
    }
    closeAllMenus(false);
    return true;
}

void MenuManager::showContextMenu(int x, int y) {
    if (!m_renderer) return;

    int hitValue = 0;
    ControlType hit = m_renderer->getControlAt(x, y, &hitValue);
    if (hit == ControlType::PlaylistItem) {
        m_contextPlaylistIndex = hitValue;
        m_activeContextMenu = m_playlistContextMenu;
    } else {
        m_contextPlaylistIndex = -1;
        m_activeContextMenu = m_playbackContextMenu;
    }

    m_contextMenuX = x;
    m_contextMenuY = y;
    m_showContextMenu = true;
    m_activeTopMenu = -1;
    m_cascadeStack.clear();

    int menuW = calculateMenuWidth(m_activeContextMenu);
    int itemH = 30;
    int sepH = 12;
    int contentH = 0;
    for (const auto& child : m_activeContextMenu->children) {
        contentH += child->isSeparator ? sepH : itemH;
    }
    int menuH = contentH + 10;

    int renderX = x;
    int renderY = y;
    if (renderX + menuW > m_renderer->m_windowWidth) {
        renderX = std::max(0, m_renderer->m_windowWidth - menuW - 8);
    }
    if (renderY + menuH > m_renderer->m_windowHeight) {
        renderY = std::max(0, m_renderer->m_windowHeight - menuH - 8);
    }

    m_cascadeStack.push_back({ m_activeContextMenu, renderX, renderY, menuW, menuH, -1 });
}

void MenuManager::hideContextMenu() {
    if (m_showContextMenu) {
        m_showContextMenu = false;
        m_cascadeStack.clear();
    }
}

void MenuManager::executeItemAction(const MenuItemPtr& item) {
    if (!item || !m_renderer) return;

    if (item->onTrigger) {
        item->onTrigger();
    } else if (item->id != 0) {
        MenuId id = static_cast<MenuId>(item->id);
        if (id == MenuId::PlaylistRemove && m_renderer->m_playlistRemoveCallback &&
            m_contextPlaylistIndex >= 0) {
            m_renderer->m_playlistRemoveCallback(static_cast<size_t>(m_contextPlaylistIndex));
        } else if (id == MenuId::PlaylistClear && m_renderer->m_playlistClearCallback) {
            m_renderer->m_playlistClearCallback();
        } else if (id == MenuId::PlaylistPlayItem && m_renderer->m_playlistItemCallback &&
                   m_contextPlaylistIndex >= 0) {
            m_renderer->m_playlistItemCallback(static_cast<size_t>(m_contextPlaylistIndex));
        } else if (m_renderer->m_menuCallback) {
            m_renderer->m_menuCallback(id);
        }
    }
}

} // namespace VideoPlay
