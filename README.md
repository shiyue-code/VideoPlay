# VideoPlay - FFmpeg + SDL3 视频播放器

基于 FFmpeg 和 SDL3 开发的跨平台视频播放器，支持变速播放、字幕显示和播放列表管理。

## 技术栈

| 组件 | 用途 |
|-----|------|
| **FFmpeg 6.0+** | 视频/音频解码、解封装 |
| **SDL3 3.4+** | 窗口管理、视频渲染、事件处理 |
| **miniaudio** | 音频播放 |
| **OpenGL** | 硬件加速渲染 (可选) |

## 功能特性

- 🎬 基于 FFmpeg 的视频解码和播放
- ⚡ 变速播放支持 (0.25x - 4.0x)
- 📝 字幕解析和显示 (SRT/ASS/VTT 格式)
- 📁 播放列表管理
- 🖱️ 文件拖放支持
- ⌨️ 键盘快捷键支持
- 🔊 音量控制和静音
- ⛶ 全屏模式
- 🎯 精确 seeking

## 快捷键

| 快捷键 | 功能 |
|-------|------|
| `Space` | 播放/暂停 |
| `F` | 全屏切换 |
| `S` | 停止 |
| `← / →` | 后退/前进 5 秒 |
| `↑ / ↓` | 音量增加/减少 |
| `M` | 静音切换 |
| `.` | 循环切换播放速度 |
| `N` | 下一个文件 |
| `P` | 上一个文件 |
| `Esc` | 退出全屏 |

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
│   ├── app.h/cpp             # 主应用类
│   ├── core/
│   │   ├── ffmpegplayer.h/cpp   # FFmpeg 播放器核心
│   │   ├── audioplayer.h/cpp    # 音频播放 (miniaudio)
│   │   ├── settings.h/cpp       # 配置管理
│   │   └── common.h             # 公共定义
│   ├── renderer/
│   │   └── sdlrenderer.h/cpp    # SDL 渲染器
│   ├── subtitles/
│   │   └── subtitleparser.h/cpp # 字幕解析
│   └── utils/
│       └── logger.h/cpp         # 日志系统
├── 3rdparty/
│   ├── miniaudio/            # miniaudio 音频库
│   ├── SDL3/                 # SDL3 (submodule)
│   └── SDL3_ttf/             # SDL3_ttf (submodule)
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

1. **DLL 依赖**: Windows 运行时需要 FFmpeg 和 SDL3 的 DLL 文件
2. **字幕支持**: 自动加载同名字幕文件 (`.srt`, `.ass`, `.vtt`)
3. **文件拖放**: 支持直接拖放视频文件到窗口播放

## License

GPLv3
