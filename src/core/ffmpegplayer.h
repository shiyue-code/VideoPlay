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
#include <libavutil/hwcontext.h>
#include <libavutil/mem.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
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
using NetworkStateCallback = std::function<void(NetworkState)>;

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
    void setAudioFilterConfig(const AudioFilterConfig& config);
    AudioFilterConfig audioFilterConfig() const;

    void setStateCallback(StateCallback callback);
    void setPositionCallback(PositionCallback callback);
    void setDurationCallback(DurationCallback callback);
    void setErrorCallback(ErrorCallback callback);
    void setSpeedCallback(SpeedCallback callback);
    void setVolumeCallback(VolumeCallback callback);
    void setMuteCallback(MuteCallback callback);
    void setVideoFrameCallback(VideoFrameCallback callback);
    void setNetworkStateCallback(NetworkStateCallback callback);

    bool isPreloading() const;
    bool checkPreloadComplete();

    bool getVideoFrame(int64_t targetPtsMs, VideoFrame& frame);

    std::vector<ChapterInfo> chapters() const;
    void setChapters(const std::vector<ChapterInfo>& chapters);
    MediaInfo mediaInfo() const;

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
        int lastSrcRate = 0;
        int lastSrcChannels = 0;
        AVSampleFormat lastSrcFormat = AV_SAMPLE_FMT_NONE;
    };

    void initialize();
    void cleanup();
    void decodeLoop();
    bool initializeVideoContext();
    bool initializeAudioContext();
    VideoFrame convertVideoFrame(AVFrame* frame);
    std::vector<float> resampleAudioFrame(AVFrame* frame);
    std::vector<float> processAudioFrame(AVFrame* frame);
    void handleSeek(int64_t positionMs);
    void synchronizeVideo(double pts);
    bool setupHardwareDecoder(const AVCodec* codec);
    void releaseHardwareDecoder();
    static AVPixelFormat selectHardwareFormat(AVCodecContext* ctx, const AVPixelFormat* pixFmts);
    bool ensureAudioFilterGraph(AVFrame* frame);
    void cleanupAudioFilterGraph();
    std::string buildAudioFilterDescription() const;
    void setNetworkState(NetworkState state);

    AVFormatContext* m_formatContext = nullptr;
    VideoContext m_videoCtx;
    AudioContext m_audioCtx;
    AudioFilterConfig m_audioFilterConfig;
    bool m_audioFilterRuntimeDisabled = false;
    AVFilterGraph* m_audioFilterGraph = nullptr;
    AVFilterContext* m_audioFilterSrc = nullptr;
    AVFilterContext* m_audioFilterSink = nullptr;
    int m_audioFilterSampleRate = 0;
    int m_audioFilterChannels = 0;
    AVSampleFormat m_audioFilterSampleFormat = AV_SAMPLE_FMT_NONE;
    std::string m_audioFilterDescription;
    mutable std::mutex m_audioFilterMutex;
    MediaInfo m_mediaInfo;
    AVBufferRef* m_hwDeviceCtx = nullptr;
    AVPixelFormat m_hwPixelFormat = AV_PIX_FMT_NONE;
    AVHWDeviceType m_hwDeviceType = AV_HWDEVICE_TYPE_NONE;
    
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
    std::atomic<NetworkState> m_networkState{NetworkState::Idle};
    SourceType m_sourceType = SourceType::LocalFile;
    
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
    NetworkStateCallback m_networkStateCallback;
};

} // namespace VideoPlay
