# VideoPlay Agent Development Guide

This file provides essential information for agents working on this codebase.

**Language**: 中文 (项目文档和注释主要使用中文)  
**Platform**: Windows + MSVC2019 + Qt 6.7.3  
**License**: GPLv3

## Project Overview

VideoPlay 是一个基于 C++17、Qt6 和 FFmpeg 的现代化视频播放器，支持变速播放、字幕显示和播放列表管理。

### Core Features
- 基于 FFmpeg 的视频解码和播放
- 变速播放支持 (0.25x - 4.0x)
- 字幕解析和显示 (SRT/ASS/VTT 格式)
- 播放列表管理
- Qlementine 现代化 UI 样式
- 全屏模式、窗口置顶、截图功能
- 音量控制和静音
- 最近文件记录

## Technology Stack

| Component | Version | Purpose |
|-----------|---------|---------|
| C++ | C++17 | 编程语言 |
| CMake | 3.16+ | 构建系统 |
| Qt | 6.7.3 | GUI 框架 (Core, Widgets, Gui, OpenGL, OpenGLWidgets, Multimedia) |
| FFmpeg | 6.0+ | 视频/音频解码 (avcodec, avformat, avutil, swscale, swresample) |
| Qlementine | Latest | 第三方 Qt 样式库 (位于 `3rdparty/qlementine/`) |

## Build Commands

### Configure
```bash
cmake -B build -G "Visual Studio 16 2019" -A x64 \
  -DCMAKE_PREFIX_PATH="D:/Qt/6.7.3/msvc2019_64;D:/ffmpeg/ffmpeg-master-latest-win64-gpl-shared" \
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

### Post-Build DLL Copy (MANDATORY)
CMake automatic DLL copying does NOT work reliably with MSVC. After every build:
```powershell
# Qt DLLs
Copy-Item D:/Qt/6.7.3/msvc2019_64/bin/Qt6*.dll build/bin/Release/
# FFmpeg DLLs (if using)
Copy-Item D:/ffmpeg/ffmpeg-master-latest-win64-gpl-shared/bin/*.dll build/bin/Release/
# Qt Plugins
New-Item -ItemType Directory -Force build/bin/Release/platforms
Copy-Item D:/Qt/6.7.3/msvc2019_64/plugins/platforms/qwindows.dll build/bin/Release/platforms/
```

### Tests
```bash
cd build
ctest -C Release --output-on-failure
```
**Note**: Tests are currently broken (linkage issues). `tests/CMakeLists.txt` references non-existent `video_core` and `video_plugins` targets. Fix by linking against `VideoPlay` target instead.

## Code Organization

```
src/
├── main.cpp                     # 入口点 (设置 QT_MEDIA_BACKEND=windows)
├── core/                        # 核心播放引擎
│   ├── ffmpegplayer.h/cpp       # FFmpeg 解码和播放实现
│   ├── audioplaybackthread.h/cpp # 音频播放线程 (QAudioSink)
│   ├── playerengine.h/cpp       # 播放器引擎包装类
│   ├── settings.h/cpp           # 单例 QSettings 配置管理
│   └── common.h                 # 枚举定义、时间格式化辅助函数
├── ui/                          # 用户界面
│   ├── mainwindow.h/cpp         # 主窗口、菜单、拖放、快捷键
│   ├── videorenderer.h/cpp      # 统一视频+字幕渲染器 (QPainter)
│   ├── controls.h/cpp           # 播放控制栏 (播放/暂停/进度/音量/倍速)
│   └── playlistwidget.h/cpp     # 播放列表管理
├── subtitles/                   # 字幕模块
│   └── subtitleparser.h/cpp     # SRT/ASS/VTT 字幕解析
└── utils/                       # 工具类
    └── logger.h/cpp             # 单例文件/控制台日志系统

3rdparty/qlementine/             # 第三方 Qt 样式库
├── lib/include/oclero/qlementine/
│   ├── style/QlementineStyle.hpp
│   └── widgets/                 # 自定义组件
└── lib/src/                     # 样式实现

cmake/
├── FindFFmpeg.cmake             # FFmpeg 查找模块
└── VideoPlayConfig.cmake.in     # 配置模板

resources/
├── icons/                       # 应用图标
└── subtitles/                   # 示例字幕文件

tests/
├── CMakeLists.txt               # 测试配置 (需修复)
└── test_playbackcontroller.cpp  # Google Test 测试文件
```

## Architecture Details

### 1. 播放流程
```
main.cpp
  └── QApplication
        └── MainWindow
              ├── PlayerEngine (播放控制接口)
              │     └── FFmpegPlayer (FFmpeg 解码实现)
              │           ├── DecodeThread (视频解码线程)
              │           └── AudioPlaybackThread (音频播放线程)
              ├── VideoRenderer (统一视频+字幕渲染)
              ├── Controls (控制栏 UI)
              ├── PlaylistWidget (播放列表)
              └── SubtitleParser (字幕解析)
```

### 2. FFmpegPlayer 核心设计
- **视频解码**: 使用 `avcodec_send_packet()` / `avcodec_receive_frame()`
- **音频重采样**: 使用 `SwrContext` 转换为 Qt 音频格式
- **时钟同步**: 以音频时钟为主时钟 (`m_audioClock`)
- **变速播放**: 通过调整音频重采样和播放速度实现
- **Seek 处理**: 异步 seek 请求，解码线程处理

### 3. 字幕显示机制 (统一渲染方案)
使用 `VideoRenderer` 统一处理视频和字幕渲染：
- 继承 `QWidget`，完全使用 QPainter 渲染
- `paintEvent()` 中先绘制视频帧，再绘制字幕
- 避免了 Windows 原生窗口 Z 序问题
- 字幕样式支持：字体、大小、颜色、描边
- 自动适应窗口大小调整字体

## Code Style Guidelines

### Naming Conventions
| Type | Convention | Example |
|------|------------|---------|
| Classes | PascalCase | `PlayerEngine`, `FFmpegPlayer` |
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
#include <QtHeader>         // Qt 头文件 (尖括号)
#include "other/module.h"   // 其他项目头文件
```

### Signal-Slot 语法
只使用新语法:
```cpp
connect(sender, &Sender::signal, receiver, &Receiver::slot);
```

## Critical Gotchas

1. **Media Backend**: `QT_MEDIA_BACKEND=windows` 在 `main.cpp` 中设置。**不要移除**。

2. **VideoRenderer Architecture**: 使用统一的 `VideoRenderer` 类同时处理视频帧和字幕渲染，完全基于 QPainter，**不使用** `QVideoWidget` 或 `QStackedLayout`，避免 Windows 原生窗口 Z 序问题。

3. **Progress Bar Seek**: 只在 `sliderReleased` 时执行 seek，不要在拖动过程中 seek。使用 `sliderPressed`/`sliderReleased` 在拖动期间阻塞位置更新。

5. **Path Handling**: 文件路径总是使用 `QUrl::fromLocalFile()`。使用 `QFileInfo` 获取绝对路径。

6. **Adding new files**: `CMakeLists.txt` 显式列出所有源文件 (不使用 `file(GLOB)`)。必须在 `SOURCES` 和 `HEADERS` 列表中手动添加 `.h` 和 `.cpp` 文件。

7. **FFmpeg is optional**: 使用 `#ifdef HAS_FFMPEG` 保护 FFmpeg 特定代码。

8. **Qlementine Style**: 应用使用 Qlementine 样式 (`oclero::qlementine::QlementineStyle`)。包含头文件使用 `<oclero/qlementine.hpp>`。

9. **Audio Output**: 使用 `QAudioSink` (Qt6) 而非已弃用的 `QAudioOutput`。

10. **Logging**: 使用 `Logger::instance()` 单例进行文件日志记录，日志位置: `%APPDATA%/VideoPlay/logs/`。

## Memory Management
- 使用 Qt 父子层次结构: 将 `this` 作为父对象传递给子控件/对象
- 不需要手动 `delete` 有父对象的对象
- 线程对象需要显式停止和等待 (`quit()` / `wait()`)

## Error Handling
- 通过信号报告错误: `PlayerEngine::errorOccurred(QString)`
- 使用 `Logger::instance().error()` 记录日志
- 致命错误在 `main.cpp` 中显示 `QMessageBox::critical`
- FFmpeg 错误码使用 `av_err2str()` 转换

## Debugging
- 构建 Debug 配置用于调试
- 使用 `qDebug()` 进行控制台输出
- 使用 `Logger::instance().debug()` 进行文件日志记录 (`%APPDATA%/VideoPlay/logs/`)
- 日志文件: `videoplay.log`

## Settings System
使用 `Settings` 单例类 (`src/core/settings.h`) 进行持久化配置:
- 窗口位置和大小
- 音量和静音状态
- 播放速度
- 最近文件列表
- 字幕样式 (字体、大小、颜色)

配置存储在 `%APPDATA%/VideoPlay/VideoPlay.ini`

## Testing Strategy
- 测试框架: Google Test (gtest)
- 测试文件位于 `tests/`
- **当前状态**: 测试配置损坏，需要修复 `tests/CMakeLists.txt` 中的目标引用

## Security Considerations
- 文件路径处理使用 Qt 的字符串处理，避免缓冲区溢出
- 用户输入通过 Qt 的拖放事件处理，已验证
- FFmpeg 输入通过 `avformat_open_input()` 处理，会自动验证文件格式
- 日志文件可能包含文件路径信息，注意隐私

## Dependencies Installation

### Qt 6.7.3
从 Qt 官网下载在线安装器，安装:
- MSVC 2019 64-bit
- Qt Multimedia 模块

### FFmpeg
下载 Windows 构建版本:
- https://www.gyan.dev/ffmpeg/builds/ 或
- https://github.com/BtbN/FFmpeg-Builds/releases

解压并设置 `FFmpeg_ROOT` 指向解压目录。

## License
本项目采用 GPLv3 许可证。详见 LICENSE 文件。
