#include "ai/gemini_client.h"
#include "ai/aianalyzer.h"
#include "ai/ai_utils.h"
#include "ai/transcriber.h"
#include "ai/cache_manager.h"
#include <fstream>
#include <thread>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace VideoPlay {

GeminiClient::GeminiClient(AIAnalyzer* analyzer) : m_analyzer(analyzer) {}

AIAnalysisResult GeminiClient::analyzeWithGeminiVideoUnderstanding(const std::string& videoPath,
                                                                 const AIConfig& config,
                                                                 ProgressCallback onProgress) {
    AIAnalysisResult result;

    if (onProgress) onProgress(0.1f, "正在为 Gemini 准备视频...");
    std::string mp4Path = m_analyzer->m_transcriber->extractVideoForAI(videoPath, onProgress, 18 * 1024 * 1024, 90);
    if (mp4Path.empty()) {
        logger().error("[AI] Failed to extract video for Gemini");
        return result;
    }

    if (onProgress) onProgress(0.45f, "正在编码 Gemini 视频...");
    std::string base64Data = m_analyzer->m_transcriber->fileToBase64(mp4Path);

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
                        {"text", analysisJsonPrompt(10, config.analysisDetailLevel)}
                    }
                })}
            }
        })},
        {"generationConfig", {
            {"temperature", 0.2},
            {"maxOutputTokens", analysisMaxOutputTokens(config.analysisDetailLevel)},
            {"responseMimeType", "application/json"}
        }}
    };

    std::string endpoint = "v1beta/models/" + config.model + ":generateContent";
    logger().info("[AI] Calling Gemini video understanding: " + config.baseUrl + "/" + endpoint);
    logger().info("[AI] Model: " + config.model);

    auto response = http.post(endpoint, requestBody.dump());

    if (m_analyzer->m_cancelled) return result;

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
        result.transcript = parseTranscriptSegments(aiResult, 0);
        result.analyzedAt = std::chrono::system_clock::now().time_since_epoch().count();
        result.valid = hasUsableAnalysis(result);

        if (result.valid) {
            logger().info("[AI] Gemini video analysis complete: " +
                std::to_string(result.chapters.size()) + " chapters, " +
                std::to_string(result.transcript.size()) + " detail segments");
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

AIAnalysisResult GeminiClient::analyzeWithGPT(const std::vector<TranscriptSegment>& transcript,
                                             const std::string& videoPath,
                                             ProgressCallback onProgress) {
    AIAnalysisResult result;
    AnalysisDetailSpec detailSpec = analysisDetailSpec(m_analyzer->snapshotConfig().analysisDetailLevel);
    if (onProgress) onProgress(0.7f, "正在生成摘要...");

    std::string model;
    std::string apiKey;
    std::string baseUrl;
    {
        std::lock_guard<std::mutex> lock(m_analyzer->m_mutex);
        model = m_analyzer->m_config.model;
        apiKey = m_analyzer->m_config.apiKey;
        baseUrl = m_analyzer->m_config.baseUrl;
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
1. 生成中文摘要（)" + std::to_string(detailSpec.summaryMinWords) + "-" +
        std::to_string(detailSpec.summaryMaxWords) + R"(字），覆盖主题、关键论点、重要细节和结论
2. 根据内容主题变化自动划分章节（每个章节至少30秒，最多)" +
        std::to_string(detailSpec.maxChapters) + R"(个章节）

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
        {"max_tokens", analysisMaxOutputTokens(m_analyzer->snapshotConfig().analysisDetailLevel)}
    };

    logger().info("[AI] Calling GPT API: " + baseUrl + "/v1/chat/completions");

    auto response = m_analyzer->m_http.post("v1/chat/completions", requestBody.dump());

    if (m_analyzer->m_cancelled) return result;

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

std::string GeminiClient::askGeminiVideoDirect(const std::string& videoPath,
                                             const std::string& question,
                                             const AIConfig& config,
                                             ProgressCallback onProgress,
                                             ErrorCallback onError) {
    GeminiVideoFile videoFile = ensureGeminiVideoFile(videoPath, config, onProgress, onError);
    if (!videoFile.valid) {
        return {};
    }

    HttpClient http;
    http.setBaseUrl(config.baseUrl);
    http.setApiKey(config.apiKey);
    http.setAuthHeader("x-goog-api-key", "");
    http.setTimeout(120);

    std::string prompt =
        "请直接根据这个视频回答用户的搜索或问题。\n"
        "如果用户是在搜索内容，请列出最相关的片段和时间点；如果用户是在提问，请直接作答。\n"
        "请尽量给出 [MM:SS] 或 [HH:MM:SS] 时间点。不要要求用户先进行视频分析。\n\n"
        "用户输入：\n" + question;

    nlohmann::json requestBody = {
        {"contents", nlohmann::json::array({
            {
                {"role", "user"},
                {"parts", nlohmann::json::array({
                    {
                        {"file_data", {
                            {"mime_type", videoFile.mimeType},
                            {"file_uri", videoFile.uri}
                        }}
                    },
                    {
                        {"text", prompt}
                    }
                })}
            }
        })},
        {"generationConfig", {
            {"temperature", 0.2},
            {"maxOutputTokens", 2048}
        }}
    };

    if (onProgress) onProgress(0.75f, "正在请求 Gemini 回答...");
    std::string endpoint = "v1beta/models/" + config.model + ":generateContent";
    logger().info("[AI] Calling Gemini direct video QA: " +
                  config.baseUrl + "/" + endpoint + ", file=" + videoFile.name);
    auto response = http.post(endpoint, requestBody.dump());
    logger().info("[AI] Gemini direct video QA response status: " +
                  std::to_string(response.statusCode));
    if (!response.success()) {
        logger().error("[AI] Gemini direct video QA error: status=" +
                       std::to_string(response.statusCode));
        logger().error("[AI] Response: " + response.body.substr(0, 500));
        if (onError) onError(conciseHttpError(config.provider, "请求失败", response));
        return {};
    }

    try {
        auto j = nlohmann::json::parse(response.body);
        return geminiContentText(j);
    } catch (const std::exception& e) {
        if (onError) onError(std::string("解析失败: ") + e.what());
        return {};
    }
}

GeminiVideoFile GeminiClient::ensureGeminiVideoFile(const std::string& videoPath,
                                                              const AIConfig& config,
                                                              ProgressCallback onProgress,
                                                              ErrorCallback onError) {
    constexpr int64_t kGeminiDirectVideoLimit = 18 * 1024 * 1024;
    constexpr int kGeminiDirectVideoSeconds = 120;

    std::string sourceHash = m_analyzer->m_cacheManager->computeSourceHash(videoPath);
    if (sourceHash.empty()) {
        if (onError) onError("无法识别当前视频，不能创建 Gemini 复用文件");
        return {};
    }

    double duration = 0.0;
    AVFormatContext* inputCtx = nullptr;
    if (avformat_open_input(&inputCtx, videoPath.c_str(), nullptr, nullptr) == 0) {
        if (avformat_find_stream_info(inputCtx, nullptr) >= 0 &&
            inputCtx->duration != AV_NOPTS_VALUE && inputCtx->duration > 0) {
            duration = inputCtx->duration / static_cast<double>(AV_TIME_BASE);
        }
        avformat_close_input(&inputCtx);
    }

    double effectiveDuration = duration > 0.0
        ? std::min(duration, static_cast<double>(kGeminiDirectVideoSeconds))
        : static_cast<double>(kGeminiDirectVideoSeconds);
    int clipSeconds = std::max(1, static_cast<int>(std::ceil(effectiveDuration)));
    std::string cachePath = m_analyzer->m_cacheManager->getGeminiFileCachePath(sourceHash, clipSeconds, kGeminiDirectVideoLimit);

    GeminiVideoFile cachedFile = m_analyzer->m_cacheManager->loadGeminiFileCache(cachePath);
    if (cachedFile.valid) {
        if (onProgress) onProgress(0.35f, "复用 Gemini 视频文件...");
        if (waitForGeminiFileActive(cachedFile, config, onProgress)) {
            logger().info("[AI] Reusing Gemini video file: " + cachedFile.name);
            return cachedFile;
        }
        std::filesystem::remove(cachePath);
        logger().warning("[AI] Gemini cached file is no longer usable, uploading again");
    }

    if (onProgress) onProgress(0.1f, "正在为 Gemini 准备视频...");
    std::string mp4Path = m_analyzer->m_transcriber->extractVideoForAI(videoPath, onProgress,
                                            kGeminiDirectVideoLimit,
                                            kGeminiDirectVideoSeconds);
    if (mp4Path.empty()) {
        if (onError) onError("视频转码失败，无法调用 Gemini 搜索");
        return {};
    }

    return uploadGeminiVideoFile(mp4Path, cachePath, config, onProgress, onError);
}

GeminiVideoFile GeminiClient::uploadGeminiVideoFile(const std::string& mp4Path,
                                                              const std::string& cachePath,
                                                              const AIConfig& config,
                                                              ProgressCallback onProgress,
                                                              ErrorCallback onError) {
    GeminiVideoFile file;

    std::ifstream input(mp4Path, std::ios::binary);
    if (!input.is_open()) {
        if (onError) onError("无法读取转码视频，不能上传到 Gemini");
        return file;
    }
    std::string videoBytes((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    if (videoBytes.empty()) {
        if (onError) onError("转码视频为空，不能上传到 Gemini");
        return file;
    }

    HttpClient http;
    http.setBaseUrl(config.baseUrl);
    http.setApiKey(config.apiKey);
    http.setAuthHeader("x-goog-api-key", "");
    http.setTimeout(120);

    if (onProgress) onProgress(0.48f, "正在上传视频到 Gemini...");
    nlohmann::json startBody = {
        {"file", {
            {"display_name", std::filesystem::path(mp4Path).filename().u8string()}
        }}
    };

    std::map<std::string, std::string> startHeaders = {
        {"Content-Type", "application/json"},
        {"X-Goog-Upload-Protocol", "resumable"},
        {"X-Goog-Upload-Command", "start"},
        {"X-Goog-Upload-Header-Content-Length", std::to_string(videoBytes.size())},
        {"X-Goog-Upload-Header-Content-Type", "video/mp4"}
    };

    auto startResponse = http.postRaw("upload/v1beta/files", startBody.dump(), startHeaders);
    if (!startResponse.success()) {
        logger().error("[AI] Gemini file upload start failed: status=" +
                       std::to_string(startResponse.statusCode));
        logger().error("[AI] Response: " + startResponse.body.substr(0, 500));
        if (onError) onError(conciseHttpError(config.provider, "Gemini 文件上传初始化失败", startResponse));
        return file;
    }

    auto uploadIt = startResponse.headers.find("x-goog-upload-url");
    if (uploadIt == startResponse.headers.end() || uploadIt->second.empty()) {
        if (onError) onError("Gemini 没有返回文件上传地址");
        return file;
    }

    if (onProgress) onProgress(0.55f, "正在完成 Gemini 视频上传...");
    std::map<std::string, std::string> uploadHeaders = {
        {"Content-Type", "video/mp4"},
        {"Content-Length", std::to_string(videoBytes.size())},
        {"X-Goog-Upload-Offset", "0"},
        {"X-Goog-Upload-Command", "upload, finalize"}
    };

    auto uploadResponse = http.postRaw(uploadIt->second, videoBytes, uploadHeaders);
    if (!uploadResponse.success()) {
        logger().error("[AI] Gemini file upload failed: status=" +
                       std::to_string(uploadResponse.statusCode));
        logger().error("[AI] Response: " + uploadResponse.body.substr(0, 500));
        if (onError) onError(conciseHttpError(config.provider, "Gemini 文件上传失败", uploadResponse));
        return file;
    }

    try {
        auto j = nlohmann::json::parse(uploadResponse.body);
        const auto& fileJson = j.contains("file") && j["file"].is_object() ? j["file"] : j;
        file.name = fileJson.value("name", std::string());
        file.uri = fileJson.value("uri", std::string());
        file.mimeType = fileJson.value("mimeType", std::string("video/mp4"));
        file.expiresAt = static_cast<int64_t>(std::time(nullptr)) + 46 * 60 * 60;
        file.valid = !file.name.empty() && !file.uri.empty();
    } catch (const std::exception& e) {
        if (onError) onError(std::string("解析 Gemini 文件上传结果失败: ") + e.what());
        return GeminiVideoFile();
    }

    if (!file.valid) {
        if (onError) onError("Gemini 文件上传结果缺少 file_uri");
        return GeminiVideoFile();
    }

    if (!waitForGeminiFileActive(file, config, onProgress)) {
        if (onError) onError("Gemini 视频文件未能完成处理");
        return GeminiVideoFile();
    }

    m_analyzer->m_cacheManager->saveGeminiFileCache(cachePath, file);
    logger().info("[AI] Gemini video file uploaded and cached: " + file.name);
    return file;
}

bool GeminiClient::waitForGeminiFileActive(const GeminiVideoFile& file,
                                         const AIConfig& config,
                                         ProgressCallback onProgress) {
    if (!file.valid || file.name.empty()) {
        return false;
    }

    HttpClient http;
    http.setBaseUrl(config.baseUrl);
    http.setApiKey(config.apiKey);
    http.setAuthHeader("x-goog-api-key", "");
    http.setTimeout(30);

    std::string endpoint = "v1beta/" + file.name;
    for (int attempt = 0; attempt < 12; ++attempt) {
        if (m_analyzer->m_cancelled) {
            return false;
        }

        auto response = http.get(endpoint);
        if (!response.success()) {
            logger().warning("[AI] Gemini file status check failed: status=" +
                             std::to_string(response.statusCode));
            return false;
        }

        try {
            auto j = nlohmann::json::parse(response.body);
            std::string state = j.value("state", std::string());
            if (state.empty() || state == "ACTIVE") {
                return true;
            }
            if (state == "FAILED") {
                logger().warning("[AI] Gemini file processing failed: " + file.name);
                return false;
            }
            if (onProgress) {
                onProgress(0.62f, "等待 Gemini 处理视频...");
            }
        } catch (const std::exception& e) {
            logger().warning("[AI] Failed to parse Gemini file status: " + std::string(e.what()));
            return false;
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    logger().warning("[AI] Gemini file did not become ACTIVE in time: " + file.name);
    return false;
}


} // namespace VideoPlay
