# VideoPlay - ElaWidgetTools Fluent UI 设计方案

## 一、ElaWidgetTools 简介

**ElaWidgetTools** 是 B 站大佬 _Ela 开发的开源 Qt Widgets Fluent UI 组件库：
- 🎨 **50+ 组件**：完整覆盖现代 UI 需求
- 🌓 **主题切换**：浅色/深色/自动模式
- 💎 **亚克力/云母效果**：原生 Windows 11 体验
- ⚡ **纯 QWidget**：性能优异，无需 QML
- 📦 **开源免费**：MIT 协议，约 2k star

**GitHub**: https://github.com/Liniyous/ElaWidgetTools

---

## 二、新界面设计草图

```
┌─────────────────────────────────────────────────────────────────┐
│  [图标] VideoPlay                              🌓 ⚙ 🗕 🗖 ✕     │  <- ElaWindow (无边框)
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                                                         │   │
│  │                    视频播放区域                          │   │  <- ElaGraphicsView
│  │              (支持亚克力背景效果)                         │   │     或 VideoRenderer
│  │                                                         │   │
│  │           ┌─────────────────────────┐                   │   │
│  │           │      字幕显示区域        │                   │   │
│  │           └─────────────────────────┘                   │   │
│  │                                                         │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  ▶  ⏹  [━━━━━━●━━━━━━━━━━━━━━━━━━━━━]  02:30 / 10:00   │   │  <- ElaSlider + ElaIconButton
│  │                                                         │   │
│  │  🔊 [━━━━━]    1.0x    [截图] [全屏] [设置]             │   │
│  └─────────────────────────────────────────────────────────┘   │  <- 底部控制面板 (ElaCard)
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 三、组件映射表

| 现有组件 | ElaWidgetTools 替代 | 效果提升 |
|----------|---------------------|----------|
| `QMainWindow` | `ElaWindow` | 无边框 + 亚克力/云母效果 |
| `Controls` | `ElaContentPage` + 自定义 | 卡片式布局 + 动画过渡 |
| `QPushButton` | `ElaIconButton` | 圆角 + 图标 + 悬停动画 |
| `QSlider` | `ElaSlider` | 流畅滑动 + 进度预览 |
| `QLabel` | `ElaText` | 字体优化 + 主题适配 |
| `PlaylistWidget` | `ElaListView` | 流畅滚动 + 项动画 |
| `QDockWidget` | `ElaNavigationBar` | 侧边导航 + 折叠效果 |
| `QMessageBox` | `ElaContentDialog` | 统一风格对话框 |
| `QMenu` | `ElaMenu` | 圆角菜单 + 阴影 |
| `QComboBox` | `ElaComboBox` | 下拉动画 + 主题色 |

---

## 四、核心界面组件设计

### 1. ElaVideoWindow (主窗口)

```cpp
class ElaVideoWindow : public ElaWindow {
    Q_OBJECT
public:
    explicit ElaVideoWindow(QWidget* parent = nullptr);
    
private:
    void setupUi();
    void setupNavigation();
    void setupPlayerPage();
    void setupPlaylistPage();
    void setupSettingsPage();
    
    // ElaWidgetTools 组件
    ElaNavigationBar* m_navBar;
    ElaContentPage* m_playerPage;
    ElaContentPage* m_playlistPage;
    ElaContentPage* m_settingsPage;
    
    // 视频播放组件
    VideoRenderer* m_videoRenderer;
    ElaControlPanel* m_controlPanel;  // 自定义控制面板
};
```

### 2. ElaControlPanel (播放控制面板)

```cpp
class ElaControlPanel : public ElaCard {
    Q_OBJECT
public:
    explicit ElaControlPanel(QWidget* parent = nullptr);
    
    void setPlaybackState(PlaybackState state);
    void setPosition(qint64 position, qint64 duration);
    void setVolume(int volume);
    void setSpeed(double speed);
    
signals:
    void playClicked();
    void pauseClicked();
    void stopClicked();
    void seekRequested(qint64 position);
    void volumeChanged(int volume);
    void speedChanged(double speed);
    void screenshotClicked();
    void fullscreenClicked();
    
private:
    void setupUi();
    
    // 播放控制
    ElaIconButton* m_playPauseBtn;
    ElaIconButton* m_stopBtn;
    
    // 进度
    ElaSlider* m_progressSlider;
    ElaText* m_timeLabel;
    
    // 音量
    ElaIconButton* m_volumeBtn;
    ElaSlider* m_volumeSlider;
    
    // 倍速
    ElaComboBox* m_speedCombo;  // 0.5x, 0.75x, 1.0x, 1.25x, 1.5x, 2.0x
    
    // 功能按钮
    ElaIconButton* m_screenshotBtn;
    ElaIconButton* m_fullscreenBtn;
    ElaToggleSwitch* m_loopSwitch;  // 循环播放开关
};
```

### 3. ElaPlaylistView (播放列表)

```cpp
class ElaPlaylistView : public ElaListView {
    Q_OBJECT
public:
    explicit ElaPlaylistView(QWidget* parent = nullptr);
    
    void addVideo(const QString& filePath, const QPixmap& thumbnail);
    void removeVideo(int index);
    void clear();
    
signals:
    void videoSelected(int index);
    void videoDoubleClicked(int index);
    void videoRemoved(int index);
    
private:
    // 自定义项委托，显示缩略图 + 标题 + 时长
    ElaPlaylistItemDelegate* m_itemDelegate;
};
```

---

## 五、主题配置

```cpp
// main.cpp
#include <ElaApplication.h>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // 初始化 ElaWidgetTools
    eApp->init();
    
    // 设置主题模式
    eApp->setThemeMode(ElaThemeMode::Auto);  // 跟随系统
    // 或
    eApp->setThemeMode(ElaThemeMode::Light); // 浅色
    eApp->setThemeMode(ElaThemeMode::Dark);  // 深色
    
    // 设置窗口效果
    eApp->setWindowEffect(ElaWindowEffect::Acrylic);  // 亚克力
    // 或
    eApp->setWindowEffect(ElaWindowEffect::Mica);     // 云母
    
    ElaVideoWindow window;
    window.show();
    
    return app.exec();
}
```

---

## 六、界面截图预览（概念图）

### 浅色主题
```
┌────────────────────────────────────┐
│ [浅色标题栏]  VideoPlay       ─ □ ✕ │
├────────────────────────────────────┤
│  [视频区域 - 白色背景]              │
│                                    │
│  ┌──────────────────────────────┐  │
│  │     控制面板 (白色卡片)       │  │
│  │  ▶  [═══════════●════] 02:30 │  │
│  │  🔊 [════]  1.0x  ⛶  📷     │  │
│  └──────────────────────────────┘  │
└────────────────────────────────────┘
```

### 深色主题
```
┌────────────────────────────────────┐
│ [深色标题栏]  VideoPlay       ─ □ ✕ │
├────────────────────────────────────┤
│  [视频区域 - 深色背景]              │
│                                    │
│  ┌──────────────────────────────┐  │
│  │     控制面板 (深灰卡片)       │  │
│  │  ▶  [═══════════●════] 02:30 │  │
│  │  🔊 [════]  1.0x  ⛶  📷     │  │
│  └──────────────────────────────┘  │
└────────────────────────────────────┘
```

### 亚克力效果 (Windows 11)
```
┌────────────────────────────────────┐
│ [透明模糊标题栏]  VideoPlay       ─ │  <- 背景模糊效果
├────────────────────────────────────┤
│                                    │
│     [桌面壁纸模糊可见]              │
│                                    │
│  ┌──────────────────────────────┐  │
│  │  半透明控制面板               │  │
│  └──────────────────────────────┘  │
└────────────────────────────────────┘
```

---

## 七、集成步骤

### Step 1: 添加子模块
```bash
cd VideoPlay
git submodule add https://github.com/Liniyous/ElaWidgetTools.git 3rdparty/elawidgettools
```

### Step 2: 修改 CMakeLists.txt
```cmake
# 添加 ElaWidgetTools
add_subdirectory(3rdparty/elawidgettools)

# 链接库
target_link_libraries(VideoPlay PRIVATE
    ElaWidgetTools
    # ... 其他库
)

# 包含路径
target_include_directories(VideoPlay PRIVATE
    3rdparty/elawidgettools/include
)
```

### Step 3: 创建新界面类
- `src/ui/ela/ElaVideoWindow.h/cpp`
- `src/ui/ela/ElaControlPanel.h/cpp`
- `src/ui/ela/ElaPlaylistView.h/cpp`

### Step 4: 替换 main.cpp
```cpp
#include <ElaApplication.h>
#include "ui/ela/ElaVideoWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    eApp->init();
    
    VideoPlay::ElaVideoWindow window;
    window.show();
    
    return app.exec();
}
```

---

## 八、功能增强建议

| 功能 | 使用组件 | 效果 |
|------|----------|------|
| 缩略图预览 | `ElaGraphicsView` + 自定义项 | 播放列表显示视频缩略图 |
| 动画过渡 | `ElaAnimation` | 页面切换动画 |
| 消息通知 | `ElaMessageBar` | 操作成功/失败提示 |
| 加载动画 | `ElaProgressRing` | 视频缓冲时显示 |
| 右键菜单 | `ElaMenu` | 视频区域右键菜单 |
| 工具提示 | `ElaToolTip` | 按钮悬停提示 |
| 设置对话框 | `ElaContentDialog` | 偏好设置弹窗 |

---

## 九、参考项目

1. **ElaWidgetTools Example** - 官方示例程序
   - 路径: `3rdparty/elawidgettools/example`
   - 包含所有组件的使用示例

2. **ElaWidgetTools 文档**
   - GitHub Wiki: https://github.com/Liniyous/ElaWidgetTools/wiki

---

## 十、实施建议

### 方案 A: 完全替换 (推荐)
- 完全使用 ElaWidgetTools 重写 UI
- 最佳 Fluent 体验
- 工作量: 中等 (约 2-3 天)

### 方案 B: 渐进式迁移
1. 先替换主窗口为 `ElaWindow`
2. 逐步替换控制栏组件
3. 最后替换播放列表
- 工作量: 较小，可分阶段完成

### 方案 C: 混合模式
- 保留现有核心逻辑
- 仅使用 ElaWidgetTools 的关键组件（如 `ElaSlider`, `ElaIconButton`）
- 工作量: 最小，快速见效

---

*设计方案版本: v1.0*
*设计工具: ElaWidgetTools v1.x*
*目标平台: Windows 10/11, Qt 6.7+*
