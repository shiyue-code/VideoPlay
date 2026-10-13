#pragma once

#include "core/common.h"
#include "core/audioplayer.h"
#include "core/videoadjuster.h"

#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <deque>
#include <map>
#include <iterator>
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

// 内封字幕回调：startMs/endMs=字幕显示区间(毫秒)，text=纯文本(已剥离标签)
using SubtitleTextCallback = std::function<void(int64_t startMs, int64_t endMs, const std::string& text)>;

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
    // 实时统计：视频队列帧数 / 音频已入队毫秒数
    int videoQueueSize() const;
    int64_t audioQueuedMs() const;

    void setPlaybackSpeed(double speed);
    double playbackSpeed() const;
    void setVolume(int volume);
    int volume() const;
    void setMuted(bool muted);
    bool isMuted() const;
    void setHardwareDecodingEnabled(bool enabled);
    bool hardwareDecodingEnabled() const;
    void setAudioOutputDevice(SDL_AudioDeviceID deviceId);
    SDL_AudioDeviceID audioOutputDevice() const;
    void setAudioFilterConfig(const AudioFilterConfig& config);
    AudioFilterConfig audioFilterConfig() const;
    // 声道映射（单声道/左右交换）：重采样后就地处理，不经滤镜图
    void setChannelMapMode(ChannelMapMode mode);
    ChannelMapMode channelMapMode() const;

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
    // 逐帧回退专用 seek：以 anchorPtsMs（当前显示帧 pts）为上界，
    // 落点是解码序上 pts < anchor 的最大帧，即真正的上一帧。
    // 与 fps/容器精度/B 帧重排深度都无关
    void seekStepBack(int64_t anchorPtsMs);
    // 暂停 seek/逐帧回退的落点帧：等补解码窗口收齐后取帧。
    // 普通 seek 取队首（pts 最小）；回退落点取队尾（pts < anchor 最大），
    // 队列为空且回退落点就绪时返回 true 且 frame 为空（已是第一帧）
    bool getStepLandingFrame(VideoFrame& frame);
    // +1 步进专用：取 pts 大于 prevPtsMs 的第一帧（逐帧步进的下一帧）。
    // 更早的帧不移除（它们构成 -1 落点可直接命中的回退窗口）；
    // 若队列里没有更晚的帧，顺带把已消费的最旧帧迁入历史腾队位，
    // 让暂停态下被背压停住的解码线程继续产帧
    bool getStepForwardFrame(int64_t prevPtsMs, VideoFrame& frame);

    // 画面调节参数变化后用基础帧就地重算 frame.data（仅主线程调用）；
    // 无需更新或无基础帧返回 false
    bool refreshVideoFrame(VideoFrame& frame);

    // 进度条缩略图：独立解码，不占用主解码队列
    void requestPreview(int64_t ptsMs);
    bool takePreviewFrame(VideoFrame& frame);

    std::vector<ChapterInfo> chapters() const;
    void setChapters(const std::vector<ChapterInfo>& chapters);
    MediaInfo mediaInfo() const;

    // FFmpeg 运行时版本串（av_version_info）
    static std::string ffmpegVersion();

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
    // decodeLoop 拆分出的子步骤（均只在解码线程调用）
    bool waitForDecodeSlot();
    bool handleReadFailure(int ret, int& consecutiveReadErrors);
    void drainEndOfStream(AVFrame* vframe, AVFrame* aframe);
    void decodeVideoPacket(AVPacket* packet, AVFrame* frame, double dtRead);
    void decodeAudioPacket(AVPacket* packet, AVFrame* frame, double dtRead);
    void decodeSubtitlePacket(AVPacket* packet);
    void receiveVideoFrames(AVFrame* frame, double dtRead);
    void receiveAudioFrames(AVFrame* frame, double dtRead);
    void handleDecodedVideoFrame(AVFrame* frame, double dtRead);
    void handleDecodedAudioFrame(AVFrame* frame, double dtRead);
    // 流时间戳转媒体毫秒：以容器 start_time 为基准（NOPTS 或空流返回 0）
    int64_t streamPtsToMs(const AVStream* stream, int64_t pts) const;
    bool initializeVideoContext();
    bool initializeAudioContext();
    VideoFrame convertVideoFrame(AVFrame* frame);
    std::vector<float> resampleAudioFrame(AVFrame* frame);
    std::vector<float> processAudioFrame(AVFrame* frame);
    void handleSeek(int64_t positionMs);
    void startFallbackClock();
    void stopFallbackClock();
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
    double m_audioFilterTempo = 1.0;   // 当前滤镜图实际生效的 atempo（m_audioFilterMutex 保护）
    mutable std::mutex m_audioFilterMutex;

    // 阶段 1 视频滤镜（HDR 映射/去隔行，解码线程内构建）
    // 阶段 2 画面调节（eq/hue + BGRA）由两个 VideoAdjuster 分别承担
    VideoFilterConfig m_videoFilterConfig;
    mutable std::mutex m_videoFilterMutex;
    AVFilterGraph* m_videoFilterGraph = nullptr;
    AVFilterContext* m_videoFilterSrc = nullptr;
    AVFilterContext* m_videoFilterSink = nullptr;
    int m_videoFilterWidth = 0;
    int m_videoFilterHeight = 0;
    AVPixelFormat m_videoFilterFormat = AV_PIX_FMT_NONE;
    std::string m_videoFilterDescription;
    // 画面调节代次：eq/hue 相关参数变化时 +1，帧上记录生成时的代次
    std::atomic<uint64_t> m_videoAdjustGeneration{1};
    VideoAdjuster m_decodeAdjuster;    // 解码线程专用
    VideoAdjuster m_refreshAdjuster;   // 主线程专用（refreshVideoFrame）

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
    // 无音轨文件的主时钟回退：以 m_audioBaseMs 为基准，叠加挂钟流逝×倍速。
    // 起点的 steady_clock 纳秒数存为原子量，解码/渲染线程都会读写
    std::atomic<int64_t> m_fallbackClockStartNs{0};
    std::atomic<bool> m_fallbackClockRunning{false};
    std::atomic<double> m_playbackSpeed{1.0};
    std::atomic<int> m_volume{100};
    std::atomic<bool> m_muted{false};
    std::atomic<bool> m_hardwareDecodingEnabled{true};
    SDL_AudioDeviceID m_audioOutputDeviceId = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    std::atomic<ChannelMapMode> m_channelMapMode{ChannelMapMode::Original};
    std::atomic<PlaybackState> m_state{PlaybackState::Stopped};
    std::atomic<NetworkState> m_networkState{NetworkState::Idle};
    SourceType m_sourceType = SourceType::LocalFile;
    
    std::atomic<bool> m_abortRequest{false};
    std::atomic<bool> m_seekRequested{false};
    std::atomic<int64_t> m_seekPosition{0};
    std::atomic<int64_t> m_audioBaseMs{0};
    std::atomic<bool> m_preloading{false};
    std::chrono::steady_clock::time_point m_preloadStartTime;
    
    std::atomic<int64_t> m_audioSyncOffsetMs{0};  // 音频同步偏移：正值=音频延后

    // decodeLoop 的 PERF 帧计数（仅解码线程读写）
    int m_vframeCount = 0;
    int m_aframeCount = 0;

    // 音频媒体时间跟踪（仅解码线程访问；seek/stop/切轨时由持有方置位）
    double m_audioNextPtsMs = -1.0;        // 下一段入队数据的起始媒体时间
    double m_audioInputExpectedMs = -1.0;  // 解码时间戳连续性检测
    int64_t m_audioTrimBeforeMs = -1;      // seek 后丢弃整帧都早于此时间的音频帧
    int64_t m_videoTrimBeforeMs = -1;      // seek 后丢弃 pts 早于此时间的视频帧

    // Video frame queue（按 pts 排序，B 帧重排的解码顺序不会破坏单调性）
    std::deque<VideoFrame> m_videoFrameQueue;
    mutable std::mutex m_videoQueueMutex;
    static constexpr size_t kMaxVideoQueueSize = 10;
    // paused seek 补解码窗口期间允许队列超限，避免落点帧在 B 重排
    // 窗口收齐前被队首丢弃
    static constexpr size_t kSeekBurstQueueSize = 64;
    // 目标到达后再补约 800ms 的解码量，覆盖 B 帧重排窗口
    static constexpr int64_t kSeekGuardMs = 800;
    std::atomic<bool> m_seekBurstActive{false};
    std::atomic<int64_t> m_seekLandingReady{-1};  // paused seek 落点已收齐可消费
    std::atomic<bool> m_landingIsStepBack{false}; // 本次落点按回退模式取 pts<anchor 最大帧
    // 逐帧回退锚点（当前显示帧 pts）：>=0 时为回退补解码模式，
    // 落点是 pts < anchor 的最大帧；>=anchor 的帧也入队供 +1 步进消费
    std::atomic<int64_t> m_stepBackAnchorMs{-1};
    int64_t m_stepBackDecodedMaxMs = -1;          // 回退窗口内已解出的最大 pts（仅解码线程）
    int m_stepBackFwdCount = 0;                   // 回退窗口内已解出的 >=anchor 帧数（仅解码线程）
    static constexpr int kStepBackFwdKeep = 32;   // 回退补解码保留的锚点后帧数（覆盖重排+步进缓冲）

    // 步进历史（均持 m_videoQueueMutex）：离开队列的消费帧按 pts 入此表，
    // 与队列并集构成连续的已解码窗口——-1 落点可直接命中解码序上的相邻
    // 前帧，免 seek、免解码。被丢弃而未入历史的帧抬 m_stepDropWatermark：
    // 水位以下的候选可能与锚点隔着被挤掉的帧，不可信，回退走真实 seek
    std::map<int64_t, VideoFrame> m_stepHistory;
    int64_t m_stepDropWatermark = -1;
    size_t m_stepHistBytes = 0;
    static constexpr size_t kStepHistByteCap = 256ull * 1024 * 1024;
    void pushStepHistory(VideoFrame frame);

    static constexpr int kMaxAudioQueueMs = 1000;
    static constexpr int kPreloadAudioMs = 40;
    static constexpr int kPreloadTimeoutMs = 1000;
    void pushVideoFrame(VideoFrame&& frame);

    static constexpr int kPreviewWidth = 120;
    static constexpr int kPreviewHeight = 68;
    // 缓存桶粒度，与渲染层请求量化（500ms）一致
    static constexpr int64_t kPreviewBucketMs = 500;
    // 桶上限：120x68 BGRA ≈ 32KB/帧，96 桶约 3MB
    static constexpr size_t kPreviewCacheCap = 96;
    // 目标在上次解出帧之后该跨度内时顺序续解，不再 seek
    static constexpr int64_t kPreviewForwardMs = 4000;
    void startPreviewDecoder();
    void stopPreviewDecoder();
    void previewLoop();
    bool openPreviewContext();
    void closePreviewContext();
    bool decodePreviewFrame(int64_t ptsMs, VideoFrame& out, uint32_t seq);
    // 解码帧转 120x68 BGRA 缩略图（信箱填充）；仅预览线程调用
    bool convertToPreviewFrame(const AVFrame* src, int64_t ptsMs, VideoFrame& out);
    // 桶内无帧才插入并做 LRU 淘汰；内部加锁
    void cachePreviewFrame(int64_t bucketPts, const VideoFrame& frame);

    std::thread m_previewThread;
    std::mutex m_previewMutex;
    std::condition_variable m_previewCv;
    std::atomic<bool> m_previewAbort{false};
    // 请求代次：解码循环轮询它，发现更新的请求立即放弃当前解码
    std::atomic<uint32_t> m_previewRequestSeq{0};
    std::string m_previewPath;
    AVFormatContext* m_previewFmt = nullptr;
    AVCodecContext* m_previewCodec = nullptr;
    int m_previewStreamIndex = -1;
    int64_t m_previewRequestPts = -1;
    bool m_previewHasRequest = false;
    VideoFrame m_previewFrame;
    bool m_previewReady = false;
    // 最近交付的请求 pts，避免悬停同一点位反复解码（m_previewMutex 保护）
    int64_t m_previewServedPts = -1;
    // 500ms 桶 → 缩略图，deque 记 LRU 顺序（均受 m_previewMutex 保护）
    std::map<int64_t, VideoFrame> m_previewCache;
    std::deque<int64_t> m_previewCacheOrder;
    // 以下仅预览线程访问：上次解码产出的最大帧 pts，用于判断能否顺序续解
    bool m_previewCanContinue = false;
    int64_t m_previewLastPts = -1;

    bool isHdrFrame(const AVFrame* frame) const;
    std::string buildVideoFilterDescription(const AVFrame* frame) const;
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
