#pragma once

#include "core/common.h"
#include "core/episodedetector.h"
#include "renderer/windowframe.h"

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <mutex>

// SDL forward declarations
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
union SDL_Event;
typedef struct TTF_Font TTF_Font;
typedef struct SDL_Cursor SDL_Cursor;

namespace VideoPlay {

// UI 回调函数类型
using FileDropCallback = std::function<void(const std::string&)>;
using FileOpenCallback = std::function<void()>;
using KeyCallback = std::function<void(int, bool)>;  // keycode, pressed
using MouseCallback = std::function<void(int, int, int)>;  // x, y, button
using SeekCallback = std::function<void(double)>;  // position (0-1)
using VolumeCallback = std::function<void(int)>;   // volume delta (>=1000 means absolute: value-1000)
using UIMuteCallback = std::function<void()>;
using PlayPauseCallback = std::function<void()>;
using StopCallback = std::function<void()>;
using SpeedCallback = std::function<void(double)>; // speed multiplier
using PrevCallback = std::function<void()>;
using NextCallback = std::function<void()>;
using FullscreenCallback = std::function<void()>;
using MenuCallback = std::function<void(int)>;     // menu item id
using PlaylistItemCallback = std::function<void(size_t)>;
using EpisodeItemCallback = std::function<void(size_t)>;
using EpisodePrevCallback = std::function<void()>;
using EpisodeNextCallback = std::function<void()>;

// 控件类型
enum class ControlType {
    None,
    PlayButton,
    StopButton,
    PrevButton,
    NextButton,
    ProgressBar,
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
    SysCloseButton
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

// 菜单项
struct MenuItem {
    int id;
    std::string label;
    std::string shortcut;
    bool separator = false;
    bool enabled = true;
};

// 菜单
struct Menu {
    std::string label;
    std::vector<MenuItem> items;
    bool open = false;
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

    // 设置回调
    void setFileDropCallback(FileDropCallback callback);
    void setFileOpenCallback(FileOpenCallback callback);
    void setKeyCallback(KeyCallback callback);
    void setMouseCallback(MouseCallback callback);
    void setSeekCallback(SeekCallback callback);
    void setVolumeCallback(VolumeCallback callback);
    void setMuteCallback(UIMuteCallback callback);
    void setPlayPauseCallback(PlayPauseCallback callback);
    void setStopCallback(StopCallback callback);
    void setSpeedCallback(SpeedCallback callback);
    void setPrevCallback(PrevCallback callback);
    void setNextCallback(NextCallback callback);
    void setFullscreenCallback(FullscreenCallback callback);
    void setMenuCallback(MenuCallback callback);
    void setPlaylistItemCallback(PlaylistItemCallback callback);
    void setEpisodeItemCallback(EpisodeItemCallback callback);
    void setEpisodePrevCallback(EpisodePrevCallback callback);
    void setEpisodeNextCallback(EpisodeNextCallback callback);

    // 剧集数据
    void setEpisodeData(const std::vector<EpisodeInfo>* episodes, size_t currentIndex,
                        const std::string& seriesName = "", int seasonNumber = 0);
    void setEpisodeProgress(const std::vector<float>& progress);
    void setPlaylistProgress(const std::vector<float>& progress);
    void setPrevNextTooltip(const std::string& prevTooltip, const std::string& nextTooltip);
    void toggleEpisodePanel();
    void togglePlaylistPanel();

    // 渲染 UI 控件
    void renderUI(int64_t position, int64_t duration, int volume, bool isMuted,
                  bool isPlaying, double speed, const std::string& filename,
                  const std::string& subtitle = {},
                  const std::vector<std::string>& playlist = {}, size_t currentPlaylistIndex = 0,
                  int64_t audioPts = 0, int64_t videoPts = 0, double avDiff = 0.0,
                  bool isPreloading = false);

    // 打开文件对话框（异步回调）
    void openFileDialog(std::function<void(const std::string&)> callback, const std::vector<std::string>& filters = {});
    void openSubtitleDialog(std::function<void(const std::string&)> callback);
    void openFolderDialog(std::function<void(const std::string&)> callback);

    // 获取窗口尺寸
    int windowWidth() const { return m_windowWidth; }
    int windowHeight() const { return m_windowHeight; }

    // 显示消息框
    void showMessageBox(const std::string& title, const std::string& message, bool isError = false);

private:
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

    // 菜单处理
    void initMenus();
    void renderMenuBar();
    void renderMenu(const Menu& menu, int x, int y, float alpha = 1.0f);
    bool handleMenuClick(int x, int y);
    void closeAllMenus(bool animate = true);
    bool isMenuOpen() const;

    // 渲染各个部分
    void renderControls(int64_t position, int64_t duration, int volume, bool isMuted,
                        bool isPlaying, double speed, bool isPreloading);
    void renderProgressBar(int64_t position, int64_t duration, int controlY, bool isPreloading, bool isPlaying);
    void renderVolumeControl(int volume, bool isMuted, int controlY);
    void renderPlaybackControls(bool isPlaying, int controlY);
    void renderSpeedButton(double speed, int controlY);
    void renderTimeDisplay(int64_t position, int64_t duration, int controlY);
    void renderFilename(const std::string& filename);
    void renderSubtitle(const std::string& subtitle);
    void renderSyncInfo(int64_t audioPts, int64_t videoPts, double avDiff, bool playlistVisible = false);
    void renderTooltip();
    void renderLoadingAnimation();
    void renderPlaylistPanel(const std::vector<std::string>& playlist, size_t currentIndex);
    void renderEpisodePanel();

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
    int m_playlistScrollOffset = 0;
    int m_episodeScrollOffset = 0;
    bool m_isPlaying = false;
    std::vector<float> m_episodeProgress;
    std::vector<float> m_playlistProgress;
    const std::vector<EpisodeInfo>* m_episodeData = nullptr;
    size_t m_currentEpisodeIndex = 0;
    std::string m_episodeSeriesName;
    int m_episodeSeasonNumber = 0;
    bool m_draggingProgress = false;
    float m_dragProgressRatio = 0.0f;
    bool m_draggingVolume = false;
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
    
    // 菜单
    std::vector<Menu> m_menus;
    int m_activeMenu = -1;
    bool m_menuBarHovered = false;

    // 菜单动画
    void updateMenuAnimation();
    float m_menuAnimAlpha = 0.0f;
    bool m_menuAnimating = false;
    uint64_t m_menuAnimStartTime = 0;
    static constexpr uint64_t MENU_ANIM_DURATION_MS = 120;
    int m_pendingMenu = -1;

    // 进度条宽度动画 (0.0 = 1/3 width, 1.0 = full width)
    float m_progressBarScale = 0.0f;
    uint64_t m_lastProgressAnimTime = 0;
    static constexpr float PROGRESS_BAR_ANIM_SPEED = 12.0f; // 指数衰减系数，越大响应越快

    // 鼠标状态
    int m_mouseX = 0;
    int m_mouseY = 0;
    bool m_mouseDown = false;
    ControlType m_hoveredControl = ControlType::None;
    ControlType m_pressedControl = ControlType::None;
    int m_pressedControlValue = 0;
    uint64_t m_lastMouseMove = 0;

    // 窗口框架管理器（无边框模式）
    std::unique_ptr<WindowFrame> m_windowFrame;

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

    // 回调
    FileDropCallback m_fileDropCallback;
    FileOpenCallback m_fileOpenCallback;
    KeyCallback m_keyCallback;
    MouseCallback m_mouseCallback;
    SeekCallback m_seekCallback;
    VolumeCallback m_volumeCallback;
    UIMuteCallback m_muteCallback;
    PlayPauseCallback m_playPauseCallback;
    StopCallback m_stopCallback;
    SpeedCallback m_speedCallback;
    PrevCallback m_prevCallback;
    NextCallback m_nextCallback;
    FullscreenCallback m_fullscreenCallback;
    MenuCallback m_menuCallback;
    PlaylistItemCallback m_playlistItemCallback;
    EpisodeItemCallback m_episodeItemCallback;
    EpisodePrevCallback m_episodePrevCallback;
    EpisodeNextCallback m_episodeNextCallback;

    // 异步对话框结果（跨线程安全）
    std::mutex m_dialogMutex;
    std::string m_pendingDialogResult;
    bool m_dialogResultReady = false;
    std::function<void(const std::string&)> m_dialogCallback;
};

} // namespace VideoPlay
