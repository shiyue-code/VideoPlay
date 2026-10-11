// ai_utils.h 纯函数单元测试
#include <gtest/gtest.h>

#include "ai/ai_utils.h"

using namespace VideoPlay;

// ---------- normalizeProvider ----------

TEST(AiUtils, NormalizeProviderEmptyAndAutoFallbackToMimo)
{
    EXPECT_EQ(normalizeProvider("", "", ""), "mimo");
    EXPECT_EQ(normalizeProvider("auto", "", ""), "mimo");
    EXPECT_EQ(normalizeProvider(" AUTO ", "", ""), "mimo");
}

TEST(AiUtils, NormalizeProviderDetectsGeminiByBaseUrlOrModel)
{
    EXPECT_EQ(normalizeProvider("", "https://generativelanguage.googleapis.com", ""),
              "gemini");
    EXPECT_EQ(normalizeProvider("auto", "HTTPS://GENERATIVELANGUAGE.GOOGLEAPIS.COM/v1beta",
                              ""), "gemini");
    EXPECT_EQ(normalizeProvider("", "", "gemini-2.5-flash"), "gemini");
    EXPECT_EQ(normalizeProvider("auto", "", "Gemini-Pro"), "gemini");
    // baseUrl 与 model 都没有 gemini 线索时回落 mimo
    EXPECT_EQ(normalizeProvider("", "https://example.com", "some-model"), "mimo");
}

TEST(AiUtils, NormalizeProviderAliases)
{
    EXPECT_EQ(normalizeProvider("google", "", ""), "gemini");
    EXPECT_EQ(normalizeProvider("Google", "", ""), "gemini");
    EXPECT_EQ(normalizeProvider("xiaomi", "", ""), "mimo");
    EXPECT_EQ(normalizeProvider("xiaomimimo", "", ""), "mimo");
    EXPECT_EQ(normalizeProvider("XiaoMi", "", ""), "mimo");
}

TEST(AiUtils, NormalizeProviderUnknownPassthrough)
{
    EXPECT_EQ(normalizeProvider("gemini", "", ""), "gemini");
    EXPECT_EQ(normalizeProvider("mimo", "", ""), "mimo");
    // 未知 provider 原样回传（trim + 小写化后的值）
    EXPECT_EQ(normalizeProvider("CustomLLM", "", ""), "customllm");
}

// ---------- defaultBaseUrlForProvider / defaultModelForProvider ----------

TEST(AiUtils, DefaultBaseUrlForProvider)
{
    EXPECT_EQ(defaultBaseUrlForProvider("gemini"),
              "https://generativelanguage.googleapis.com");
    EXPECT_EQ(defaultBaseUrlForProvider("mimo"), "https://api.xiaomimimo.com");
    EXPECT_EQ(defaultBaseUrlForProvider("unknown"), "https://api.xiaomimimo.com");
}

TEST(AiUtils, DefaultModelForProvider)
{
    EXPECT_EQ(defaultModelForProvider("gemini"), "gemini-2.5-flash");
    EXPECT_EQ(defaultModelForProvider("mimo"), "mimo-v2.5");
    EXPECT_EQ(defaultModelForProvider("unknown"), "mimo-v2.5");
}

// ---------- normalizeProviderModel ----------

TEST(AiUtils, NormalizeProviderModelGemini)
{
    // 空模型、mimo- 前缀、被列入黑名单的型号都回落 gemini 默认模型
    EXPECT_EQ(normalizeProviderModel("gemini", ""), "gemini-2.5-flash");
    EXPECT_EQ(normalizeProviderModel("gemini", "mimo-v2.5"), "gemini-2.5-flash");
    EXPECT_EQ(normalizeProviderModel("gemini", "Mimo-v9"), "gemini-2.5-flash");
    EXPECT_EQ(normalizeProviderModel("gemini", "gemini-3.5-flash"), "gemini-2.5-flash");
    // 正常 gemini 模型名原样保留（去首尾空白后返回）
    EXPECT_EQ(normalizeProviderModel("gemini", "gemini-2.5-pro"), "gemini-2.5-pro");
    EXPECT_EQ(normalizeProviderModel("gemini", "  gemini-pro  "), "gemini-pro");
}

TEST(AiUtils, NormalizeProviderModelMimo)
{
    EXPECT_EQ(normalizeProviderModel("mimo", ""), "mimo-v2.5");
    // 含 gemini 字样的模型名回落默认模型
    EXPECT_EQ(normalizeProviderModel("mimo", "gemini-2.5-flash"), "mimo-v2.5");
    // 两个旧型号被硬编码回落
    EXPECT_EQ(normalizeProviderModel("mimo", "mimo-v2-pro"), "mimo-v2.5");
    EXPECT_EQ(normalizeProviderModel("mimo", "mimo-v2.5-pro"), "mimo-v2.5");
    EXPECT_EQ(normalizeProviderModel("mimo", "mimo-v3"), "mimo-v3");
    // 非 gemini provider（含未知值）走同一个 mimo 分支
    EXPECT_EQ(normalizeProviderModel("other", "gemini-x"), "mimo-v2.5");
    EXPECT_EQ(normalizeProviderModel("other", "custom-model"), "custom-model");
}

// ---------- normalizeProviderBaseUrl ----------

TEST(AiUtils, NormalizeProviderBaseUrlDefaults)
{
    EXPECT_EQ(normalizeProviderBaseUrl("gemini", ""),
              "https://generativelanguage.googleapis.com");
    EXPECT_EQ(normalizeProviderBaseUrl("mimo", ""), "https://api.xiaomimimo.com");
    EXPECT_EQ(normalizeProviderBaseUrl("unknown", "   "), "https://api.xiaomimimo.com");
}

TEST(AiUtils, NormalizeProviderBaseUrlStripsVersionSuffix)
{
    EXPECT_EQ(
        normalizeProviderBaseUrl("gemini", "https://generativelanguage.googleapis.com/v1beta"),
        "https://generativelanguage.googleapis.com");
    EXPECT_EQ(normalizeProviderBaseUrl(
                  "gemini", "https://generativelanguage.googleapis.com/v1beta/"),
              "https://generativelanguage.googleapis.com");
    EXPECT_EQ(normalizeProviderBaseUrl("mimo", "https://api.xiaomimimo.com/v1"),
              "https://api.xiaomimimo.com");
    // /v1beta 后缀在任意自定义 gemini 地址上都会被剥掉
    EXPECT_EQ(normalizeProviderBaseUrl("gemini", "https://proxy.example.com/v1beta"),
              "https://proxy.example.com");
    // provider 不匹配时 /v1 与 /v1beta 不剥
    EXPECT_EQ(normalizeProviderBaseUrl("mimo", "https://example.com/v1beta"),
              "https://example.com/v1beta");
    EXPECT_EQ(normalizeProviderBaseUrl("gemini", "https://example.com/v1"),
              "https://example.com/v1");
}

TEST(AiUtils, NormalizeProviderBaseUrlPreservesCustom)
{
    EXPECT_EQ(normalizeProviderBaseUrl("mimo", "https://example.com/api"),
              "https://example.com/api");
    // 末尾多余斜杠统一去掉
    EXPECT_EQ(normalizeProviderBaseUrl("mimo", "https://example.com/api//"),
              "https://example.com/api");
}

TEST(AiUtils, NormalizedConfigCombinesAllRules)
{
    AIConfig config;
    config.provider = "auto";
    config.baseUrl = "https://generativelanguage.googleapis.com/v1beta/";
    config.model = "";
    config.analysisDetailLevel = 99;

    AIConfig normalized = normalizedConfig(config);
    EXPECT_EQ(normalized.provider, "gemini");
    EXPECT_EQ(normalized.baseUrl, "https://generativelanguage.googleapis.com");
    EXPECT_EQ(normalized.model, "gemini-2.5-flash");
    // 越界 detailLevel 被钳到 0-2
    EXPECT_EQ(normalized.analysisDetailLevel, 2);
}

// ---------- analysisDetailSpec / analysisMaxOutputTokens / analysisJsonPrompt ----------

TEST(AiUtils, AnalysisDetailSpecLevels)
{
    auto brief = analysisDetailSpec(0);
    EXPECT_EQ(brief.label, std::string(u8"简略"));
    EXPECT_EQ(brief.summaryMinWords, 120);
    auto standard = analysisDetailSpec(1);
    EXPECT_EQ(standard.label, std::string(u8"标准"));
    EXPECT_EQ(standard.maxChapters, 12);
    auto detail = analysisDetailSpec(2);
    EXPECT_EQ(detail.label, std::string(u8"详细"));
    EXPECT_EQ(detail.maxSegments, 60);
    // 越界钳制
    EXPECT_EQ(analysisDetailSpec(-5).label, std::string(u8"简略"));
    EXPECT_EQ(analysisDetailSpec(99).label, std::string(u8"详细"));
}

TEST(AiUtils, AnalysisMaxOutputTokens)
{
    EXPECT_EQ(analysisMaxOutputTokens(0), 2048);
    EXPECT_EQ(analysisMaxOutputTokens(1), 4096);
    EXPECT_EQ(analysisMaxOutputTokens(2), 8192);
    EXPECT_EQ(analysisMaxOutputTokens(42), 8192);
}

TEST(AiUtils, AnalysisJsonPromptEmbedsConstraints)
{
    std::string prompt = analysisJsonPrompt(77, 1);
    EXPECT_NE(prompt.find(u8"详细程度：标准"), std::string::npos);
    EXPECT_NE(prompt.find(u8"至少77秒"), std::string::npos);
    EXPECT_NE(prompt.find(u8"最多12个章节"), std::string::npos);
    EXPECT_NE(prompt.find("300-500"), std::string::npos);
    EXPECT_NE(prompt.find(u8"请只返回 JSON"), std::string::npos);
    EXPECT_NE(prompt.find("\"transcript\""), std::string::npos);
}

TEST(AiUtils, AnalysisJsonPromptDetailLevels)
{
    EXPECT_NE(analysisJsonPrompt(30, 0).find(u8"简略"), std::string::npos);
    EXPECT_NE(analysisJsonPrompt(30, 2).find(u8"详细"), std::string::npos);
    EXPECT_NE(analysisJsonPrompt(30, 2).find("600-900"), std::string::npos);
}

// ---------- extractJsonObjectText ----------

TEST(AiUtils, ExtractJsonObjectTextPlain)
{
    EXPECT_EQ(extractJsonObjectText("{\"a\":1}"), "{\"a\":1}");
    EXPECT_EQ(extractJsonObjectText("   {\"a\":1}   "), "{\"a\":1}");
    EXPECT_EQ(extractJsonObjectText(""), "");
    EXPECT_EQ(extractJsonObjectText("   "), "");
}

TEST(AiUtils, ExtractJsonObjectTextCodeFence)
{
    EXPECT_EQ(extractJsonObjectText("```json\n{\"a\":1}\n```"), "{\"a\":1}");
    EXPECT_EQ(extractJsonObjectText("```\n{\"a\":1}\n```"), "{\"a\":1}");
    // 围栏内花括号外的内容也被剔除
    EXPECT_EQ(extractJsonObjectText("```json\nintro {\"a\":1} outro\n```"), "{\"a\":1}");
}

TEST(AiUtils, ExtractJsonObjectTextSurroundingText)
{
    EXPECT_EQ(extractJsonObjectText(u8"结果如下 {\"a\":1} 请查收"), "{\"a\":1}");
    // 多段 JSON 时取最外一对花括号（结果不一定是合法 JSON）
    EXPECT_EQ(extractJsonObjectText("pre {\"a\":1} mid {\"b\":2} post"),
              "{\"a\":1} mid {\"b\":2}");
}

TEST(AiUtils, ExtractJsonObjectTextNoBraces)
{
    // 没有花括号时原样返回（已 trim）
    EXPECT_EQ(extractJsonObjectText("no json here"), "no json here");
    EXPECT_EQ(extractJsonObjectText(u8"只有右括号 }"), u8"只有右括号 }");
}

// ---------- openAIContentText / geminiContentText ----------

TEST(AiUtils, OpenAIContentTextString)
{
    auto j = nlohmann::json::parse(R"({"choices":[{"message":{"content":"hello world"}}]})");
    EXPECT_EQ(openAIContentText(j), "hello world");
}

TEST(AiUtils, OpenAIContentTextPartsArray)
{
    auto j = nlohmann::json::parse(R"({
        "choices":[{"message":{"content":[
            {"type":"text","text":"第一部分"},
            {"type":"image_url","image_url":{"url":"x"}},
            {"text":"第二部分"}
        ]}}]
    })");
    // 数组 content 拼接所有带 text 的片段
    EXPECT_EQ(openAIContentText(j), std::string(u8"第一部分第二部分"));
}

TEST(AiUtils, OpenAIContentTextMalformed)
{
    EXPECT_TRUE(openAIContentText(nlohmann::json::object()).empty());
    EXPECT_TRUE(openAIContentText(nlohmann::json::parse("{\"choices\":[]}")).empty());
    // content 不是字符串/数组时返回空
    EXPECT_TRUE(openAIContentText(
        nlohmann::json::parse("{\"choices\":[{\"message\":{\"content\":123}}]}")).empty());
    EXPECT_TRUE(openAIContentText(
        nlohmann::json::parse("{\"choices\":[{\"message\":{\"content\":null}}]}")).empty());
}

TEST(AiUtils, GeminiContentTextParts)
{
    auto j = nlohmann::json::parse(
        R"({"candidates":[{"content":{"parts":[{"text":"你好"},{"type":"x"},{"text":"世界"}]}}]})");
    EXPECT_EQ(geminiContentText(j), std::string(u8"你好世界"));
}

TEST(AiUtils, GeminiContentTextMalformed)
{
    EXPECT_TRUE(geminiContentText(nlohmann::json::object()).empty());
    EXPECT_TRUE(geminiContentText(nlohmann::json::parse("{\"candidates\":[]}")).empty());
    EXPECT_TRUE(geminiContentText(nlohmann::json::parse(
        "{\"candidates\":[{\"content\":{\"parts\":{}}}]}")).empty());
}

// ---------- conciseHttpError ----------

TEST(AiUtils, ConciseHttpErrorStatusMessages)
{
    HttpResponse resp;
    resp.statusCode = 429;
    std::string msg = conciseHttpError("gemini", u8"分析失败", resp);
    EXPECT_NE(msg.find(u8"配额已用尽"), std::string::npos);
    EXPECT_NE(msg.find("Gemini"), std::string::npos);

    resp.statusCode = 401;
    EXPECT_NE(conciseHttpError("mimo", u8"分析失败", resp).find(u8"鉴权失败"),
              std::string::npos);
    resp.statusCode = 403;
    EXPECT_NE(conciseHttpError("mimo", u8"分析失败", resp).find(u8"鉴权失败"),
              std::string::npos);
}

TEST(AiUtils, ConciseHttpErrorExtractsApiMessage)
{
    HttpResponse resp;
    resp.statusCode = 500;
    resp.body = R"({"error":{"message":"quota exceeded"}})";
    std::string msg = conciseHttpError("mimo", u8"分析失败", resp);
    EXPECT_NE(msg.find("HTTP 500"), std::string::npos);
    EXPECT_NE(msg.find("quota exceeded"), std::string::npos);

    // 非 JSON 正文时截取原文
    resp.body = "internal error";
    EXPECT_NE(conciseHttpError("mimo", u8"分析失败", resp).find("internal error"),
              std::string::npos);

    // apiMessage 超长时截断并追加省略号
    resp.body = "{\"error\":{\"message\":\"" + std::string(200, 'x') + "\"}}";
    std::string truncated = conciseHttpError("mimo", u8"分析失败", resp);
    EXPECT_TRUE(endsWith(truncated, "..."));
}

// ---------- parseTimeStringMs / parseTimeValueMs ----------

TEST(AiUtils, ParseTimeStringMs)
{
    EXPECT_EQ(parseTimeStringMs("1:30"), 90000);
    EXPECT_EQ(parseTimeStringMs("01:02:03"), 3723000);
    EXPECT_EQ(parseTimeStringMs("45"), 45000);    // <1000 按秒解释
    EXPECT_EQ(parseTimeStringMs("1500"), 1500);   // >=1000 直接当毫秒
    EXPECT_EQ(parseTimeStringMs("1:30.5"), 90500);
    EXPECT_EQ(parseTimeStringMs("0.25"), 250);
    // 逗号小数点在含冒号时归一为点
    EXPECT_EQ(parseTimeStringMs("0:10,250"), 10250);
}

TEST(AiUtils, ParseTimeStringMsInvalid)
{
    EXPECT_EQ(parseTimeStringMs(""), -1);
    EXPECT_EQ(parseTimeStringMs("abc"), -1);
    EXPECT_EQ(parseTimeStringMs("1:2:3:4"), -1);
    // 负数不被拦截，原样返回负毫秒数
    EXPECT_EQ(parseTimeStringMs("-5"), -5);
}

TEST(AiUtils, ParseTimeValueMs)
{
    EXPECT_EQ(parseTimeValueMs(nlohmann::json(5.0)), 5000);
    EXPECT_EQ(parseTimeValueMs(nlohmann::json(1500)), 1500);
    EXPECT_EQ(parseTimeValueMs(nlohmann::json("1:30")), 90000);
    EXPECT_EQ(parseTimeValueMs(nlohmann::json(true)), -1);
    EXPECT_EQ(parseTimeValueMs(nlohmann::json::object()), -1);
}

// ---------- chapterStartTimeMs / chapterTitle / parseChapters ----------

TEST(AiUtils, ChapterStartTimeMsKeys)
{
    EXPECT_EQ(chapterStartTimeMs(nlohmann::json::parse("{\"startTime\":5000}")), 5000);
    EXPECT_EQ(chapterStartTimeMs(nlohmann::json::parse("{\"start_time\":\"1:00\"}")), 60000);
    EXPECT_EQ(chapterStartTimeMs(nlohmann::json::parse("{\"timestamp\":2000}")), 2000);
    EXPECT_EQ(chapterStartTimeMs(nlohmann::json::parse("{\"begin\":\"0:30\"}")), 30000);
    EXPECT_EQ(chapterStartTimeMs(nlohmann::json::parse("{\"title\":\"x\"}")), -1);
}

TEST(AiUtils, ChapterTitleKeysAndFallback)
{
    EXPECT_EQ(chapterTitle(nlohmann::json::parse("{\"title\":\"开场\"}"), 0),
              std::string(u8"开场"));
    EXPECT_EQ(chapterTitle(nlohmann::json::parse("{\"name\":\"N\"}"), 0), "N");
    // 空白标题与不存在的键都回落「章节 N」
    EXPECT_EQ(chapterTitle(nlohmann::json::parse("{\"title\":\"  \"}"), 2),
              std::string(u8"章节 3"));
    EXPECT_EQ(chapterTitle(nlohmann::json::parse("{}"), 0), std::string(u8"章节 1"));
}

TEST(AiUtils, ParseChaptersSortsAndChainsEndTimes)
{
    auto j = nlohmann::json::parse(R"({
        "chapters": [
            {"title":"后半","startTime":90000},
            {"title":"开场","startTime":5000}
        ]
    })");
    auto chapters = parseChapters(j, 200000);
    ASSERT_EQ(chapters.size(), 2u);
    EXPECT_EQ(chapters[0].title, std::string(u8"开场"));
    EXPECT_EQ(chapters[0].startTime, 5000);
    EXPECT_EQ(chapters[0].endTime, 90000);    // endTime 链到下一章 startTime
    EXPECT_EQ(chapters[1].endTime, 200000);   // 末章用回退时长
}

TEST(AiUtils, ParseChaptersSkipsInvalidAndDedups)
{
    auto j = nlohmann::json::parse(R"({
        "chapters": [
            {"title":"无时间"},
            {"title":"A","startTime":1000},
            {"title":"A2","startTime":1000},
            {"title":"B","startTime":2000}
        ]
    })");
    auto chapters = parseChapters(j, 0);
    ASSERT_EQ(chapters.size(), 2u);           // 无 startTime 跳过，重复 startTime 去重
    EXPECT_EQ(chapters[0].startTime, 1000);
    EXPECT_EQ(chapters[1].startTime, 2000);
    EXPECT_EQ(chapters[1].endTime, 12000);    // 无回退时长时末章补 +10s
}

TEST(AiUtils, ParseChaptersEmptyOrMissing)
{
    EXPECT_TRUE(parseChapters(nlohmann::json::object(), 1000).empty());
    EXPECT_TRUE(parseChapters(nlohmann::json::parse("{\"chapters\":{}}"), 1000).empty());
}

// ---------- segmentText / parseTranscriptSegments ----------

TEST(AiUtils, SegmentTextKeys)
{
    EXPECT_EQ(segmentText(nlohmann::json::parse("{\"text\":\"T\"}")), "T");
    EXPECT_EQ(segmentText(nlohmann::json::parse("{\"content\":\"C\"}")), "C");
    EXPECT_EQ(segmentText(nlohmann::json::parse("{\"description\":\"D\"}")), "D");
    // 空白文本与不存在的键返回空
    EXPECT_TRUE(segmentText(nlohmann::json::parse("{\"text\":\" \"}")).empty());
    EXPECT_TRUE(segmentText(nlohmann::json::parse("{\"other\":1}")).empty());
}

TEST(AiUtils, ParseTranscriptSegmentsBasic)
{
    auto j = nlohmann::json::parse(R"({
        "transcript": [
            {"startTime":1000,"endTime":5000,"text":"第一段"},
            {"startTime":6000,"text":"第二段"}
        ]
    })");
    auto segs = parseTranscriptSegments(j, 50000);
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_EQ(segs[0].startTime, 1000);
    EXPECT_EQ(segs[0].endTime, 5000);
    EXPECT_EQ(segs[0].text, std::string(u8"第一段"));
    EXPECT_FLOAT_EQ(segs[0].confidence, 0.9f);
    // 缺失 endTime 先补 +10s，末段又被回退时长覆盖
    EXPECT_EQ(segs[1].endTime, 50000);
}

TEST(AiUtils, ParseTranscriptSegmentsAlternateKeys)
{
    auto j = nlohmann::json::parse(R"({
        "observations": [
            {"start":"0:05","end":"0:10","content":"内容文本"}
        ]
    })");
    auto segs = parseTranscriptSegments(j, 0);
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].text, std::string(u8"内容文本"));
    EXPECT_EQ(segs[0].startTime, 5000);
    EXPECT_EQ(segs[0].endTime, 10000);
}

TEST(AiUtils, ParseTranscriptSegmentsDefaultsAndSkips)
{
    auto j = nlohmann::json::parse(R"({
        "transcript": [
            {"startTime":1000,"endTime":4000,"text":"有"},
            {"text":"缺时间"},
            {"startTime":9000,"endTime":12000}
        ]
    })");
    auto segs = parseTranscriptSegments(j, 20000);
    // 第三条无 text 被跳过
    ASSERT_EQ(segs.size(), 2u);
    // 缺 startTime 继承前一条 endTime
    EXPECT_EQ(segs[1].startTime, 4000);
    EXPECT_EQ(segs[1].text, std::string(u8"缺时间"));
}

TEST(AiUtils, ParseTranscriptSegmentsClampsOverlap)
{
    auto j = nlohmann::json::parse(R"({
        "transcript": [
            {"startTime":0,"endTime":8000,"text":"A"},
            {"startTime":5000,"endTime":9000,"text":"B"}
        ]
    })");
    auto segs = parseTranscriptSegments(j, 0);
    ASSERT_EQ(segs.size(), 2u);
    // 重叠的前段 endTime 被压到下一段 startTime
    EXPECT_EQ(segs[0].endTime, 5000);
}

TEST(AiUtils, HasUsableAnalysis)
{
    AIAnalysisResult result;
    EXPECT_FALSE(hasUsableAnalysis(result));
    result.chapters.push_back(ChapterInfo{});
    EXPECT_TRUE(hasUsableAnalysis(result));
}
