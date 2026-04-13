# VideoPlay 项目迁移说明

## 概述

本项目已从 Qt + ElaWidgetTools 架构迁移到 C++17 + FFmpeg + EUI-NEO 架构。

## 主要变更

### 1. 移除的依赖
- **Qt 6** (Core, Widgets, Gui, OpenGL, OpenGLWidgets, Multimedia)
- **ElaWidgetTools** (Fluent UI 组件库)

### 2. 新增/替换的依赖
- **EUI-NEO**: OpenGL + GLFW 的声明式 2D GUI 框架
- **miniaudio**: 单头文件音频播放库 (替换 QAudioSink)
- **nlohmann/json**: 单头文件 JSON 库 (替换 QSettings)

### 3. 核心模块变更

| 模块 | 原实现 | 新实现 |
|------|--------|--------|
| 线程 | QThread | std::thread |
| 互斥锁 | QMutex | std::mutex |
| 条件变量 | QWaitCondition | std::condition_variable |
| 字符串 | QString | std::string |
| 容器 | QByteArray, QVector | std::vector |
| 队列 | QQueue | std::queue |
| 回调 | Qt 信号槽 | std::function |
| 文件 I/O | QFile | std::fstream |
| 配置存储 | QSettings (INI) | JSON 文件 |

## 文件结构

```
src/
├── main.cpp                     # 应用入口
├── core/                        # 核心播放引擎
│   ├── ffmpegplayer.h/cpp       # FFmpeg 解码 (C++17 版本)
│   ├── audioplayer.h/cpp        # 音频播放 (miniaudio)
│   ├── settings.h/cpp           # 配置管理 (JSON)
│   └── common.h                 # 公共类型和工具函数
├── subtitles/                   # 字幕模块
│   └── subtitleparser.h/cpp     # SRT/ASS/VTT 字幕解析
├── ui/                          # UI 层
│   └── videoplayerapp.h/cpp     # EUI-NEO 主应用
└── utils/                       # 工具类
    └── logger.h/cpp             # 日志系统

3rdparty/
├── nlohmann/json.hpp            # JSON 库
├── miniaudio/miniaudio.h        # 音频库
└── eui/                         # EUI-NEO 框架 (需手动克隆)
```

## 后续步骤

### 1. 克隆 EUI-NEO 框架

```bash
cd 3rdparty
git clone https://github.com/sudoevolve/EUI.git eui
```

### 2. 更新 CMakeLists.txt

根据 EUI-NEO 的实际 CMake 配置，可能需要调整 `CMakeLists.txt` 中的链接目标名称。

### 3. 完善 EUI-NEO 集成

当前 `videoplayerapp.cpp` 中的 EUI API 调用是基于 README 的示例编写的，需要根据实际 API 进行调整：

- 确认 `EUINEO::UIContext` 和 `EUINEO::RectFrame` 的实际定义
- 确认组件构建器的实际方法名
- 实现文件拖放功能
- 实现文件对话框 (Windows: `GetOpenFileName`, Linux: GTK/Qt)

### 4. 构建项目

```bash
# 配置 (调整 FFmpeg 路径)
cmake -B build -G "Visual Studio 16 2019" -A x64 `
  -DFFmpeg_ROOT="D:/ffmpeg/ffmpeg-master-latest-win64-gpl-shared"

# 构建
cmake --build build --config Release

# 运行
./build/bin/Release/VideoPlay.exe
```

## 已知问题

1. **EUI-NEO 集成**: 需要实际克隆 EUI-NEO 代码库并根据实际 API 调整 videoplayerapp.cpp

2. **文件对话框**: 当前未实现，需要使用平台特定 API:
   - Windows: `GetOpenFileName` (Comdlg32.lib)
   - Linux: GTK 文件选择器或调用 `zenity`

3. **拖放功能**: EUI-NEO 需要支持文件拖放事件处理

## 保留的功能

- [x] FFmpeg 视频解码
- [x] 音频播放 (miniaudio)
- [x] 变速播放
- [x] 进度控制 (seek)
- [x] 音量控制
- [x] 静音切换
- [x] 字幕解析 (SRT/ASS/VTT)
- [x] 配置持久化 (JSON)
- [x] 最近文件列表
- [x] 日志系统

## 需要实现的功能

- [ ] 文件对话框 (打开文件)
- [ ] 文件拖放支持
- [ ] 播放列表管理 UI
- [ ] 全屏模式
- [ ] 截图功能
- [ ] 字幕样式设置
