# VideoPlay - 基于C++ + Qt的视频播放器

## 项目概述
VideoPlay是一个基于C++、CMake、Qt和clangd的现代化视频播放器，支持插件化架构，可扩展解码器和编码器。

## 核心功能
- 支持不同倍率播放（0.25x, 0.5x, 1x, 1.5x, 2x, 4x等）
- 支持视频格式转换
- 插件化解码器和编码器架构
- 现代化Qt界面
- 支持多种视频格式

## 技术栈
- **语言**: C++17
- **构建系统**: CMake 3.16+
- **GUI框架**: Qt 6.2+
- **开发工具**: clangd
- **多媒体**: FFmpeg集成

## 架构设计

### 目录结构
```
VideoPlay/
├── src/                    # 源代码
│   ├── core/              # 核心播放器引擎
│   ├── plugins/           # 插件系统
│   │   ├── decoders/      # 解码器插件
│   │   ├── encoders/      # 编码器插件
│   │   └── converters/    # 格式转换插件
│   ├── ui/                # 用户界面
│   └── utils/             # 工具类
├── include/               # 头文件
│   ├── core/
│   ├── plugins/
│   ├── ui/
│   └── utils/
├── tests/                 # 单元测试
├── examples/              # 示例代码
├── docs/                  # 文档
├── cmake/                 # CMake模块
└── build/                 # 构建输出
```

### 核心组件

#### 1. 播放器引擎 (PlayerEngine)
- 视频渲染和音频播放
- 时间控制和同步
- 播放状态管理
- 插件加载和管理

#### 2. 插件系统 (PluginSystem)
- 动态库加载
- 插件接口定义
- 插件生命周期管理
- 插件通信机制

#### 3. 解码器插件 (DecoderPlugin)
- 继承自IDecoder接口
- 实现特定格式的解码
- 硬件加速支持
- 错误处理和恢复

#### 4. 编码器插件 (EncoderPlugin)
- 继承自IEncoder接口
- 实现特定格式的编码
- 质量和参数控制
- 多线程处理

#### 5. 格式转换器 (FormatConverter)
- 支持多种输入输出格式
- 批量转换
- 进度监控
- 参数配置

### 播放器特性

#### 倍率播放
- 支持变速播放（0.25x - 4.0x）
- 音频音调调整
- 帧插值算法
- 内存优化

#### 视频格式转换
- 支持格式：MP4, AVI, MKV, MOV, FLV等
- 编码格式：H.264, H.265, VP9, AV1等
- 音频格式：AAC, MP3, FLAC, OPUS等
- 自定义参数配置

## 插件开发

### 解码器插件开发
```cpp
class MyDecoder : public IDecoder {
public:
    bool initialize(const DecoderConfig& config) override;
    bool decodeFrame(const AVPacket* packet, AVFrame* frame) override;
    void cleanup() override;
    // 其他接口实现...
};
```

### 编码器插件开发
```cpp
class MyEncoder : public IEncoder {
public:
    bool initialize(const EncoderConfig& config) override;
    bool encodeFrame(const AVFrame* frame, AVPacket* packet) override;
    void flush(AVPacket* packet) override;
    // 其他接口实现...
};
```

## 构建和开发

### 环境要求
- C++17兼容编译器
- CMake 3.16+
- Qt 6.2+
- FFmpeg开发库
- clangd（可选）

### 构建步骤
```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### 开发环境配置
1. 使用clangd进行代码补全和静态分析
2. 配置CMake Presets进行快速构建
3. 使用CTest进行单元测试
4. 集成Git进行版本控制

## 扩展性设计

### 插件API
- 统一的插件接口定义
- 版本兼容性机制
- 热插拔支持
- 配置文件支持

### 配置系统
- JSON格式配置文件
- 运行时配置修改
- 插件配置隔离
- 用户偏好设置

## 性能优化
- 硬件加速解码
- 多线程处理
- 内存池管理
- 缓存机制