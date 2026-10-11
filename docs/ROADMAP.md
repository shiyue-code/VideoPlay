# 功能路线图

> **读者**：维护者、贡献者  
> **状态**：现行  
> **更新**：2026-10-11

对照 PotPlayer 的常用能力，列出本项目还缺什么、先做什么。已稳定的用法见 [`../README.md`](../README.md)。实现约束见 [`../AGENTS.md`](../AGENTS.md)。

## 约定

| 优先级 | 含义 |
|--------|------|
| P0 | 没有会明显差于常见播放器 |
| P1 | 常用，做完可用性上一个台阶 |
| P2 | 增强，可后做 |
| P3 | 成本高或偏离定位，暂不做 |

状态用词见 [`README.md`](README.md)。

## 已具备（不要再当作缺口）

| 类别 | 内容 |
|------|------|
| 播放 | 解码、变速、AB 循环、章节、播放列表、剧集识别、预缓冲 |
| 画面 | 比例、亮度/对比度/饱和度/色调/伽马、硬件解码 |
| 字幕 | SRT/ASS/VTT/PGS、内封轨、同名外挂自动加载、字幕同步 |
| 音频 | 音量/静音、多音轨、音频同步、音频滤镜预设 |
| 导航 | 用户书签、媒体信息（Tab）、进度记忆 |
| 界面 | 全屏、无边框、置顶、截图、菜单栏、控制栏 |
| 网络 | http/https 播放、断线重连 |
| AI | MiMo / Gemini 分析、搜索、缓存 |

## 差距

表中「已完成」仅作对照，后续迭代不要再排进开发任务。

### 画面

| 功能 | 优先级 | 状态 | 说明 |
|------|--------|------|------|
| 亮度/对比度/饱和度/色调/伽马 | P0 | 已完成 | FFmpeg `eq` / `hue` |
| 去隔行 | P1 | 已完成 | 播放菜单，自动/Yadif/Bwdif，接在 eq 滤镜链前 |
| 旋转/镜像/裁剪 | P1 | 已完成 | SDL 渲染层变换，播放菜单「画面变换」 |
| HDR Tone Mapping | P1 | 已完成 | 播放菜单，自动/开启/关闭，Hable/Mobius/Reinhard，基于 FFmpeg `zscale` + `tonemap` |
| 降噪/锐化 | P2 | 未开始 | |
| 色域切换 | P2 | 未开始 | |
| 外挂 Pixel Shader | P3 | 已取消 | 架构改动过大 |

### 音频

| 功能 | 优先级 | 状态 | 说明 |
|------|--------|------|------|
| 音频同步 | P0 | 已完成 | Shift+G / Shift+H |
| 输出设备选择 | P1 | 已完成 | 播放菜单「音频输出」，按设备名持久化 |
| 声道映射 | P1 | 未开始 | 单声道、左右交换 |
| 保持音高的变调 | P2 | 已完成 | 音频滤镜链末尾追加 `atempo` |
| Bitstreaming | P3 | 已取消 | |

### 字幕

| 功能 | 优先级 | 状态 | 说明 |
|------|--------|------|------|
| 自动加载同名字幕 | P0 | 已完成 | |
| 在线字幕搜索/下载 | P1 | 未开始 | OpenSubtitles / 射手网 |
| ~~外挂字幕编码探测~~ | P1 | 已完成 | `decodeToUtf8`：UTF-8 BOM / UTF-16 LE/BE（含无 BOM 探测）/ GBK→UTF-8 |
| 字幕样式设置 | P1 | 部分完成 | 渲染已按 `SubtitleStyle` 配置（字号/颜色/描边），无设置 UI，`fontFamily` 不支持 |
| 双语字幕同时显示 | P2 | 未开始 | |

### 播放交互

| 功能 | 优先级 | 状态 | 说明 |
|------|--------|------|------|
| 用户书签 | P1 | 已完成 | Ctrl+B |
| 详细媒体信息 | P2 | 已完成 | Tab |
| 进度条缩略图 | P1 | 部分完成 | 悬停/拖动进度条显示 120×68 预览；无拖拽缓存、网络流不生成 |
| 播放列表删除/清空 | P1 | 已完成 | 右键菜单、Delete、播放菜单「清空播放列表」 |
| 播放列表持久化 | P1 | 已完成 | 写入 `VideoPlay.json` 的 `playlist` |
| 播放列表拖拽排序 | P1 | 已完成 | 列表项拖拽调整顺序 |
| 睡眠定时 | P2 | 未开始 | |
| 逐帧步进 | P1 | 未开始 | 需先修「暂停时 seek 不生效」 |
| 跳转时间对话框 | P1 | 未开始 | 输入 hh:mm:ss 定位 |
| ~~打开 URL 对话框~~ | P1 | 已完成 | 文件菜单「打开 URL...」/ Ctrl+U，`InputDialog` 模态输入框；拖入 .srt/.ass/.ssa/.vtt 自动加载为外挂字幕 |
| 缩略图导航视图 | P3 | 未开始 | |

### 网络

| 功能 | 优先级 | 状态 | 说明 |
|------|--------|------|------|
| M3U / IPTV | P1 | 未开始 | |
| YouTube / B 站 URL 解析 | P1 | 未开始 | 外部调用 yt-dlp |
| DLNA / 投屏 | P3 | 已取消 | |

### 录制与信息

| 功能 | 优先级 | 状态 | 说明 |
|------|--------|------|------|
| GIF 片段 | P1 | 未开始 | 可复用 AB 区间 |
| 视频录制 | P2 | 未开始 | |
| 帧序列导出 | P2 | 未开始 | |
| 实时统计（fps/码率/丢帧） | P1 | 未开始 | 不要和已有 Tab 媒体信息混为一谈 |
| 快捷键自定义 / 皮肤 | P3 | 未开始 | |

## 阶段

### Phase 1 — 已完成

音频同步、同名字幕、画面 eq、用户书签。

### Phase 2 — 下一轮（建议）

目标：老视频、手机视频、多设备音频更好用。

| 功能 | 模块 | 验收 |
|------|------|------|
进度条缩略图、音频输出设备、播放列表删除/持久化/拖拽、画面旋转/镜像/裁剪、去隔行已落地。

风险：缩略图必须独立 decode-one-frame；去隔行在硬解路径上要验证。

### Phase 3

M3U/IPTV、在线字幕、yt-dlp、双语字幕。

### Phase 4

HDR tone mapping、GIF、实时统计、快捷键自定义、主题。

## 建议顺序

1. ~~修三个播放正确性问题：无音轨定格、暂停 seek、内封字幕标签/时长~~ ✅ 已修
2. ~~外挂字幕编码探测（GBK/UTF-16）；内封文本字幕走 `removeFormatting`~~ ✅ 已修
3. ~~半成品功能接线：音量/窗口尺寸持久化、`MenuId::Borderless`、help 文本~~ ✅ 已修（字幕样式 UI 未做）
4. ~~给 `EpisodeDetector`、Settings、ASS 补测试~~ ✅ 已补
5. 打开 URL 对话框、逐帧步进、跳转时间、截图到剪贴板等低成本交互项

## 已知缺陷（2026-10-09 审核）

| 缺陷 | 位置 | 说明 |
|------|------|------|
| ~~无音轨视频定格~~ ✅ | `ffmpegplayer.cpp` `audioPositionMs()` | 已加挂钟回退时钟（`m_fallbackClock*`），pause/resume/seek/stop 均正确冻结与重置 |
| ~~暂停时 seek 挂起~~ ✅ | `ffmpegplayer.cpp` `decodeLoop` | 暂停态唤醒处理 seek，补解码上限 4096 包直到目标帧入队（`back().pts >= 目标`） |
| ~~内封字幕标签未剥离~~ ✅ | `ffmpegplayer.cpp` 字幕包解码 | 走 `SubtitleParser::embeddedTextToPlain()`：提取 Dialogue 文本、剥 ASS/HTML 标签、`\N` 转换行 |
| ~~内封字幕时长写死~~ ✅ | `app.cpp` 字幕回调 | 回调签名改为 `(startMs, endMs, text)`，优先 `end_display_time`，其次 packet duration，兜底 5 秒 |
| ~~截图通道数假设~~ ✅ | `VideoPlayerApp::takeScreenshot` | 已修：截图改取 `m_displayFrame`，按与渲染一致的裁剪/翻转/旋转变换后写 PNG，不再包含 UI/OSD |
| ~~解码错误无 UI 反馈~~ ✅ | `app.cpp` `onError` | 错误经 `m_pendingError` 队列到主线程弹「播放错误」对话框 |
| ~~播放时钟是挂钟~~ ✅ | `audioplayer.cpp` / `audioclock.cpp` | `AudioClock` 记录每段入队采样的媒体 pts 与单帧媒体时长，按 SDL 未取走帧数推算媒体位置；欠载时停在队尾 |
| ~~倍速未作用到音频~~ ✅ | `ffmpegplayer_filters.cpp` / `audioplayer.cpp` | 音频滤镜链末尾追加 `atempo`；修复前音频按 1x 消耗而解码按倍速推进，队列满后直接丢弃音频包 |
| ~~长 GOP seek 后音画错位~~ ✅ | `ffmpegplayer.cpp` `handleSeek` | `av_seek_frame(BACKWARD)` 落在目标前关键帧，解码出的目标前音频/视频帧按 `m_audioTrimBeforeMs`/`m_videoTrimBeforeMs` 裁剪 |
| ~~EOF 截尾~~ ✅ | `ffmpegplayer.cpp` `drainEndOfStream` | EOF 先 send NULL flush 解码器，再等视频队列与音频缓冲排空才置 Stopped，不再丢弃最后约 1 秒 |
| ~~纯中文系列名无法组成剧集~~ ✅ | `episodedetector.cpp` `normalizeSeriesName` | `normalizeSeriesName` 保留非 ASCII 字符、丢弃全角/CJK 标点；`第N集`、`【N】` 正则改为多字节交替，修复系列名残留半个 UTF-8 字符（会导致配置 dump 抛异常、save 失败） |

## 半成品功能（声明但未接线）

| 项 | 现状 |
|----|------|
| ~~窗口尺寸/位置记忆~~ ✅ | 启动恢复 w/h 与 x/y；退出时非最大化才记录普通态尺寸 |
| ~~音量/静音/倍速持久化~~ ✅ | 启动从 Settings 恢复；setVolume/toggleMute/setSpeed 变更即写盘 |
| 字幕样式 | 渲染已按 `SubtitleStyle` 配置（字号/颜色/描边），仍无设置 UI，`fontFamily` 暂不支持 |
| ~~`MenuId::Borderless`~~ ✅ | 视频菜单新增「无边框窗口」勾选项 |
| ~~`MenuId::PlaylistRemove`~~ ✅ | `menu_manager.cpp` 删除项直接调 `playlistRemove` 回调；`app_events.cpp` 的空 case 为有意保留 |
| ~~positions/bookmarks 等 map~~ ✅ | 启动时清理本地文件已不存在的条目（URL/UNC 跳过，父目录不存在的保留） |

## 工程债（非功能）

| 项 | 说明 |
|----|------|
| HTTP 仅 Windows | `src/ai/httpclient.cpp` 无 POSIX 后端 |
| `autoAnalyze` | 配置有字段，打开文件不会自动分析 |
| 证书校验关闭 | WinHTTP `SECURITY_FLAG_IGNORE_*` |
| CI | 只构建 Windows |
| 测试 | 10 个目标 101 个用例；剧集识别、Settings 读写、ASS 已有单测 |
| ~~帮助文本~~ ✅ | F1 已补齐 Shift+G/H、Ctrl+B/Shift+B、Ctrl+F、C/F6、Ctrl(±Shift)+方向键、Ctrl+0、Delete |
| ~~渲染循环~~ ✅ | 已开 vsync（`SDL_SetRenderVSync`）；vsync 下最小帧间隔 4ms，present 未阻塞时补睡 |

## 参考

- [`design/episode-playlist.md`](design/episode-playlist.md)
- [`design/refactoring.md`](design/refactoring.md) — 重构计划（对话框骨架去重、回调收敛、大文件拆分）
- `src/core/ffmpegplayer.cpp`
- `src/renderer/sdlrenderer_ui.cpp`
- `src/core/settings.h`
