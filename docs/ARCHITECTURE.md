# VideoPlay - 架构设计文档

## 1. 总体架构

VideoPlay采用分层架构，主要分为以下几层:

```
┌─────────────────────────────────────────────────────────────┐
│                    用户界面层 (UI Layer)                     │
│  MainWindow | Controls | VideoWidget | ConvertDialog      │
└─────────────────────────────────────────────────────────────┘
                            │ 信号/槽
┌─────────────────────────────────────────────────────────────┐
│                    应用逻辑层 (Business Logic)              │
│           PlayerEngine | PluginManager | Settings           │
└─────────────────────────────────────────────────────────────┘
                            │ 调用
┌─────────────────────────────────────────────────────────────┐
│                    插件层 (Plugin Layer)                    │
│      IDecoder | IEncoder | FormatConverter (接口)           │
│      H264Decoder | H264Encoder  (实现)                      │
└─────────────────────────────────────────────────────────────┘
                            │ 调用
┌─────────────────────────────────────────────────────────────┐
│                    库层 (Library Layer)                     │
│           Qt6 | FFmpeg | STD                                    │
└─────────────────────────────────────────────────────────────┘
```

## 2. 核心组件设计

### 2.1 PlayerEngine (播放器引擎)

**职责**:
- 管理播放状态(播放/暂停/停止/缓冲)
- 内部控制时间轴和同步
- 协调插件加载和使用
- 提供统一的播放控制API

**关键功能**:
```cpp
class PlayerEngine : public QObject {
    bool loadFile(const QString& filePath);      // 加载文件
    void play();                                 // 播放
    void pause();                                // 暂停
    void stop();                                 // 停止
    void seek(qint64 position);                  // 定位
    void setPlaybackRate(PlaybackRate rate);    // 设置倍率
    bool convertFormat(...);                    // 格式转换
    // ...
};
```

### 2.2 插件系统 (Plugin System)

**架构**:
```cpp
PluginInterface      <- 基础接口
├── IDecoder        <- 解码器接口
├── IEncoder        <- 编码器接口
└── IConverter      <- 转换器接口

PluginLoader        <- 动态加载和插件管理
PluginRegistry      <- 插件注册表
```

**插件加载流程**:
1. PluginLoader搜索指定目录下的动态库
2. 使用QPluginLoader加载库
3. 验证接口(PasPluginInterface)
4. 调用initialize()初始化
5. 注册到PluginRegistry
6. 应用程序可查询和使用的插件

**示例 - 自定义解码器**:
```cpp
class MyCustomDecoder : public IDecoder {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.videoplay.Decoder/1.0" FILE "mydecoder.json")
    Q_INTERFACES(VideoPlay::PluginInterface VideoPlay::IDecoder)
    
public:
    PluginInfo getInfo() const override;
    bool initialize(const DecoderConfig& config) override;
    bool decode(DecoderPacket& packet, AVFrame* frame) override;
    // 其他接口方法...
};
```

### 2.3 倍率播放实现

**设计要点**:
- 使用QMediaPlayer的setPlaybackRate()调整播放速率
- 音频音调调整(需要音频处理)
- 帧插值算法可选(高级功能)
- 保持A/V同步

**支持的倍率**: 0.25x, 0.5x, 0.75x, 1x, 1.25x, 1.5x, 2x, 4x

### 2.4 视频格式转换

**转换流程**:
```
输入文件 → 解码器插件 → 原始帧 → 编码器插件 → 输出文件
    │                                          │
检测格式                                   选择目标格式
    │                                          │
选择解码器                                 选择编码器
```

**FormatConverter实现**:
- 内部使用FFmpeg命令行工具进行快速实现
- 可选插件化的沉浸式转换
- 支持批量转换和进度回调
- 参数配置(分辨率/帧率/码率/质量等)

## 3. UI设计

### 3.1 主窗口(MainWindow)
- 菜单栏: 文件/播放/视图/工具/帮助
- 工具栏: 播放控制/音量/全屏等
- 视频显示区: VideoWidget (支持缩放/旋转/镜像)
- 控制栏: 播放按钮/进度条/音量/倍率选择
- 状态栏: 位置/时长/状态

### 3.2 对话框
- ConvertDialog: 格式转换设置
- PluginManagerDialog: 插件管理
- SettingsDialog: 全局设置

## 4. 配置系统

使用QSettings存储配置，支持INI文件格式:

- 位置: Windows `%APPDATA%/VideoPlay/settings.ini`
- 节:<section>/<key>  
- 支持的类型: QString, int, double, bool, QStringList

## 5. 日志系统

Logger提供日志级别控制:
- Debug
- Info
- Warning
- Error
- Critical

日志输出到控制台和文件，支持彩色输出(Unix)

## 6. 插件开发指南

### 6.1 解码器插件

1. 继承`IDecoder`接口
2. 实现所有纯虚函数
3. 添加`Q_PLUGIN_METADATA`宏
4. 添加`Q_INTERFACES`宏
5. 创建插件JSON配置文件
6. 在CMake中配置为MODULE库

### 6.2 编码器插件

1. 继承`IEncoder`接口  
2. 实现所有纯虚函数
3. 类似解码器插件配置

### 6.3 配置CMake

```cmake
add_library(my_plugin MODULE
    myplugin.cpp
)

target_link_libraries(my_plugin
    PRIVATE video_plugins
    PRIVATE FFmpeg::AVCodec
    PRIVATE FFmpeg::AVFormat
    # ...
)

# 复制插件到输出目录
add_custom_command(TARGET video_player POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    $<TARGET_FILE:my_plugin>
    $<TARGET_FILE_DIR:video_player>/plugins/decoders/
)
```

## 7. 扩展点

### 7.1 添加新的视频渲染引擎
- 继承QVideoSink或自定义VideoRenderer
- 通过PlayerEngine的接口切换

### 7.2 添加硬件加速
- 在解码器中集成VAAPI(Intel), NVDEC(NVIDIA), VideoDecodeAcceleration(Apple)
- 实现纹理上传到GPU
- 配置选项

### 7.3 添加字幕支持
- 实现字幕解析器插件
- 支持ASS, SRT, VTT格式
- 渲染到VideoWidget

### 7.4 添加屏幕录制
- 创建屏幕捕获编解码器插件
- 音频捕获支持
- 编码和保存

## 8. 性能优化

### 8.1 内存管理
- 使用内存池管理帧缓冲区
- AVFrame复用减少分配

### 8.2 多线程
- 解码线程与渲染线程分离
- 使用ThreadPool并行多个解码任务
- 懒加载插件

### 8.3 I/O优化
- 使用缓冲读取
- 异步文件加载
- 网络流支持

## 9. 测试策略

- 单元测试: 核心类和插件接口
- 集成测试: 播放流程和转换
- UI测试: 界面交互(可选)
- 压力测试: 大文件/长时间播放

## 10. 部署

### 10.1 Windows
- 使用NSIS创建安装包
- 打包Qt运行时库
- 打包FFmpeg DLL
- 添加注册表项和桌面快捷方式

### 10.2 Linux
- 创建DEB/RPM包
- 依赖系统库(Qt, FFmpeg)
- 桌面文件集成

### 10.3 macOS
- 使用macdeployqt打包Qt
- 创建DMG镜像
- 应用签名(可选)

## 11. 未来规划

- [ ] 网络流播放(HTTP, RTSP, HLS)
- [ ] 播放列表和媒体库
- [ ] 截图和录屏功能
- [ ] 音频均衡器和音效
- [ ] 视频滤镜和增强
- [ ] 字幕编辑和编辑工具
- [ ] 云同步配置
- [ ] 多语言界面

## 12. 常见问题

Q: 为什么选择插件架构?  
A: 提高可扩展性，允许动态添加新解码器，无需修改核心代码。

Q: 如何支持新的视频格式?  
A: 实现IDecoder或IEncoder接口的插件，编译后放入plugins目录。

Q: 倍率播放音调如何调整?  
A: 使用SoundTouch库或FFmpeg的atempo滤镜实现音频音调保持。

Q: 是否支持GPU加速?  
A: 基础版本暂不支持，但插件接口预留了硬件加速能力(通过AVHWDeviceContext)。