# VideoPlay 开发指南

> **读者**：开发者、Agent  
> **状态**：现行  
> **更新**：2026-10-13

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

生成器用 VS 2019 或更新版本均可，本机实测 `Visual Studio 18 2026` 可用。

FFmpeg 默认指向 `3rdparty/FFmpeg/`。输出在 `build/bin/<Config>/`。添加源文件必须改根目录 `CMakeLists.txt` 的 `SOURCES` / `HEADERS`，不要用 `file(GLOB)`。

当前测试 16 个 gtest 目标：`test_playbackcontroller`、`test_subtitleparser`、`test_menuitem`、`test_fileassoc`、`test_audioclock`、`test_settings_maintenance`、`test_frame_image`、`test_string_utils`、`test_common`、`test_ai_utils`、`test_searchengine`、`test_ffmpegplayer_playback`、`test_settings`、`test_episodedetector`、`test_videoadjuster`、`test_m3uplaylist`。覆盖倍速映射、时间格式化、音频滤镜预设、SRT/VTT/ASS、菜单项、AudioClock、配置清理、帧变换/PNG、字符串工具（含括号/HMS 时间戳解析）、轨道标签/语言名/滤镜默认值等 common.h 内联助手、AI provider 归一化与 JSON 提示词/应答解析、检索引擎（转录/字幕/章节倒排索引）、Settings 读写/容错/AI 配置归一化、剧集识别、画面调节（eq/hue + BGRA）、文件关联默认值接管决策与扩展名列表约定。`test_ffmpegplayer_playback` 仅 Windows：用内置 `3rdparty/FFmpeg/bin/ffmpeg.exe` 现场生成媒体素材，走 `SDL_AUDIO_DRIVER=dummy` 真实时间消耗，整轮约 15 秒。`test_settings` 通过 `::testing::Environment` 把 `APPDATA`/`HOME` 重定向到临时目录后才初始化 `Settings` 单例，重定向失败直接 `abort`，不会读写真实配置。

## 源码结构

```
src/
├── main.cpp
├── app.h / app.cpp              # 初始化、主循环、render、openFile、shutdown
├── app_playback.cpp             # play/pause/seek/volume/speed/AB/书签
├── app_playlist.cpp             # 播放列表、剧集、连播
├── app_events.cpp               # 菜单、字幕、AI、帮助
├── core/
│   ├── ffmpegplayer.h/cpp       # 解码、音视频同步、预缓冲
│   ├── ffmpegplayer_filters.cpp # HDR/去隔行/音频滤镜链
│   ├── ffmpegplayer_tracks.cpp  # 轨道扫描/切换、字幕流
│   ├── ffmpegplayer_preview.cpp # 进度条缩略图预览解码
│   ├── ffmpegplayer_internal.h  # 拆分文件共享的内部助手
│   ├── videoadjuster.h/cpp      # 画面调节（eq/hue）+ BGRA 转换
│   ├── audioclock.h/cpp         # 音频媒体时钟（按入队采样推算位置）
│   ├── audioplayer.h/cpp        # SDL3 音频
│   ├── settings.h/cpp           # JSON 配置单例
│   ├── settings_maintenance.*   # 配置中失效本地文件条目的清理
│   ├── episodedetector.h/cpp    # 剧集文件名识别
│   ├── m3uplaylist.h/cpp        # M3U/M3U8 清单解析（EXTINF 标题、HLS 识别、URI 解析）
│   ├── fileassoc.h/cpp          # HKCU 文件关联（仅 Windows 实装，其它平台 no-op）
│   └── common.h                 # 枚举、VideoFrame、轨道标签、mediaExtensions()
├── renderer/
│   ├── sdlrenderer.h/cpp        # 构造、纹理、菜单栏
│   ├── sdlrenderer_events.cpp   # 鼠标键盘、命中检测
│   ├── sdlrenderer_menus.cpp    # 菜单数据与点击
│   ├── sdlrenderer_ui.cpp       # 控制栏、进度条、侧栏、书签
│   ├── sdlrenderer_draw.cpp     # 绘图原语
│   ├── sdlrenderer_internal.h   # 拆分文件共享常量
│   ├── menu_manager / ui_manager / dialog_manager
│   ├── modaldialog.*            # 无边框模态对话框基类
│   ├── textutils.h              # 独立窗口共享的文本绘制/测量
│   ├── settingsdialog / subtitlestyledialog / inputfield / inputdialog
│   ├── ui_style.h / ui_draw.*   # 信息面板样式常量与通用绘制原语
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
    ├── frame_image.*            # 帧裁剪/翻转/旋转与 PNG 写出（截图用）
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
- 变速：音频滤镜链末尾追加 `atempo`（保持音高），速度 <0.5 时拆成多级串联
- 声道映射（单声道/左右交换）不经滤镜图：`resampleAudioFrame` 尾部对交错 F32 采样就地处理（`mapAudioChannels`），滤镜与直通路径共用
- 音频时钟不是挂钟：`AudioClock` 记录每段入队采样的媒体 pts 与单帧媒体时长，按 SDL 未取走的帧数推算当前媒体位置
- 时间戳统一经 `streamPtsToMs` 以容器 `start_time` 为基准（视频/音频/内封字幕）
- Seek：异步请求，解码线程处理；解码出的目标位置之前的音频/视频帧会被裁剪
- 预缓冲：`play()` 设 `m_preloading = true`，主循环每帧调 `checkPreloadComplete()`。视频队列 ≥1 帧且音频缓冲 ≥40ms 才启动音频；超时 1 秒强制开播
- 背压：音视频队列满时解码线程阻塞等待（暂停态补解码例外，音频满仍丢包）
- EOF：先给解码器 send NULL 收完尾帧，再等视频队列与音频缓冲排空才置 Stopped；排空期间 abort 直接退出、不再发 Stopped 回调
- 视频画面处理分两阶段：阶段 1 在解码线程完成硬解转存、HDR 色调映射、去隔行（`bwdif` / `yadif`）；阶段 2 由 `VideoAdjuster` 做 `eq` / `hue` 调节再转 BGRA，解码线程与主线程各持一个实例（实例非线程安全）
- 每个 `VideoFrame` 持有阶段 1 基础帧 `base` 与 `adjustGeneration`；主线程每次渲染前调 `refreshVideoFrame`，代次过时就拿 `base` 就地重算 `data`，画面调节在拖动中与暂停时立即生效。去隔行/HDR 改动仍只作用于新解码帧；代价是视频队列同时保留基础帧，内存随之增加
- 画面调节配置写盘有 500ms 防抖（拖动滑杆连续触发回调），退出时若仍有 pending 会补落盘
- 音频滤镜走 `avfilter`；FFmpeg 8 下 `abuffer`/`abuffersink` 必须 `avfilter_graph_alloc_filter` → `av_buffersrc_parameters_set` → `avfilter_init_dict`，不能直接用 `avfilter_graph_create_filter`

### 渲染与交互

- 先画视频，再叠字幕（文本或 PGS 位图），再画 UI
- 控件全是坐标命中，没有原生控件
- 视频空白区域左键双击切全屏：`handleMouseButtonDown` 在菜单栏/打开菜单/信息面板拦截之后、控件分发之前判断 `clicks >= 2 && m_pressedControl == None`，并按几何排除底部控制栏（栏内控件间隙同样返回 None，不触发）
- 进度条只在鼠标释放时 seek，拖动只更新 `m_dragProgressRatio`
- 画面旋转/翻转/中心裁剪在 `SDLRenderer::renderFrame` 用 `SDL_RenderTextureRotated` 完成，不要改成 FFmpeg `transpose`（硬解路径过不去）
- 渲染已开 vsync（`SDL_SetRenderVSync`）；vsync 生效时主循环最小帧间隔降到 4ms，只在 present 未阻塞（最小化/遮挡）时补睡 `SDL_DelayPrecise`，否则保持 16ms
- 截图走 `VideoPlayerApp::takeScreenshot`：对 `m_displayFrame` 按与渲染一致的「裁剪 → 翻转 → 顺时针旋转」顺序变换后写 PNG（依 `SDL_RenderTextureRotated` 语义，先翻转后旋转），不含 UI/OSD
- GIF 导出走 `VideoPlayerApp::startGifExport`：AB 区间优先，否则当前位置+输入秒数（≤60s）；后台线程跑 `SubprocessRunner` 调 exe 同目录 `ffmpeg.exe`（palettegen/paletteuse 单遍调色板，fps=12、宽 480），结果经 `m_gifExport` 共享状态在 render() 消费，detached 线程不触碰 this
- 字幕样式取自 `setSubtitleStyle` 缓存的 `SubtitleStyle`（字号/颜色/描边生效，`fontFamily` 暂不支持）；有描边时不画背景框、按偏移叠加描边层。设置 UI 为文件菜单「字幕样式...」的 `SubtitleStyleDialog`，保存即写盘并即时生效
- 消息型弹窗统一用窗口内信息面板（`renderInfoPanel`，媒体信息/关于/帮助/消息共用）：模态遮罩，点背板/Esc/Enter/右上角 × 关闭；命中在 `getControlAt` 分发前拦截，只有 `InfoPanelClose`/`InfoPanelLink` 可悬停。`showMessageBox`/`showInfoPanel` 仅主线程调用；Custom 面板显示期间新消息入队（≤8，丢弃最旧），关闭后依次显示；内容超高时滚动（滚轮接管，标题与 × 不滚动）；正文里的 `[mm:ss]` 高亮可点击。轻量提示走 `showOSD`（如「请先打开一个视频文件」「AI 缓存已清除」「截图已保存」）。CustomMessageBox 独立窗口已删除
- 输入型弹窗（`InputDialog`/`SettingsDialog`/`SubtitleStyleDialog`/`InputField`，`ModalDialog` 子类）仍是独立窗口模态，但视觉与信息面板一致：UiStyle 提供全部配色/尺寸，`UiDraw` 统一绘制圆角矩形/按钮/×；标题用 `getTitleFont()`（18px）
- 章节来自 `AVFormatContext::chapters`；进度条书签独立命中 `ControlType::ChapterMarker`
- M3U/M3U8 在 `openFile` 分流：`openM3uSource` 读本地文件 / WinHTTP 抓远程 .m3u，`parseM3uPlaylist` 判定含 `#EXT-X` 的 HLS 清单返回 false 让 FFmpeg 接手（远程 .m3u8 不抓取直接交 FFmpeg）；普通清单解析后 `resolveM3uUri` 按清单位置解析相对路径，全部入 `m_playlist` 并播首项；`#EXTINF` 标题存 `m_playlistTitles`（会话内有效），渲染层经 `m_cachedPlaylistDisplay` 显示名替代路径；展开期置 `m_m3uExpanding` 防嵌套清单递归
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

配置单例 `Settings`（`src/core/settings.h`），文件 `%APPDATA%/VideoPlay/VideoPlay.json`。写入为原子替换（先写 `.tmp` 再 rename）；解析失败时坏文件备份为 `VideoPlay.json.broken`。启动时 `positions`/`bookmarks`/`seriesProgress` 中本地文件已不存在的条目会被清理（URL 与 UNC 路径跳过，父目录不存在的条目保留）。

## 已知约束

1. `HttpClient` 只有 WinHTTP。非 Windows 编 AI 模块会失败。
2. `AudioPlayer` 可通过播放菜单「音频输出」切换设备，按设备名写入配置。
3. 播放列表可删除、清空、拖拽排序并持久化。窗口宽小于 860px 时若两栏同时开则关闭剧集面板。
4. 进度条缩略图由 `FFmpegPlayer` 独立预览解码线程生成（首个悬停请求才懒打开容器；500ms 桶 LRU 缓存、前向 4s 内顺序续解；pb 报不可 seek 的流自动放弃），不要从主视频队列 `getVideoFrame` 里抽帧。
5. `AIConfig::autoAnalyze` 已写入配置，打开文件时不会自动分析。
6. HTTPS 请求忽略证书错误（`SECURITY_FLAG_IGNORE_*`），不要在新代码里扩大这个范围。
7. 应用内帮助文本（F1）已与实际按键一致：`.` 调速，`[` `]` `\` 为 AB 循环；改键位时同步更新 `app_events.cpp` 的帮助表与 README。
8. 文件关联（`fileassoc.*`）只写 `HKCU\Software\Classes` 与 `HKCU\Software\RegisteredApplications`，不写 HKLM/HKCR、不要管理员权限；扩展名默认值被其它应用占用时只加 `OpenWithProgids`，不抢占。

## 安全

- 路径用 `std::filesystem`，不要手写缓冲区拼接
- 拖放文件由 SDL 事件进入
- 日志可能含本地路径，不要往 issue 里贴完整日志
