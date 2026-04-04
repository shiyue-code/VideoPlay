# VideoPlay 构建指南

本指南说明如何在 Windows 上构建和运行 VideoPlay 视频播放器。

## 系统要求

- **C++编译器**: Visual Studio 2019/2022 或 MinGW-w64
- **CMake**: 3.16 或更高版本
- **Qt6**: 6.5.0 或更高版本 (Core, Widgets, Gui, Multimedia, Concurrent 模块)
- **FFmpeg**: 6.0 或更高版本 (包含 libavcodec, libavformat, libavutil, libswscale, libswresample)
- **Git** (可选，用于克隆源代码)

## 前置准备

### 1. 安装 Qt6

**方案 A: 使用 Qt 在线安装器 (推荐)**
1. 下载 Qt 在线安装器: https://www.qt.io/download-qt-installer
2. 安装 Qt 6.5.x 或更高版本
3. 确保安装以下组件:
   - Qt 6.5.x → MSVC 2022 64-bit (或 MinGW 64-bit)
   - Qt 6.5.x → 附加组件 → Qt Multimedia

**方案 B: 手动设置路径**
安装完成后，记下 Qt 安装路径，例如:
- MSVC 2022: `C:\Qt\6.5.0\msvc2022_64`
- MinGW: `C:\Qt\6.5.0\mingw_64`

### 2. 安装 FFmpeg

**方案 A: 使用预编译二进制文件**
1. 下载 FFmpeg Windows 构建: https://www.gyan.dev/ffmpeg/builds/
2. 解压到目录，例如 `C:\ffmpeg`
3. 将 `bin` 目录添加到系统 PATH 环境变量

**方案 B: 从源码编译** (高级用户)
```bash
git clone https://git.ffmpeg.org/ffmpeg.git
cd ffmpeg
# 按照 FFmpeg 官方文档编译
```

### 3. 设置环境变量

打开命令提示符或 PowerShell，设置环境变量:

**对于 Qt:**
```bash
# 如果使用 MSVC
set Qt6_DIR=C:\Qt\6.5.0\msvc2022_64\lib\cmake\Qt6

# 或者使用 vcpkg (如果通过 vcpkg 安装)
# set Qt6_DIR=C:\vcpkg\installed\x64-windows\share\qt6
```

**对于 FFmpeg:**
```bash
set FFMPEG_PATH=C:\ffmpeg
set PATH=%FFMPEG_PATH%\bin;%PATH%
```

**永久设置 (推荐):**
- 右键点击"此电脑" → 属性 → 高级系统设置 → 环境变量
- 在"系统变量"中添加上述变量

## 构建步骤

### 方法一: 使用 CMake 命令行 (推荐)

1. **打开开发人员命令提示符**
   - Visual Studio 2022: 搜索"Developer Command Prompt for VS 2022"
   - 或设置好 MSVC 环境变量

2. **克隆或进入项目目录**
```bash
cd F:\WorkSpace\QWorkSpace\VideoPlay
```

3. **创建构建目录**
```bash
mkdir build
cd build
```

4. **配置项目** (选择一种)

**使用 Visual Studio 2022 (MSVC):**
```bash
cmake -G "Visual Studio 17 2022" -A x64 ..
```

**使用 Visual Studio 2019:**
```bash
cmake -G "Visual Studio 16 2019" -A x64 ..
```

**使用 MinGW Makefiles:**
```bash
cmake -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="C:/Qt/6.5.0/mingw_64" ..
```

**指定 Qt 和 FFmpeg 路径:**
```bash
cmake -G "Visual Studio 17 2022" -A x64 ^
  -DQt6_DIR="C:/Qt/6.5.0/msvc2022_64/lib/cmake/Qt6" ^
  -DFFmpeg_DIR="C:/ffmpeg/lib/cmake" ..
```

5. **构建项目**

**Visual Studio 生成器:**
```bash
cmake --build . --config Release
# 或 Debug
cmake --build . --config Debug
```

**MinGW 生成器:**
```bash
cmake --build . -- -j4
```

6. **查找可执行文件**
```bash
# 构建完成后，可执行文件位于:
# build/bin/Release/video_player.exe
# 或 build/bin/Debug/video_player.exe
```

### 方法二: 使用 CMake Presets

项目包含预设配置，可以简化构建过程:

```bash
# 配置 (使用预设)
cmake --preset windows-default

# 构建
cmake --build --preset windows-default
```

如果要构建 Release 版本:
```bash
cmake --preset windows-release
cmake --build --preset windows-release
```

**注意**: 预设假设 Qt 安装在 `C:/Qt/6.5.0/msvc2022_64`，FFmpeg 路径在环境变量中。如果路径不同，需要编辑 `CMakePresets.json` 或手动配置。

### 方法三: 使用 Qt Creator

1. 打开 Qt Creator
2. 文件 → 打开文件或项目 → 选择 `CMakeLists.txt`
3. 选择Kit (例如: Desktop Qt 6.5.0 MSVC 2022 64-bit)
4. 点击"Configure Project"
5. 点击左下角"锤子"图标构建项目
6. 构建完成后，点击"运行"按钮

## 运行软件

构建成功后，需要手动复制 DLL 文件：

1. **复制 Qt DLLs**:
```bash
cp D:/Qt/6.7.3/msvc2019_64/bin/Qt6*.dll build/bin/Release/
```

2. **复制 FFmpeg DLLs**:
```bash
cp D:/ffmpeg/ffmpeg-master-latest-win64-gpl-shared/bin/*.dll build/bin/Release/
```

3. **复制 Qt 插件**:
```bash
mkdir -p build/bin/Release/platforms
cp D:/Qt/6.7.3/msvc2019_64/plugins/platforms/qwindows.dll build/bin/Release/platforms/

mkdir -p build/bin/Release/multimedia
cp D:/Qt/6.7.3/msvc2019_64/plugins/multimedia/*.dll build/bin/Release/multimedia/
```

**注意**: CMake 自动复制 DLL 在 MSVC 生成器上不可靠，需要手动复制。

4. **运行应用**:
```bash
cd build/bin/Release
VideoPlay.exe
```

## 常见问题

### Q1: CMake 找不到 Qt6
**错误**: `Could not find a package configuration file provided by "Qt6"`

**解决**:
- 检查 Qt 是否正确安装
- 手动设置 `Qt6_DIR` 环境变量:
  ```bash
  set Qt6_DIR=C:\Qt\6.5.0\msvc2022_64\lib\cmake\Qt6
  ```

### Q2: CMake 找不到 FFmpeg
**错误**: `Could NOT find FFmpeg (missing: AVCODEC AVFORMAT AVUTIL SWSCALE SWRESAMPLE)`

**解决**:
- 下载 FFmpeg Windows builds
- 设置 `FFMPEG_PATH` 环境变量指向 FFmpeg 根目录
- 或手动指定:
  ```bash
  -DFFmpeg_DIR="C:/ffmpeg/lib/cmake"
  ```

### Q3: 编译错误: 缺少头文件
**解决**:
- 确保 include 路径正确
- 检查 `include/` 目录是否在项目根目录

### Q4: 运行时报错: 找不到插件
**原因**: 插件 DLL 不在正确位置

**解决**:
- 确保 `plugins/` 目录在可执行文件旁边
- 结构应该是:
  ```
  video_player.exe
  plugins/
    decoders/
      decoder_h264.dll
    subtitles/
      subtitle_srt.dll
      subtitle_ass.dll
      subtitle_vtt.dll
  resources/
  ```

### Q5: NMAKE 错误
**错误**: `NMAKE : fatal error U1077: '"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133\bin\Hostx64\x64\cl.EXE"' : return code '0x1'`

**解决**:
- 使用正确的 Visual Studio 开发者命令提示符
- 或使用 `cmake --build . --config Release -- /m` 使用多线程构建

## 验证构建

构建成功后，可以运行以下测试:

1. **打开视频文件**:
   - 文件 → 打开文件
   - 选择 MP4, AVI, MKV 等格式

2. **测试播放控制**:
   - 空格键: 播放/暂停
   - 方向键左右: 快退/快进 5秒
   - 方向键上下: 调整音量
   - F 键: 全屏切换

3. **测试播放速率**:
   - 播放 → 播放速率 → 选择 0.25x 到 4x

4. **测试字幕**:
   - 字幕 → 打开字幕
   - 选择 `resources/subtitles/sample.srt`
   - 字幕应该显示在视频底部

5. **测试格式转换**:
   - 工具 → 转换格式
   - 选择输入文件和输出格式

## 故障排除

### 检查构建日志
如果构建失败，查看详细的 CMake 输出:
```bash
cmake .. 2>&1 | tee configure.log
```

### 启用详细编译
```bash
cmake --build . --config Debug --verbose
```

### 清理重新构建
```bash
cd build
rm -rf *
cmake ..
cmake --build . --config Release
```

### 检查依赖版本
运行以下命令检查 CMake 配置:
```bash
cd build
cmake -L
```

查看输出中的:
- `Qt6_DIR`
- `FFmpeg_INCLUDE_DIRS`
- `FFmpeg_LIBRARIES`

## 开发说明

### 项目结构
```
VideoPlay/
├── CMakeLists.txt          # 主 CMake 配置
├── CMakePresets.json       # 构建预设
├── cmake/                  # CMake 模块 (FindFFmpeg.cmake)
├── include/                # 公共头文件
│   ├── core/              # 核心引擎
│   ├── plugins/           # 插件接口
│   ├── ui/                # 用户界面
│   └── utils/             # 工具类
├── src/                   # 源代码
│   ├── CMakeLists.txt    # 子项目 CMake
│   ├── core/             # 核心实现
│   ├── plugins/          # 插件实现
│   ├── ui/               # UI 实现
│   └── utils/            # 工具实现
├── tests/                # 单元测试
├── resources/            # 资源文件
│   └── subtitles/       # 示例字幕
├── docs/                 # 文档
└── examples/            # 示例代码
```

### 添加新文件
1. 在 `include/` 添加头文件
2. 在 `src/` 添加实现文件
3. 在 `src/CMakeLists.txt` 中添加源文件到相应 target
4. 重新运行 CMake 配置

### 调试
使用 Debug 配置:
```bash
cmake --build . --config Debug
cd bin/Debug
video_player.exe
```

在 Visual Studio 中:
- 打开 `VideoPlay.sln`
- 选择 Debug 配置
- 按 F5 运行调试

## 获取帮助

- 查看文档: `docs/` 目录
- 提交 Issue: https://github.com/yourrepo/VideoPlay/issues
- 查阅 Qt 文档: https://doc.qt.io/
- 查阅 FFmpeg 文档: https://ffmpeg.org/documentation.html

## 许可证

本项目采用 GPLv3 许可证。详见 LICENSE 文件。
