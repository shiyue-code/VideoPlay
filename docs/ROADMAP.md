# VideoPlay 改进方案

## 更新日志

### v1.0.1 (2026-04-11)
- ✅ **性能优化**：修复视频/音频卡顿，添加帧率控制和时钟同步
- ✅ **架构重构**：VideoRenderer 统一渲染视频和字幕，移除 QVideoWidget 依赖
- ✅ **音频优化**：增大缓冲区，添加队列大小限制
- ✅ **字幕显示**：字幕渲染集成到视频渲染器，支持样式设置

---

## 已实现功能汇总

| 功能 | 实现状态 | 代码位置 |
|------|----------|----------|
| **基础播放** |||
| 播放/暂停/停止 | ✅ | PlayerEngine |
| 快进/快退 (5s) | ✅ | MainWindow |
| 无极倍速 (0.25x-4.0x) | ✅ | FFmpegPlayer |
| 音量控制/静音 | ✅ | Controls |
| **播放列表** |||
| 播放列表管理 | ✅ | PlaylistWidget |
| 拖拽排序 | ✅ | PlaylistWidget |
| **字幕** |||
| 字幕解析 (SRT/ASS/VTT) | ✅ | SubtitleParser |
| 字幕显示 | ✅ | VideoRenderer |
| 字幕延迟调整 | ✅ | MainWindow (+/- 100ms) |
| 字幕样式 (字体/颜色/描边) | ✅ | VideoRenderer |
| **窗口功能** |||
| 全屏模式 | ✅ | MainWindow |
| 窗口置顶 | ✅ | MainWindow::onToggleAlwaysOnTop |
| 拖放文件 | ✅ | VideoRenderer |
| 快捷键支持 | ✅ | MainWindow |
| 截图 | ✅ | MainWindow::onTakeScreenshot |
| 循环播放 (单曲/全部/关) | ✅ | MainWindow::onToggleLoopMode |
| **系统** |||
| 设置持久化 | ✅ | Settings (QSettings) |
| 窗口位置/大小记忆 | ✅ | Settings::windowGeometry |
| 音量/倍速记忆 | ✅ | Settings |
| 最近文件列表 | ✅ | Settings::recentFiles |
| 日志系统 | ✅ | Logger |
| Qlementine 样式 | ✅ | 应用级样式 |

---

## 一、当前状态分析

### 已完成功能
- ✅ 基本播放控制（播放/暂停/停止/快进/快退）
- ✅ 音量控制与静音
- ✅ 无极倍速播放（0.1x - 4.0x）
- ✅ 播放列表管理
- ✅ 字幕解析（SRT/ASS/VTT）
- ✅ Qlementine 现代化样式
- ✅ 全屏模式
- ✅ 拖放文件
- ✅ 快捷键支持

### 当前架构
```
src/
├── main.cpp              # 入口
├── common.h              # 通用定义
├── playerengine.h/cpp    # 播放引擎
├── mainwindow.h/cpp      # 主窗口
├── videowidget.h/cpp     # 视频显示
├── controls.h/cpp        # 控制栏
├── playlistwidget.h/cpp  # 播放列表
└── subtitleparser.h/cpp  # 字幕解析
```

---

## 二、同类播放器功能对比

| 功能 | PotPlayer | MPC-HC | mpv | VLC | VideoPlay |
|------|-----------|--------|-----|-----|-----------|
| 多音轨切换 | ✅ | ✅ | ✅ | ✅ | ❌ |
| 多字幕加载 | ✅ | ✅ | ✅ | ✅ | ❌ |
| 字幕样式自定义 | ✅ | ❌ | ✅ | ✅ | ✅ |
| 截图 | ✅ | ✅ | ✅ | ✅ | ✅ |
| 循环播放(A-B) | ✅ | ✅ | ✅ | ✅ | ⚠️ (单曲/全部) |
| 画面旋转/翻转 | ✅ | ✅ | ✅ | ✅ | ❌ |
| 画中画 | ✅ | ❌ | ❌ | ❌ | ❌ |
| 硬件加速 | ✅ | ✅ | ✅ | ✅ | ❌ |
| 视频滤镜 | ✅ | ✅ | ✅ | ✅ | ❌ |
| 音频均衡器 | ✅ | ❌ | ✅ | ✅ | ❌ |
| 最近文件记录 | ✅ | ✅ | ❌ | ✅ | ✅ |
| 窗口置顶 | ✅ | ✅ | ❌ | ✅ | ✅ |
| 迷你模式 | ✅ | ❌ | ❌ | ❌ | ❌ |
| 皮肤/主题 | ✅ | ❌ | ❌ | ✅ | ⚠️ (Qlementine样式) |
| 在线字幕搜索 | ✅ | ❌ | ✅ | ❌ | ❌ |
| 播放统计 | ✅ | ❌ | ❌ | ❌ | ❌ |
| 视频信息面板 | ✅ | ✅ | ✅ | ✅ | ❌ |

---

## 三、现代化 UI 设计趋势

### 1. 沉浸式全屏模式
- 隐藏所有 UI 元素，鼠标悬停时显示控制栏
- 毛玻璃效果（Acrylic/Fluent Design）
- 进度条使用渐变色彩
- 大字体时间显示

### 2. 扁平化控制栏
- 底部悬浮控制栏（类似 Netflix/YouTube）
- 图标使用 SVG 矢量图
- 滑块使用圆角设计
- 音量滑块弹出式

### 3. 侧边栏改进
- 可折叠的侧边栏（播放列表/字幕/章节）
- 缩略图预览
- 拖拽排序
- 搜索过滤

### 4. 主题系统
- 亮色/暗色主题切换
- 自定义强调色
- 透明度调节

### 5. 现代窗口框架
- 无边框窗口设计
- 自定义标题栏
- 系统托盘最小化
- 记忆上次窗口位置/大小

---

## 四、架构优化方案

### 当前问题
1. **扁平化结构**：所有文件在 `src/` 下，缺乏模块化
2. **缺少配置管理**：没有持久化设置
3. **缺少日志系统**：调试困难
4. **硬编码路径**：DLL 路径写死
5. **缺少单元测试**：无法保证质量

### 优化后目录结构
```
VideoPlay/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── core/                    # 核心模块
│   │   ├── playerengine.h/cpp   # 播放引擎
│   │   ├── playlist.h/cpp       # 播放列表模型
│   │   └── settings.h/cpp       # 配置管理
│   ├── ui/                      # 界面模块
│   │   ├── mainwindow.h/cpp     # 主窗口
│   │   ├── videowidget.h/cpp    # 视频显示
│   │   ├── controls.h/cpp       # 控制栏
│   │   ├── playlistview.h/cpp   # 播放列表视图
│   │   ├── titlebar.h/cpp       # 自定义标题栏
│   │   └── dialogs/             # 对话框
│   │       ├── settingsdialog.h/cpp
│   │       ├── infodialog.h/cpp
│   │       └── aboutdialog.h/cpp
│   ├── subtitles/               # 字幕模块
│   │   ├── subtitleparser.h/cpp
│   │   ├── subtitleoverlay.h/cpp
│   │   └── subtitlestyle.h/cpp
│   └── utils/                   # 工具模块
│       ├── logger.h/cpp
│       ├── fileutils.h/cpp
│       └── thumbnailer.h/cpp
├── resources/
│   ├── icons/                   # SVG 图标
│   ├── themes/                  # 主题配置
│   └── translations/            # 多语言
├── tests/                       # 单元测试
└── 3rdparty/
    └── qlementine/
```

---

## 五、功能逐步增加计划

### Phase 1: 基础完善（当前 → v1.1）
**优先级：高 | 预计时间：1-2周**

- [ ] 设置持久化（QSettings）
  - 记住上次播放位置
  - 记住窗口大小/位置
  - 记住音量/倍速设置
  - 最近文件列表
- [ ] 日志系统
  - 文件日志
  - 调试输出
- [ ] 硬件加速支持
  - DXVA2 / D3D11VA
- [ ] 多音轨切换
- [ ] 截图功能
- [ ] 窗口置顶
- [ ] 循环播放（单曲/全部/随机）

### Phase 2: 字幕增强（v1.1 → v1.2）
**优先级：高 | 预计时间：1-2周**

- [ ] 字幕样式自定义
  - 字体大小/颜色/边框
  - 位置调节
  - 背景透明度
- [ ] 多字幕同时加载
- [ ] 字幕延迟调整
- [ ] 在线字幕搜索（OpenSubtitles API）
- [ ] 字幕编码自动检测

### Phase 3: UI 改进（v1.2 → v1.3）
**优先级：中 | 预计时间：2-3周**

- [ ] 自定义无边框窗口
- [ ] 沉浸式全屏模式
  - 鼠标悬停显示控制栏
  - 进度条预览缩略图
- [ ] 迷你模式
- [ ] 系统托盘
- [ ] 主题系统（亮色/暗色）
- [ ] 播放列表缩略图
- [ ] 拖拽排序优化

### Phase 4: 高级功能（v1.3 → v1.4）
**优先级：中 | 预计时间：2-4周**

- [ ] 视频信息面板
  - 分辨率、码率、编码信息
  - 音频信息
- [ ] 音频均衡器
- [ ] 画面旋转/翻转/镜像
- [ ] A-B 循环播放
- [ ] 播放速度记忆
- [ ] 播放统计
  - 播放次数
  - 播放时长
  - 完成率
- [ ] 快捷键自定义

### Phase 5: 高级特性（v1.4 → v2.0）
**优先级：低 | 预计时间：4-6周**

- [ ] 画中画模式
- [ ] 视频滤镜
  - 亮度/对比度/饱和度
  - 锐化/模糊
  - 色彩反转
- [ ] 音频可视化
- [ ] 网络流播放
  - HTTP/HTTPS
  - RTSP
  - M3U8
- [ ] 章节支持
- [ ] DVD/Blu-ray 播放
- [ ] 插件系统
- [ ] 命令行接口

---

## 六、技术债务清理

### 需要修复的问题
1. **DLL 自动复制**：CMake 无法自动复制 DLL
   - 方案：使用 `windeployqt` 或自定义 CMake 脚本
2. **FFmpeg 路径硬编码**
   - 方案：环境变量或配置文件
3. **缺少错误处理**
   - 方案：统一错误处理机制
4. **内存泄漏风险**
   - 方案：使用智能指针和 RAII
5. **线程安全**
   - 方案：使用 Qt 线程池和信号槽

### 代码质量提升
1. 添加单元测试（Google Test / Qt Test）
2. 添加 CI/CD（GitHub Actions）
3. 代码格式化（clang-format）
4. 静态分析（clang-tidy）
5. 内存检测（Valgrind / Dr.Memory）

---

## 七、推荐实施顺序

### 第一步：立即实施
1. 设置持久化（QSettings）
2. 日志系统
3. 最近文件列表
4. 窗口置顶
5. 截图功能

### 第二步：短期（1个月内）
1. 多音轨切换
2. 字幕样式自定义
3. 硬件加速
4. 循环播放
5. 自定义无边框窗口

### 第三步：中期（3个月内）
1. 沉浸式全屏
2. 迷你模式
3. 主题系统
4. 播放列表缩略图
5. 视频信息面板

### 第四步：长期（6个月内）
1. 音频均衡器
2. 播放统计
3. 网络流播放
4. 插件系统
5. 单元测试覆盖

---

## 八、参考项目

| 项目 | 技术栈 | 亮点 |
|------|--------|------|
| [mpv](https://github.com/mpv-player/mpv) | C | 极简设计，强大配置 |
| [PotPlayer](https://potplayer.daum.net/) | C++ | 功能丰富，高度可定制 |
| [MPC-HC](https://github.com/clsid2/mpc-hc) | C++ | 轻量，经典设计 |
| [FluentFin](https://github.com/insomniachi/FluentFin) | C# | Windows 11 Fluent 设计 |
| [Bomi Player](https://github.com/x264/bomi) | C++/Qt | 现代化 UI |
| [SMPlayer](https://github.com/smplayer-dev/smplayer) | C++/Qt | 跨平台，功能完整 |

---

*文档生成时间：2025-04-04*
*版本：v1.0*
