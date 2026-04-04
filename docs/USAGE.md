# VideoPlay 使用指南

## 项目介绍
VideoPlay是一个基于C++、CMake、Qt和clangd的现代化视频播放器，支持插件化架构，可实现不同倍率播放和视频格式转换。

## 技术栈
- **C++**: C++17标准
- **CMake**: 3.16+构建系统
- **Qt**: 6.2+ GUI框架
- **FFmpeg**: 编解码库
- **clangd**: 静态分析和代码补全
- **Git**: 版本控制

## 环境要求

### Windows
1. Visual Studio 2022 (MSVC编译器)
2. Qt 6.5+ (Open Source版本)
3. FFmpeg开发库
4. CMake 3.16+
5. Git

### Linux
1. GCC 9+ 或 Clang 10+
2. Qt 6.2+ 
3. FFmpeg开发库 (libavcodec-dev, libavformat-dev, libavutil-dev, libswscale-dev, libswresample-dev)
4. CMake 3.16+
5. Git

### macOS
1. Xcode 13+
2. Qt 6.2+
3. FFmpeg (通过brew install ffmpeg)
4. CMake 3.16+
5. Git

## 依赖安装

### Windows
```bash
# 1. 安装Qt (从官网下载在线安装器)
# 2. 下载FFmpeg (https://ffmpeg.org/download.html)
#    提取到 C:/ffmpeg
# 3. 设置环境变量:
#    - FFMPEG_PATH = C:/ffmpeg
```

### Ubuntu/Debian
```bash
sudo apt update
sudo apt install build-essential cmake git \
    qt6-base-dev qt6-tools-dev libqt6core6 \
    libavcodec-dev libavformat-dev libavutil-dev \
    libswscale-dev libswresample-dev
```

### macOS
```bash
brew install cmake qt ffmpeg
```

## 构建步骤

### 1. 克隆代码
```bash
cd F:/WorkSpace/QWorkSpace
git clone <repo_url> VideoPlay
cd VideoPlay
```

### 2. 创建构建目录
```bash
mkdir build
cd build
```

### 3. 配置项目
```bash
cmake .. -DCMAKE_PREFIX_PATH="C:/Qt/6.5.0/msvc2022_64"
```

### 4. 构建项目
```bash
cmake --build . --config Release
# 或 Debug 版本
cmake --build . --config Debug
```

### 5. 运行程序
```bash
cd bin
video_player.exe
```

## 开发环境配置

### VSCode + clangd
1. 安装VSCode
2. 安装C/C++扩展和clangd扩展
3. 配置`.clangd`文件(已在项目中)
4. 打开项目文件夹

### CLion
1. 打开项目文件夹
2. CLion会自动检测CMake配置
3. 设置编译器为Clang++或MSVC

### Qt Creator
1. 打开CMakeLists.txt
2. 配置Qt版本为Qt 6.x
3. 设置构建目录

## 项目结构

```
VideoPlay/
├── src/                    # 源代码
│   ├── core/              # 核心引擎
│   │   ├── playerengine.h/cpp
│   │   ├── mediaplayer.h/cpp
│   │   ├── videorenderer.h/cpp
│   │   └── ...
│   ├── plugins/           # 插件系统
│   │   ├── plugininterface.h
│   │   ├── pluginloader.h/cpp
│   │   ├── decoderinterface.h
│   │   ├── encoderinterface.h
│   │   ├── decoders/
│   │   │   └── h264decoder.h/cpp
│   │   ├── encoders/
│   │   │   └── h264encoder.h/cpp
│   │   └── converters/
│   │       └── formatconverter.h/cpp
│   ├── ui/                # 用户界面
│   │   ├── mainwindow.h/cpp
│   │   ├── videowidget.h/cpp
│   │   ├── controls.h/cpp
│   │   └── convertdialog.h/cpp
│   └── utils/             # 工具类
│       ├── logger.h/cpp
│       ├── settings.h/cpp
│       ├── threadpool.h/cpp
│       └── fileutils.h
├── include/               # 头文件
├── tests/                 # 单元测试
├── docs/                  # 文档
├── cmake/                 # CMake模块
│   └── FindFFmpeg.cmake
├── CMakeLists.txt
├── CMakePresets.json
├── .clangd
├── .gitignore
└── README.md
```

## 功能说明

### 1. 倍率播放
播放速率支持: 0.25x, 0.5x, 1x, 1.5x, 2x, 4x
- 使用`PlayerEngine::setPlaybackRate()`实现
- 自动音频音调调整
- 支持帧插值算法

### 2. 视频格式转换
支持的格式: MP4, AVI, MKV, MOV, FLV, WebM
支持的编解码器: H.264, H.265, VP9, AV1, AAC, MP3, OPUS
- 使用`ConvertDialog`界面
- 支持批量转换
- 实时进度显示

### 3. 插件系统
插件必须实现统一的接口:
- `IDecoder` - 解码器接口
- `IEncoder` - 编码器接口
- `FormatConverter` - 格式转换接口

插件类必须包含:
```cpp
Q_PLUGIN_METADATA(IID ".../1.0" FILE "plugin.json")
Q_INTERFACES(PluginInterface IDecoder)
```

## 创建插件

### 1. 创建解码器插件

示例代码参考 `src/plugins/decoders/h264decorder.cpp`

```cpp
class MyDecoder : public IDecoder {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.videoplay.Decoder/1.0" FILE "mydecoder.json")
    Q_INTERFACES(VideoPlay::PluginInterface VideoPlay::IDecoder)
    
public:
    PluginInfo getInfo() const override;
    bool initialize() override;
    bool initialize(const DecoderConfig& config) override;
    bool decode(DecoderPacket& packet, AVFrame* frame) override;
    // ... 接口实现
};
```

### 2. 创建编码器插件

示例代码参考 `src/plugins/encoders/h264encoder.cpp`

### 3. 插件配置文件

创建 `plugin.json`:
```json
{
    "IID": "org.videoplay.Decoder/1.0",
    "Name": "My Decoder",
    "Version": "1.0.0",
    "Description": "Custom decoder plugin",
    "Author": "Your Name"
}
```

### 4. 编译插件
插件被编译为动态库:
- Windows: `.dll`
- Linux: `.so`
- macOS: `.dylib`

编译后的插件自动复制到 `build/bin/plugins/decoders/` 等目录

## 开发调试

### 日志系统
项目使用内置日志系统:
```cpp
#include "utils/logger.h"

Logger::debug("Debug message");
Logger::info("Info message");
Logger::warning("Warning message");
Logger::error("Error message");
```

日志文件位置:
- Windows: `%APPDATA%/VideoPlay/logs/`
- Linux: `~/.local/share/VideoPlay/logs/`
- macOS: `~/Library/Logs/VideoPlay/`

### 设置管理
使用`Settings`类存储配置:
```cpp
Settings settings;
settings.setValue("Playback/DefaultVolume", 0.8);
double volume = settings.value("Playback/DefaultVolume", 1.0).toDouble();
```

## 测试

### 运行单元测试
```bash
cd build
ctest -C Debug --output-on-failure
```

或直接运行测试程序:
```bash
./tests/test_core
./tests/test_plugins
./tests/test_ui
```

### 测试覆盖
```bash
# 启用覆盖率编译
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
make coverage
```

## 故障排除

### 1. 找不到Qt
确保设置`CMAKE_PREFIX_PATH`指向Qt安装目录

### 2. 找不到FFmpeg
安装FFmpeg并设置`FFMPEG_PATH`环境变量，或使用`cmake/FindFFmpeg.cmake`

### 3. 编译错误:缺少头文件
检查`include`目录是否存在，清理并重新构建:
```bash
rm -rf build
mkdir build && cd build
cmake ..
cmake --build .
```

### 4. 运行时找不到插件
确保插件被复制到正确的目录:
```
build/bin/plugins/decoders/
build/bin/plugins/encoders/
build/bin/plugins/converters/
```

## 贡献
1. Fork本仓库
2. 创建特性分支
3. 提交更改
4. 推送到分支
5. 创建Pull Request

## 许可证
GPLv3 - 详见LICENSE文件

## 联系方式
- Issues: GitHub Issues
- Email: support@videoplay.org