#include "ai/transcriber.h"
#include "ai/aianalyzer.h"
#include "ai/ai_utils.h"
#include "ai/cache_manager.h"
#include "utils/subprocess_runner.h"
#include "subtitles/subtitleparser.h"
#include <filesystem>
#include <fstream>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

namespace VideoPlay {

Transcriber::Transcriber(AIAnalyzer* analyzer) : m_analyzer(analyzer) {}

std::string Transcriber::extractAudio(const std::string& videoPath, ProgressCallback onProgress) {
    if (onProgress) onProgress(0.0f, "正在提取音频...");

    std::string audioPath = m_analyzer->m_cacheManager->getCacheDir() + "/" + m_analyzer->m_cacheManager->computeFileHash(videoPath) + "_audio.wav";
    std::filesystem::create_directories(std::filesystem::path(audioPath).parent_path());

    AVFormatContext* inputCtx = nullptr;
    if (avformat_open_input(&inputCtx, videoPath.c_str(), nullptr, nullptr) < 0) {
        logger().error("[AI] Cannot open video file: " + videoPath);
        return "";
    }

    if (avformat_find_stream_info(inputCtx, nullptr) < 0) {
        avformat_close_input(&inputCtx);
        return "";
    }

    int audioStreamIdx = -1;
    for (unsigned i = 0; i < inputCtx->nb_streams; i++) {
        if (inputCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIdx = i;
            break;
        }
    }

    if (audioStreamIdx < 0) {
        avformat_close_input(&inputCtx);
        logger().error("[AI] No audio stream found");
        return "";
    }

    AVCodecParameters* codecPar = inputCtx->streams[audioStreamIdx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        avformat_close_input(&inputCtx);
        return "";
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, codecPar);
    codecCtx->thread_count = 4;

    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&inputCtx);
        return "";
    }

    SwrContext* swrCtx = nullptr;
    AVChannelLayout outChLayout = AV_CHANNEL_LAYOUT_MONO;

    swrCtx = swr_alloc();
    if (!swrCtx) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&inputCtx);
        return "";
    }

    av_opt_set_chlayout(swrCtx, "in_chlayout", &codecCtx->ch_layout, 0);
    av_opt_set_chlayout(swrCtx, "out_chlayout", &outChLayout, 0);
    av_opt_set_int(swrCtx, "in_sample_rate", codecCtx->sample_rate, 0);
    av_opt_set_int(swrCtx, "out_sample_rate", 16000, 0);
    av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", codecCtx->sample_fmt, 0);
    av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    if (swr_init(swrCtx) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&inputCtx);
        swr_free(&swrCtx);
        return "";
    }

    std::vector<int16_t> audioBuffer;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    int64_t totalDuration = inputCtx->duration;
    int64_t processed = 0;

    logger().info("[AI] Starting audio extraction...");

    while (av_read_frame(inputCtx, packet) >= 0 && !m_analyzer->m_cancelled) {
        if (packet->stream_index == audioStreamIdx) {
            int sendResult = avcodec_send_packet(codecCtx, packet);
            if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
                av_packet_unref(packet);
                continue;
            }
            while (avcodec_receive_frame(codecCtx, frame) >= 0) {
                int outSamples = swr_get_out_samples(swrCtx, frame->nb_samples);
                if (outSamples <= 0) {
                    av_frame_unref(frame);
                    continue;
                }
                std::vector<int16_t> outBuf(outSamples);
                uint8_t* outBuffer = reinterpret_cast<uint8_t*>(outBuf.data());
                int converted = swr_convert(swrCtx, &outBuffer, outSamples,
                                            (const uint8_t**)frame->data, frame->nb_samples);
                if (converted > 0) {
                    audioBuffer.insert(audioBuffer.end(), outBuf.begin(), outBuf.begin() + converted);
                }

                if (totalDuration > 0) {
                    processed = frame->pts;
                    float progress = static_cast<float>(processed) / totalDuration * 0.3f;
                    if (onProgress) onProgress(progress, "正在提取音频...");
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&inputCtx);
    swr_free(&swrCtx);

    if (m_analyzer->m_cancelled || audioBuffer.empty()) {
        return "";
    }

    std::ofstream wavFile(audioPath, std::ios::binary);
    if (!wavFile.is_open()) return "";

    uint32_t dataSize = static_cast<uint32_t>(audioBuffer.size() * sizeof(int16_t));
    uint32_t fileSize = 36 + dataSize;
    uint32_t byteRate = 16000 * 1 * 2;
    uint16_t blockAlign = 1 * 2;

    wavFile.write("RIFF", 4);
    wavFile.write((char*)&fileSize, 4);
    wavFile.write("WAVE", 4);
    wavFile.write("fmt ", 4);

    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1;
    uint16_t numChannels = 1;
    uint32_t sampleRate = 16000;
    uint16_t bitsPerSample = 16;

    wavFile.write((char*)&fmtSize, 4);
    wavFile.write((char*)&audioFormat, 2);
    wavFile.write((char*)&numChannels, 2);
    wavFile.write((char*)&sampleRate, 4);
    wavFile.write((char*)&byteRate, 4);
    wavFile.write((char*)&blockAlign, 2);
    wavFile.write((char*)&bitsPerSample, 2);

    wavFile.write("data", 4);
    wavFile.write((char*)&dataSize, 4);
    wavFile.write((char*)audioBuffer.data(), dataSize);

    wavFile.close();
    logger().info("[AI] Audio extracted to: " + audioPath);
    return audioPath;
}

std::vector<TranscriptSegment> Transcriber::transcribe(const std::string& audioPath, ProgressCallback onProgress) {
    std::vector<TranscriptSegment> segments;
    if (onProgress) onProgress(0.3f, "正在转录音频...");

    std::string model;
    std::string apiKey;
    std::string baseUrl;
    {
        std::lock_guard<std::mutex> lock(m_analyzer->m_mutex);
        model = m_analyzer->m_config.model;
        apiKey = m_analyzer->m_config.apiKey;
        baseUrl = m_analyzer->m_config.baseUrl;
    }

    logger().info("[AI] Transcribe - baseUrl: " + baseUrl);
    logger().info("[AI] Transcribe - model: " + model);
    logger().info("[AI] Transcribe - apiKey length: " + std::to_string(apiKey.length()));

    // 检查模型是否配置
    if (model.empty()) {
        logger().warning("[AI] Model not configured, skipping transcription");
        return segments;
    }

    if (onProgress) onProgress(0.4f, "正在调用 Whisper API...");

    logger().info("[AI] Calling Whisper API: " + baseUrl + "/audio/transcriptions");

    // 使用 HttpClient 的 uploadFile 方法
    std::map<std::string, std::string> fields;
    fields["model"] = model;
    fields["response_format"] = "verbose_json";
    fields["timestamp_granularities[]"] = "segment";

    auto result = m_analyzer->m_http.uploadFile("audio/transcriptions", audioPath, "file", fields);

    if (m_analyzer->m_cancelled) return segments;

    logger().info("[AI] Whisper API response status: " + std::to_string(result.statusCode));

    if (result.statusCode != 200) {
        logger().error("[AI] Whisper API error: status=" + std::to_string(result.statusCode));
        logger().error("[AI] Response body: " + result.body.substr(0, 500));
        return segments;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(result.body);

        if (j.contains("segments") && j["segments"].is_array()) {
            for (const auto& seg : j["segments"]) {
                TranscriptSegment segment;
                segment.startTime = static_cast<int64_t>(seg.value("start", 0.0) * 1000);
                segment.endTime = static_cast<int64_t>(seg.value("end", 0.0) * 1000);
                segment.text = seg.value("text", std::string());
                segment.confidence = seg.value("avg_logprob", 0.0f);
                segments.push_back(segment);
            }
        }

        logger().info("[AI] Transcription complete: " + std::to_string(segments.size()) + " segments");
    } catch (const std::exception& e) {
        logger().error("[AI] Failed to parse Whisper response: " + std::string(e.what()));
    }

    if (onProgress) onProgress(0.7f, "转录完成");
    return segments;
}

std::string Transcriber::extractVideoForAI(const std::string& videoPath,
                                          ProgressCallback onProgress,
                                          int64_t maxOutputBytes,
                                          int maxDurationSeconds) {
    // 统一转为 H.264/AVC1 MP4，按 provider 的限制压缩到指定大小以内。
    std::string sourceHash = m_analyzer->m_cacheManager->computeSourceHash(videoPath);
    if (sourceHash.empty()) {
        logger().error("[AI] Cannot compute source hash: " + videoPath);
        return "";
    }

    // 获取视频时长
    AVFormatContext* inputCtx = nullptr;
    if (avformat_open_input(&inputCtx, videoPath.c_str(), nullptr, nullptr) < 0) {
        logger().error("[AI] Cannot open video file: " + videoPath);
        return "";
    }
    if (avformat_find_stream_info(inputCtx, nullptr) < 0) {
        avformat_close_input(&inputCtx);
        return "";
    }
    double duration = 0.0;
    if (inputCtx->duration != AV_NOPTS_VALUE && inputCtx->duration > 0) {
        duration = inputCtx->duration / (double)AV_TIME_BASE;
    }
    avformat_close_input(&inputCtx);
    
    logger().info("[AI] Video duration: " + std::to_string(duration) + "s");
    
    bool needTrim = (duration > maxDurationSeconds);
    double effectiveDuration = duration > 0.0
        ? std::min(duration, static_cast<double>(maxDurationSeconds))
        : static_cast<double>(maxDurationSeconds);
    int clipSeconds = std::max(1, static_cast<int>(std::ceil(effectiveDuration)));
    std::string outputPath = m_analyzer->m_cacheManager->getTranscodeCachePath(sourceHash, clipSeconds, maxOutputBytes);
    std::filesystem::path outputPathObj = std::filesystem::u8path(outputPath);
    std::filesystem::create_directories(outputPathObj.parent_path());

    std::string reusablePath = m_analyzer->m_cacheManager->findReusableTranscodeCache(sourceHash, clipSeconds, maxOutputBytes);
    if (!reusablePath.empty()) {
        if (onProgress) onProgress(0.35f, "复用已转码视频...");
        logger().info("[AI] Reusing transcode cache: " + reusablePath);
        return reusablePath;
    }
    
    // 多级压缩策略：尝试不同质量级别
    struct QualityPreset {
        int crf;
        int maxWidth;
        int maxHeight;
        int audioBitrate;
        const char* name;
    };
    
    std::vector<QualityPreset> presets = {
        {28, 854, 480, 48, "medium"},   // 中等质量
        {32, 640, 360, 32, "low"},      // 低质量
        {38, 480, 270, 24, "very_low"}  // 极低质量
    };
    
    for (size_t i = 0; i < presets.size(); i++) {
        const auto& preset = presets[i];
        
        if (i > 0) {
            logger().info("[AI] Retrying with lower quality: " + std::string(preset.name));
            if (std::filesystem::exists(outputPathObj)) {
                std::filesystem::remove(outputPathObj);
            }
        }
        
        // 构建 FFmpeg 命令
        std::string cmd = "ffmpeg -hide_banner -loglevel error -nostdin";
        cmd += " -threads 1 -filter_threads 1 -filter_complex_threads 1";
        cmd += " -y -i \"" + videoPath + "\"";
        if (needTrim) {
            cmd += " -t " + std::to_string(maxDurationSeconds);
        }
        
        // 视频：H.264，限制分辨率和码率
        cmd += " -c:v libx264 -preset veryfast -threads 1";
        cmd += " -crf " + std::to_string(preset.crf);
        cmd += " -vf \"scale='min(" + std::to_string(preset.maxWidth) + ",iw)':'min(" + std::to_string(preset.maxHeight) + ",ih)':force_original_aspect_ratio=decrease\"";
        cmd += " -maxrate 500k -bufsize 1000k";
        
        // 音频：AAC 单声道
        cmd += " -c:a aac -b:a " + std::to_string(preset.audioBitrate) + "k -ac 1";
        cmd += " -movflags +faststart";
        cmd += " \"" + outputPath + "\"";
        
        logger().info("[AI] FFmpeg command: " + cmd);
        if (onProgress) onProgress(0.2f + i * 0.1f, "正在转码视频 (" + std::string(preset.name) + ")...");
        
        std::string ffmpegErr;
        int ret = SubprocessRunner::run(cmd, ffmpegErr);
        if (ret != 0) {
            logger().error("[AI] FFmpeg failed with code: " + std::to_string(ret));
            if (!ffmpegErr.empty()) {
                logger().error("[AI] FFmpeg stderr: " + ffmpegErr);
            }
            continue;
        }
        
        if (std::filesystem::exists(outputPathObj)) {
            auto fileSize = std::filesystem::file_size(outputPathObj);
            logger().info("[AI] Output video size: " + std::to_string(fileSize / 1024) + " KB (" + std::string(preset.name) + ")");
            
            if (fileSize <= maxOutputBytes) {
                return outputPath;
            }
            
            logger().warning("[AI] Video still too large, trying lower quality...");
        }
    }
    
    // 所有质量级别都失败
    logger().error("[AI] Failed to compress video to acceptable size");
    if (std::filesystem::exists(outputPathObj)) {
        std::filesystem::remove(outputPathObj);
    }
    return "";
}

std::string Transcriber::fileToBase64(const std::string& filePath) {
    // Base64 编码表
    static const char base64Chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        logger().error("[AI] Cannot open file for base64: " + filePath);
        return "";
    }
    
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();
    
    // 计算 base64 长度
    size_t base64Len = ((fileSize + 2) / 3) * 4;
    std::string result;
    result.reserve(base64Len);
    
    for (size_t i = 0; i < fileSize; i += 3) {
        uint32_t n = static_cast<uint32_t>(buffer[i]) << 16;
        if (i + 1 < fileSize) n |= static_cast<uint32_t>(buffer[i + 1]) << 8;
        if (i + 2 < fileSize) n |= static_cast<uint32_t>(buffer[i + 2]);
        
        result += base64Chars[(n >> 18) & 0x3F];
        result += base64Chars[(n >> 12) & 0x3F];
        result += (i + 1 < fileSize) ? base64Chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < fileSize) ? base64Chars[n & 0x3F] : '=';
    }
    
    return result;
}

std::string Transcriber::findSubtitleFile(const std::string& videoPath) {
    std::filesystem::path video(videoPath);
    std::string baseName = video.stem().u8string();
    std::string dir = video.parent_path().u8string();
    
    // 常见字幕扩展名
    std::vector<std::string> extensions = {".srt", ".ass", ".ssa", ".vtt"};
    
    for (const auto& ext : extensions) {
        std::string subtitlePath = dir + "/" + baseName + ext;
        if (std::filesystem::exists(subtitlePath)) {
            logger().info("[AI] Found subtitle file: " + subtitlePath);
            return subtitlePath;
        }
    }
    
    return "";
}

std::vector<TranscriptSegment> Transcriber::loadSubtitleAsTranscript(const std::string& subtitlePath) {
    std::vector<TranscriptSegment> segments;
    
    // 使用项目的 SubtitleParser 解析字幕
    SubtitleParser parser;
    if (!parser.loadFile(subtitlePath)) {
        logger().error("[AI] Failed to load subtitle file: " + subtitlePath);
        return segments;
    }
    
    auto entries = parser.entries();
    for (const auto& entry : entries) {
        TranscriptSegment segment;
        segment.startTime = entry.startTime;
        segment.endTime = entry.endTime;
        segment.text = entry.text;
        segment.confidence = 1.0f; // 字幕文件的置信度为 1
        segments.push_back(segment);
    }
    
    return segments;
}


} // namespace VideoPlay
