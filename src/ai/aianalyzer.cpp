#include "ai/aianalyzer.h"
#include "utils/logger.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <memory>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto logger = Logger::get("ai");
    return *logger;
}

std::string trim(const std::string& value) {
    auto start = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c);
    });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c);
    }).base();
    return start < end ? std::string(start, end) : std::string();
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), value.begin());
}

std::string normalizeProvider(std::string provider, const std::string& baseUrl,
                              const std::string& model) {
    provider = toLower(trim(provider));
    if (provider.empty() || provider == "auto") {
        std::string lowerBaseUrl = toLower(baseUrl);
        std::string lowerModel = toLower(model);
        if (lowerBaseUrl.find("generativelanguage.googleapis.com") != std::string::npos ||
            lowerModel.find("gemini") != std::string::npos) {
            return "gemini";
        }
        return "mimo";
    }

    if (provider == "google") {
        return "gemini";
    }
    if (provider == "xiaomi" || provider == "xiaomimimo") {
        return "mimo";
    }
    return provider;
}

std::string defaultBaseUrlForProvider(const std::string& provider) {
    if (provider == "gemini") {
        return "https://generativelanguage.googleapis.com";
    }
    return "https://api.xiaomimimo.com";
}

std::string defaultModelForProvider(const std::string& provider) {
    if (provider == "gemini") {
        return "gemini-3.5-flash";
    }
    return "mimo-v2.5";
}

std::string normalizeProviderModel(const std::string& provider, std::string model) {
    model = trim(model);
    std::string lowerModel = toLower(model);
    if (provider == "gemini") {
        if (model.empty() || startsWith(lowerModel, "mimo-")) {
            return defaultModelForProvider(provider);
        }
        return model;
    }

    if (model.empty() || lowerModel.find("gemini") != std::string::npos ||
        model == "mimo-v2-pro" || model == "mimo-v2.5-pro") {
        return defaultModelForProvider(provider);
    }
    return model;
}

std::string normalizeProviderBaseUrl(const std::string& provider, std::string baseUrl) {
    baseUrl = trim(baseUrl);
    if (baseUrl.empty()) {
        return defaultBaseUrlForProvider(provider);
    }

    while (!baseUrl.empty() && baseUrl.back() == '/') {
        baseUrl.pop_back();
    }
    if (provider == "gemini" && baseUrl.size() >= 7 &&
        baseUrl.substr(baseUrl.size() - 7) == "/v1beta") {
        baseUrl = baseUrl.substr(0, baseUrl.size() - 7);
    } else if (provider == "mimo" && baseUrl.size() >= 3 &&
               baseUrl.substr(baseUrl.size() - 3) == "/v1") {
        baseUrl = baseUrl.substr(0, baseUrl.size() - 3);
    }
    return baseUrl;
}

AIConfig normalizedConfig(AIConfig config) {
    config.provider = normalizeProvider(config.provider, config.baseUrl, config.model);
    config.baseUrl = normalizeProviderBaseUrl(config.provider, config.baseUrl);
    config.model = normalizeProviderModel(config.provider, config.model);
    return config;
}

std::string analysisJsonPrompt(int minChapterSeconds) {
    return "请分析这个视频，完成以下任务：\n"
           "1. 生成简洁的中文摘要（100-200字）\n"
           "2. 根据内容主题变化自动划分章节（每个章节至少" +
           std::to_string(minChapterSeconds) + "秒，最多10个章节）\n\n"
           "请只返回 JSON，不要包含 Markdown 代码块或额外说明：\n"
           "{\n"
           "  \"summary\": \"摘要内容\",\n"
           "  \"language\": \"zh\",\n"
           "  \"chapters\": [\n"
           "    {\"title\": \"章节标题\", \"startTime\": 开始时间毫秒}\n"
           "  ]\n"
           "}";
}

std::string extractJsonObjectText(std::string content) {
    content = trim(content);
    if (content.empty()) {
        return content;
    }

    if (startsWith(content, "```")) {
        size_t firstNewline = content.find('\n');
        size_t lastFence = content.rfind("```");
        if (firstNewline != std::string::npos && lastFence != std::string::npos &&
            lastFence > firstNewline) {
            content = content.substr(firstNewline + 1, lastFence - firstNewline - 1);
        }
    }

    size_t start = content.find('{');
    size_t end = content.rfind('}');
    if (start != std::string::npos && end != std::string::npos && end >= start) {
        return content.substr(start, end - start + 1);
    }
    return content;
}

std::string openAIContentText(const nlohmann::json& responseJson) {
    if (!responseJson.contains("choices") || responseJson["choices"].empty()) {
        return {};
    }
    const auto& message = responseJson["choices"][0]["message"];
    if (!message.contains("content")) {
        return {};
    }
    if (message["content"].is_string()) {
        return message["content"].get<std::string>();
    }
    if (message["content"].is_array()) {
        std::string text;
        for (const auto& part : message["content"]) {
            if (part.contains("text") && part["text"].is_string()) {
                text += part["text"].get<std::string>();
            }
        }
        return text;
    }
    return {};
}

std::string geminiContentText(const nlohmann::json& responseJson) {
    if (!responseJson.contains("candidates") || responseJson["candidates"].empty()) {
        return {};
    }
    const auto& content = responseJson["candidates"][0]["content"];
    if (!content.contains("parts") || !content["parts"].is_array()) {
        return {};
    }

    std::string text;
    for (const auto& part : content["parts"]) {
        if (part.contains("text") && part["text"].is_string()) {
            text += part["text"].get<std::string>();
        }
    }
    return text;
}

int64_t parseTimeStringMs(const std::string& rawValue) {
    std::string value = trim(rawValue);
    if (value.empty()) {
        return -1;
    }

    size_t colonCount = std::count(value.begin(), value.end(), ':');
    if (colonCount > 0) {
        std::replace(value.begin(), value.end(), ',', '.');

        std::vector<double> parts;
        std::stringstream stream(value);
        std::string part;
        while (std::getline(stream, part, ':')) {
            try {
                parts.push_back(std::stod(part));
            } catch (...) {
                return -1;
            }
        }

        double seconds = 0.0;
        if (parts.size() == 3) {
            seconds = parts[0] * 3600.0 + parts[1] * 60.0 + parts[2];
        } else if (parts.size() == 2) {
            seconds = parts[0] * 60.0 + parts[1];
        } else if (parts.size() == 1) {
            seconds = parts[0];
        } else {
            return -1;
        }
        return static_cast<int64_t>(seconds * 1000.0);
    }

    try {
        double numericValue = std::stod(value);
        if (numericValue >= 0.0 && numericValue < 1000.0) {
            return static_cast<int64_t>(numericValue * 1000.0);
        }
        return static_cast<int64_t>(numericValue);
    } catch (...) {
        return -1;
    }
}

int64_t parseTimeValueMs(const nlohmann::json& value) {
    if (value.is_number()) {
        double numericValue = value.get<double>();
        if (numericValue >= 0.0 && numericValue < 1000.0) {
            return static_cast<int64_t>(numericValue * 1000.0);
        }
        return static_cast<int64_t>(numericValue);
    }

    if (value.is_string()) {
        return parseTimeStringMs(value.get<std::string>());
    }

    return -1;
}

int64_t chapterStartTimeMs(const nlohmann::json& chapterJson) {
    static const char* keys[] = {
        "startTime", "start_time", "start", "time", "timestamp", "begin"
    };

    for (const char* key : keys) {
        if (chapterJson.contains(key)) {
            int64_t timeMs = parseTimeValueMs(chapterJson[key]);
            if (timeMs >= 0) {
                return timeMs;
            }
        }
    }

    return -1;
}

std::string chapterTitle(const nlohmann::json& chapterJson, size_t index) {
    static const char* keys[] = {"title", "name", "heading", "summary"};

    for (const char* key : keys) {
        if (chapterJson.contains(key) && chapterJson[key].is_string()) {
            std::string title = trim(chapterJson[key].get<std::string>());
            if (!title.empty()) {
                return title;
            }
        }
    }

    return "章节 " + std::to_string(index + 1);
}

std::vector<ChapterInfo> parseChapters(const nlohmann::json& aiResult,
                                       int64_t fallbackEndTimeMs) {
    std::vector<ChapterInfo> chapters;
    if (!aiResult.contains("chapters") || !aiResult["chapters"].is_array()) {
        return chapters;
    }

    for (const auto& ch : aiResult["chapters"]) {
        if (!ch.is_object()) {
            continue;
        }

        int64_t startTime = chapterStartTimeMs(ch);
        if (startTime < 0) {
            continue;
        }

        ChapterInfo chapter;
        chapter.startTime = startTime;
        chapter.title = chapterTitle(ch, chapters.size());
        chapters.push_back(chapter);
    }

    std::sort(chapters.begin(), chapters.end(), [](const ChapterInfo& lhs,
                                                   const ChapterInfo& rhs) {
        return lhs.startTime < rhs.startTime;
    });

    chapters.erase(std::unique(chapters.begin(), chapters.end(),
        [](const ChapterInfo& lhs, const ChapterInfo& rhs) {
            return lhs.startTime == rhs.startTime;
        }), chapters.end());

    for (size_t i = 0; i < chapters.size(); ++i) {
        if (i + 1 < chapters.size()) {
            chapters[i].endTime = chapters[i + 1].startTime;
        } else if (fallbackEndTimeMs > chapters[i].startTime) {
            chapters[i].endTime = fallbackEndTimeMs;
        } else {
            chapters[i].endTime = chapters[i].startTime + 10000;
        }
    }

    return chapters;
}

bool hasUsableAnalysis(const AIAnalysisResult& result) {
    return !result.chapters.empty();
}

}


AIAnalyzer::AIAnalyzer() = default;

AIAnalyzer::~AIAnalyzer() {
    cancel();
}

void AIAnalyzer::configure(const AIConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = normalizedConfig(config);
    m_http.setBaseUrl(m_config.baseUrl);
    m_http.setApiKey(m_config.apiKey);
    m_http.setAuthHeader("Authorization", "Bearer ");
    m_http.setTimeout(120); // 增加超时时间以支持大视频上传
}

bool AIAnalyzer::isConfigured() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_config.apiKey.empty();
}

AIConfig AIAnalyzer::snapshotConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return normalizedConfig(m_config);
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
    try {
        if (!std::filesystem::exists(filePath)) return "";
        
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return "";

        auto fileSize = std::filesystem::file_size(filePath);
        auto modTime = std::filesystem::last_write_time(filePath).time_since_epoch().count();

        std::stringstream ss;
        AIConfig config = snapshotConfig();
        ss << filePath << "_" << fileSize << "_" << modTime
           << "_" << config.provider << "_" << config.model;

        std::string input = ss.str();
        size_t hash = std::hash<std::string>{}(input);

        std::stringstream result;
        result << std::hex << hash;
        return result.str();
    } catch (const std::exception& e) {
        logger().error("[AI] computeFileHash failed: " + std::string(e.what()));
        return "";
    }
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

        result.valid = hasUsableAnalysis(result);
        if (result.valid) {
            logger().info("[AI] Loaded cache for: " + videoPath +
                " (" + std::to_string(result.chapters.size()) + " chapters)");
        } else {
            logger().warning("[AI] Ignoring incomplete cache without chapters: " + videoPath);
        }
    } catch (const std::exception& e) {
        logger().error("[AI] Failed to load cache: " + std::string(e.what()));
        result.valid = false;
    }

    return result;
}

void AIAnalyzer::saveCache(const std::string& videoPath, const AIAnalysisResult& result) {
    if (!hasUsableAnalysis(result)) {
        logger().warning("[AI] Skip saving incomplete analysis without chapters: " + videoPath);
        return;
    }

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

        logger().info("[AI] Saved cache for: " + videoPath);
    } catch (const std::exception& e) {
        logger().error("[AI] Failed to save cache: " + std::string(e.what()));
    }
}

void AIAnalyzer::clearCache(const std::string& videoPath) {
    std::string path = getCachePath(videoPath);
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
        logger().info("[AI] Cleared cache for: " + videoPath);
    }
}

void AIAnalyzer::clearAllCache() {
    std::string cacheDir = getCacheDir();
    if (std::filesystem::exists(cacheDir)) {
        std::filesystem::remove_all(cacheDir);
        logger().info("[AI] Cleared all cache");
    }
}

std::string AIAnalyzer::extractAudio(const std::string& videoPath, ProgressCallback onProgress) {
    if (onProgress) onProgress(0.0f, "正在提取音频...");

    std::string audioPath = getCacheDir() + "/" + computeFileHash(videoPath) + "_audio.wav";
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

    while (av_read_frame(inputCtx, packet) >= 0 && !m_cancelled) {
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
    logger().info("[AI] Audio extracted to: " + audioPath);
    return audioPath;
}

std::vector<TranscriptSegment> AIAnalyzer::transcribe(const std::string& audioPath, ProgressCallback onProgress) {
    std::vector<TranscriptSegment> segments;
    if (onProgress) onProgress(0.3f, "正在转录音频...");

    std::string model;
    std::string apiKey;
    std::string baseUrl;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        model = m_config.model;
        apiKey = m_config.apiKey;
        baseUrl = m_config.baseUrl;
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

    auto result = m_http.uploadFile("audio/transcriptions", audioPath, "file", fields);

    if (m_cancelled) return segments;

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

AIAnalysisResult AIAnalyzer::analyzeWithGPT(const std::vector<TranscriptSegment>& transcript,
                                              const std::string& videoPath,
                                              ProgressCallback onProgress) {
    AIAnalysisResult result;
    if (onProgress) onProgress(0.7f, "正在生成摘要...");

    std::string model;
    std::string apiKey;
    std::string baseUrl;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        model = m_config.model;
        apiKey = m_config.apiKey;
        baseUrl = m_config.baseUrl;
    }

    logger().info("[AI] GPT Analysis - baseUrl: " + baseUrl);
    logger().info("[AI] GPT Analysis - model: " + model);

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
        {"model", model},
        {"messages", {
            {{"role", "system"}, {"content", "你是一个视频内容分析助手，擅长从转录文本中提取关键信息并划分章节。"}},
            {{"role", "user"}, {"content", prompt}}
        }},
        {"temperature", 0.3},
        {"max_tokens", 2000}
    };

    logger().info("[AI] Calling GPT API: " + baseUrl + "/v1/chat/completions");

    auto response = m_http.post("v1/chat/completions", requestBody.dump());

    if (m_cancelled) return result;

    if (!response.success()) {
        logger().error("[AI] GPT API error: status=" + std::to_string(response.statusCode));
        logger().error("[AI] Response body: " + response.body.substr(0, 500));
        return result;
    }

    logger().info("[AI] GPT API response status: " + std::to_string(response.statusCode));

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

        int64_t transcriptEndTime = transcript.empty() ? 0 : transcript.back().endTime;
        result.chapters = parseChapters(aiResult, transcriptEndTime);

        result.transcript = transcript;
        result.analyzedAt = std::chrono::system_clock::now().time_since_epoch().count();
        result.valid = hasUsableAnalysis(result);

        if (result.valid) {
            logger().info("[AI] GPT analysis complete: " +
                std::to_string(result.chapters.size()) + " chapters");
        } else {
            logger().warning("[AI] GPT analysis returned no usable chapters. Content: " +
                content.substr(0, 500));
        }
    } catch (const std::exception& e) {
        logger().error("[AI] Failed to parse GPT response: " + std::string(e.what()));
    }

    if (onProgress) onProgress(0.9f, "分析完成");
    return result;
}

void AIAnalyzer::analyze(const std::string& videoPath,
                          CompleteCallback onComplete,
                          ProgressCallback onProgress,
                          ErrorCallback onError) {
    m_cancelled = false;

    AIConfig config = snapshotConfig();
    struct ProviderRoute {
        std::string id;
        std::string displayName;
        std::function<AIAnalysisResult(const std::string&, const AIConfig&, ProgressCallback)> analyze;
    };
    std::vector<ProviderRoute> providers = {
        {"mimo", "MiMo",
         [this](const std::string& path, const AIConfig& cfg, ProgressCallback progress) {
             return analyzeWithMimoVideoUnderstanding(path, cfg, progress);
         }},
        {"gemini", "Gemini",
         [this](const std::string& path, const AIConfig& cfg, ProgressCallback progress) {
             return analyzeWithGeminiVideoUnderstanding(path, cfg, progress);
         }}
    };

    auto providerIt = std::find_if(providers.begin(), providers.end(),
        [&config](const ProviderRoute& provider) {
            return provider.id == config.provider;
        });

    if (config.apiKey.empty()) {
        if (onError) onError("请先配置 AI API Key");
        return;
    }
    if (config.baseUrl.empty()) {
        if (onError) onError("请先配置 AI API 地址");
        return;
    }
    if (providerIt == providers.end()) {
        if (onError) onError("暂不支持的 AI 服务商: " + config.provider);
        return;
    }
    ProviderRoute provider = *providerIt;

    if (hasCache(videoPath)) {
        AIAnalysisResult result = loadCache(videoPath);
        if (result.valid) {
            if (onProgress) onProgress(1.0f, "使用缓存结果");
            if (onComplete) onComplete(result);
            return;
        }
    }

    // 检查文件是否存在
    if (!std::filesystem::exists(videoPath)) {
        if (onError) onError("视频文件不存在: " + videoPath);
        return;
    }

    std::thread([this, videoPath, config, provider, onComplete, onProgress, onError]() {
        try {
            if (onProgress) onProgress(0.1f, "正在使用 " + provider.displayName + " 视频理解分析...");

            AIAnalysisResult result = provider.analyze(videoPath, config, onProgress);

            if (m_cancelled) {
                if (onError) onError("已取消");
                return;
            }
            if (!result.valid) {
                if (onError) onError(provider.displayName + " 视频分析没有生成有效章节，请检查:\n"
                                      "1. 服务商、API Key、地址和模型名称是否匹配\n"
                                      "2. 当前模型是否支持视频输入\n"
                                      "3. 视频是否成功转码并满足该服务商的大小限制");
                return;
            }

            saveCache(videoPath, result);
            if (onProgress) onProgress(1.0f, "分析完成");
            if (onComplete) onComplete(result);
        } catch (const std::exception& e) {
            logger().error("[AI] Analysis failed: " + std::string(e.what()));
            if (onError) onError(std::string("分析异常: ") + e.what());
        }
    }).detach();
}

AIAnalysisResult AIAnalyzer::analyzeWithMimoVideoUnderstanding(const std::string& videoPath,
                                                               const AIConfig& config,
                                                               ProgressCallback onProgress) {
    AIAnalysisResult result;

    // 提取视频为 MP4 (H.264, 限制 90 秒)
    if (onProgress) onProgress(0.1f, "正在提取视频...");
    std::string mp4Path = extractVideoForAI(videoPath, onProgress, 35 * 1024 * 1024, 90);
    if (mp4Path.empty()) {
        logger().error("[AI] Failed to extract video for AI");
        return result;
    }

    // 转换为 base64
    if (onProgress) onProgress(0.4f, "正在编码视频...");
    logger().info("[AI] Converting video to base64...");
    std::string base64Data = fileToBase64(mp4Path);
    
    // 清理临时文件
    if (std::filesystem::exists(mp4Path)) {
        std::filesystem::remove(mp4Path);
    }

    if (base64Data.empty()) {
        logger().error("[AI] Failed to convert video to base64");
        return result;
    }
    logger().info("[AI] Base64 size: " + std::to_string(base64Data.length()) + " chars");

    // 构建请求
    if (onProgress) onProgress(0.6f, "正在调用 MiMo 视频理解...");
    
    std::string model = config.model;
    std::string baseUrl = config.baseUrl;
    HttpClient http;
    http.setBaseUrl(baseUrl);
    http.setApiKey(config.apiKey);
    http.setAuthHeader("Authorization", "Bearer ");
    http.setTimeout(120);

    // 构建 MiMo 视频理解请求体
    std::string videoUrl = "data:video/mp4;base64," + base64Data;
    
    nlohmann::json requestBody = {
        {"model", model},
        {"messages", {
            {{"role", "system"}, {"content", "你是 MiMo 视频理解助手。请基于用户提供的视频内容进行分析。"}},
            {{"role", "user"}, {"content", {
                {
                    {"type", "video_url"},
                    {"video_url", {{"url", videoUrl}}},
                    {"fps", 2},
                    {"media_resolution", "default"}
                },
                {{"type", "text"}, {"text", analysisJsonPrompt(10)}},
            }}}
        }},
        {"max_completion_tokens", 4096}
    };

    logger().info("[AI] Calling MiMo video understanding: " + baseUrl + "/v1/chat/completions");
    logger().info("[AI] Model: " + model);

    auto response = http.post("v1/chat/completions", requestBody.dump());

    if (m_cancelled) return result;

    if (!response.success()) {
        logger().error("[AI] MiMo API error: status=" + std::to_string(response.statusCode));
        logger().error("[AI] Response: " + response.body.substr(0, 500));
        return result;
    }

    logger().info("[AI] MiMo API response status: " + std::to_string(response.statusCode));

    try {
        nlohmann::json j = nlohmann::json::parse(response.body);
        std::string content = extractJsonObjectText(openAIContentText(j));

        nlohmann::json aiResult = nlohmann::json::parse(content);

        result.summary = aiResult.value("summary", std::string());
        result.language = aiResult.value("language", std::string());

        result.chapters = parseChapters(aiResult, 0);

        result.analyzedAt = std::chrono::system_clock::now().time_since_epoch().count();
        result.valid = hasUsableAnalysis(result);

        if (result.valid) {
            logger().info("[AI] MiMo video analysis complete: " +
                std::to_string(result.chapters.size()) + " chapters");
        } else {
            logger().warning("[AI] MiMo video analysis returned no usable chapters. Content: " +
                content.substr(0, 500));
        }
    } catch (const std::exception& e) {
        logger().error("[AI] Failed to parse MiMo response: " + std::string(e.what()));
        logger().error("[AI] Response body: " + response.body.substr(0, 500));
    }

    if (onProgress) onProgress(0.9f, "分析完成");
    return result;
}

AIAnalysisResult AIAnalyzer::analyzeWithGeminiVideoUnderstanding(const std::string& videoPath,
                                                                 const AIConfig& config,
                                                                 ProgressCallback onProgress) {
    AIAnalysisResult result;

    if (onProgress) onProgress(0.1f, "正在为 Gemini 准备视频...");
    std::string mp4Path = extractVideoForAI(videoPath, onProgress, 18 * 1024 * 1024, 90);
    if (mp4Path.empty()) {
        logger().error("[AI] Failed to extract video for Gemini");
        return result;
    }

    if (onProgress) onProgress(0.45f, "正在编码 Gemini 视频...");
    std::string base64Data = fileToBase64(mp4Path);
    if (std::filesystem::exists(mp4Path)) {
        std::filesystem::remove(mp4Path);
    }

    if (base64Data.empty()) {
        logger().error("[AI] Failed to convert Gemini video to base64");
        return result;
    }

    if (onProgress) onProgress(0.65f, "正在调用 Gemini 视频理解...");

    HttpClient http;
    http.setBaseUrl(config.baseUrl);
    http.setApiKey(config.apiKey);
    http.setAuthHeader("x-goog-api-key", "");
    http.setTimeout(180);

    nlohmann::json requestBody = {
        {"contents", nlohmann::json::array({
            {
                {"role", "user"},
                {"parts", nlohmann::json::array({
                    {
                        {"inline_data", {
                            {"mime_type", "video/mp4"},
                            {"data", base64Data}
                        }}
                    },
                    {
                        {"text", analysisJsonPrompt(10)}
                    }
                })}
            }
        })},
        {"generationConfig", {
            {"temperature", 0.2},
            {"maxOutputTokens", 4096},
            {"responseMimeType", "application/json"}
        }}
    };

    std::string endpoint = "v1beta/models/" + config.model + ":generateContent";
    logger().info("[AI] Calling Gemini video understanding: " + config.baseUrl + "/" + endpoint);
    logger().info("[AI] Model: " + config.model);

    auto response = http.post(endpoint, requestBody.dump());

    if (m_cancelled) return result;

    if (!response.success()) {
        logger().error("[AI] Gemini API error: status=" + std::to_string(response.statusCode));
        logger().error("[AI] Response: " + response.body.substr(0, 500));
        return result;
    }

    logger().info("[AI] Gemini API response status: " + std::to_string(response.statusCode));

    try {
        nlohmann::json j = nlohmann::json::parse(response.body);
        std::string content = extractJsonObjectText(geminiContentText(j));
        nlohmann::json aiResult = nlohmann::json::parse(content);

        result.summary = aiResult.value("summary", std::string());
        result.language = aiResult.value("language", std::string());
        result.chapters = parseChapters(aiResult, 0);
        result.analyzedAt = std::chrono::system_clock::now().time_since_epoch().count();
        result.valid = hasUsableAnalysis(result);

        if (result.valid) {
            logger().info("[AI] Gemini video analysis complete: " +
                std::to_string(result.chapters.size()) + " chapters");
        } else {
            logger().warning("[AI] Gemini video analysis returned no usable chapters. Content: " +
                content.substr(0, 500));
        }
    } catch (const std::exception& e) {
        logger().error("[AI] Failed to parse Gemini response: " + std::string(e.what()));
        logger().error("[AI] Response body: " + response.body.substr(0, 500));
    }

    if (onProgress) onProgress(0.9f, "分析完成");
    return result;
}

std::string AIAnalyzer::extractVideoForAI(const std::string& videoPath,
                                          ProgressCallback onProgress,
                                          int64_t maxOutputBytes,
                                          int maxDurationSeconds) {
    // 统一转为 H.264/AVC1 MP4，按 provider 的限制压缩到指定大小以内。
    
    std::string outputPath = getCacheDir() + "/" + computeFileHash(videoPath) + "_ai.mp4";
    std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path());

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
    double duration = inputCtx->duration / (double)AV_TIME_BASE;
    avformat_close_input(&inputCtx);
    
    logger().info("[AI] Video duration: " + std::to_string(duration) + "s");
    
    bool needTrim = (duration > maxDurationSeconds);
    
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
            if (std::filesystem::exists(outputPath)) {
                std::filesystem::remove(outputPath);
            }
        }
        
        // 构建 FFmpeg 命令
        std::string cmd = "ffmpeg -y -i \"" + videoPath + "\"";
        if (needTrim) {
            cmd += " -t " + std::to_string(maxDurationSeconds);
        }
        
        // 视频：H.264，限制分辨率和码率
        cmd += " -c:v libx264 -preset fast";
        cmd += " -crf " + std::to_string(preset.crf);
        cmd += " -vf \"scale='min(" + std::to_string(preset.maxWidth) + ",iw)':'min(" + std::to_string(preset.maxHeight) + ",ih)':force_original_aspect_ratio=decrease\"";
        cmd += " -maxrate 500k -bufsize 1000k";
        
        // 音频：AAC 单声道
        cmd += " -c:a aac -b:a " + std::to_string(preset.audioBitrate) + "k -ac 1";
        cmd += " -movflags +faststart";
        cmd += " \"" + outputPath + "\"";
        
        logger().info("[AI] FFmpeg command: " + cmd);
        if (onProgress) onProgress(0.2f + i * 0.1f, "正在转码视频 (" + std::string(preset.name) + ")...");
        
        int ret = system(cmd.c_str());
        if (ret != 0) {
            logger().error("[AI] FFmpeg failed with code: " + std::to_string(ret));
            continue;
        }
        
        if (std::filesystem::exists(outputPath)) {
            auto fileSize = std::filesystem::file_size(outputPath);
            logger().info("[AI] Output video size: " + std::to_string(fileSize / 1024) + " KB (" + std::string(preset.name) + ")");
            
            if (fileSize <= maxOutputBytes) {
                return outputPath;
            }
            
            logger().warning("[AI] Video still too large, trying lower quality...");
        }
    }
    
    // 所有质量级别都失败
    logger().error("[AI] Failed to compress video to acceptable size");
    if (std::filesystem::exists(outputPath)) {
        std::filesystem::remove(outputPath);
    }
    return "";
}

std::string AIAnalyzer::fileToBase64(const std::string& filePath) {
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

std::string AIAnalyzer::findSubtitleFile(const std::string& videoPath) {
    std::filesystem::path video(videoPath);
    std::string baseName = video.stem().string();
    std::string dir = video.parent_path().string();
    
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

std::vector<TranscriptSegment> AIAnalyzer::loadSubtitleAsTranscript(const std::string& subtitlePath) {
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
