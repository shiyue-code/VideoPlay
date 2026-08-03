#pragma once

#include "core/common.h"
#include "core/settings.h"
#include "ai/httpclient.h"
#include "utils/logger.h"
#include "utils/string_utils.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace VideoPlay {

namespace {
Logger& logger() {
    static auto logger = Logger::get("ai");
    return *logger;
}

constexpr const char* kAnalysisCacheVersion = "detail-v2";
constexpr const char* kVideoTranscodeCacheVersion = "transcode-v1";

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
        return "gemini-2.5-flash";
    }
    return "mimo-v2.5";
}

std::string normalizeProviderModel(const std::string& provider, std::string model) {
    model = trim(model);
    std::string lowerModel = toLower(model);
    if (provider == "gemini") {
        if (model.empty() || startsWith(lowerModel, "mimo-") ||
            lowerModel == "gemini-3.5-flash") {
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
    config.analysisDetailLevel = std::clamp(config.analysisDetailLevel, 0, 2);
    return config;
}

struct AnalysisDetailSpec {
    int summaryMinWords = 300;
    int summaryMaxWords = 500;
    int maxChapters = 12;
    int minSegments = 12;
    int maxSegments = 30;
    std::string label = "标准";
};

AnalysisDetailSpec analysisDetailSpec(int detailLevel) {
    detailLevel = std::clamp(detailLevel, 0, 2);
    if (detailLevel == 0) {
        return {120, 220, 8, 6, 12, "简略"};
    }
    if (detailLevel == 2) {
        return {600, 900, 18, 30, 60, "详细"};
    }
    return {};
}

int analysisMaxOutputTokens(int detailLevel) {
    detailLevel = std::clamp(detailLevel, 0, 2);
    if (detailLevel == 0) return 2048;
    if (detailLevel == 2) return 8192;
    return 4096;
}

std::string analysisJsonPrompt(int minChapterSeconds, int detailLevel) {
    AnalysisDetailSpec spec = analysisDetailSpec(detailLevel);
    return "请分析这个视频，完成以下任务：\n"
           "详细程度：" + spec.label + "\n"
           "1. 生成中文摘要（" + std::to_string(spec.summaryMinWords) + "-" +
           std::to_string(spec.summaryMaxWords) + "字），覆盖主题、人物/对象、关键动作、结论和可见细节\n"
           "2. 根据内容主题变化自动划分章节（每个章节至少" +
           std::to_string(minChapterSeconds) + "秒，最多" +
           std::to_string(spec.maxChapters) + "个章节）\n"
           "3. 生成详细观察记录 transcript：按时间顺序列出 " +
           std::to_string(spec.minSegments) + "-" + std::to_string(spec.maxSegments) +
           " 条片段，记录画面、字幕/文字、动作、场景变化、重要说法或结论。"
           "每条片段要足够具体，便于后续问答检索；不要只写“继续讲解”。\n\n"
           "请只返回 JSON，不要包含 Markdown 代码块或额外说明：\n"
           "{\n"
           "  \"summary\": \"摘要内容\",\n"
           "  \"language\": \"zh\",\n"
           "  \"chapters\": [\n"
           "    {\"title\": \"章节标题\", \"startTime\": 开始时间毫秒}\n"
           "  ],\n"
           "  \"transcript\": [\n"
           "    {\"startTime\": 开始时间毫秒, \"endTime\": 结束时间毫秒, \"text\": \"这一时间段内的具体观察和信息\"}\n"
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

std::string conciseHttpError(const std::string& provider, const std::string& prefix,
                             const HttpResponse& response) {
    std::string apiMessage;
    try {
        auto j = nlohmann::json::parse(response.body);
        if (j.contains("error") && j["error"].contains("message") &&
            j["error"]["message"].is_string()) {
            apiMessage = j["error"]["message"].get<std::string>();
        }
    } catch (...) {
    }

    std::string providerName = provider == "gemini" ? "Gemini" : "AI";
    if (response.statusCode == 429) {
        return providerName + " 配额已用尽或触发限流，请稍后重试，或在 AI 设置中切换模型/API Key。";
    }
    if (response.statusCode == 401 || response.statusCode == 403) {
        return providerName + " 鉴权失败，请检查当前选中的服务商、API Key、Base URL 和模型权限是否匹配。";
    }

    std::string message = prefix + " (HTTP " + std::to_string(response.statusCode) + ")";
    if (!apiMessage.empty()) {
        if (apiMessage.size() > 160) {
            apiMessage = apiMessage.substr(0, 160) + "...";
        }
        message += ": " + apiMessage;
    } else if (!response.body.empty()) {
        message += ": " + response.body.substr(0, 160);
    }
    return message;
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

std::string segmentText(const nlohmann::json& segmentJson) {
    static const char* keys[] = {
        "text", "content", "description", "summary", "observation", "caption", "detail"
    };

    for (const char* key : keys) {
        if (segmentJson.contains(key) && segmentJson[key].is_string()) {
            std::string text = trim(segmentJson[key].get<std::string>());
            if (!text.empty()) {
                return text;
            }
        }
    }

    return {};
}

std::vector<TranscriptSegment> parseTranscriptSegments(const nlohmann::json& aiResult,
                                                       int64_t fallbackEndTimeMs) {
    std::vector<TranscriptSegment> segments;
    static const char* arrayKeys[] = {
        "transcript", "observations", "detailedObservations", "details", "moments"
    };

    const nlohmann::json* source = nullptr;
    for (const char* key : arrayKeys) {
        if (aiResult.contains(key) && aiResult[key].is_array()) {
            source = &aiResult[key];
            break;
        }
    }
    if (!source) {
        return segments;
    }

    for (const auto& item : *source) {
        if (!item.is_object()) {
            continue;
        }

        int64_t startTime = chapterStartTimeMs(item);
        if (startTime < 0) {
            startTime = segments.empty() ? 0 : segments.back().endTime;
        }

        int64_t endTime = -1;
        static const char* endKeys[] = {"endTime", "end_time", "end", "stop", "finish"};
        for (const char* key : endKeys) {
            if (item.contains(key)) {
                endTime = parseTimeValueMs(item[key]);
                if (endTime >= 0) {
                    break;
                }
            }
        }

        std::string text = segmentText(item);
        if (text.empty()) {
            continue;
        }

        TranscriptSegment segment;
        segment.startTime = startTime;
        segment.endTime = endTime > startTime ? endTime : startTime + 10000;
        segment.text = text;
        segment.confidence = 0.9f;
        segments.push_back(segment);
    }

    std::sort(segments.begin(), segments.end(), [](const TranscriptSegment& lhs,
                                                   const TranscriptSegment& rhs) {
        return lhs.startTime < rhs.startTime;
    });

    for (size_t i = 0; i < segments.size(); ++i) {
        if (i + 1 < segments.size() && segments[i].endTime > segments[i + 1].startTime) {
            segments[i].endTime = segments[i + 1].startTime;
        } else if (i + 1 == segments.size() && fallbackEndTimeMs > segments[i].startTime &&
                   segments[i].endTime <= segments[i].startTime + 10000) {
            segments[i].endTime = fallbackEndTimeMs;
        }
    }

    return segments;
}

bool hasUsableAnalysis(const AIAnalysisResult& result) {
    return !result.chapters.empty();
}

}


} // namespace VideoPlay
