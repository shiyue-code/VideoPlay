# 剧集面板与播放列表面板交互设计文档

## 1. 现状分析与问题诊断

### 1.1 当前架构

| 维度 | 播放列表 (Playlist) | 剧集 (Series) |
|------|---------------------|---------------|
| **来源** | 用户手动添加（文件对话框、拖放、命令行） | 自动检测当前文件所在目录 |
| **范围** | 任意文件，可跨目录 | 同一目录下匹配名称模式的媒体文件 |
| **切换方式** | `playNext()` / `playPrevious()`（循环） | `playNextEpisode()` / `playPreviousEpisode()`（不循环） |
| **面板位置** | 窗口右侧 | 窗口左侧 |
| **持久化** | 无 | `Settings::seriesProgress` 保存进度 |
| **默认显示** | `true` | `false`（已修复） |

### 1.2 现存问题

1. **面板生命周期绑定控制栏**：两个侧边面板受 `m_showControls` 控制，鼠标静止 3 秒后强制隐藏。用户无法让面板常驻。
2. **Prev/Next 语义单一**：底部控制栏的 Prev/Next 按钮只操作播放列表。当当前文件属于剧集时，用户期望的是"上一集/下一集"而非"播放列表上一首/下一首"。
3. **快捷键缺失**：Help 文本声明了 `Ctrl+Shift+Left/Right` 用于剧集切换，但代码中未实现（`SDLK_LEFT/RIGHT` 在任何 modifier 下都只触发 seek）。
4. **自动连播策略单一**：播放结束只考虑剧集连播，不考虑播放列表推进。两者缺乏联动。
5. **两个面板虽互斥但缺乏独立意识**：互斥是"补丁式"修复，而非基于产品定位的设计决策。

---

## 2. 设计原则

### 2.1 概念分层

```
用户层
  ├── 播放列表 (Playlist) —— 用户主动构建的"播放队列"
  │       └── 类比：音乐播放器的播放队列
  └── 剧集 (Series) —— 自动识别的"内容系列"
          └── 类比：视频网站的"选集"功能
```

**核心原则**：播放列表和剧集是不同层面的概念，**应该共存而非互斥**。一个播放列表中可以包含多个不同剧集的文件。

### 2.2 交互设计原则

1. **侧边面板拥有独立生命周期**：不受底部控制栏自动隐藏影响，用户可主动决定常驻或关闭。
2. **智能默认**：当检测到剧集时，优先以剧集维度组织导航；没有剧集时回退到播放列表维度。
3. **操作语义就近原则**：底部控制栏的 Prev/Next 应对应当前最相关的导航维度（剧集优先）。
4. **快捷键完备**：所有声明的快捷键都必须有实际实现，避免"文档与代码不一致"。

---

## 3. 面板显示策略

### 3.1 生命周期分离

**修改前**：
```
m_showControls ──→ 控制底部控制栏 + 菜单栏 + 两个侧边面板 + 文件名 + 同步信息
```

**修改后**：
```
m_showControls ──→ 控制底部控制栏 + 菜单栏 + 文件名 + 同步信息（仍随鼠标自动隐藏）
m_showPlaylistPanel ──→ 独立控制播放列表面板（仅用户主动开关）
m_showEpisodePanel ──→ 独立控制剧集面板（仅用户主动开关）
```

**好处**：
- 用户可以把播放列表一直打开，同时享受沉浸式观影（控制栏自动隐藏）
- 两个面板真正独立，打开一个不需要关闭另一个

### 3.2 渲染顺序

```cpp
// 1. 底部控制栏相关（受 m_showControls 控制）
if (m_showControls) {
    drawGradientVignette();
    renderMenuBar();
    renderControls(...);
    renderFilename(...);      // 若面板打开，文件名自动避让
    renderSyncInfo(...);
} else {
    closeAllMenus(false);
}

// 2. 侧边面板（独立生命周期）
if (m_showPlaylistPanel && !playlist.empty()) {
    renderPlaylistPanel(...);
}
if (m_showEpisodePanel && m_episodeData && !m_episodeData->empty()) {
    renderEpisodePanel();
}

// 3. 字幕（始终显示）
if (!subtitle.empty()) {
    renderSubtitle(subtitle);
}
```

### 3.3 小窗口保护

当窗口宽度小于 `860px`（两个面板 + 最小视频区域 `400px`）时：
- 若两个面板同时打开，**自动关闭剧集面板**（播放列表优先级更高，因为用户主动构建）
- 最小视频区域宽度硬限制为 `400px`，确保视频仍可辨认

---

## 4. 交互逻辑设计

### 4.1 打开/关闭方式

| 操作 | 播放列表 | 剧集面板 |
|------|---------|---------|
| **底部按钮** | PlaylistButton（汉堡图标） | 无（避免控制栏拥挤） |
| **菜单栏** | 播放菜单 → "播放列表"（新增） | 剧集菜单 → "切换选集面板"（已有） |
| **快捷键** | `Ctrl+L`（新增） | `Ctrl+E`（已有） |
| **自动打开** | 启动时若有命令行参数或历史文件则打开 | 打开文件检测到剧集时自动打开 |

### 4.2 底部控制栏 Prev/Next 智能语义

**修改前**：
- PrevButton → `playPrevious()`（播放列表循环）
- NextButton → `playNext()`（播放列表循环）

**修改后**：
```cpp
void VideoPlayerApp::playNext() {
    // 优先剧集维度
    if (m_currentSeries) {
        playNextEpisode();
        return;
    }
    // 回退到播放列表维度
    if (m_playlist.empty()) return;
    m_currentIndex++;
    if (m_currentIndex >= m_playlist.size()) {
        m_currentIndex = 0;
    }
    playFromPlaylist(m_currentIndex);
}

void VideoPlayerApp::playPrevious() {
    if (m_currentSeries) {
        playPreviousEpisode();
        return;
    }
    if (m_playlist.empty()) return;
    if (m_currentIndex == 0) {
        m_currentIndex = m_playlist.size() - 1;
    } else {
        m_currentIndex--;
    }
    playFromPlaylist(m_currentIndex);
}
```

**视觉反馈**：当当前文件属于剧集时，Prev/Next 按钮的 tooltip 显示为"上一集"/"下一集"；否则显示"上一个"/"下一个"。

### 4.3 菜单栏重新组织

**播放菜单**（新增播放列表入口）：
```
播放
├── 播放/暂停       Space
├── 停止            S
├── ───────────────
├── 上一个          P        ← 智能语义（剧集优先）
├── 下一个          N        ← 智能语义（剧集优先）
├── ───────────────
├── 上一集          Ctrl+Shift+Left   ← 剧集专属
├── 下一集          Ctrl+Shift+Right  ← 剧集专属
├── ───────────────
├── 播放列表        Ctrl+L   ← 新增
├── ───────────────
├── 增加速度        ]
├── 降低速度        [
├── ───────────────
├── 全屏            F
└── 无边框模式      B
```

**剧集菜单**：
```
剧集
├── 上一集          Ctrl+Shift+Left
├── 下一集          Ctrl+Shift+Right
├── ───────────────
└── 切换选集面板    Ctrl+E
```

### 4.4 键盘快捷键完整映射

| 快捷键 | 功能 | 实现状态 |
|--------|------|---------|
| `P` | 上一个（智能语义） | 已有 |
| `N` | 下一个（智能语义） | 已有 |
| `Ctrl+Shift+Left` | 上一集（剧集专属） | **需实现** |
| `Ctrl+Shift+Right` | 下一集（剧集专属） | **需实现** |
| `Ctrl+E` | 切换选集面板 | 已有 |
| `Ctrl+L` | 切换播放列表 | **需实现** |
| `←` / `→` | seek ±5 秒 | 已有 |

**`Ctrl+Shift+Left/Right` 实现要点**：
在 `sdlrenderer.cpp` 的 `handleEvent` 中，`SDLK_LEFT`/`SDLK_RIGHT` 的处理需要增加 modifier 判断：
```cpp
case SDLK_LEFT:
    if ((event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) == (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) {
        if (m_episodePrevCallback) m_episodePrevCallback();
    } else {
        if (m_seekCallback) m_seekCallback(-5.0);
    }
    break;
case SDLK_RIGHT:
    if ((event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) == (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) {
        if (m_episodeNextCallback) m_episodeNextCallback();
    } else {
        if (m_seekCallback) m_seekCallback(5.0);
    }
    break;
```

---

## 5. 数据流与自动连播设计

### 5.1 播放结束时的决策流程

```
PlaybackState::Stopped
  │
  ├─ m_isManualOperation == true ?
  │     └─ 是 → 重置标志，停止
  │
  ├─ m_currentSeries 存在 ?
  │     ├─ 当前集不是最后一集 ?
  │     │     └─ 是 → playEpisode(nextIndex)  【剧集连播】
  │     └─ 是最后一集 && 播放列表非空 ?
  │           └─ 是 → playNext()  【剧集结束，继续播放列表】
  │
  └─ 播放列表非空 ?
        └─ 是 → playNext()  【播放列表推进】
```

### 5.2 剧集切换时的播放列表联动

**修改前**：`playEpisode(index)` → `openFile(path)` → `addToPlaylist(path)` → 播放列表可能膨胀（虽然 `addToPlaylist` 有去重）

**修改后**：`playEpisode(index)` 应该在切换前更新播放列表当前索引：
```cpp
void VideoPlayerApp::playEpisode(size_t index) {
    if (!m_currentSeries || index >= m_currentSeries->episodes.size()) return;

    saveSeriesProgress();

    std::string path = m_currentSeries->episodes[index].path;

    // 更新播放列表：如果该集已在列表中，将当前索引指向它
    auto it = std::find(m_playlist.begin(), m_playlist.end(), path);
    if (it != m_playlist.end()) {
        m_currentIndex = std::distance(m_playlist.begin(), it);
    } else {
        // 不在列表中，不自动添加（保持播放列表的用户可控性）
        // 或者：m_playlist.push_back(path); m_currentIndex = m_playlist.size() - 1;
    }

    // 临时清空剧集数据防止递归连播
    m_currentSeries = std::nullopt;
    m_renderer->setEpisodeData(nullptr, 0);
    m_isManualOperation = true;
    openFile(path);
}
```

### 5.3 `m_isManualOperation` 标志的语义澄清

| 场景 | m_isManualOperation | 原因 |
|------|---------------------|------|
| 用户点击停止 | `true` | `stop()` 设置 |
| 用户点击剧集面板的某集 | `true` | `playEpisode()` 设置 |
| 用户点击播放列表的某首 | `true` | `playFromPlaylist()` 设置 |
| 用户点击 Prev/Next 按钮 | `true` | `playNext()` / `playPrevious()` 设置 |
| 用户 seek | `true` | `seek()` / `seekTo()` 设置 |
| 正常播放中（每帧 render） | `false` | `render()` 重置，确保播放自然结束时能自动连播 |

**注意**：`playNext()` / `playPrevious()` 目前未设置 `m_isManualOperation = true`，这是一个 bug。需要修复，否则用户点击 Next 后，状态变为 Stopped 时会误判为"自然结束"而再次触发自动连播。

---

## 6. 视觉与体验细节

### 6.1 面板视觉统一

两个面板保持完全一致的视觉风格：
- 宽度 260px，圆角 20px
- 投影 + 白边 + 半透明背景（玻璃拟态）
- 列表项 28px 高，当前项蓝色高亮，hover 灰色高亮
- 字体大小、颜色统一

### 6.2 剧集面板增强

1. **播放进度指示**：在剧集列表中，已播放过的集数显示一个小圆点或进度条（根据 `Settings::lastPosition` 判断）
2. **集数显示格式**：
   - 有季数：`S1E3`、`第 3 集`
   - 无季数：`第 3 集`、`EP3`
3. **当前集标识**：除了蓝色背景高亮，可以在左侧加一个 3px 的竖条指示器

### 6.3 播放列表增强

1. **播放进度记忆**：鼠标悬浮在某项上时，显示该文件的已播放进度百分比
2. **拖拽排序**：未来可支持拖拽调整播放列表顺序
3. **右键菜单**：未来可支持删除、清空播放列表

### 6.4 文件名避让逻辑

已修复：文件名渲染同时避让左右两个面板。

```cpp
int maxWidth = m_windowWidth - 340;
if (m_showEpisodePanel && m_episodeData && !m_episodeData->empty()) {
    maxWidth -= 284; // 左侧剧集面板
}
if (m_showPlaylistPanel) {
    maxWidth -= 284; // 右侧播放列表面板
}
```

---

## 7. 实现步骤（优先级排序）

### Phase 1：面板生命周期分离（高优先级）
1. 修改 `renderUI()`，将 `renderPlaylistPanel()` 和 `renderEpisodePanel()` 移出 `m_showControls` 判断块
2. 修改 `handleEvent()`，Esc 键只关闭菜单，不关闭侧边面板
3. 测试：鼠标静止 3 秒后，控制栏隐藏但面板保持显示

### Phase 2：Prev/Next 智能语义（高优先级）
1. 修改 `VideoPlayerApp::playNext()` 和 `playPrevious()`，优先操作剧集
2. 在 `SDLRenderer` 中增加 `setPrevNextTooltip()` 或类似机制，让按钮 tooltip 随内容变化
3. 测试：打开剧集文件后，Prev/Next 操作剧集；打开非剧集文件后，操作播放列表

### Phase 3：快捷键补齐（中优先级）
1. 在 `sdlrenderer.cpp` 的 `SDLK_LEFT`/`SDLK_RIGHT` 处理中加入 `Ctrl+Shift` modifier 判断
2. 新增 `Ctrl+L` 快捷键用于切换播放列表
3. 更新 `showHelp()` 中的帮助文本，确保文档与代码一致
4. 在菜单中新增"播放列表"入口（ID 分配注意不要冲突）

### Phase 4：自动连播策略完善（中优先级）
1. 修改 `onStateChanged(Stopped)`，增加播放列表推进逻辑
2. 修复 `playNext()` / `playPrevious()` 未设置 `m_isManualOperation = true` 的 bug
3. 修改 `playEpisode()`，切换时同步更新 `m_currentIndex`

### Phase 5：体验增强（低优先级）
1. 剧集列表中显示已播放进度
2. 播放列表悬浮显示进度百分比
3. 小窗口自动关闭面板保护

---

## 8. 附录：接口变更清单

### 新增接口

```cpp
// app.cpp
void togglePlaylistPanel();          // 切换播放列表面板显示

// sdlrenderer.cpp/h
void togglePlaylistPanel();          // 切换播放列表面板（已有 toggleEpisodePanel 的对称接口）
void setPlaylistPanelVisible(bool);  // 程序化控制显示
```

### 修改接口

```cpp
// app.cpp
void playNext();                     // 增加剧集优先逻辑
void playPrevious();                 // 增加剧集优先逻辑
void playEpisode(size_t);            // 增加播放列表索引同步

// sdlrenderer.cpp
void handleEvent(SDL_Event&);        // SDLK_LEFT/RIGHT 增加 modifier 判断
void renderUI(...);                  // 面板移出 m_showControls 控制块
```

### 新增菜单项 ID 分配

当前已用 ID：1-4（文件）、10-17（播放）、20-21（帮助）、30-32（剧集）

建议新增：
- `18`：播放列表（播放菜单）
- `19`：上一集（播放菜单，与剧集菜单 30 功能相同，提供就近入口）
- `20`：下一集（播放菜单，与剧集菜单 31 功能相同）
  - **注意**：20 已被"快捷键"占用，需要重新分配

建议重新梳理菜单 ID，按模块分配段：
- 1-9：文件菜单
- 10-29：播放菜单
- 30-49：剧集菜单
- 50-69：帮助菜单
