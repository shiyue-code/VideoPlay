# VideoPlay 开发指南

> **读者**：开发者、Agent  
> **状态**：现行  
> **更新**：2026-08-23

给改代码的人看。用户功能与快捷键见 [`README.md`](README.md)。未完成功能见 [`docs/ROADMAP.md`](docs/ROADMAP.md)。文档怎么写见 [`docs/README.md`](docs/README.md)。

**语言**：注释和文档用中文。  
**平台**：Windows + MSVC2019+ + SDL3 为主。  
**许可证**：GPLv3。

## 技术栈

| 组件 | 版本 | 用途 |
|------|------|------|
| C++ | C++17 | 语言 |
| CMake | 3.16+ | 构建 |
| SDL3 | 子模块 `3rdparty/SDL3/` | 窗口、事件、音频 |
| SDL3_ttf | 子模块 `3rdparty/SDL3_ttf/` | 字体 |
| FFmpeg | 6.0+（内置 `3rdparty/FFmpeg/`，Git LFS） | 解码、滤镜 |
| nlohmann/json | `3rdparty/nlohmann/` | 配置 |
| GoogleTest | 子模块 `3rdparty/googletest/` | 单测 |
| WinHTTP | 系统库 | AI HTTP（仅 Windows） |
| spdlog | 可选 | 日志后端 |

不使用 Qt。

## 构建

```bash
git lfs pull
git submodule update --init --recursive
cmake -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release --target VideoPlay
ctest --test-dir build -C Release --output-on-failure
```

FFmpeg 默认指向 `3rdparty/FFmpeg/`。输出在 `build/bin/<Config>/`。添加源文件必须改根目录 `CMakeLists.txt` 的 `SOURCES` / `HEADERS`，不要用 `file(GLOB)`。

当前测试 9 个：`tests/test_playbackcontroller_gtest.cpp`、`tests/test_subtitleparser_gtest.cpp`。覆盖倍速映射、时间格式化、音频滤镜预设、SRT/VTT。剧集识别、Settings、ASS 尚无单测。

## 源码结构

```
src/
├── main.cpp
├── app.h / app.cpp              # 初始化、主循环、render、openFile、shutdown
├── app_playback.cpp             # play/pause/seek/volume/speed/AB/书签
├── app_playlist.cpp             # 播放列表、剧集、连播
├── app_events.cpp               # 菜单、字幕、AI、帮助
├── core/
│   ├── ffmpegplayer.h/cpp       # 解码、滤镜、音视频同步、预缓冲
│   ├── audioplayer.h/cpp        # SDL3 音频
│   ├── settings.h/cpp           # JSON 配置单例
│   ├── episodedetector.h/cpp    # 剧集文件名识别
│   └── common.h                 # 枚举、VideoFrame、轨道标签
├── renderer/
│   ├── sdlrenderer.h/cpp        # 构造、纹理、菜单栏
│   ├── sdlrenderer_events.cpp   # 鼠标键盘、命中检测
│   ├── sdlrenderer_menus.cpp    # 菜单数据与点击
│   ├── sdlrenderer_ui.cpp       # 控制栏、进度条、侧栏、书签
│   ├── sdlrenderer_draw.cpp     # 绘图原语
│   ├── sdlrenderer_internal.h   # 拆分文件共享常量
│   ├── menu_manager / ui_manager / dialog_manager
│   ├── settingsdialog / inputfield / custommessagebox
│   └── windowframe*.cpp         # 无边框；Win32 与 Linux 分流编译
├── subtitles/subtitleparser.*   # SRT/ASS/VTT
├── ai/
│   ├── aianalyzer.*             # 分析入口
│   ├── mimo_client / gemini_client
│   ├── transcriber / cache_manager / searchengine
│   ├── httpclient.*             # WinHTTP，无 POSIX 实现
│   └── ai_utils.h
└── utils/
    ├── logger.*                 # Logger::root() / Logger::get(name)
    ├── subprocess_runner.*
    ├── string_utils.h
    └── stb_image*.h
```

`app.cpp` 与 `sdlrenderer.cpp` 已按上表拆分。每个 `.cpp` 自己 `#include`，不依赖翻译单元顺序。拆分文件共享 `sdlrenderer_internal.h`。

## 架构

```
VideoPlayerApp
├── FFmpegPlayer          解码线程 decodeLoop()
│     └── AudioPlayer     SDL_AudioStream
├── SDLRenderer           视频帧 + 字幕 + UI（同一套 SDL 渲染）
├── SubtitleParser
├── AIAnalyzer
└── SearchEngine
```

### FFmpegPlayer

- 视频：`avcodec_send_packet` / `avcodec_receive_frame`
- 音频：`SwrContext` → F32LE；主时钟是音频时钟
- 变速：重采样率与播放速度一起调
- Seek：异步请求，解码线程处理
- 预缓冲：`play()` 设 `m_preloading = true`，主循环每帧调 `checkPreloadComplete()`。视频队列 ≥1 帧且音频缓冲 ≥40ms 才启动音频；超时 1 秒强制开播
- 画面参数走 FFmpeg `eq` / `hue` 滤镜；音频滤镜走 `avfilter`

### 渲染与交互

- 先画视频，再叠字幕（文本或 PGS 位图），再画 UI
- 控件全是坐标命中，没有原生控件
- 进度条只在鼠标释放时 seek，拖动只更新 `m_dragProgressRatio`
- 章节来自 `AVFormatContext::chapters`；进度条书签独立命中 `ControlType::ChapterMarker`
- 绝对 seek 回调编码：`1000.0 + ratio * 1000.0`

### 无边框窗口（Win32）

`WindowFrameWin32` 用原生语义，不要改回自绘拖动或 ShadowWindow：

- `SDL_SetWindowBordered(false)` 后由系统处理拖动、缩放、双击最大化、贴靠
- 代码只在 `WM_NCHITTEST` 返回 `HTCAPTION` / 缩放码 / 系统按钮
- 标题栏命中以渲染层为准：`setCaptionHitTest()` → `SDLRenderer::captionHitTestAt()`
- 系统按钮在非客户区，hover/点击走 `setFrameMouseHandler()` → `handleFrameMouse()`
- `WM_ENTERSIZEMOVE` 里用定时器回调 `renderLiveFrame()`，否则模态循环会卡住画面
- `usesNativeResize() == true` 时渲染层不要再做自绘 resize
- 外侧缩放环 `VideoPlayResizeRing`：不能在 `WM_NCCALCSIZE` 留隐形边框（SDL 假设客户区等于窗口矩形，会越缩越小）。8px 热区是 owned + `WS_EX_LAYERED`(alpha=1) + `SetWindowRgn` 挖空的弹出窗口，把按下转成 `WM_NCLBUTTONDOWN`。**禁止**加 `WS_EX_TRANSPARENT`

Linux 仍是自绘拖动，`usesNativeResize()` 为 false。

## 代码约定

| 类型 | 规则 | 例子 |
|------|------|------|
| 类 | PascalCase | `VideoPlayerApp` |
| 函数 | camelCase | `loadFile()` |
| 变量 | camelCase | `filePath` |
| 成员 | `m_` 前缀 | `m_player` |
| 常量 | `kPascalCase` 或 `SCREAMING_SNAKE_CASE` | `kMaxVolume` |
| 枚举值 | PascalCase | `PlaybackState::Playing` |

- 缩进 4 空格，Allman 括号，行宽约 100
- 命名空间：`VideoPlay`
- 包含顺序：本文件头文件 → 系统/第三方 `<>` → 其它项目头文件
- 可能间接包含 `<windows.h>` 的 `.cpp`，在包含前 `#define NOMINMAX`
- 路径用 `std::string` + `std::filesystem`，含中文路径用 `u8path`
- 日志：`Logger::get("module")`，不要用不存在的 `Logger::instance()`
- 对象用 `unique_ptr`；线程要 `join`；SDL/FFmpeg 走显式 `shutdown()` / `stop()`
- 状态与错误用回调：`setStateCallback`、`setErrorCallback`；FFmpeg 错误用 `av_err2str`

配置单例 `Settings`（`src/core/settings.h`），文件 `%APPDATA%/VideoPlay/VideoPlay.json`。

## 已知约束

1. `HttpClient` 只有 WinHTTP。非 Windows 编 AI 模块会失败。
2. `AudioPlayer` 固定打开默认播放设备，不能切换输出。
3. 播放列表可删除、清空并持久化；拖拽排序尚未实现。
4. 进度条缩略图由 `FFmpegPlayer` 独立预览解码线程生成，不要从主视频队列 `getVideoFrame` 里抽帧。
5. `AIConfig::autoAnalyze` 已写入配置，打开文件时不会自动分析。
6. HTTPS 请求忽略证书错误（`SECURITY_FLAG_IGNORE_*`），不要在新代码里扩大这个范围。
7. 应用内帮助文本仍把 `[` / `]` 同时写成调速和 AB 循环；实际按键以 [`README.md`](README.md) 为准（`.` 调速，`[` `]` 为 AB 循环）。

## 安全

- 路径用 `std::filesystem`，不要手写缓冲区拼接
- 拖放文件由 SDL 事件进入
- 日志可能含本地路径，不要往 issue 里贴完整日志
