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
#include <optional>

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

// 内封字幕回调：ptsMs=字幕开始时间(毫秒)，text=字幕文本
using SubtitleTextCallback = std::function<void(int64_t ptsMs, const std::string& text)>;

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
    void setHardwareDecodingEnabled(bool enabled);
    bool hardwareDecodingEnabled() const;
    void setAudioFilterConfig(const AudioFilterConfig& config);
    AudioFilterConfig audioFilterConfig() const;

    void setVideoFilterConfig(const VideoFilterConfig& config);
    VideoFilterConfig videoFilterConfig() const;

    // 音频同步偏移（毫秒）：正值=音频延后，负值=音频提前
    void setAudioSyncOffsetMs(int64_t offsetMs);
    void adjustAudioSync(int64_t deltaMs);
    int64_t audioSyncOffsetMs() const;

    void setStateCallback(StateCallback callback);
    void setPositionCallback(PositionCallback callback);
    void setDurationCallback(DurationCallback callback);
    void setErrorCallback(ErrorCallback callback);
    void setSpeedCallback(SpeedCallback callback);
    void setVolumeCallback(VolumeCallback callback);
    void setMuteCallback(MuteCallback callback);
    void setVideoFrameCallback(VideoFrameCallback callback);
    void setNetworkStateCallback(NetworkStateCallback callback);
    void setSubtitleTextCallback(SubtitleTextCallback callback);

    // 图形字幕（PGS）位图：解码线程产生，主线程消费
    std::optional<SubtitleBitmap> popSubtitleBitmap();
    void clearSubtitleBitmaps();

    bool isPreloading() const;
    bool checkPreloadComplete();

    bool getVideoFrame(int64_t targetPtsMs, VideoFrame& frame);

    // 进度条缩略图：独立解码，不占用主解码队列
    void requestPreview(int64_t ptsMs);
    bool takePreviewFrame(VideoFrame& frame);

    std::vector<ChapterInfo> chapters() const;
    void setChapters(const std::vector<ChapterInfo>& chapters);
    MediaInfo mediaInfo() const;

    // 多音轨/多字幕轨支持
    const std::vector<TrackInfo>& audioTracks() const { return m_audioTracks; }
    const std::vector<TrackInfo>& subtitleTracks() const { return m_subtitleTracks; }
    int currentAudioTrack() const { return m_currentAudioTrack; }
    int currentSubtitleTrack() const { return m_currentSubtitleTrack; }
    // 切换音轨（trackIndex 为 m_audioTracks 的下标，-1 表示关闭）
    bool setAudioTrack(int trackIndex);
    // 切换内封字幕轨（trackIndex 为 m_subtitleTracks 的下标，-1 表示关闭内封字幕）
    bool setSubtitleTrack(int trackIndex);

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

    // 多轨道支持
    void scanTracks();
    std::string streamTitle(AVStream* stream) const;
    std::string streamLanguage(AVStream* stream) const;
    bool openAudioStream(int streamIndex);
    bool openSubtitleStream(int streamIndex);
    void closeSubtitleStream();

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

    // 视频基础参数滤镜 (eq / hue)
    VideoFilterConfig m_videoFilterConfig;
    mutable std::mutex m_videoFilterMutex;
    AVFilterGraph* m_videoFilterGraph = nullptr;
    AVFilterContext* m_videoFilterSrc = nullptr;
    AVFilterContext* m_videoFilterSink = nullptr;
    int m_videoFilterWidth = 0;
    int m_videoFilterHeight = 0;
    AVPixelFormat m_videoFilterFormat = AV_PIX_FMT_NONE;
    std::string m_videoFilterDescription;

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
    // 多轨道支持
    std::vector<TrackInfo> m_audioTracks;
    std::vector<TrackInfo> m_subtitleTracks;
    int m_currentAudioTrack = 0;       // m_audioTracks 下标
    int m_currentSubtitleTrack = -1;   // m_subtitleTracks 下标，-1=关闭内封字幕
    StreamContext m_subtitleCtx;       // 当前内封字幕流上下文
    std::mutex m_subtitleBitmapMutex;
    std::deque<SubtitleBitmap> m_subtitleBitmapQueue;
    std::atomic<int64_t> m_position{0};
    std::atomic<double> m_playbackSpeed{1.0};
    std::atomic<int> m_volume{100};
    std::atomic<bool> m_muted{false};
    std::atomic<bool> m_hardwareDecodingEnabled{true};
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
    std::atomic<int64_t> m_audioSyncOffsetMs{0};  // 音频同步偏移：正值=音频延后

    // Video frame queue with PTS ordering
    std::deque<VideoFrame> m_videoFrameQueue;
    std::mutex m_videoQueueMutex;
    static constexpr size_t kMaxVideoQueueSize = 10;
    static constexpr int kMaxAudioQueueMs = 1000;
    static constexpr int kPreloadAudioMs = 40;
    static constexpr int kPreloadTimeoutMs = 1000;
    void pushVideoFrame(VideoFrame&& frame);

    static constexpr int kPreviewWidth = 120;
    static constexpr int kPreviewHeight = 68;
    void startPreviewDecoder();
    void stopPreviewDecoder();
    void previewLoop();
    bool openPreviewContext();
    void closePreviewContext();
    bool decodePreviewFrame(int64_t ptsMs, VideoFrame& out);

    std::thread m_previewThread;
    std::mutex m_previewMutex;
    std::condition_variable m_previewCv;
    std::atomic<bool> m_previewAbort{false};
    std::string m_previewPath;
    AVFormatContext* m_previewFmt = nullptr;
    AVCodecContext* m_previewCodec = nullptr;
    int m_previewStreamIndex = -1;
    int64_t m_previewRequestPts = -1;
    bool m_previewHasRequest = false;
    VideoFrame m_previewFrame;
    bool m_previewReady = false;

    std::string buildVideoFilterDescription() const;
    bool ensureVideoFilterGraph(AVFrame* frame);
    void cleanupVideoFilterGraph();
    AVFrame* processVideoFilter(AVFrame* frame);

    StateCallback m_stateCallback;
    PositionCallback m_positionCallback;
    DurationCallback m_durationCallback;
    ErrorCallback m_errorCallback;
    SpeedCallback m_speedCallback;
    VolumeCallback m_volumeCallback;
    MuteCallback m_muteCallback;
    VideoFrameCallback m_videoFrameCallback;
    NetworkStateCallback m_networkStateCallback;
    SubtitleTextCallback m_subtitleTextCallback;
};

} // namespace VideoPlay
