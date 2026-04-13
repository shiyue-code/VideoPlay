# 纯 FFmpeg + SDL 视频播放器方案

## 技术栈概述

```
FFmpeg 6.0+    (视频/音频解码、解封装)
SDL2 2.28+     (窗口创建、视频渲染、音频播放)
OpenGL (可选)  (高性能视频渲染)
```

---

## 1. 为什么选择 FFmpeg + SDL？

### ✅ 核心优势

| 特性 | 说明 |
|------|------|
| **完全可控** | 不依赖高层框架，解码、渲染全流程自主掌控 |
| **极致性能** | 可精确控制内存、线程、GPU 资源 |
| **跨平台** | FFmpeg + SDL 支持所有主流平台 |
| **轻量级** | 无庞大 GUI 框架依赖，适合嵌入式 |
| **学习价值** | 深入理解音视频原理的最佳实践 |

### ⚠️ 需要注意的挑战

1. **开发周期长** - 需自行实现播放控制、UI、同步等
2. **代码量大** - 播放器核心代码约 2000-5000 行
3. **音视频同步** - 需自行处理 PTS/DTS 同步逻辑
4. **多线程处理** - 解码线程、渲染线程、音频线程需协调

---

## 2. 核心技术架构

```
┌─────────────────────────────────────────────────────────┐
│                        主线程                           │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐      │
│  │ 文件读取  │→│ 解封装    │→│ 音视频包分发     │      │
│  │(avformat)│  │(avformat)│  │                  │      │
│  └──────────┘  └──────────┘  └────────┬─────────┘      │
└───────────────────────────────────────┼─────────────────┘
                                        │
                    ┌───────────────────┴───────────────────┐
                    ▼                                       ▼
┌────────────────────────────────┐      ┌────────────────────────────────┐
│        视频解码线程            │      │        音频解码线程            │
│  ┌──────────┐  ┌──────────┐   │      │  ┌──────────┐  ┌──────────┐   │
│  │ 视频解码  │→│ 帧队列    │   │      │  │ 音频解码  │→│ 音频队列  │   │
│  │(avcodec) │  │          │   │      │  │(avcodec) │  │          │   │
│  └──────────┘  └────┬─────┘   │      │  └──────────┘  └────┬─────┘   │
└─────────────────────┼─────────┘      └─────────────────────┼─────────┘
                      │                                      │
                      ▼                                      ▼
┌────────────────────────────────┐      ┌────────────────────────────────┐
│        视频渲染线程            │      │        音频播放 (SDL)          │
│  ┌──────────┐  ┌──────────┐   │      │  ┌──────────┐  ┌──────────┐   │
│  │ 格式转换  │→│ SDL渲染   │   │      │  │ 重采样    │→│ SDL音频   │   │
│  │(swscale) │  │(SDL_Texture)│  │      │  │(swresample)│  │(SDL_Queue)│  │
│  └──────────┘  └──────────┘   │      │  └──────────┘  └──────────┘   │
└────────────────────────────────┘      └────────────────────────────────┘
```

---

## 3. 目录结构建议

```
FFmpegPlayer/
├── 3rdparty/
│   ├── ffmpeg/              # FFmpeg 头文件和库
│   └── sdl2/                # SDL2 头文件和库
├── src/
│   ├── main.cpp             # 程序入口
│   ├── player.h/.cpp        # 播放器主类
│   ├── demuxer.h/.cpp       # 解封装模块
│   ├── videodecoder.h/.cpp  # 视频解码
│   ├── audiodecoder.h/.cpp  # 音频解码
│   ├── videorenderer.h/.cpp # 视频渲染 (SDL/OpenGL)
│   ├── audiorenderer.h/.cpp # 音频播放 (SDL)
│   ├── packetqueue.h/.cpp   # 线程安全队列
│   ├── clock.h/.cpp         # 音视频同步时钟
│   └── utils.h/.cpp         # 工具函数
├── include/
│   └── common.h             # 公共定义
├── CMakeLists.txt           # 构建配置
└── README.md
```

---

## 4. 核心代码示例

### 4.1 播放器主类

```cpp
// player.h
#pragma once
#include <string>
#include <thread>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <SDL.h>
}

class PacketQueue;
class VideoDecoder;
class AudioDecoder;
class VideoRenderer;
class AudioRenderer;
class Clock;

class FFmpegPlayer {
public:
    FFmpegPlayer();
    ~FFmpegPlayer();

    bool initialize();
    bool openFile(const std::string& filepath);
    void play();
    void pause();
    void stop();
    void seek(double seconds);
    void setVolume(float volume);
    
    double getDuration() const;
    double getCurrentTime() const;
    bool isPlaying() const;

private:
    void demuxThreadFunc();
    void videoThreadFunc();
    void audioThreadFunc();
    
    // FFmpeg 上下文
    AVFormatContext* m_formatCtx = nullptr;
    AVCodecContext* m_videoCodecCtx = nullptr;
    AVCodecContext* m_audioCodecCtx = nullptr;
    
    // 流索引
    int m_videoStreamIndex = -1;
    int m_audioStreamIndex = -1;
    
    // 组件
    std::unique_ptr<PacketQueue> m_videoQueue;
    std::unique_ptr<PacketQueue> m_audioQueue;
    std::unique_ptr<VideoRenderer> m_videoRenderer;
    std::unique_ptr<AudioRenderer> m_audioRenderer;
    std::unique_ptr<Clock> m_clock;
    
    // 线程
    std::thread m_demuxThread;
    std::thread m_videoThread;
    std::thread m_audioThread;
    
    // 状态
    std::atomic<bool> m_isRunning{false};
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_shouldSeek{false};
    double m_seekTarget = 0;
};
```

### 4.2 解封装模块

```cpp
// demuxer.cpp
#include "demuxer.h"

bool Demuxer::open(const std::string& filepath) {
    // 打开输入文件
    int ret = avformat_open_input(&m_formatCtx, filepath.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "无法打开文件: " << errbuf << std::endl;
        return false;
    }
    
    // 获取流信息
    ret = avformat_find_stream_info(m_formatCtx, nullptr);
    if (ret < 0) {
        std::cerr << "无法获取流信息" << std::endl;
        return false;
    }
    
    // 打印文件信息
    av_dump_format(m_formatCtx, 0, filepath.c_str(), 0);
    
    // 查找视频流
    m_videoStreamIndex = av_find_best_stream(m_formatCtx, AVMEDIA_TYPE_VIDEO, 
                                              -1, -1, nullptr, 0);
    // 查找音频流                                              
    m_audioStreamIndex = av_find_best_stream(m_formatCtx, AVMEDIA_TYPE_AUDIO,
                                              -1, -1, nullptr, 0);
    
    return true;
}

void Demuxer::readPacket(PacketQueue& videoQueue, PacketQueue& audioQueue) {
    AVPacket* packet = av_packet_alloc();
    
    while (m_isRunning) {
        // 控制队列大小，避免内存无限增长
        if (videoQueue.size() > 100 || audioQueue.size() > 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        int ret = av_read_frame(m_formatCtx, packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                // 文件结束
                break;
            }
            continue;
        }
        
        if (packet->stream_index == m_videoStreamIndex) {
            videoQueue.push(packet);
        } else if (packet->stream_index == m_audioStreamIndex) {
            audioQueue.push(packet);
        } else {
            av_packet_unref(packet);
        }
    }
    
    av_packet_free(&packet);
}
```

### 4.3 视频解码与渲染

```cpp
// videorenderer.cpp (使用 SDL2)
#include "videorenderer.h"

bool VideoRenderer::initialize(int width, int height) {
    // 创建 SDL 窗口
    m_window = SDL_CreateWindow("FFmpeg Player",
                                SDL_WINDOWPOS_UNDEFINED,
                                SDL_WINDOWPOS_UNDEFINED,
                                width, height,
                                SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!m_window) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // 创建 SDL 渲染器
    m_renderer = SDL_CreateRenderer(m_window, -1, 
                                     SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) {
        std::cerr << "SDL_CreateRenderer Error: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // 创建纹理 (YUV420P 格式)
    m_texture = SDL_CreateTexture(m_renderer,
                                  SDL_PIXELFORMAT_YV12,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  width, height);
    if (!m_texture) {
        std::cerr << "SDL_CreateTexture Error: " << SDL_GetError() << std::endl;
        return false;
    }
    
    return true;
}

void VideoRenderer::renderFrame(AVFrame* frame) {
    // 更新 YUV 纹理
    SDL_UpdateYUVTexture(m_texture, nullptr,
                         frame->data[0], frame->linesize[0],  // Y
                         frame->data[1], frame->linesize[1],  // U
                         frame->data[2], frame->linesize[2]); // V
    
    // 清屏
    SDL_RenderClear(m_renderer);
    
    // 复制纹理到渲染器
    SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
    
    // 呈现
    SDL_RenderPresent(m_renderer);
}
```

### 4.4 音频播放 (SDL2)

```cpp
// audiorenderer.cpp
#include "audiorenderer.h"

bool AudioRenderer::initialize(int sampleRate, int channels, AVSampleFormat format) {
    // 设置音频规格
    SDL_AudioSpec wanted_spec, obtained_spec;
    wanted_spec.freq = sampleRate;
    wanted_spec.format = AUDIO_S16SYS;  // 16位有符号整数
    wanted_spec.channels = channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = 1024;  // 音频缓冲区大小
    wanted_spec.callback = audioCallback;
    wanted_spec.userdata = this;
    
    // 打开音频设备
    if (SDL_OpenAudio(&wanted_spec, &obtained_spec) < 0) {
        std::cerr << "SDL_OpenAudio Error: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // 开始播放
    SDL_PauseAudio(0);
    
    return true;
}

void AudioRenderer::audioCallback(void* userdata, Uint8* stream, int len) {
    auto* renderer = static_cast<AudioRenderer*>(userdata);
    renderer->fillAudio(stream, len);
}

void AudioRenderer::fillAudio(Uint8* stream, int len) {
    SDL_memset(stream, 0, len);  // 清空缓冲区
    
    while (len > 0 && !m_audioQueue.empty()) {
        AVFrame* frame = m_audioQueue.front();
        
        // 音频重采样 (如果需要)
        // 将 FFmpeg 解码的音频数据转换为 SDL 需要的格式
        
        int dataSize = /* 计算音频数据大小 */;
        int copySize = (dataSize < len) ? dataSize : len;
        
        // 复制音频数据
        SDL_MixAudio(stream, frame->data[0], copySize, m_volume);
        
        len -= copySize;
        stream += copySize;
    }
}
```

### 4.5 音视频同步 (时钟)

```cpp
// clock.h
#pragma once
#include <chrono>

class Clock {
public:
    // 以音频时钟为主时钟
    void setAudioClock(double pts);
    double getAudioClock() const;
    
    // 视频同步到音频
    double getVideoDelay(double videoPts) const;
    
    void pause();
    void resume();

private:
    double m_audioClock = 0;  // 音频播放位置 (秒)
    std::chrono::steady_clock::time_point m_pauseTime;
    bool m_isPaused = false;
    double m_pauseDuration = 0;
};

// clock.cpp
void Clock::setAudioClock(double pts) {
    m_audioClock = pts;
}

double Clock::getVideoDelay(double videoPts) const {
    double diff = videoPts - m_audioClock;
    
    // 如果视频落后太多，加速播放
    if (diff < -0.05) {
        return 0;  // 立即显示
    }
    // 如果视频超前太多，延迟播放
    else if (diff > 0.05) {
        return diff;  // 等待 diff 秒
    }
    
    return 0;  // 正常播放
}
```

---

## 5. CMake 构建配置

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(FFmpegPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 FFmpeg
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED 
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
)

# 查找 SDL2
find_package(SDL2 REQUIRED)

# 源文件
set(SOURCES
    src/main.cpp
    src/player.cpp
    src/demuxer.cpp
    src/videodecoder.cpp
    src/audiodecoder.cpp
    src/videorenderer.cpp
    src/audiorenderer.cpp
    src/packetqueue.cpp
    src/clock.cpp
    src/utils.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    include
)

target_link_libraries(${PROJECT_NAME}
    ${FFMPEG_LIBRARIES}
    ${SDL2_LIBRARIES}
)

# Windows 需要复制 DLL
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL2::SDL2>
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()
```

---

## 6. 依赖安装

### Windows

```powershell
# 使用 vcpkg 安装
vcpkg install ffmpeg sdl2

# 或手动下载
# FFmpeg: https://www.gyan.dev/ffmpeg/builds/
# SDL2: https://github.com/libsdl-org/SDL/releases
```

### macOS

```bash
brew install ffmpeg sdl2
```

### Ubuntu/Debian

```bash
sudo apt-get install ffmpeg libavformat-dev libavcodec-dev \
    libavutil-dev libswscale-dev libswresample-dev libsdl2-dev
```

---

## 7. 进阶优化方向

### 7.1 硬件加速解码
```cpp
// 使用 DXVA/D3D11VA (Windows) 或 VideoToolbox (macOS) 或 VAAPI (Linux)
av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_DXVA2, nullptr, nullptr, 0);
m_codecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
```

### 7.2 OpenGL 渲染 (高性能)
```cpp
// 使用 OpenGL shader 进行 YUV→RGB 转换
// 比 SDL 的 CPU 转换效率更高
```

### 7.3 字幕支持
```cpp
// 集成 libass 或自行解析 SRT/ASS
```

---

## 8. 与 Qt 方案对比

| 特性 | FFmpeg + SDL | Qt Multimedia |
|------|-------------|---------------|
| **代码量** | 2000+ 行 | 200 行 |
| **开发周期** | 2-4 周 | 2-4 天 |
| **可控性** | 极高 | 中等 |
| **UI 美观** | 需自行实现 | Qt 自带 |
| **学习价值** | 极高 | 中等 |
| **适用场景** | 嵌入式/专业播放器 | 通用桌面应用 |

---

## 总结

**FFmpeg + SDL 方案**适合以下场景：
1. 需要深度定制播放器行为
2. 嵌入式系统资源受限
3. 学习音视频原理
4. 开发专业级播放器 (如 VLC、MPV 早期版本)

如果你希望快速上手，可以先从这个简化版本开始，逐步添加功能。
