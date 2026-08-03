#include "ai/aianalyzer.h"
#include "ai/cache_manager.h"
#include "ai/transcriber.h"
#include "ai/gemini_client.h"
#include "ai/mimo_client.h"
#include "ai/ai_utils.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <algorithm>
#include <chrono>

namespace VideoPlay {

AIAnalyzer::AIAnalyzer() {
    m_cacheManager = std::make_unique<CacheManager>(this);
    m_transcriber = std::make_unique<Transcriber>(this);
    m_geminiClient = std::make_unique<GeminiClient>(this);
    m_mimoClient = std::make_unique<MimoClient>(this);
}

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
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();
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
             return m_mimoClient->analyzeWithMimoVideoUnderstanding(path, cfg, progress);
         }},
        {"gemini", "Gemini",
         [this](const std::string& path, const AIConfig& cfg, ProgressCallback progress) {
             return m_geminiClient->analyzeWithGeminiVideoUnderstanding(path, cfg, progress);
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

    if (m_cacheManager->hasCache(videoPath)) {
        AIAnalysisResult result = m_cacheManager->loadCache(videoPath);
        if (result.valid) {
            if (!result.transcript.empty()) {
                if (onProgress) onProgress(1.0f, "使用缓存结果");
                if (onComplete) onComplete(result);
                return;
            }
            logger().info("[AI] Cache lacks detailed transcript, reanalyzing: " + videoPath);
        }
    }

    // 检查文件是否存在
    if (!std::filesystem::exists(std::filesystem::u8path(videoPath))) {
        if (onError) onError("视频文件不存在: " + videoPath);
        return;
    }

    m_workers.emplace_back([this, videoPath, config, provider, onComplete, onProgress, onError]() {
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

            m_cacheManager->saveCache(videoPath, result);
            if (onProgress) onProgress(1.0f, "分析完成");
            if (onComplete) onComplete(result);
        } catch (const std::exception& e) {
            logger().error("[AI] Analysis failed: " + std::string(e.what()));
            if (onError) onError(std::string("分析异常: ") + e.what());
        }
    });
}


void AIAnalyzer::askVideoDirect(const std::string& videoPath,
                                const std::string& question,
                                QuestionCallback onComplete,
                                ProgressCallback onProgress,
                                ErrorCallback onError) {
    m_cancelled = false;
    AIConfig config = snapshotConfig();
    if (config.apiKey.empty() || config.baseUrl.empty()) {
        if (onError) onError("API Key 或 Base URL 未配置");
        return;
    }

    m_workers.emplace_back([this, videoPath, question, config, onComplete, onProgress, onError]() {
        try {
            bool errorReported = false;
            auto reportError = [&](const std::string& error) {
                errorReported = true;
                if (onError) {
                    onError(error);
                }
            };

            std::string answer;
            if (config.provider == "gemini") {
                answer = m_geminiClient->askGeminiVideoDirect(videoPath, question, config, onProgress, reportError);
            } else if (config.provider == "mimo") {
                answer = m_mimoClient->askMimoVideoDirect(videoPath, question, config, onProgress, reportError);
            } else {
                reportError("暂不支持的 AI 服务商: " + config.provider);
                return;
            }

            if (m_cancelled) {
                return;
            }
            if (answer.empty()) {
                if (!errorReported) {
                    reportError("API 没有返回可用答案");
                }
                return;
            }
            if (onProgress) onProgress(1.0f, "搜索完成");
            if (onComplete) onComplete(answer);
        } catch (const std::exception& e) {
            if (onError) onError(std::string("API 搜索异常: ") + e.what());
        }
    });
}


void AIAnalyzer::askQuestion(const std::string& question, const AIAnalysisResult& context, QuestionCallback onComplete, ErrorCallback onError) {
    m_cancelled = false;
    m_workers.emplace_back([this, question, context, onComplete, onError]() {
        AIConfig config = snapshotConfig();
        if (config.apiKey.empty() || config.baseUrl.empty()) {
            if (onError) onError("API Key 或 Base URL 未配置");
            return;
        }

        std::string prompt = "请根据提供的视频上下文回答用户的问题。\n\n";
        prompt += "【视频摘要】\n" + context.summary + "\n\n";
        prompt += "【视频章节】\n";
        for (const auto& ch : context.chapters) {
            prompt += "- [" + VideoPlay::formatTime(ch.startTime) + "] " + ch.title + "\n";
        }
        prompt += "\n【视频详细记录/字幕/文稿】\n";
        size_t maxLen = 60000;
        size_t currentLen = 0;
        for (const auto& seg : context.transcript) {
            std::string line = "[" + VideoPlay::formatTime(seg.startTime) + "] " + seg.text + "\n";
            if (currentLen + line.length() > maxLen) break;
            prompt += line;
            currentLen += line.length();
        }
        if (context.transcript.empty()) {
            prompt += "（没有详细记录，仅可依据摘要和章节回答。）\n";
        }
        prompt += "\n【用户提问】\n" + question + "\n\n";
        prompt += "要求：优先引用详细记录中的具体事实作答；不要只复述摘要。"
                  "能定位到时间点时使用 [MM:SS] 或 [HH:MM:SS] 标出。"
                  "如果上下文不足，请明确说明缺少哪些信息，并给出已有依据。";

        HttpClient http;
        http.setBaseUrl(config.baseUrl);
        http.setApiKey(config.apiKey);
        http.setTimeout(60);

        HttpResponse response;
        if (config.provider == "gemini") {
            http.setAuthHeader("x-goog-api-key", "");

            nlohmann::json payload = {
                {"contents", nlohmann::json::array({
                    {
                        {"role", "user"},
                        {"parts", nlohmann::json::array({
                            {{"text", "你是一个视频助手，只能根据提供的视频上下文回答问题。\n\n" + prompt}}
                        })}
                    }
                })},
                {"generationConfig", {
                    {"temperature", 0.7},
                    {"maxOutputTokens", 2048}
                }}
            };

            std::string endpoint = "v1beta/models/" + config.model + ":generateContent";
            logger().info("[AI] Calling Gemini QA: " + config.baseUrl + "/" + endpoint);
            response = http.post(endpoint, payload.dump());
        } else {
            http.setAuthHeader("Authorization", "Bearer ");

            nlohmann::json payload = {
                {"model", config.model},
                {"messages", nlohmann::json::array({
                    {{"role", "system"}, {"content", "你是一个视频助手，只能根据提供的视频上下文回答问题。"}},
                    {{"role", "user"}, {"content", prompt}}
                })},
                {"temperature", 0.7}
            };

            response = http.post("v1/chat/completions", payload.dump());
        }

        if (m_cancelled) return;

        if (!response.success()) {
            logger().error("[AI] QA API error: status=" + std::to_string(response.statusCode));
            logger().error("[AI] Response: " + response.body.substr(0, 500));
            if (onError) onError(conciseHttpError(config.provider, "请求失败", response));
            return;
        }

        try {
            auto j = nlohmann::json::parse(response.body);
            if (config.provider == "gemini") {
                std::string answer = geminiContentText(j);
                if (!answer.empty()) {
                    if (onComplete) onComplete(answer);
                } else if (onError) {
                    onError("Gemini 响应格式错误");
                }
            } else if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
                std::string answer = j["choices"][0]["message"]["content"].get<std::string>();
                if (onComplete) onComplete(answer);
            } else {
                if (onError) onError("API 响应格式错误");
            }
        } catch (const std::exception& e) {
            if (onError) onError(std::string("解析失败: ") + e.what());
        }
    });
}


bool AIAnalyzer::hasCache(const std::string& videoPath) const {
    return m_cacheManager->hasCache(videoPath);
}

AIAnalysisResult AIAnalyzer::loadCache(const std::string& videoPath) const {
    return m_cacheManager->loadCache(videoPath);
}

void AIAnalyzer::clearCache(const std::string& videoPath) {
    m_cacheManager->clearCache(videoPath);
}

void AIAnalyzer::clearAllCache() {
    m_cacheManager->clearAllCache();
}

} // namespace VideoPlay
