#include "core/ffmpegplayer.h"
#include "core/ffmpegplayer_internal.h"
#include "subtitles/subtitleparser.h"

#include <algorithm>

namespace VideoPlay {

std::string FFmpegPlayer::streamTitle(AVStream* stream) const {
    if (!stream) return {};
    AVDictionaryEntry* entry = av_dict_get(stream->metadata, "title", nullptr, 0);
    return entry ? std::string(entry->value) : std::string{};
}

std::string FFmpegPlayer::streamLanguage(AVStream* stream) const {
    if (!stream) return {};
    // 优先读取 BCP-47 / IETF 语言标签（MKV/WebM 常见），信息更丰富
    for (const char* key : {"language_ietf", "BCP47", "LanguageIETF", "Language", "language"}) {
        AVDictionaryEntry* entry = av_dict_get(stream->metadata, key, nullptr, 0);
        if (entry && entry->value[0]) {
            return std::string(entry->value);
        }
    }
    return {};
}

void FFmpegPlayer::scanTracks() {
    m_audioTracks.clear();
    m_subtitleTracks.clear();
    if (!m_formatContext) return;

    for (unsigned int i = 0; i < m_formatContext->nb_streams; i++) {
        AVStream* stream = m_formatContext->streams[i];
        AVCodecParameters* codecPar = stream->codecpar;
        if (!codecPar) continue;

        if (codecPar->codec_type == AVMEDIA_TYPE_AUDIO) {
            TrackInfo t;
            t.streamIndex = static_cast<int>(i);
            t.language = streamLanguage(stream);
            t.title = streamTitle(stream);
            const AVCodecDescriptor* desc = avcodec_descriptor_get(codecPar->codec_id);
            t.codecName = desc ? desc->name : "unknown";
            t.channels = codecPar->ch_layout.nb_channels;
            t.sampleRate = codecPar->sample_rate;
            t.isDefault = (stream->disposition & AV_DISPOSITION_DEFAULT) != 0;
            m_audioTracks.push_back(t);
        } else if (codecPar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            // 跳过附加图片字幕（cover）
            TrackInfo t;
            t.streamIndex = static_cast<int>(i);
            t.language = streamLanguage(stream);
            t.title = streamTitle(stream);
            const AVCodecDescriptor* desc = avcodec_descriptor_get(codecPar->codec_id);
            t.codecName = desc ? desc->name : "unknown";
            t.isDefault = (stream->disposition & AV_DISPOSITION_DEFAULT) != 0;
            t.isForced = (stream->disposition & AV_DISPOSITION_FORCED) != 0;
            // 判断字幕类型
            if (codecPar->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE) {
                t.subtitleType = "pgs";
            } else if (codecPar->codec_id == AV_CODEC_ID_ASS ||
                       codecPar->codec_id == AV_CODEC_ID_SSA) {
                t.subtitleType = "ass";
            } else if (codecPar->codec_id == AV_CODEC_ID_SUBRIP ||
                       codecPar->codec_id == AV_CODEC_ID_SRT) {
                t.subtitleType = "srt";
            } else if (codecPar->codec_id == AV_CODEC_ID_WEBVTT) {
                t.subtitleType = "vtt";
            } else {
                t.subtitleType = "text";
            }
            m_subtitleTracks.push_back(t);
        }
    }

    // 确定当前音轨下标（匹配 m_audioCtx.stream）
    m_currentAudioTrack = 0;
    if (m_audioCtx.stream) {
        for (size_t i = 0; i < m_audioTracks.size(); i++) {
            if (m_audioTracks[i].streamIndex == static_cast<int>(m_audioCtx.stream->index)) {
                m_currentAudioTrack = static_cast<int>(i);
                break;
            }
        }
    }
    m_currentSubtitleTrack = -1;

    logger().info("Scanned tracks: " + std::to_string(m_audioTracks.size()) + " audio, " +
                  std::to_string(m_subtitleTracks.size()) + " subtitle");
    for (size_t i = 0; i < m_audioTracks.size(); i++) {
        const auto& t = m_audioTracks[i];
        logger().info("  Audio " + std::to_string(i) + ": stream=" + std::to_string(t.streamIndex) +
                      " lang=" + t.language + " title=" + t.title +
                      " " + std::to_string(t.channels) + "ch/" + std::to_string(t.sampleRate) + "Hz");
    }
    for (size_t i = 0; i < m_subtitleTracks.size(); i++) {
        const auto& t = m_subtitleTracks[i];
        logger().info("  Subtitle " + std::to_string(i) + ": stream=" + std::to_string(t.streamIndex) +
                      " lang=" + t.language + " title=" + t.title +
                      " type=" + t.subtitleType + (t.isForced ? " [forced]" : ""));
    }
}

bool FFmpegPlayer::openAudioStream(int streamIndex) {
    if (!m_formatContext || streamIndex < 0 ||
        streamIndex >= static_cast<int>(m_formatContext->nb_streams)) {
        return false;
    }
    AVStream* stream = m_formatContext->streams[streamIndex];
    if (!stream || stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) return false;

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return false;

    // 关闭旧音频上下文
    if (m_audioCtx.swrContext) {
        swr_free(&m_audioCtx.swrContext);
        m_audioCtx.lastSrcRate = 0;
        m_audioCtx.lastSrcChannels = 0;
        m_audioCtx.lastSrcFormat = AV_SAMPLE_FMT_NONE;
    }
    {
        std::lock_guard<std::mutex> filterLock(m_audioFilterMutex);
        cleanupAudioFilterGraph();
    }
    if (m_audioCtx.codecContext) {
        avcodec_free_context(&m_audioCtx.codecContext);
    }

    m_audioCtx.stream = stream;
    m_audioCtx.codecContext = avcodec_alloc_context3(codec);
    if (!m_audioCtx.codecContext) return false;
    avcodec_parameters_to_context(m_audioCtx.codecContext, stream->codecpar);
    if (avcodec_open2(m_audioCtx.codecContext, codec, nullptr) < 0) {
        avcodec_free_context(&m_audioCtx.codecContext);
        m_audioCtx.stream = nullptr;
        return false;
    }
    m_audioCtx.startTime = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    initializeAudioContext();
    return true;
}

bool FFmpegPlayer::openSubtitleStream(int streamIndex) {
    if (!m_formatContext || streamIndex < 0 ||
        streamIndex >= static_cast<int>(m_formatContext->nb_streams)) {
        return false;
    }
    AVStream* stream = m_formatContext->streams[streamIndex];
    if (!stream || stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) return false;

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return false;

    closeSubtitleStream();
    m_subtitleCtx.stream = stream;
    m_subtitleCtx.codecContext = avcodec_alloc_context3(codec);
    if (!m_subtitleCtx.codecContext) return false;
    avcodec_parameters_to_context(m_subtitleCtx.codecContext, stream->codecpar);
    if (avcodec_open2(m_subtitleCtx.codecContext, codec, nullptr) < 0) {
        avcodec_free_context(&m_subtitleCtx.codecContext);
        m_subtitleCtx.stream = nullptr;
        return false;
    }
    m_subtitleCtx.startTime = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    logger().info("Opened subtitle stream " + std::to_string(streamIndex));
    return true;
}

void FFmpegPlayer::closeSubtitleStream() {
    if (m_subtitleCtx.codecContext) {
        avcodec_free_context(&m_subtitleCtx.codecContext);
    }
    m_subtitleCtx.stream = nullptr;
    m_subtitleCtx.startTime = 0;
    clearSubtitleBitmaps();
}

std::optional<SubtitleBitmap> FFmpegPlayer::popSubtitleBitmap() {
    std::lock_guard<std::mutex> lock(m_subtitleBitmapMutex);
    if (m_subtitleBitmapQueue.empty()) {
        return std::nullopt;
    }
    SubtitleBitmap bm = std::move(m_subtitleBitmapQueue.front());
    m_subtitleBitmapQueue.pop_front();
    return bm;
}

void FFmpegPlayer::clearSubtitleBitmaps() {
    std::lock_guard<std::mutex> lock(m_subtitleBitmapMutex);
    m_subtitleBitmapQueue.clear();
}

bool FFmpegPlayer::setAudioTrack(int trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_audioTracks.size())) {
        return false;
    }
    if (trackIndex == m_currentAudioTrack && m_audioCtx.stream) {
        return true; // 已是当前音轨
    }
    const TrackInfo& t = m_audioTracks[trackIndex];
    logger().info("Switching audio track to " + std::to_string(trackIndex) +
                  " (stream " + std::to_string(t.streamIndex) + ")");

    // 记住当前播放位置
    int64_t curPos = m_position.load();

    bool wasPlaying = (m_state == PlaybackState::Playing);
    if (wasPlaying) {
        pause();
    }

    bool ok = openAudioStream(t.streamIndex);
    if (!ok) {
        logger().error("Failed to open audio stream " + std::to_string(t.streamIndex));
        if (wasPlaying) play();
        return false;
    }
    m_currentAudioTrack = trackIndex;

    // 重置音频播放器与媒体时间跟踪（随后 seek 会重新置位）
    if (m_audioPlayer) {
        m_audioPlayer->stop();
        m_audioPlayer->reset();
    }
    m_audioNextPtsMs = -1.0;
    m_audioInputExpectedMs = -1.0;
    m_audioTrimBeforeMs = -1;

    // seek 回原位置以重新填充缓冲
    if (wasPlaying) {
        play();
        seek(curPos);
    } else {
        seek(curPos);
    }
    return true;
}

bool FFmpegPlayer::setSubtitleTrack(int trackIndex) {
    if (trackIndex < -1 || trackIndex >= static_cast<int>(m_subtitleTracks.size())) {
        return false;
    }
    if (trackIndex == m_currentSubtitleTrack) return true;

    if (trackIndex == -1) {
        // 关闭内封字幕
        closeSubtitleStream();
        m_currentSubtitleTrack = -1;
        logger().info("Closed embedded subtitle stream");
        return true;
    }

    const TrackInfo& t = m_subtitleTracks[trackIndex];
    bool ok = openSubtitleStream(t.streamIndex);
    if (!ok) return false;
    m_currentSubtitleTrack = trackIndex;
    return true;
}

void FFmpegPlayer::setSubtitleTextCallback(SubtitleTextCallback callback) {
    m_subtitleTextCallback = std::move(callback);
}


} // namespace VideoPlay
