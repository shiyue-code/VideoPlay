# 重构计划

> **读者**：做重构的人、Agent  
> **状态**：现行  
> **范围**：纯重构——不改行为、不加功能。功能缺口见 [`../ROADMAP.md`](../ROADMAP.md)。

第一轮已完成：共享文本工具 `TextRender`、删除 `synchronizeVideo` 死代码并把音频同步偏移接到 `audioPositionMs()`、音量逻辑去重为 `setVolumeTo()`。

第二轮已完成（2026-10-09）：`parseTimestampMs` 归位并改名、回调绝对值编码常量化、`ModalDialog` 基类落地、`RendererCallbacks` 收敛、`ffmpegplayer.cpp` 拆出 filters/tracks/preview 三个子文件。

第三轮已完成（2026-10-10）：`decodeLoop` 拆分为私有方法 `waitForDecodeSlot` / `handleReadFailure` / `decodeVideoPacket` / `decodeAudioPacket` / `decodeSubtitlePacket` / `receiveVideoFrames` / `receiveAudioFrames`；新增可独立单测的纯逻辑模块 `AudioClock`（`core/audioclock.*`）、`settings_maintenance`（`core/settings_maintenance.*`）、`frame_image`（`utils/frame_image.*`）。下表为剩余项。

第四轮已完成（2026-10-11）：信息面板样式常量收敛到 `UiStyle`（`renderer/ui_style.h`）、通用绘制原语收敛到 `UiDraw`（`renderer/ui_draw.*`：`fillRoundRect` / `drawCloseButton` / `drawButton`）；`settingsdialog.cpp` 与 `inputfield.cpp` 的 static `fillRoundRect` 副本已删除；`CustomMessageBox` 独立窗口已删除，消息型弹窗全部改为窗口内信息面板（`showInfoPanel`/`showMessageBox`，支持正文折行、滚动、可点时间戳与消息队列）；`ModalDialog`/`InputDialog`/`SettingsDialog`/`InputField` 已按同一套 UiStyle 视觉重绘。

## 原则

- 一次只做一项，提交信息 `refactor: ...`，不夹带行为修改
- 每轮跑 Debug 构建 + 全部测试（`build/bin/Debug/` 下三个 gtest 可执行文件）
- `3rdparty/` 的 LFS/子模块检出差异不进提交

## P0 — 高收益、低风险

### 1. ~~模态对话框骨架重复~~ ✅

`CustomMessageBox`、`InputDialog`、`SettingsDialog` 三个类各自重复实现同一套模态窗口骨架：

| 重复点 | 位置 |
|--------|------|
| `SDL_CreateWindow`(borderless) + `SDL_CreateRenderer` + 销毁 | 各自 `show()` / 析构 |
| 父窗口居中定位 | 各自 `show()` |
| `handleEvents()` 事件循环（Esc 关闭、按钮 hover） | 三个文件各一份 |
| `isPointInRect` / 命中矩形 | `settingsdialog.cpp`、`sdlrenderer_ui.cpp` 等多份 |

**方案**：新增 `ModalDialogBase`（`src/renderer/modaldialog.h/cpp`）承担窗口/渲染器生命周期、居中、事件循环骨架；子类只实现 `render()` 与事件分发。`isPointInRect` 挪进公共头（可并入 `textutils.h` 或新 `ui_utils.h`）。

**注意**：`SettingsDialog` 有下拉框 overlay，事件循环要留虚函数钩子；不要为了复用牺牲无边框窗口的现有行为。

### 2. ~~`parseTimestampMs` 同名不同义 ×2~~ ✅

已归位 `src/utils/string_utils.h` 并改名：`parseBracketTimestampMs`（`[mm:ss]` → int64）与 `parseHmsTimestampMs`（`hh:mm:ss` → int）。「跳转时间对话框」可直接复用后者。

### 3. ~~回调编号魔法值 `>=1000`~~ ✅

已常量化：`kSeekAbsoluteBase` / `kVolumeAbsoluteBase`（`sdlrenderer.h`），编码处（`sdlrenderer_events.cpp`）与解码处（`app.cpp`）共用。

## P1 — 中收益、中风险

### 4. ~~`ffmpegplayer.cpp` 2612 行拆分~~ ✅ 部分

已拆出 `ffmpegplayer_filters.cpp`（音频/视频滤镜链）、`ffmpegplayer_tracks.cpp`（轨道扫描与切换、字幕流）、`ffmpegplayer_preview.cpp`（进度条缩略图预览解码线程）。共享助手在 `ffmpegplayer_internal.h`。`decodeLoop` 已拆成辅助方法（见第三轮）。剩余可再拆：网络重连。

### 5. ~~`sdlrenderer.h` 回调收敛~~ ✅

`RendererCallbacks` 结构体落地，`setCallbacks()` 一次传入；app.cpp 约 200 行 setter 接线收敛为 `cb.xxx = lambda` 赋值。顺带删除死回调 `m_mouseCallback`/`m_chapterSeekCallback`（setter 从未被调用）。`liveRender` 因在 initialize 之后设置，保留独立 setter。

### 6. `sdlrenderer_ui.cpp` 2032 行的大渲染函数

`renderMediaInfoPanel`、`renderVideoControlPanel`、`renderPlaylistPanel` 等单函数 150~300 行，每行都在算坐标。

**方案**：按面板再拆文件（`sdlrenderer_panel_info.cpp` 等），或提取「面板骨架」（背景+标题栏+命中注册）为公共函数。属文件组织层面，风险低但 diff 大，适合闲时做。

## P2 — 低收益或需先讨论

### 7. `settings.cpp` 981 行

JSON 读写单体。可按「播放列表 / 窗口 / AI / 键位」分域拆函数，但都在一个文件内且格式稳定，收益一般。

### ~~8. `fillRoundRect` 两处实现~~ ✅

已统一：`UiDraw::fillRoundRect`（`renderer/ui_draw.*`）为唯一实现，`SDLRenderer::fillRoundRect` 委托之，`settingsdialog.cpp`/`inputfield.cpp` 的 static 副本已删除。

### 9. 日志格式化重复

`std::to_string` 拼日志散落各处（`logger().info("Volume set to: " + ...)` 与 `logger().info("... {}", ...)` 两种风格混用）。统一为 fmt 风格，属风格统一，顺手做。

## 建议顺序

1. ~~`parseTimestampMs` 改名归位~~ ✅
2. ~~`>=1000` 魔法值常量化~~ ✅
3. ~~模态对话框基类~~ ✅（`ModalDialog`，三个对话框已改为子类）
4. ~~`RendererCallbacks` 收敛~~ ✅
5. ~~`ffmpegplayer.cpp` 拆分~~ ✅（filters/tracks/preview + `decodeLoop` 拆辅助方法）
6. UI 面板拆分（最后，纯文件组织）
