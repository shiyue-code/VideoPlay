# VideoPlay - FFmpeg + SDL3 视频播放器

> **🤖 AI 编程项目** — 本项目由 AI 辅助开发完成，从架构设计到功能实现均有人工智能参与协作。

基于 FFmpeg 和 SDL3 开发的现代化视频播放器，支持变速播放、字幕显示、播放列表管理和剧集自动识别。

## 技术栈

| 组件 | 用途 |
|-----|------|
| **FFmpeg 6.0+** | 视频/音频解码、解封装 |
| **SDL3 3.4+** | 窗口管理、视频渲染、事件处理 |
| **SDL3 Audio** | 音频播放 |
| **SDL3_ttf** | 字体渲染 |
| **OpenGL** | 硬件加速渲染 (可选) |
| **nlohmann/json** | 配置持久化 |
| **WinHTTP** | AI API 通信 (Windows) |

## 功能特性

### 核心播放
- 🎬 基于 FFmpeg 的视频解码和播放
- ⚡ 变速播放支持 (0.25x - 4.0x)
- 🔄 循环播放模式（单曲循环 / 列表循环 / 不循环）
- 📐 画面比例调整（原始 / 16:9 / 4:3 / 铺满窗口）
- 🎯 精确 seeking，支持 5 秒/30 秒步进

### 字幕与音频
- 📝 字幕解析和显示（SRT/ASS/VTT 格式）
- 🔊 音量控制和静音

### 播放列表与剧集
- 📁 播放列表管理，支持文件拖放
- 📺 剧集自动识别与选集面板
- 📌 播放进度记忆与恢复
- ⏭️ 自动连播下一集/下一个文件
- 🔖 章节支持：MKV/MP4 章节自动解析，进度条书签标记
- 🔁 AB 循环播放（`[` 设置 A 点、`]` 设置 B 点、`\` 清除）

### AI 智能分析
- 🤖 MiMo 视频理解分析（自动生成摘要和章节）
- 🔍 全文搜索（支持转录文本和章节内容）
- 💾 分析结果缓存（避免重复调用 API）
- ⚙️ 可配置 API 地址和模型（支持 MiMo v2-pro / v2.5-pro）

### UI 与交互
- 🖱️ 无边框窗口，支持自定义标题栏和拖拽调整大小
- 📷 截图功能（`F12` 保存到桌面）
- 📂 最近文件菜单（最多 10 个，LRU）
- 📋 菜单栏（文件/播放/章节/AI/剧集/帮助）
- 🖱️ 右键上下文菜单（播放控制、AB 循环、AI 分析）
- ⛶ 全屏模式
- 📌 窗口置顶
- 💾 最大化状态记忆

## 快捷键

| 快捷键 | 功能 |
|-------|------|
| `Space` | 播放/暂停 |
| `F` | 全屏切换 |
| `S` | 停止 |
| `← / →` | 后退/前进 5 秒 |
| `Shift + ← / →` | 后退/前进 30 秒 |
| `Ctrl+Shift + ← / →` | 上一集/下一集 |
| `↑ / ↓` | 音量增加/减少 |
| `M` | 静音切换 |
| `.` | 循环切换播放速度 |
| `L` | 循环切换播放模式 |
| `A` | 循环切换画面比例 |
| `T` | 窗口置顶切换 |
| `N` | 下一个文件 |
| `P` | 上一个文件 |
| `F12` | 截图 |
| `Esc` | 退出全屏 / 关闭菜单 |
| `Ctrl+O` | 打开文件 |
| `Ctrl+L` | 切换播放列表面板 |
| `Ctrl+E` | 切换选集面板 |
| `[` | AB 循环：设置 A 点 |
| `]` | AB 循环：设置 B 点 |
| `\` | AB 循环：清除 |

## 构建要求

### Windows

- Visual Studio 2019 或更高版本
- CMake 3.16+
- FFmpeg Windows 构建版（已内置于 `3rdparty/FFmpeg/`）
- Git LFS（用于拉取内置 FFmpeg 的 DLL/EXE/LIB）
- Git (用于拉取 SDL3 submodule)

### macOS

```bash
brew install cmake ffmpeg
# 然后拉取 SDL3 submodule
git submodule update --init --recursive
```

### Linux (Ubuntu/Debian)

```bash
sudo apt-get install cmake ffmpeg libavcodec-dev libavformat-dev \
    libavutil-dev libswscale-dev libswresample-dev
# 然后拉取 SDL3 submodule
git submodule update --init --recursive
```

## 构建步骤

### 1. 配置

```bash
# Windows (PowerShell)
git lfs install
git lfs pull
git submodule update --init --recursive
cmake -B build -G "Visual Studio 16 2019" -A x64

# macOS/Linux
git submodule update --init --recursive
cmake -B build -DFFmpeg_ROOT=/path/to/ffmpeg
```

### 2. 编译

```bash
cmake --build build --config Release
```

### 3. 运行

```bash
./build/bin/Release/VideoPlay.exe [视频文件路径]
```

## 项目结构

```
VideoPlay/
├── src/
│   ├── main.cpp                   # 程序入口
│   ├── app.h                      # 主应用类声明
│   ├── app.cpp                    # 核心: 初始化、主循环、渲染调度
│   ├── app_playback.cpp           # 播放控制: play/pause/seek/volume/speed
│   ├── app_playlist.cpp           # 播放列表、剧集识别、自动连播
│   ├── app_events.cpp             # 事件与菜单回调处理
│   ├── core/
│   │   ├── ffmpegplayer.h/cpp     # FFmpeg 解码和播放核心
│   │   ├── audioplayer.h/cpp      # SDL3 音频输出封装
│   │   ├── settings.h/cpp         # JSON 配置管理 (单例)
│   │   ├── episodedetector.h/cpp  # 剧集自动识别
│   │   └── common.h               # 公共定义、枚举、时间格式化
│   ├── renderer/
│   │   ├── sdlrenderer.h          # 统一渲染器接口
│   │   ├── sdlrenderer.cpp        # 核心: 构造/初始化/视频纹理
│   │   ├── sdlrenderer_events.cpp # 事件处理: 鼠标/键盘/命中检测
│   │   ├── sdlrenderer_menus.cpp  # 菜单逻辑与动画
│   │   ├── sdlrenderer_ui.cpp     # UI 渲染: 控制栏/进度条/书签
│   │   ├── sdlrenderer_draw.cpp   # 底层绘图原语
│   │   ├── sdlrenderer_internal.h # 共享常量
│   │   ├── windowframe.h/cpp      # 无边框窗口框架
│   │   ├── windowframe_win32.cpp  # Win32 实现
│   │   └── windowframe_linux.cpp  # Linux 实现
│   ├── subtitles/
│   │   └── subtitleparser.h/cpp   # SRT/ASS/VTT 字幕解析
│   ├── ai/
│   │   ├── aianalyzer.h/cpp       # AI 视频分析（MiMo 集成）
│   │   ├── httpclient.h/cpp       # HTTP 客户端（WinHTTP）
│   │   └── searchengine.h/cpp     # 全文搜索引擎
│   └── utils/
│       ├── logger.h/cpp           # 单例日志系统
│       ├── stb_image.h            # 图片加载
│       └── stb_image_write.h      # PNG 截图输出
├── 3rdparty/
│   ├── SDL3/                      # SDL3 (submodule)
│   ├── SDL3_ttf/                  # SDL3_ttf (submodule)
│   └── nlohmann/                  # JSON 库 (头文件)
├── cmake/
│   ├── FindFFmpeg.cmake           # FFmpeg 查找模块
│   └── VideoPlayConfig.cmake.in
├── tests/
│   └── test_playbackcontroller.cpp
├── CMakeLists.txt
└── README.md
```

## 架构说明

```
┌─────────────────────────────────────────────────────────┐
│                      VideoPlayerApp                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │  SDLRenderer │  │ FFmpegPlayer │  │   Playlist   │  │
│  │  (UI/Render) │  │ (Decode)     │  │   Manager    │  │
│  └──────┬───────┘  └──────┬───────┘  └──────────────┘  │
│         │                 │                             │
│  ┌──────┴───────┐  ┌──────┴───────┐                    │
│  │  AIAnalyzer  │  │ SearchEngine │                    │
│  │  (MiMo API)  │  │  (全文搜索)  │                    │
│  └──────────────┘  └──────────────┘                    │
└─────────────────────────────────────────────────────────┘
```

## AI 配置

1. 菜单栏 → AI → AI 设置
2. 输入 API 地址和 API Key
3. 选择模型（默认 `mimo-v2-pro`）
4. 打开视频后，点击 AI → AI 分析当前视频

支持的模型：
- `mimo-v2-pro` - MiMo v2 Pro 视频理解
- `mimo-v2.5-pro` - MiMo v2.5 Pro（最新）

## 注意事项

1. **DLL 依赖**: Windows 运行时需要 FFmpeg 和 SDL3 的 DLL 文件，CMake 已配置自动复制
2. **字幕支持**: 自动加载同名字幕文件 (`.srt`, `.ass`, `.vtt`)
3. **文件拖放**: 支持直接拖放视频文件到窗口播放
4. **配置存储**: 设置保存在 `%APPDATA%/VideoPlay/VideoPlay.json` (Windows)
5. **日志文件**: 日志保存在 `%APPDATA%/VideoPlay/logs/videoplay.log`
6. **AI 缓存**: AI 分析结果缓存在 `%APPDATA%/VideoPlay/ai_cache/`
7. **Debug 模式**: 使用 Release 构建可获得最佳性能

## License

GPLv3
