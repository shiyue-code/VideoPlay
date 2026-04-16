# VideoPlay Agent Development Guide

This file provides essential information for agents working on this codebase.

**Language**: 中文 (项目文档和注释主要使用中文)  
**Platform**: Windows + MSVC2019 + SDL3  
**License**: GPLv3

## Project Overview

VideoPlay 是一个基于 C++17、SDL3 和 FFmpeg 的现代化视频播放器，支持变速播放、字幕显示和播放列表管理。

### Core Features
- 基于 FFmpeg 的视频解码和播放
- 变速播放支持 (0.25x - 4.0x)
- 字幕解析和显示 (SRT/ASS/VTT 格式)
- 播放列表管理
- 基于 SDL3 的自定义 UI 渲染
- 全屏模式、截图功能
- 音量控制和静音
- 最近文件记录
- 播放前预缓冲，保证首帧音画同步

## Technology Stack

| Component | Version | Purpose |
|-----------|---------|---------|
| C++ | C++17 | 编程语言 |
| CMake | 3.16+ | 构建系统 |
| SDL3 | Latest | 窗口、事件、音频输出 (子模块 `3rdparty/SDL3/`) |
| SDL3_ttf | Latest | 字体渲染 (子模块 `3rdparty/SDL3_ttf/`) |
| FFmpeg | 6.0+ | 视频/音频解码 (avcodec, avformat, avutil, swscale, swresample) |
| nlohmann/json | Latest | JSON 配置持久化 (位于 `3rdparty/nlohmann/`) |

## Build Commands

### 初始化子模块
```bash
git submodule update --init --recursive
```

### Configure
```bash
cmake -B build -G "Visual Studio 16 2019" -A x64 ^
  -DFFmpeg_ROOT="D:/ffmpeg/ffmpeg-master-latest-win64-gpl-shared"
```

### Build
```bash
cmake --build build --config Release --target VideoPlay
```

### Run
```bash
start build/bin/Release/VideoPlay.exe
```

### Post-Build DLL Copy
CMake 已配置自动复制 FFmpeg、SDL3、SDL3_ttf DLL 到输出目录。若缺失，可手动复制：
```powershell
# FFmpeg DLLs
Copy-Item D:/ffmpeg/ffmpeg-master-latest-win64-gpl-shared/bin/*.dll build/bin/Release/
```

### Tests
```bash
cd build
ctest -C Release --output-on-failure
```
**Note**: Tests are currently broken. `tests/CMakeLists.txt` references non-existent `video_core`, `video_plugins` targets and `Qt6::Core` / `Qt6::Test`. Fix by linking against `VideoPlay` target or actual source files instead.

## Code Organization

```
src/
├── main.cpp                     # 入口点
├── app.h/cpp                    # VideoPlayerApp 主应用类 (主循环、播放控制)
├── core/                        # 核心播放引擎
│   ├── ffmpegplayer.h/cpp       # FFmpeg 解码和播放实现
│   ├── audioplayer.h/cpp        # SDL3 音频播放封装
│   ├── settings.h/cpp           # 单例 JSON 配置管理
│   └── common.h                 # 枚举定义、VideoFrame 结构、时间格式化
├── renderer/                    # SDL3 渲染层
│   ├── sdlrenderer.h/cpp        # 统一视频+字幕+UI 渲染器 (完全基于 SDL3)
├── subtitles/                   # 字幕模块
│   ├── subtitleparser.h/cpp     # SRT/ASS/VTT 字幕解析
│   └── subtitleparser.h
└── utils/                       # 工具类
    ├── logger.h/cpp             # 单例文件/控制台日志系统
    └── stb_image.h              # STB 图片加载 (头文件库)

3rdparty/
├── SDL3/                        # SDL3 子模块
├── SDL3_ttf/                    # SDL3_ttf 子模块
└── nlohmann/                    # nlohmann/json 头文件库

cmake/
├── FindFFmpeg.cmake             # FFmpeg 查找模块
└── VideoPlayConfig.cmake.in     # 配置模板

resources/
├── fonts/                       # 字体文件 (NotoSansCJKsc-Regular.otf)
├── icons/                       # 应用图标 (app.ico)
└── subtitles/                   # 示例字幕文件

tests/
├── CMakeLists.txt               # 测试配置 (需修复)
└── test_playbackcontroller.cpp  # Google Test 测试文件
```

## Architecture Details

### 1. 播放流程
```
main.cpp
  └── VideoPlayerApp
        ├── FFmpegPlayer (FFmpeg 解码和播放控制)
        │     ├── decodeLoop() (视频/音频解码线程)
        │     └── AudioPlayer (SDL3 音频输出)
        ├── SDLRenderer (统一视频+字幕+UI 渲染)
        └── SubtitleParser (字幕解析)
```

### 2. FFmpegPlayer 核心设计
- **视频解码**: 使用 `avcodec_send_packet()` / `avcodec_receive_frame()`
- **音频重采样**: 使用 `SwrContext` 转换为 SDL3 音频格式 (F32LE)
- **时钟同步**: 以音频时钟为主时钟 (`m_audioClock`)
- **变速播放**: 通过调整音频重采样和播放速度实现
- **Seek 处理**: 异步 seek 请求，解码线程处理
- **预缓冲**: 首次播放时先启动解码线程，由主循环轮询 `checkPreloadComplete()`，当视频帧队列 ≥1 帧且音频缓冲 ≥40ms 时启动音频；1 秒超时后强制播放

### 3. 字幕显示机制
使用 `SDLRenderer` 统一处理视频和字幕渲染：
- 完全基于 SDL3 (`SDL_Renderer`、`SDL_Texture`、`SDL_RenderGeometry`)
- 先渲染视频帧，再叠加字幕文本
- 字幕样式支持：字体、大小、颜色、描边
- 自动适应窗口大小调整字体和位置

### 4. UI 渲染
- 自定义控件渲染在 `SDLRenderer` 中实现
- 支持底部控制栏、菜单栏、播放列表面板、进度条、音量条
- 所有控件通过鼠标事件坐标命中检测
- 进度条拖动只在释放时触发 seek

## Code Style Guidelines

### Naming Conventions
| Type | Convention | Example |
|------|------------|---------|
| Classes | PascalCase | `VideoPlayerApp`, `FFmpegPlayer`, `SDLRenderer` |
| Methods | camelCase | `loadFile()`, `setPlaybackSpeed()` |
| Variables | camelCase | `filePath`, `playbackSpeed` |
| Member variables | `m_` prefix | `m_engine`, `m_videoStream` |
| Constants | kPascalCase or SCREAMING_SNAKE_CASE | `kMaxVolume`, `DEFAULT_SPEED` |
| Enums | PascalCase values | `PlaybackState::Playing` |

### Formatting
- **Indentation**: 4 spaces (no tabs)
- **Braces**: Allman style (左括号另起一行)
- **Max line length**: ~100 字符
- **Namespace**: 所有代码在 `VideoPlay` 命名空间下

### Include Order
```cpp
#include "own_header.h"     // 项目头文件优先
#include <SystemHeader>       // 系统/第三方库头文件 (尖括号)
#include "other/module.h"     // 其他项目头文件
```

## Critical Gotchas

1. **VideoRenderer Architecture**: 使用统一的 `SDLRenderer` 类同时处理视频帧、字幕和 UI 渲染，完全基于 SDL3。**不依赖 Qt** 或原生窗口控件。

2. **Progress Bar Seek**: 只在鼠标释放 (`handleMouseButtonUp`) 时执行 seek，拖动过程中仅更新 `m_dragProgressRatio` 用于 UI 实时反馈。

3. **Path Handling**: 文件路径使用标准 C++ `std::string` 和 `std::filesystem`，不涉及 Qt 的 `QUrl` 或 `QFileInfo`。

4. **Adding new files**: `CMakeLists.txt` 显式列出所有源文件 (不使用 `file(GLOB)`)。必须在 `SOURCES` 和 `HEADERS` 列表中手动添加 `.h` 和 `.cpp` 文件。

5. **Audio Output**: 使用 SDL3 的 `SDL_AudioStream` / `SDL_AudioDevice` 进行音频输出，**不使用 Qt Multimedia**。

6. **Logging**: 使用 `Logger::instance()` 单例进行文件日志记录，日志位置: `%APPDATA%/VideoPlay/logs/`。

7. **Preload State**: 首次播放时 `FFmpegPlayer::play()` 不会立即启动音频，而是设置 `m_preloading = true`。主循环必须每帧调用 `checkPreloadComplete()` 来结束预缓冲状态。

## Memory Management
- 使用标准 C++ 智能指针 (`std::unique_ptr`) 管理核心对象生命周期
- 显式 `shutdown()` / `stop()` 清理 SDL 和 FFmpeg 资源
- 线程对象需要显式停止和等待 (`join()`)

## Error Handling
- 使用回调函数报告状态变化: `FFmpegPlayer::setStateCallback()`, `setErrorCallback()`
- 使用 `Logger::instance().error()` 记录日志
- 致命错误在 `main.cpp` 中捕获异常并输出到日志和 `stderr`
- FFmpeg 错误码使用 `av_err2str()` 转换

## Debugging
- 构建 Debug 配置用于调试
- 使用 `Logger::instance().debug()` 进行文件日志记录 (`%APPDATA%/VideoPlay/logs/`)
- 日志文件: `videoplay.log`

## Settings System
使用 `Settings` 单例类 (`src/core/settings.h`) 进行持久化配置:
- 窗口位置和大小
- 音量和静音状态
- 播放速度
- 最近文件列表
- 字幕样式 (字体、大小、颜色)
- 播放位置记忆

配置存储在 `%APPDATA%/VideoPlay/VideoPlay.json` (使用 nlohmann/json)

## Testing Strategy
- 测试框架: Google Test (gtest)
- 测试文件位于 `tests/`
- **当前状态**: 测试配置损坏，需要修复 `tests/CMakeLists.txt` 中的目标引用和 Qt 依赖

## Security Considerations
- 文件路径处理使用 C++17 `std::filesystem`，避免缓冲区溢出
- 用户输入通过 SDL 的拖放事件处理，已验证
- FFmpeg 输入通过 `avformat_open_input()` 处理，会自动验证文件格式
- 日志文件可能包含文件路径信息，注意隐私

## Dependencies Installation

### SDL3 + SDL3_ttf
项目使用 Git 子模块管理 SDL3 和 SDL3_ttf：
```bash
git submodule update --init --recursive
```

### FFmpeg
下载 Windows 构建版本:
- https://www.gyan.dev/ffmpeg/builds/ 或
- https://github.com/BtbN/FFmpeg-Builds/releases

解压并设置 `FFmpeg_ROOT` 指向解压目录。

## License
本项目采用 GPLv3 许可证。详见 LICENSE 文件。
