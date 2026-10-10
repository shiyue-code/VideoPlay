#pragma once

#include "core/common.h"
#include "core/audioplayer.h"
#include "core/episodedetector.h"
#include "renderer/windowframe.h"
#include "renderer/menu_manager.h"
#include "renderer/ui_manager.h"
#include "renderer/dialog_manager.h"

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <mutex>
#include <map>
#include <unordered_map>
#include <algorithm>

// SDL forward declarations
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
union SDL_Event;
typedef struct TTF_Font TTF_Font;
typedef struct SDL_Cursor SDL_Cursor;

namespace VideoPlay {

// 菜单项 ID（静态菜单；动态范围保留为 100-109 / 200-249 / 400-449 / 450-499）
enum class MenuId : int {
    // 文件菜单
    OpenFile = 1,
    OpenFolder = 2,
    ImportSubtitle = 4,
    RecentFiles = 5,
    MediaInfo = 6,
    OpenUrl = 7,
    Exit = 3,

    // 播放菜单
    PlayPause = 10,
    Stop = 11,
    Prev = 12,
    Next = 13,
    SpeedUp = 14,
    SpeedDown = 15,
    Fullscreen = 16,
    Borderless = 17,
    Playlist = 18,
    PrevEpisodePlayMenu = 19,
    NextEpisodePlayMenu = 22,
    PlaylistRemove = 23,
    PlaylistClear = 24,
    PlaylistPlayItem = 25,
    EpisodePanel = 32,

    // 剧集菜单
    PrevEpisode = 30,
    NextEpisode = 31,
    EpisodePrev = 30,
    EpisodeNext = 31,

    // 帮助
    Help = 50,
    About = 51,

    // AB 循环 / 循环 / 画面比例（父菜单 & 子项）
    ABLoop = 58,
    ABLoopSetA = 90,
    ABLoopSetB = 91,
    ABLoopClear = 92,

    Loop = 59,
    LoopNone = 60,
    LoopSingle = 61,
    LoopPlaylist = 62,

    Aspect = 69,
    AspectOriginal = 70,
    Aspect16_9 = 71,
    Aspect4_3 = 72,
    AspectFill = 73,

    // 窗口 & 滤镜
    AlwaysOnTop = 80,
    Screenshot = 88,
    AudioFilter = 89,
    AudioFilterOff = 93,
    AudioFilterVoice = 94,
    AudioFilterBass = 95,
    AudioFilterNight = 96,
    HardwareDecoding = 97,
    AudioOutput = 98,

    // 视频基础参数：从 120 开始，避免与最近文件 100-109 冲突
    VideoFilter = 120,
    VideoFilterBrightnessUp = 121,
    VideoFilterBrightnessDown = 122,
    VideoFilterContrastUp = 123,
    VideoFilterContrastDown = 124,
    VideoFilterSaturationUp = 125,
    VideoFilterReset = 126,
    VideoFilterPanel = 127,
    VideoFilterSaturationDown = 128,

    VideoTransform = 130,
    Rotate0 = 131,
    Rotate90 = 132,
    Rotate180 = 133,
    Rotate270 = 134,
    FlipHorizontal = 135,
    FlipVertical = 136,
    CropOff = 137,
    Crop10 = 138,
    Crop20 = 139,
    TransformReset = 140,

    Deinterlace = 141,
    DeinterlaceOff = 142,
    DeinterlaceAuto = 143,
    DeinterlaceYadif = 144,
    DeinterlaceBwdif = 145,

    // HDR 色调映射
    HdrToneMap = 146,
    HdrToneMapAuto = 147,
    HdrToneMapOn = 148,
    HdrToneMapOff = 149,
    HdrAlgoHable = 150,
    HdrAlgoMobius = 151,
    HdrAlgoReinhard = 152,
    HdrAlgorithmMenu = 153,

    // AI 菜单
    AIAnalyze = 300,
    AISummary = 301,
    Search = 302,
    ClearAICache = 303,
    AISettings = 304,

    // 动态范围起始值
    RecentFileBase = 100,
    ChapterBase = 200,
    AudioTrackBase = 400,
    SubtitleTrackBase = 450,
    AudioDeviceBase = 500
};

// 各动态菜单项数量
constexpr int kRecentFileCount = 10;
constexpr int kChapterCount = 50;
constexpr int kAudioTrackCount = 50;
constexpr int kSubtitleTrackCount = 50;
constexpr int kAudioDeviceCount = 32;

// UI 回调函数类型
using FileDropCallback = std::function<void(const std::string&)>;
using FileOpenCallback = std::function<void()>;
using KeyCallback = std::function<void(int, bool)>;  // keycode, pressed
// 回调编码约定：UI 只知道自己拿到的相对量，seek/音量条拖动需要回传绝对值。
// 统一编码为「base + 值」，接收方按 >= base 判绝对值再解码。
inline constexpr double kSeekAbsoluteBase = 1000.0;  // seek: base + ratio*1000，解码后 ratio ∈ [0,1]
inline constexpr int kVolumeAbsoluteBase = 1000;     // volume: base + 音量(0~100)

using SeekCallback = std::function<void(double)>;  // 相对秒数 或 kSeekAbsoluteBase + ratio*1000
using VolumeCallback = std::function<void(int)>;   // 相对增量 或 kVolumeAbsoluteBase + 音量值
using UIMuteCallback = std::function<void()>;
using PlayPauseCallback = std::function<void()>;
using StopCallback = std::function<void()>;
using SpeedCallback = std::function<void(double)>; // speed multiplier
using PrevCallback = std::function<void()>;
using NextCallback = std::function<void()>;
using FullscreenCallback = std::function<void()>;
using MenuCallback = std::function<void(MenuId)>;     // menu item id
using PlaylistItemCallback = std::function<void(size_t)>;
using EpisodeItemCallback = std::function<void(size_t)>;
using EpisodePrevCallback = std::function<void()>;
using EpisodeNextCallback = std::function<void()>;
using LoopModeCallback = std::function<void(int)>;  // 0=None, 1=Single, 2=Playlist
using SearchCallback = std::function<void(const std::string&)>;
using SubtitleSyncCallback = std::function<void(int)>;  // deltaMs (positive = delay, negative = advance)
using AudioSyncCallback = std::function<void(int)>;   // deltaMs (positive = audio delay, negative = audio advance)
using ABLoopCallback = std::function<void(char)>;  // 'a'=set A, 'b'=set B, 'c'=clear
using AudioTrackCallback = std::function<void(int)>;  // 音轨下标，-1=关闭
using SubtitleTrackCallback = std::function<void(int)>;  // 字幕轨下标，-1=关闭内封
using VideoFilterConfigCallback = std::function<void(const VideoFilterConfig&)>;

// App → Renderer 的全部回调，一次 setCallbacks() 传入。
// 字段即原 setXxxCallback 的对应项；留空表示不关心。
struct RendererCallbacks {
    FileDropCallback fileDrop;
    FileOpenCallback fileOpen;
    KeyCallback key;
    SeekCallback seek;
    VolumeCallback volume;
    UIMuteCallback mute;
    PlayPauseCallback playPause;
    StopCallback stop;
    SpeedCallback speed;
    PrevCallback prev;
    NextCallback next;
    FullscreenCallback fullscreen;
    MenuCallback menu;
    PlaylistItemCallback playlistItem;
    PlaylistItemCallback playlistRemove;
    std::function<void()> playlistClear;
    std::function<void(size_t, size_t)> playlistReorder;
    EpisodeItemCallback episodeItem;
    EpisodePrevCallback episodePrev;
    EpisodeNextCallback episodeNext;
    LoopModeCallback loopMode;
    SearchCallback search;
    SubtitleSyncCallback subtitleSync;
    AudioSyncCallback audioSync;
    std::function<void()> addBookmark;
    std::function<void()> clearBookmarks;
    std::function<void(int)> bookmarkClick;
    ABLoopCallback abLoop;
    AudioTrackCallback audioTrack;
    SubtitleTrackCallback subtitleTrack;
    VideoFilterConfigCallback videoFilterConfig;
    std::function<void()> screenshot;   // 截图请求（由 app 截取当前视频帧）
    std::function<void()> liveRender;  // 系统拖动/缩放模态循环期间实时重绘
};

enum class OSDType {
    Message,
    Play,
    Pause,
    Volume,
    Mute,
    SeekBackward,
    SeekForward,
    Speed,
    Info
};

// 控件类型
enum class ControlType {
    None,
    PlayButton,
    StopButton,
    PrevButton,
    NextButton,
    ProgressBar,
    ChapterMarker,
    BookmarkMarker,
    VolumeButton,
    VolumeBar,
    SpeedButton,
    PlaylistButton,
    PlaylistItem,
    EpisodePrev,
    EpisodeNext,
    EpisodeItem,
    EpisodePanelToggle,
    MenuBar,
    MenuItem,
    SysMinButton,
    SysMaxButton,
    SysCloseButton,
    SearchPanelToggle,
    SearchCloseButton,
    SearchInput,
    SearchTimestamp,
    SearchMessageCopy,
    PanelBackground,
    VideoPanelToggle,
    VideoPanelClose,
    VideoPanelBrightnessBar,
    VideoPanelContrastBar,
    VideoPanelSaturationBar,
    VideoPanelReset,
    VideoPanelHdrToggle
};

// 无边框窗口 resize 模式
enum class ResizeMode {
    None,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};


struct ChatMessage {
    bool isUser;
    std::string text;
};

class SDLRenderer {
public:
    SDLRenderer();
    ~SDLRenderer();

    // 禁用拷贝
    SDLRenderer(const SDLRenderer&) = delete;
    SDLRenderer& operator=(const SDLRenderer&) = delete;

    // 初始化和清理
    bool initialize(const std::string& title, int width, int height);
    void shutdown();
    bool isInitialized() const { return m_initialized; }

    // 窗口操作
    void setWindowTitle(const std::string& title);
    void setWindowSize(int width, int height);
    void toggleFullscreen();
    bool isFullscreen() const { return m_fullscreen; }
    void toggleBorderless();
    bool isBorderless() const;
    bool isBorderlessEnabled() const { return m_borderless; }

    // 视频渲染
    void renderFrame(const VideoFrame& frame);
    void clear();
    void present();

    // 事件处理
    bool processEvents();

    // 一次性设置全部 App 回调
    void setCallbacks(RendererCallbacks callbacks);
    // 实时重绘回调在 initialize 之后设置，单独保留入口
    void setLiveRenderCallback(std::function<void()> callback);
    void setVideoFilterConfig(const VideoFilterConfig& config);
    const VideoFilterConfig& videoFilterConfig() const { return m_videoFilterConfig; }
    void toggleVideoControlPanel();
    bool isVideoControlPanelOpen() const { return m_showVideoControlPanel; }
    void adjustBrightness(float delta);
    void adjustContrast(float delta);
    void adjustSaturation(float delta);
    void resetVideoFilter();

    // 多轨道数据
    void setAudioTracks(const std::vector<TrackInfo>& tracks, int currentIndex);
    void setSubtitleTracks(const std::vector<TrackInfo>& tracks, int currentIndex);

    // 图形字幕（PGS）位图
    void setSubtitleBitmap(const SubtitleBitmap& bitmap);
    void clearSubtitleBitmap();

    // 章节数据
    void setChapters(const std::vector<ChapterInfo>& chapters);

    // 用户书签数据
    void setBookmarks(const std::vector<Bookmark>& bookmarks);

    // 搜索热力图数据
    void setSearchHighlights(const std::vector<int64_t>& timestamps);

    // 进度条缩略图
    int64_t previewTargetPtsMs() const { return m_previewTargetPtsMs; }
    void setPreviewFrame(VideoFrame frame);
    void clearPreview();

    // 剧集数据
    void setEpisodeData(const std::vector<EpisodeInfo>* episodes, size_t currentIndex,
                        const std::string& seriesName = "", int seasonNumber = 0);
    void setEpisodeProgress(const std::vector<float>& progress);
    void setPlaylistProgress(const std::vector<float>& progress);
    void setPrevNextTooltip(const std::string& prevTooltip, const std::string& nextTooltip);
    void toggleEpisodePanel();
    void togglePlaylistPanel();
    void showSearchPanel();
    void toggleSearchPanel();
    void addChatMessage(bool isUser, const std::string& text);
    void toggleMediaInfoPanel();
    void setMediaInfo(const MediaInfo& info);
    void showOSD(const std::string& text);
    void showOSD(OSDType type, const std::string& text, float progress = -1.0f);
    void setHardwareDecodingEnabled(bool enabled);
    void setAudioFilterPreset(AudioFilterPreset preset);
    void setDeinterlaceMode(DeinterlaceMode mode);
    void setHdrToneMapMode(HdrToneMapMode mode);
    void setHdrToneMapAlgorithm(HdrToneMapAlgorithm algo);
    void refreshAudioOutputDevices();
    void setAudioOutputDeviceName(const std::string& name);
    const std::vector<AudioOutputDevice>& audioOutputDevices() const { return m_audioOutputDevices; }
    void setNetworkState(NetworkState state);
    void setAIAnalysisState(bool active, float progress, const std::string& status);

    // 渲染 UI 控件
    void renderUI(int64_t position, int64_t duration, int volume, bool isMuted,
                  bool isPlaying, double speed, const std::string& filename,
                  const std::string& subtitle = {},
                  const std::vector<std::string>& playlist = {}, size_t currentPlaylistIndex = 0,
                  int64_t audioPts = 0, int64_t videoPts = 0, double avDiff = 0.0,
                  bool isPreloading = false);

    // 打开文件对话框（异步回调）
    void openFileDialog(std::function<void(const std::string&)> callback, const std::vector<std::string>& filters = {});
    void showInputDialog(const std::string& title, const std::string& placeholder,
                         std::function<void(const std::string&)> callback);
    void openSubtitleDialog(std::function<void(const std::string&)> callback);
    void openFolderDialog(std::function<void(const std::string&)> callback);

    // 获取窗口尺寸
    int windowWidth() const { return m_windowWidth; }
    int windowHeight() const { return m_windowHeight; }

    // 获取窗口和字体对象
    SDL_Window* getWindow() const { return m_window; }
    TTF_Font* getFont() const { return m_font; }

    // 显示消息框
    void showMessageBox(const std::string& title, const std::string& message,
                        bool isError = false,
                        std::function<void(int64_t timestampMs)> timestampCallback = nullptr);

    // 更新文件菜单中的最近文件列表
    void updateRecentFilesMenu();

    // 设置循环模式 (0=None, 1=Single, 2=Playlist)
    void setLoopMode(int mode);

    // 设置画面比例
    void setAspectMode(AspectMode mode);
    void setVideoTransform(const VideoTransform& transform);
    VideoTransform videoTransform() const { return m_videoTransform; }
    AspectMode aspectMode() const;

    // 窗口置顶
    void toggleAlwaysOnTop();
    bool isAlwaysOnTop() const;

    // 渲染垂直同步是否已启用
    bool isVSyncEnabled() const { return m_vsyncEnabled; }

    // 窗口最大化状态
    bool isMaximized() const;
    void maximizeWindow();
    void restoreWindow();

private:
    friend class MenuManager;
    friend class UIManager;
    friend class DialogManager;

    // 内部实现（由 Managers 调用）
    void renderUIImpl(int64_t position, int64_t duration, int volume, bool isMuted,
                      bool isPlaying, double speed, const std::string& filename,
                      const std::string& subtitle, const std::vector<std::string>& playlist,
                      size_t currentPlaylistIndex, int64_t audioPts, int64_t videoPts,
                      double avDiff, bool isPreloading);
    void handleEvent(const SDL_Event& event);
    void handleMouseClick(int x, int y);
    void handleMouseMotion(int x, int y);
    void handleMouseButtonDown(int x, int y);
    void handleMouseButtonUp(int x, int y);
    void handleMouseWheel(int y);

    // 控件检测
    ControlType getControlAt(int x, int y, int* outValue = nullptr);
    ResizeMode getResizeModeAt(int x, int y) const;
    void updateCursorForResize(ResizeMode mode);

    // 无边框窗口框架协作
    void setupWindowFrameCallbacks();
    FrameHitTest captionHitTestAt(int x, int y);
    void handleFrameMouse(FrameHitTest hit, FrameMouseAction action);
    void renderLiveFrame();

    // 菜单处理（由 MenuManager 统一接管）
    void initMenus();
    void updateChapterMenuItems();
    void updateTrackMenus();
    void renderMenuBar();
    bool handleMenuClick(int x, int y);
    void closeAllMenus(bool animate = true);
    void closeMenu();
    bool isMenuOpen() const;
    bool isTopMenuVisible(size_t index) const;

    // 右键上下文菜单
    void showContextMenu(int x, int y);
    void hideContextMenu();
    void closeContextMenu();
    void renderContextMenu();
    bool handleContextMenuClick(int x, int y);
    bool isContextMenuOpen() const;

    // 渲染各个部分
    void renderControls(int64_t position, int64_t duration, int volume, bool isMuted,
                        bool isPlaying, double speed, bool isPreloading);
    void renderProgressBar(int64_t position, int64_t duration, int controlY, bool isPreloading, bool isPlaying);
    void renderProgressPreview();
    void applySmallWindowPanelGuard();
    void renderVolumeControl(int volume, bool isMuted, int controlY);
    void renderPlaybackControls(bool isPlaying, int controlY);
    void renderSpeedButton(double speed, int controlY);
    void renderTimeDisplay(int64_t position, int64_t duration, int controlY);
    void renderFilename(const std::string& filename);
    void renderSubtitle(const std::string& subtitle);
    void renderSubtitleBitmap(int64_t positionMs);
    void renderSyncInfo(int64_t audioPts, int64_t videoPts, double avDiff, bool playlistVisible = false);
    void renderTooltip();
    void renderOSD();
    void renderMediaInfoPanel();
    void renderVideoControlPanel();
    void renderNetworkState();
    void renderAIAnalysisOverlay();
    void renderLoadingAnimation();
    void renderPlaylistPanel(const std::vector<std::string>& playlist, size_t currentIndex);
    void renderEpisodePanel();
    void renderSearchPanel();
    bool hasSearchSelection() const;
    std::pair<size_t, size_t> searchSelectionRange() const;
    void clearSearchSelection();
    void selectAllSearchQuery();
    void deleteSearchSelection();
    void insertSearchText(const std::string& text);
    void deleteSearchCharBefore();
    void deleteSearchCharAfter();
    void copySearchSelection();
    void cutSearchSelection();
    void pasteSearchClipboard();
    void copyChatMessage(size_t index);
    void copyLastAIChatMessage();

    // 创建/更新纹理
    void ensureTexture(int width, int height);

    // 字体和文字渲染
    bool loadFont(const std::string& fontPath, int fontSize);
    void closeFont();
    void drawText(const std::string& text, int x, int y, uint8_t r, uint8_t g, uint8_t b, int fontSize = 0, uint8_t alpha = 255);
    int getTextWidth(const std::string& text, int fontSize = 0);
    int getFontHeight(int fontSize = 0);

    // 图标纹理
    void loadIconTextures();
    void clearIconTextures();
    SDL_Texture* getIconTexture(const std::string& type);
    SDL_Texture* createIconTexture(const std::string& type);

    // 绘制辅助函数
    void drawRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void fillRoundRect(int x, int y, int w, int h, int radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void fillCircle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void renderGlowBar(int cx, int cy, int width, int height, uint8_t red, uint8_t green, uint8_t blue, uint8_t centerAlpha, int clipLeft, int clipRight);
    void drawGradientVignette();
    void renderSmoothRoundRect(int x, int y, int w, int h, int radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void renderSmoothCircle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void drawButton(int x, int y, int w, int h, const std::string& iconType, bool hovered, bool pressed);
    void drawIcon(int cx, int cy, const std::string& type, bool hovered, float scale = 1.0f);

    // SDL 对象
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    SDL_Texture* m_videoTexture = nullptr;
    
    // 字体
    TTF_Font* m_font = nullptr;
    TTF_Font* m_fontSmall = nullptr;
    TTF_Font* m_fontLarge = nullptr;
    std::string m_fontPath;

    // 子系统管理器
    friend class MenuManager;
    std::unique_ptr<MenuManager> m_menuManager;
    std::unique_ptr<UIManager> m_uiManager;
    std::unique_ptr<DialogManager> m_dialogManager;

    // 图标纹理
    std::unordered_map<std::string, SDL_Texture*> m_iconTextures;
    
    // 文字纹理缓存（LRU 淘汰）
    struct TextCacheEntry {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
        uint64_t lastUsed = 0;
    };
    std::unordered_map<std::string, TextCacheEntry> m_textCache;
    void clearTextCache();
    void pruneTextCache(size_t maxEntries = 256);

    // 状态
    bool m_initialized = false;
    bool m_fullscreen = false;
    bool m_borderless = false;
    bool m_showControls = true;
    bool m_showPlaylistPanel = true;
    bool m_showEpisodePanel = false;
    bool m_showSearchPanel = false;
    bool m_showMediaInfoPanel = false;
    int m_playlistScrollOffset = 0;
    bool m_playlistDragging = false;
    int m_playlistDragFrom = -1;
    int m_playlistDragTo = -1;
    int m_playlistDragStartY = 0;
    int m_playlistItemCount = 0;
    int m_episodeScrollOffset = 0;
    int m_searchScrollOffset = 0;
    std::string m_searchQuery;
    bool m_isSearchInputFocused = false;
    size_t m_searchSelectionStart = 0;
    size_t m_searchSelectionEnd = 0;
    uint64_t m_lastSearchInputTime = 0;
    std::vector<ChatMessage> m_chatHistory;
    std::mutex m_chatMutex;
    bool m_isPlaying = false;
    int m_loopMode = 2; // 0=None, 1=Single, 2=Playlist
    bool m_hardwareDecodingEnabled = true;
    AudioFilterPreset m_audioFilterPreset = AudioFilterPreset::Off;
    DeinterlaceMode m_deinterlaceMode = DeinterlaceMode::Off;
    HdrToneMapMode m_hdrToneMapMode = HdrToneMapMode::Auto;
    HdrToneMapAlgorithm m_hdrToneMapAlgorithm = HdrToneMapAlgorithm::Hable;
    std::vector<AudioOutputDevice> m_audioOutputDevices;
    std::string m_audioOutputDeviceName;
    NetworkState m_networkState = NetworkState::Idle;
    bool m_aiAnalysisActive = false;
    float m_aiAnalysisProgress = 0.0f;
    std::string m_aiAnalysisStatus;
    std::string m_aiAnalysisNoticeText;
    float m_aiAnalysisNoticeProgress = -1.0f;
    uint64_t m_aiAnalysisStartTime = 0;
    uint64_t m_aiAnalysisNoticeStartTime = 0;
    static constexpr uint64_t AI_ANALYSIS_NOTICE_DURATION_MS = 2600;
    AspectMode m_aspectMode = AspectMode::Original;
    VideoTransform m_videoTransform;
    bool m_alwaysOnTop = false;
    bool m_vsyncEnabled = false;
    std::vector<float> m_episodeProgress;
    std::vector<float> m_playlistProgress;
    const std::vector<EpisodeInfo>* m_episodeData = nullptr;
    size_t m_currentEpisodeIndex = 0;
    std::string m_episodeSeriesName;
    int m_episodeSeasonNumber = 0;
    bool m_draggingProgress = false;
    float m_dragProgressRatio = 0.0f;
    int64_t m_previewTargetPtsMs = -1;
    int m_previewAnchorX = 0;
    int m_previewBarY = 0;
    SDL_Texture* m_previewTexture = nullptr;
    int m_previewTexW = 0;
    int m_previewTexH = 0;
    int64_t m_previewShownPts = -1;
    uint64_t m_previewHoverStart = 0;
    bool m_draggingVolume = false;
    bool m_showVideoControlPanel = false;
    bool m_draggingBrightness = false;
    bool m_draggingContrast = false;
    bool m_draggingSaturation = false;
    VideoFilterConfig m_videoFilterConfig;
    int m_windowWidth = 1280;
    int m_windowHeight = 720;

    // 视频纹理尺寸
    int m_videoWidth = 0;
    int m_videoHeight = 0;

    // 控件位置和尺寸
    int m_menuBarHeight = 32;
    int m_controlHeight = 70;
    int m_buttonSize = 36;
    int m_progressBarHeight = 10;
    int m_volumeWidth = 80;
    int m_margin = 10;
    
    // 控件位置（在render时计算）
    struct ControlRect {
        int x, y, w, h;
        ControlType type;
        int value;
    };
    std::vector<ControlRect> m_controlRects;
    
    std::vector<ChapterInfo> m_chapters;
    std::vector<Bookmark> m_bookmarks;
    bool m_hasChapters = false;
    std::vector<int64_t> m_searchHighlights;
    int64_t m_lastDuration = 0;
    int m_lastBarX = 0;
    int m_lastBarW = 0;

    // 进度条宽度动画 (0.0 = 1/3 width, 1.0 = full width)
    float m_progressBarScale = 0.0f;
    uint64_t m_lastProgressAnimTime = 0;
    static constexpr float PROGRESS_BAR_ANIM_SPEED = 12.0f; // 指数衰减系数，越大响应越快

    // 鼠标状态
    int m_mouseX = 0;
    int m_mouseY = 0;
    bool m_mouseDown = false;
    ControlType m_hoveredControl = ControlType::None;
    int m_hoveredControlValue = 0;
    ControlType m_pressedControl = ControlType::None;
    int m_pressedControlValue = 0;
    uint64_t m_lastMouseMove = 0;

    // 窗口框架管理器（无边框模式）
    std::unique_ptr<WindowFrame> m_windowFrame;

    // 模态循环（原生拖动/缩放）期间的实时重绘（回调存于 m_callbacks.liveRender）
    bool m_inLiveRender = false;

    // 无边框 resize 状态
    bool m_resizingWindow = false;
    ResizeMode m_resizeMode = ResizeMode::None;
    int m_resizeStartMouseX = 0;
    int m_resizeStartMouseY = 0;
    int m_resizeStartWindowX = 0;
    int m_resizeStartWindowY = 0;
    int m_resizeStartWindowW = 0;
    int m_resizeStartWindowH = 0;

    // 光标
    SDL_Cursor* m_cursorDefault = nullptr;
    SDL_Cursor* m_cursorSizeWE = nullptr;
    SDL_Cursor* m_cursorSizeNS = nullptr;
    SDL_Cursor* m_cursorSizeNWSE = nullptr;
    SDL_Cursor* m_cursorSizeNESW = nullptr;

    // 双击检测
    uint64_t m_lastClickTime = 0;
    int m_lastClickX = 0;
    int m_lastClickY = 0;
    std::string m_tooltip;
    std::string m_tooltipPrev;
    std::string m_tooltipNext;
    uint64_t m_tooltipTime = 0;
    uint64_t m_tooltipShowTime = 0;
    std::string m_osdText;
    OSDType m_osdType = OSDType::Message;
    float m_osdProgress = -1.0f;
    uint64_t m_osdStartTime = 0;
    static constexpr uint64_t OSD_DURATION_MS = 1400;
    MediaInfo m_mediaInfo;

    // 回调（App 经 setCallbacks 一次传入）
    RendererCallbacks m_callbacks;

    // 多轨道数据
    std::vector<TrackInfo> m_audioTracks;
    std::vector<TrackInfo> m_subtitleTracks;
    int m_currentAudioTrack = 0;
    int m_currentSubtitleTrack = -1;

    // 图形字幕（PGS）位图
    std::mutex m_subtitleBitmapMutex;
    SDL_Texture* m_subtitleTexture = nullptr;
    SubtitleBitmap m_currentBitmap;

};

} // namespace VideoPlay
