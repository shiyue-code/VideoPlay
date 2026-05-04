#pragma once

#include "core/common.h"
#include "core/audioplayer.h"

#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <deque>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace VideoPlay {

// VideoFrame is defined in common.h

using StateCallback = std::function<void(PlaybackState)>;
using PositionCallback = std::function<void(int64_t)>;
using DurationCallback = std::function<void(int64_t)>;
using ErrorCallback = std::function<void(const std::string&)>;
using SpeedCallback = std::function<void(double)>;
using VolumeCallback = std::function<void(int)>;
using MuteCallback = std::function<void(bool)>;
using VideoFrameCallback = std::function<void(VideoFrame)>;

class FFmpegPlayer {
public:
    FFmpegPlayer();
    ~FFmpegPlayer();

    FFmpegPlayer(const FFmpegPlayer&) = delete;
    FFmpegPlayer& operator=(const FFmpegPlayer&) = delete;

    bool loadFile(const std::string& filePath);
    void closeFile();
    std::string filePath() const;

    void play();
    void pause();
    void stop();
    void seek(int64_t positionMs);

    PlaybackState state() const;
    int64_t position() const;
    int64_t duration() const;
    int64_t audioPositionMs() const;

    void setPlaybackSpeed(double speed);
    double playbackSpeed() const;
    void setVolume(int volume);
    int volume() const;
    void setMuted(bool muted);
    bool isMuted() const;

    void setStateCallback(StateCallback callback);
    void setPositionCallback(PositionCallback callback);
    void setDurationCallback(DurationCallback callback);
    void setErrorCallback(ErrorCallback callback);
    void setSpeedCallback(SpeedCallback callback);
    void setVolumeCallback(VolumeCallback callback);
    void setMuteCallback(MuteCallback callback);
    void setVideoFrameCallback(VideoFrameCallback callback);

    bool isPreloading() const;
    bool checkPreloadComplete();

    bool getVideoFrame(int64_t targetPtsMs, VideoFrame& frame);

    std::vector<ChapterInfo> chapters() const;
    void setChapters(const std::vector<ChapterInfo>& chapters);

private:
    struct StreamContext {
        AVStream* stream = nullptr;
        AVCodecContext* codecContext = nullptr;
        int64_t startTime = 0;
    };

    struct VideoContext : StreamContext {
        SwsContext* swsContext = nullptr;
        int lastWidth = 0;
        int lastHeight = 0;
        AVPixelFormat lastFormat = AV_PIX_FMT_NONE;
        int lastColorSpace = SWS_CS_DEFAULT;
        int lastSrcRange = 0;
        std::vector<uint8_t> swsBuffer;
        int swsStride = 0;
    };

    struct AudioContext : StreamContext {
        SwrContext* swrContext = nullptr;
        AudioFormat format;
    };

    void initialize();
    void cleanup();
    void decodeLoop();
    bool initializeVideoContext();
    bool initializeAudioContext();
    VideoFrame convertVideoFrame(AVFrame* frame);
    std::vector<float> resampleAudioFrame(AVFrame* frame);
    void handleSeek(int64_t positionMs);
    void synchronizeVideo(double pts);

    AVFormatContext* m_formatContext = nullptr;
    VideoContext m_videoCtx;
    AudioContext m_audioCtx;
    
    std::unique_ptr<AudioPlayer> m_audioPlayer;
    std::thread m_decodeThread;
    
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::condition_variable m_decodeCondition;
    
    std::string m_filePath;
    int64_t m_duration = 0;
    std::vector<ChapterInfo> m_chapters;
    std::atomic<int64_t> m_position{0};
    std::atomic<double> m_playbackSpeed{1.0};
    std::atomic<int> m_volume{100};
    std::atomic<bool> m_muted{false};
    std::atomic<PlaybackState> m_state{PlaybackState::Stopped};
    
    std::atomic<bool> m_abortRequest{false};
    std::atomic<bool> m_seekRequested{false};
    std::atomic<int64_t> m_seekPosition{0};
    std::atomic<int64_t> m_audioBaseMs{0};
    std::atomic<bool> m_preloading{false};
    std::chrono::steady_clock::time_point m_preloadStartTime;
    
    double m_audioClock = 0.0;
    double m_videoClock = 0.0;
    double m_frameTimer = 0.0;

    // Video frame queue with PTS ordering
    std::deque<VideoFrame> m_videoFrameQueue;
    std::mutex m_videoQueueMutex;
    static constexpr size_t kMaxVideoQueueSize = 10;
    static constexpr int kPreloadAudioMs = 40;
    static constexpr int kPreloadTimeoutMs = 1000;
    void pushVideoFrame(VideoFrame&& frame);

    StateCallback m_stateCallback;
    PositionCallback m_positionCallback;
    DurationCallback m_durationCallback;
    ErrorCallback m_errorCallback;
    SpeedCallback m_speedCallback;
    VolumeCallback m_volumeCallback;
    MuteCallback m_muteCallback;
    VideoFrameCallback m_videoFrameCallback;
};

} // namespace VideoPlay
