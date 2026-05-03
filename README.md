# VideoPlay - FFmpeg + SDL3 视频播放器

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

### UI 与交互
- 🖱️ 无边框窗口，支持自定义标题栏和拖拽调整大小
- 📷 截图功能（`F12` 保存到桌面）
- 📂 最近文件菜单（最多 10 个，LRU）
- 📋 菜单栏（文件/播放/剧集/帮助）
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

## 构建要求

### Windows

- Visual Studio 2019 或更高版本
- CMake 3.16+
- FFmpeg Windows 构建版
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
git submodule update --init --recursive
cmake -B build -G "Visual Studio 16 2019" -A x64 `
    -DFFmpeg_ROOT="D:/ffmpeg/ffmpeg-master-latest-win64-gpl-shared"

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
│   ├── main.cpp              # 程序入口
│   ├── app.h/cpp             # 主应用类 (播放控制、菜单处理)
│   ├── core/
│   │   ├── ffmpegplayer.h/cpp   # FFmpeg 解码和播放核心
│   │   ├── audioplayer.h/cpp    # SDL3 音频输出封装
│   │   ├── settings.h/cpp       # JSON 配置管理 (单例)
│   │   ├── episodedetector.cpp  # 剧集自动识别
│   │   └── common.h             # 公共定义、枚举、时间格式化
│   ├── renderer/
│   │   ├── sdlrenderer.h/cpp    # SDL3 统一渲染器 (视频+字幕+UI)
│   │   └── windowframe.h/cpp    # 无边框窗口框架 (Win32)
│   ├── subtitles/
│   │   └── subtitleparser.h/cpp # SRT/ASS/VTT 字幕解析
│   └── utils/
│       ├── logger.h/cpp         # 单例日志系统
│       └── stb_image_write.h    # PNG 截图输出
├── 3rdparty/
│   ├── SDL3/                 # SDL3 (submodule)
│   ├── SDL3_ttf/             # SDL3_ttf (submodule)
│   └── nlohmann/             # JSON 库 (头文件)
├── cmake/
│   ├── FindFFmpeg.cmake      # FFmpeg 查找模块
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
└─────────┼─────────────────┼────────────────────────────┘
          │                 │
          ▼                 ▼
┌─────────────────┐  ┌─────────────────┐
│     SDL3        │  │     FFmpeg      │
│  Window/Render  │  │  Decode/Resample│
└─────────────────┘  └─────────────────┘
```

## 注意事项

1. **DLL 依赖**: Windows 运行时需要 FFmpeg 和 SDL3 的 DLL 文件，CMake 已配置自动复制
2. **字幕支持**: 自动加载同名字幕文件 (`.srt`, `.ass`, `.vtt`)
3. **文件拖放**: 支持直接拖放视频文件到窗口播放
4. **配置存储**: 设置保存在 `%APPDATA%/VideoPlay/VideoPlay.json` (Windows)
5. **日志文件**: 日志保存在 `%APPDATA%/VideoPlay/logs/videoplay.log`

## License

GPLv3
