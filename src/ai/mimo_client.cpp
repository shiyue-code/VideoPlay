#include "ai/mimo_client.h"
#include "ai/aianalyzer.h"
#include "ai/ai_utils.h"
#include "ai/transcriber.h"

namespace VideoPlay {

MimoClient::MimoClient(AIAnalyzer* analyzer) : m_analyzer(analyzer) {}

AIAnalysisResult MimoClient::analyzeWithMimoVideoUnderstanding(const std::string& videoPath,
                                                               const AIConfig& config,
                                                               ProgressCallback onProgress) {
    AIAnalysisResult result;

    // 提取视频为 MP4 (H.264, 限制 90 秒)
    if (onProgress) onProgress(0.1f, "正在提取视频...");
    std::string mp4Path = m_analyzer->m_transcriber->extractVideoForAI(videoPath, onProgress, 35 * 1024 * 1024, 90);
    if (mp4Path.empty()) {
        logger().error("[AI] Failed to extract video for AI");
        return result;
    }

    // 转换为 base64
    if (onProgress) onProgress(0.4f, "正在编码视频...");
    logger().info("[AI] Converting video to base64...");
    std::string base64Data = m_analyzer->m_transcriber->fileToBase64(mp4Path);

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
                {{"type", "text"}, {"text", analysisJsonPrompt(10, config.analysisDetailLevel)}},
            }}}
        }},
        {"max_completion_tokens", analysisMaxOutputTokens(config.analysisDetailLevel)}
    };

    logger().info("[AI] Calling MiMo video understanding: " + baseUrl + "/v1/chat/completions");
    logger().info("[AI] Model: " + model);

    auto response = http.post("v1/chat/completions", requestBody.dump());

    if (m_analyzer->m_cancelled) return result;

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
        result.transcript = parseTranscriptSegments(aiResult, 0);

        result.analyzedAt = std::chrono::system_clock::now().time_since_epoch().count();
        result.valid = hasUsableAnalysis(result);

        if (result.valid) {
            logger().info("[AI] MiMo video analysis complete: " +
                std::to_string(result.chapters.size()) + " chapters, " +
                std::to_string(result.transcript.size()) + " detail segments");
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

std::string MimoClient::askMimoVideoDirect(const std::string& videoPath,
                                           const std::string& question,
                                           const AIConfig& config,
                                           ProgressCallback onProgress,
                                           ErrorCallback onError) {
    if (onProgress) onProgress(0.1f, "正在为 MiMo 准备视频...");
    std::string mp4Path = m_analyzer->m_transcriber->extractVideoForAI(videoPath, onProgress, 20 * 1024 * 1024, 120);
    if (mp4Path.empty()) {
        if (onError) onError("视频转码失败，无法调用 API 搜索");
        return {};
    }

    if (onProgress) onProgress(0.45f, "正在编码视频...");
    std::string base64Data = m_analyzer->m_transcriber->fileToBase64(mp4Path);
    if (base64Data.empty()) {
        if (onError) onError("视频编码失败，无法调用 API 搜索");
        return {};
    }
    logger().info("[AI] MiMo direct video base64 size: " +
                  std::to_string(base64Data.length()) + " chars");

    HttpClient http;
    http.setBaseUrl(config.baseUrl);
    http.setApiKey(config.apiKey);
    http.setAuthHeader("Authorization", "Bearer ");
    http.setTimeout(120);

    std::string prompt =
        "请直接根据这个视频回答用户的搜索或问题。\n"
        "如果用户是在搜索内容，请列出最相关的片段和时间点；如果用户是在提问，请直接作答。\n"
        "请尽量给出 [MM:SS] 或 [HH:MM:SS] 时间点。不要要求用户先进行视频分析。\n\n"
        "用户输入：\n" + question;

    nlohmann::json requestBody = {
        {"model", config.model},
        {"messages", {
            {{"role", "system"}, {"content", "你是视频搜索与问答助手，只根据用户提供的视频回答。"}},
            {{"role", "user"}, {"content", {
                {
                    {"type", "video_url"},
                    {"video_url", {{"url", "data:video/mp4;base64," + base64Data}}},
                    {"fps", 2},
                    {"media_resolution", "default"}
                },
                {{"type", "text"}, {"text", prompt}},
            }}}
        }},
        {"max_completion_tokens", 2048}
    };

    if (onProgress) onProgress(0.75f, "正在上传视频并等待 MiMo 回答...");
    logger().info("[AI] Calling MiMo direct video QA: " +
                  config.baseUrl + "/v1/chat/completions, model=" + config.model);
    auto response = http.post("v1/chat/completions", requestBody.dump());
    logger().info("[AI] MiMo direct video QA response status: " +
                  std::to_string(response.statusCode));
    if (!response.success()) {
        logger().error("[AI] MiMo direct video QA error: status=" +
                       std::to_string(response.statusCode));
        logger().error("[AI] Response: " + response.body.substr(0, 500));
        if (onError) onError(conciseHttpError(config.provider, "请求失败", response));
        return {};
    }

    try {
        auto j = nlohmann::json::parse(response.body);
        return openAIContentText(j);
    } catch (const std::exception& e) {
        if (onError) onError(std::string("解析失败: ") + e.what());
        return {};
    }
}


} // namespace VideoPlay
