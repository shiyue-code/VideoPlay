# VideoPlay

> **读者**：使用者  
> **状态**：现行  
> **更新**：2026-08-23

基于 C++17、FFmpeg 和 SDL3 的视频播放器。支持变速播放、字幕、播放列表、剧集识别和 AI 视频分析。

开发约定与架构见 [`AGENTS.md`](AGENTS.md)。功能规划见 [`docs/ROADMAP.md`](docs/ROADMAP.md)。文档索引见 [`docs/README.md`](docs/README.md)。

## 功能

### 播放

- FFmpeg 解码；可选硬件解码（D3D11VA / DXVA2 / VAAPI / CUDA / VideoToolbox）
- 变速 0.25x–4.0x
- 循环：不循环 / 单曲 / 列表
- 画面比例：原始 / 16:9 / 4:3 / 铺满窗口
- 亮度、对比度、饱和度、色调、伽马
- Seek：方向键 5 秒，Shift+方向键 30 秒
- AB 循环、章节书签、用户书签
- 播放前预缓冲，首帧音画对齐
- http/https 流媒体与断线重连

### 字幕与音频

- 外挂 SRT / ASS / VTT，打开文件时自动加载同名字幕
- 内封字幕与 PGS 图形字幕
- 多音轨 / 多字幕轨切换
- 字幕同步（G / H）、音频同步（Shift+G / Shift+H）
- 音量、静音、音频输出设备选择
- 音频滤镜：均衡器、语音、低音、夜间、动态归一化、限幅器

### 列表与剧集

- 播放列表（拖放、菜单、Ctrl+L；删除/清空/拖拽排序；重启后恢复队列）
- 进度条悬停缩略图预览
- 同目录剧集自动识别与选集面板（Ctrl+E）
- 播放结束自动连播：优先下一集，否则列表下一项
- 播放进度记忆

### AI

- MiMo / Gemini 视频理解：摘要与章节
- 转录文本与章节全文搜索（Ctrl+F）
- 分析结果本地缓存

### 界面

- 无边框窗口（Windows 使用原生拖动 / 缩放 / 贴靠）
- 全屏、置顶、最大化状态记忆
- 截图（F12，保存到桌面）
- 最近文件（最多 10 条）
- 媒体信息面板（Tab）

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Space` | 播放 / 暂停 |
| `S` | 停止 |
| `F` | 全屏 |
| `Esc` | 退出全屏 / 关闭菜单 |
| `←` / `→` | 后退 / 前进 5 秒 |
| `Shift+←` / `Shift+→` | 后退 / 前进 30 秒 |
| `Ctrl+Shift+←` / `Ctrl+Shift+→` | 上一集 / 下一集 |
| `P` / `N` | 上一个 / 下一个（有剧集时切集） |
| `↑` / `↓` | 音量 |
| `M` | 静音 |
| `.` | 循环切换倍速 |
| `L` | 循环切换循环模式 |
| `A` | 循环切换画面比例 |
| `T` | 窗口置顶 |
| `G` / `H` | 字幕提前 / 延后 0.5 秒 |
| `Shift+G` / `Shift+H` | 音频提前 / 延后 0.5 秒 |
| `[` / `]` / `\` | AB 循环：A 点 / B 点 / 清除 |
| `Ctrl+B` / `Shift+B` | 添加书签 / 清除当前文件书签 |
| `Tab` | 媒体信息 |
| `F12` | 截图 |
| `Ctrl+O` | 打开文件 |
| `Ctrl+L` | 播放列表面板 |
| `Ctrl+E` | 选集面板 |
| `Ctrl+F` | 搜索面板 |
| `F1` | 帮助 |

## 构建

主开发平台是 Windows。Linux / macOS 可编译播放核心，AI HTTP 客户端目前仅实现 WinHTTP。

### 要求

**Windows**

- Visual Studio 2019 或更高
- CMake 3.16+
- Git LFS（内置 FFmpeg 的 DLL / LIB）
- Git 子模块：SDL3、SDL3_ttf、GoogleTest

**Linux (Ubuntu/Debian)**

```bash
sudo apt-get install cmake libavcodec-dev libavformat-dev \
    libavutil-dev libswscale-dev libswresample-dev libavfilter-dev
```

**macOS**

```bash
brew install cmake ffmpeg
```

### 步骤

```bash
git lfs install
git lfs pull
git submodule update --init --recursive

# Windows
cmake -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release

# Linux / macOS（若未使用内置 FFmpeg）
cmake -B build -DFFmpeg_ROOT=/path/to/ffmpeg
cmake --build build --config Release
```

Windows 也可使用仓库内的 Visual Studio 2022 生成器。FFmpeg 默认使用 `3rdparty/FFmpeg/`，可用 `-DFFmpeg_ROOT=...` 覆盖。

```bash
# 运行
build/bin/Release/VideoPlay.exe [视频文件路径]

# 测试
ctest --test-dir build -C Release --output-on-failure
```

CMake 会把 FFmpeg、SDL3、SDL3_ttf 的 DLL 复制到输出目录。

## 发版

GitHub Actions 在 push / pull request 上构建并运行测试。推送 `v*` 标签会打包 `VideoPlay-Windows-x64.zip` 并创建 GitHub Release。

```bash
git tag v2.0.1
git push origin v2.0.1
```

## AI 配置

1. 菜单栏 → AI → AI 设置
2. 选择服务商（MiMo 或 Gemini），填写 API 地址和 Key
3. 打开视频后：AI → AI 分析当前视频

默认服务商为 MiMo，默认模型 `mimo-v2.5`。Gemini 默认模型 `gemini-2.5-flash`。

## 数据位置（Windows）

| 内容 | 路径 |
|------|------|
| 配置 | `%APPDATA%/VideoPlay/VideoPlay.json` |
| 日志 | `%APPDATA%/VideoPlay/logs/videoplay.log` |
| AI 缓存 | `%APPDATA%/VideoPlay/ai_cache/` |

Linux 日志默认在 `~/.local/share/VideoPlay/logs/`。

## License

GPLv3，见 [`LICENSE`](LICENSE)。
