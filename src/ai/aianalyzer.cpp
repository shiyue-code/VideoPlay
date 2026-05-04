#include "ai/aianalyzer.h"
#include "utils/logger.h"
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

namespace VideoPlay {

AIAnalyzer::AIAnalyzer() = default;

AIAnalyzer::~AIAnalyzer() {
    cancel();
}

void AIAnalyzer::configure(const AIConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
    m_http.setBaseUrl(config.baseUrl);
    m_http.setApiKey(config.apiKey);
    m_http.setTimeout(60);
}

bool AIAnalyzer::isConfigured() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_config.apiKey.empty();
}

void AIAnalyzer::cancel() {
    m_cancelled = true;
}

std::string AIAnalyzer::getCacheDir() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_config.cacheDir.empty()) {
        return m_config.cacheDir;
    }
#ifdef _WIN32
    const char* appData = getenv("APPDATA");
    if (appData) {
        return std::string(appData) + "/VideoPlay/ai_cache";
    }
#else
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/VideoPlay/ai_cache";
    }
#endif
    return "./ai_cache";
}

std::string AIAnalyzer::computeFileHash(const std::string& filePath) const {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return "";

    auto fileSize = std::filesystem::file_size(filePath);
    auto modTime = std::filesystem::last_write_time(filePath).time_since_epoch().count();

    std::stringstream ss;
    ss << filePath << "_" << fileSize << "_" << modTime;

    std::string input = ss.str();
    size_t hash = std::hash<std::string>{}(input);

    std::stringstream result;
    result << std::hex << hash;
    return result.str();
}

std::string AIAnalyzer::getCachePath(const std::string& videoPath) const {
    std::string hash = computeFileHash(videoPath);
    std::string cacheDir = getCacheDir();
    std::filesystem::create_directories(cacheDir);
    return cacheDir + "/" + hash + ".json";
}

bool AIAnalyzer::hasCache(const std::string& videoPath) const {
    std::string path = getCachePath(videoPath);
    return std::filesystem::exists(path);
}

AIAnalysisResult AIAnalyzer::loadCache(const std::string& videoPath) const {
    AIAnalysisResult result;
    std::string path = getCachePath(videoPath);

    if (!std::filesystem::exists(path)) {
        return result;
    }

    try {
        std::ifstream file(path);
        nlohmann::json j;
        file >> j;

        result.summary = j.value("summary", std::string());
        result.language = j.value("language", std::string());
        result.analyzedAt = j.value("analyzedAt", int64_t(0));
        result.valid = true;

        if (j.contains("chapters") && j["chapters"].is_array()) {
            for (const auto& ch : j["chapters"]) {
                ChapterInfo chapter;
                chapter.startTime = ch.value("startTime", int64_t(0));
                chapter.endTime = ch.value("endTime", int64_t(0));
                chapter.title = ch.value("title", std::string());
                result.chapters.push_back(chapter);
            }
        }

        if (j.contains("transcript") && j["transcript"].is_array()) {
            for (const auto& seg : j["transcript"]) {
                TranscriptSegment segment;
                segment.startTime = seg.value("startTime", int64_t(0));
                segment.endTime = seg.value("endTime", int64_t(0));
                segment.text = seg.value("text", std::string());
                segment.confidence = seg.value("confidence", 0.0f);
                result.transcript.push_back(segment);
            }
        }

        Logger::instance().info("[AI] Loaded cache for: " + videoPath);
    } catch (const std::exception& e) {
        Logger::instance().error("[AI] Failed to load cache: " + std::string(e.what()));
        result.valid = false;
    }

    return result;
}

void AIAnalyzer::saveCache(const std::string& videoPath, const AIAnalysisResult& result) {
    std::string path = getCachePath(videoPath);

    try {
        nlohmann::json j;
        j["videoPath"] = videoPath;
        j["videoHash"] = computeFileHash(videoPath);
        j["summary"] = result.summary;
        j["language"] = result.language;
        j["analyzedAt"] = result.analyzedAt;

        j["chapters"] = nlohmann::json::array();
        for (const auto& ch : result.chapters) {
            j["chapters"].push_back({
                {"startTime", ch.startTime},
                {"endTime", ch.endTime},
                {"title", ch.title}
            });
        }

        j["transcript"] = nlohmann::json::array();
        for (const auto& seg : result.transcript) {
            j["transcript"].push_back({
                {"startTime", seg.startTime},
                {"endTime", seg.endTime},
                {"text", seg.text},
                {"confidence", seg.confidence}
            });
        }

        std::ofstream file(path);
        file << j.dump(2);

        Logger::instance().info("[AI] Saved cache for: " + videoPath);
    } catch (const std::exception& e) {
        Logger::instance().error("[AI] Failed to save cache: " + std::string(e.what()));
    }
}

void AIAnalyzer::clearCache(const std::string& videoPath) {
    std::string path = getCachePath(videoPath);
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
        Logger::instance().info("[AI] Cleared cache for: " + videoPath);
    }
}

void AIAnalyzer::clearAllCache() {
    std::string cacheDir = getCacheDir();
    if (std::filesystem::exists(cacheDir)) {
        std::filesystem::remove_all(cacheDir);
        Logger::instance().info("[AI] Cleared all cache");
    }
}

std::string AIAnalyzer::extractAudio(const std::string& videoPath, ProgressCallback onProgress) {
    if (onProgress) onProgress(0.0f, "正在提取音频...");

    std::string audioPath = getCacheDir() + "/" + computeFileHash(videoPath) + "_audio.wav";
    std::filesystem::create_directories(std::filesystem::path(audioPath).parent_path());

    AVFormatContext* inputCtx = nullptr;
    if (avformat_open_input(&inputCtx, videoPath.c_str(), nullptr, nullptr) < 0) {
        Logger::instance().error("[AI] Cannot open video file: " + videoPath);
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
        Logger::instance().error("[AI] No audio stream found");
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
    av_opt_set_chlayout(swrCtx, "in_chlayout", &codecCtx->ch_layout, 0);
    av_opt_set_chlayout(swrCtx, "out_chlayout", &outChLayout, 0);
    av_opt_set_int(swrCtx, "in_sample_rate", codecCtx->sample_rate, 0);
    av_opt_set_int(swrCtx, "out_sample_rate", 16000, 0);
    av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", codecCtx->sample_fmt, 0);
    av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    swrCtx = swr_alloc();
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

    while (av_read_frame(inputCtx, packet) >= 0 && !m_cancelled) {
        if (packet->stream_index == audioStreamIdx) {
            avcodec_send_packet(codecCtx, packet);
            while (avcodec_receive_frame(codecCtx, frame) >= 0) {
                int outSamples = swr_get_out_samples(swrCtx, frame->nb_samples);
                std::vector<int16_t> outBuf(outSamples);
                int converted = swr_convert(swrCtx, (uint8_t**)outBuf.data(), outSamples,
                                            (const uint8_t**)frame->data, frame->nb_samples);
                if (converted > 0) {
                    audioBuffer.insert(audioBuffer.end(), outBuf.begin(), outBuf.begin() + converted);
                }

                if (totalDuration > 0) {
                    processed = frame->pts;
                    float progress = static_cast<float>(processed) / totalDuration * 0.3f;
                    if (onProgress) onProgress(progress, "正在提取音频...");
                }
            }
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&inputCtx);
    swr_free(&swrCtx);

    if (m_cancelled || audioBuffer.empty()) {
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
    Logger::instance().info("[AI] Audio extracted to: " + audioPath);
    return audioPath;
}

std::vector<TranscriptSegment> AIAnalyzer::transcribe(const std::string& audioPath, ProgressCallback onProgress) {
    std::vector<TranscriptSegment> segments;
    if (onProgress) onProgress(0.3f, "正在转录音频...");

    std::string whisperModel;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        whisperModel = m_config.whisperModel;
    }

    httplib::MultipartFormDataItems items;
    items.push_back({"model", whisperModel, "", ""});
    items.push_back({"response_format", "verbose_json", "", ""});
    items.push_back({"timestamp_granularities[]", "segment", "", ""});

    std::ifstream audioFile(audioPath, std::ios::binary);
    if (!audioFile.is_open()) {
        Logger::instance().error("[AI] Cannot open audio file: " + audioPath);
        return segments;
    }

    std::string audioContent((std::istreambuf_iterator<char>(audioFile)),
                             std::istreambuf_iterator<char>());
    audioFile.close();

    std::string filename = std::filesystem::path(audioPath).filename().string();
    items.push_back({"file", audioContent, filename, "audio/wav"});

    std::string apiKey;
    std::string baseUrl;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        apiKey = m_config.apiKey;
        baseUrl = m_config.baseUrl;
    }

    httplib::Client client(baseUrl);
    client.set_connection_timeout(60);
    client.set_read_timeout(300);
    client.set_write_timeout(60);

    httplib::Headers headers;
    headers.emplace("Authorization", "Bearer " + apiKey);

    if (onProgress) onProgress(0.4f, "正在调用 Whisper API...");

    auto result = client.Post("/audio/transcriptions", headers, items);

    if (m_cancelled) return segments;

    if (!result) {
        Logger::instance().error("[AI] Whisper API request failed");
        return segments;
    }

    if (result->status != 200) {
        Logger::instance().error("[AI] Whisper API error: " + std::to_string(result->status) + " " + result->body);
        return segments;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(result->body);

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

        Logger::instance().info("[AI] Transcription complete: " + std::to_string(segments.size()) + " segments");
    } catch (const std::exception& e) {
        Logger::instance().error("[AI] Failed to parse Whisper response: " + std::string(e.what()));
    }

    if (onProgress) onProgress(0.7f, "转录完成");
    return segments;
}

AIAnalysisResult AIAnalyzer::analyzeWithGPT(const std::vector<TranscriptSegment>& transcript,
                                              const std::string& videoPath,
                                              ProgressCallback onProgress) {
    AIAnalysisResult result;
    if (onProgress) onProgress(0.7f, "正在生成摘要...");

    std::string gptModel;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        gptModel = m_config.gptModel;
    }

    std::string transcriptText;
    for (const auto& seg : transcript) {
        transcriptText += "[" + formatTime(seg.startTime) + "] " + seg.text + "\n";
    }

    if (transcriptText.length() > 50000) {
        transcriptText = transcriptText.substr(0, 50000) + "\n...(截断)";
    }

    std::string prompt = R"(分析以下视频转录文本，完成以下任务：
1. 生成简洁的中文摘要（100-200字）
2. 根据内容主题变化自动划分章节（每个章节至少30秒，最多10个章节）

转录文本：
)" + transcriptText + R"(

请以 JSON 格式返回，不要包含其他内容：
{
  "summary": "摘要内容",
  "language": "zh",
  "chapters": [
    {"title": "章节标题", "startTime": 开始时间毫秒}
  ]
})";

    nlohmann::json requestBody = {
        {"model", gptModel},
        {"messages", {
            {{"role", "system"}, {"content", "你是一个视频内容分析助手，擅长从转录文本中提取关键信息并划分章节。"}},
            {{"role", "user"}, {"content", prompt}}
        }},
        {"temperature", 0.3},
        {"max_tokens", 2000}
    };

    auto response = m_http.post("chat/completions", requestBody.dump());

    if (m_cancelled) return result;

    if (!response.success()) {
        Logger::instance().error("[AI] GPT API error: " + std::to_string(response.statusCode) + " " + response.body);
        return result;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(response.body);
        std::string content = j["choices"][0]["message"]["content"];

        size_t start = content.find('{');
        size_t end = content.rfind('}');
        if (start != std::string::npos && end != std::string::npos) {
            content = content.substr(start, end - start + 1);
        }

        nlohmann::json aiResult = nlohmann::json::parse(content);

        result.summary = aiResult.value("summary", std::string());
        result.language = aiResult.value("language", std::string());

        if (aiResult.contains("chapters") && aiResult["chapters"].is_array()) {
            for (size_t i = 0; i < aiResult["chapters"].size(); i++) {
                const auto& ch = aiResult["chapters"][i];
                ChapterInfo chapter;
                chapter.startTime = ch.value("startTime", int64_t(0));
                if (i + 1 < aiResult["chapters"].size()) {
                    chapter.endTime = aiResult["chapters"][i + 1].value("startTime", int64_t(0));
                } else {
                    chapter.endTime = transcript.back().endTime;
                }
                chapter.title = ch.value("title", "章节 " + std::to_string(i + 1));
                result.chapters.push_back(chapter);
            }
        }

        result.transcript = transcript;
        result.analyzedAt = std::chrono::system_clock::now().time_since_epoch().count();
        result.valid = true;

        Logger::instance().info("[AI] GPT analysis complete: " + std::to_string(result.chapters.size()) + " chapters");
    } catch (const std::exception& e) {
        Logger::instance().error("[AI] Failed to parse GPT response: " + std::string(e.what()));
    }

    if (onProgress) onProgress(0.9f, "分析完成");
    return result;
}

void AIAnalyzer::analyze(const std::string& videoPath,
                          CompleteCallback onComplete,
                          ProgressCallback onProgress,
                          ErrorCallback onError) {
    m_cancelled = false;

    if (hasCache(videoPath)) {
        AIAnalysisResult result = loadCache(videoPath);
        if (result.valid) {
            if (onProgress) onProgress(1.0f, "使用缓存结果");
            if (onComplete) onComplete(result);
            return;
        }
    }

    std::thread([this, videoPath, onComplete, onProgress, onError]() {
        try {
            std::string audioPath = extractAudio(videoPath, onProgress);
            if (m_cancelled || audioPath.empty()) {
                if (onError) onError("音频提取失败或已取消");
                return;
            }

            auto transcript = transcribe(audioPath, onProgress);
            if (m_cancelled || transcript.empty()) {
                if (onError) onError("转录失败或已取消");
                return;
            }

            AIAnalysisResult result = analyzeWithGPT(transcript, videoPath, onProgress);
            if (m_cancelled || !result.valid) {
                if (onError) onError("分析失败或已取消");
                return;
            }

            saveCache(videoPath, result);

            if (std::filesystem::exists(audioPath)) {
                std::filesystem::remove(audioPath);
            }

            if (onProgress) onProgress(1.0f, "分析完成");
            if (onComplete) onComplete(result);
        } catch (const std::exception& e) {
            Logger::instance().error("[AI] Analysis failed: " + std::string(e.what()));
            if (onError) onError(e.what());
        }
    }).detach();
}

} // namespace VideoPlay
