#include "subtitles/subtitleparser.h"
#include "utils/logger.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace VideoPlay;

// 在测试中禁用日志，避免 Release 模式下 LoggerBackend 静态初始化崩溃
// 通过定义一个空实现的 logger 函数来替代原始实现
namespace VideoPlay {
namespace {
Logger& logger() {
    // 返回一个空的 Logger 实例，不创建文件
    static std::shared_ptr<Logger> nullLogger = []() {
        // 先禁用 root logger
        Logger::root().setEnabled(false);
        // 创建一个独立的 logger
        auto ptr = Logger::get("test");
        ptr->setEnabled(false);
        return ptr;
    }();
    return *nullLogger;
}
}
}

namespace {

std::string createTempFile(const std::string& name, const std::string& content)
{
    std::string path = std::filesystem::temp_directory_path().string() + "/videoplay_test_" + name;
    std::ofstream file(path);
    file << content;
    return path;
}

std::string createTempBinaryFile(const std::string& name, const std::string& content)
{
    std::string path = std::filesystem::temp_directory_path().string() + "/videoplay_test_" + name;
    std::ofstream file(path, std::ios::binary);
    file << content;
    return path;
}

} // namespace

TEST(SubtitleParser, ParsesSrt)
{
    std::string content =
        "1\n"
        "00:00:01,000 --> 00:00:03,000\n"
        "Hello World\n"
        "\n"
        "2\n"
        "00:00:04,000 --> 00:00:06,000\n"
        "Second subtitle\n";

    std::string path = createTempFile("gtest.srt", content);
    SubtitleParser parser;

    EXPECT_TRUE(parser.loadFile(path));
    EXPECT_TRUE(parser.isLoaded());
    EXPECT_EQ(parser.subtitleAt(0), "");
    EXPECT_EQ(parser.subtitleAt(1500), "Hello World");
    EXPECT_EQ(parser.subtitleAt(5000), "Second subtitle");
    EXPECT_EQ(parser.subtitleAt(7000), "");

    std::filesystem::remove(path);
}

TEST(SubtitleParser, ParsesVtt)
{
    std::string content =
        "WEBVTT\n"
        "\n"
        "00:00:01.000 --> 00:00:03.000\n"
        "VTT subtitle\n"
        "\n"
        "00:00:05.000 --> 00:00:07.000\n"
        "Another line\n";

    std::string path = createTempFile("gtest.vtt", content);
    SubtitleParser parser;

    EXPECT_TRUE(parser.loadFile(path));
    EXPECT_EQ(parser.subtitleAt(2000), "VTT subtitle");
    EXPECT_EQ(parser.subtitleAt(6000), "Another line");

    std::filesystem::remove(path);
}

TEST(SubtitleParser, AppliesOffset)
{
    std::string content =
        "1\n"
        "00:00:02,000 --> 00:00:04,000\n"
        "Offset test\n";

    std::string path = createTempFile("gtest_offset.srt", content);
    SubtitleParser parser;
    ASSERT_TRUE(parser.loadFile(path));

    EXPECT_EQ(parser.subtitleAt(2500), "Offset test");
    parser.adjustOffset(1000);
    EXPECT_EQ(parser.offset(), 1000);
    EXPECT_EQ(parser.subtitleAt(1500), "");
    EXPECT_EQ(parser.subtitleAt(3500), "Offset test");

    parser.setOffset(-500);
    EXPECT_EQ(parser.subtitleAt(3000), "Offset test");

    std::filesystem::remove(path);
}

TEST(SubtitleParser, ClearsState)
{
    std::string content =
        "1\n"
        "00:00:01,000 --> 00:00:03,000\n"
        "Test\n";

    std::string path = createTempFile("gtest_clear.srt", content);
    SubtitleParser parser;
    ASSERT_TRUE(parser.loadFile(path));

    parser.clear();
    EXPECT_FALSE(parser.isLoaded());
    EXPECT_EQ(parser.subtitleAt(2000), "");

    std::filesystem::remove(path);
}

TEST(SubtitleParser, RejectsInvalidFile)
{
    SubtitleParser parser;
    EXPECT_FALSE(parser.loadFile("/nonexistent/path.srt"));
    EXPECT_FALSE(parser.isLoaded());
}

TEST(SubtitleParser, RemoveFormattingStripsAssAndHtml)
{
    EXPECT_EQ(SubtitleParser::removeFormatting("{\\an8}{\\pos(100,200)}Hello"),
              "Hello");
    EXPECT_EQ(SubtitleParser::removeFormatting("<i>italic</i> plain"),
              "italic plain");
    EXPECT_EQ(SubtitleParser::removeFormatting("a&nbsp;b &amp; c"),
              "a b & c");
    EXPECT_EQ(SubtitleParser::removeFormatting("no tags"), "no tags");
}

TEST(SubtitleParser, ExtractAssDialogueText)
{
    // 标准 9 字段 Dialogue，文本里含逗号必须保留
    EXPECT_EQ(SubtitleParser::extractAssDialogueText(
                  "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,Hello, world"),
              "Hello, world");
    // 不带 Dialogue: 前缀的 MKV SSA 封包格式
    EXPECT_EQ(SubtitleParser::extractAssDialogueText(
                  "0,0:00:01.00,0:00:03.00,Default,,0,0,0,,line one"),
              "line one");
    // 普通文本原样返回
    EXPECT_EQ(SubtitleParser::extractAssDialogueText("plain text"), "plain text");
}

TEST(SubtitleParser, EmbeddedTextToPlain)
{
    EXPECT_EQ(SubtitleParser::embeddedTextToPlain(
                  "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,{\\i1}Hello\\NWorld{\\i0}"),
              "Hello\nWorld");
    EXPECT_EQ(SubtitleParser::embeddedTextToPlain("plain"), "plain");
    EXPECT_EQ(SubtitleParser::embeddedTextToPlain("a\\Nb"), "a\nb");
}

TEST(SubtitleParser, ParsesGbkEncodedSrt)
{
    // "你好" 的 GBK 字节是 C4 E3 BA C3，UTF-8 是 E4 BD A0 E5 A5 BD
    std::string content =
        "1\n"
        "00:00:01,000 --> 00:00:03,000\n"
        "\xC4\xE3\xBA\xC3\n";

    std::string path = createTempBinaryFile("gtest_gbk.srt", content);
    SubtitleParser parser;

    EXPECT_TRUE(parser.loadFile(path));
    EXPECT_EQ(parser.subtitleAt(2000), "\xE4\xBD\xA0\xE5\xA5\xBD");

    std::filesystem::remove(path);
}

TEST(SubtitleParser, ParsesUtf16LeSrt)
{
    std::string ascii =
        "1\n"
        "00:00:01,000 --> 00:00:03,000\n"
        "Utf16 text\n";
    std::string utf16le = "\xFF\xFE";
    for (char c : ascii) {
        utf16le.push_back(c);
        utf16le.push_back('\0');
    }

    std::string path = createTempBinaryFile("gtest_utf16.srt", utf16le);
    SubtitleParser parser;

    EXPECT_TRUE(parser.loadFile(path));
    EXPECT_EQ(parser.subtitleAt(2000), "Utf16 text");

    std::filesystem::remove(path);
}

TEST(SubtitleParser, ReturnsOverlappingEntries)
{
    std::string content =
        "1\n"
        "00:00:01,000 --> 00:00:04,000\n"
        "Speaker A\n"
        "\n"
        "2\n"
        "00:00:02,000 --> 00:00:05,000\n"
        "Speaker B\n";

    std::string path = createTempFile("gtest_overlap.srt", content);
    SubtitleParser parser;

    ASSERT_TRUE(parser.loadFile(path));
    EXPECT_EQ(parser.subtitleAt(1500), "Speaker A");
    EXPECT_EQ(parser.subtitleAt(2500), "Speaker A\nSpeaker B");
    EXPECT_EQ(parser.subtitleAt(4500), "Speaker B");

    std::filesystem::remove(path);
}

TEST(SubtitleParser, ParsesUnsortedAss)
{
    // ASS 事件顺序不保证按时间排序，loadFile 后应按开始时间重排
    std::string content =
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "\n"
        "[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour\n"
        "Style: Default,Arial,16,&H00FFFFFF\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:05.00,0:00:07.00,Default,,0,0,0,,Late line\n"
        "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,Early line\n";

    std::string path = createTempFile("gtest_unsorted.ass", content);
    SubtitleParser parser;

    ASSERT_TRUE(parser.loadFile(path));
    EXPECT_EQ(parser.subtitleAt(2000), "Early line");
    EXPECT_EQ(parser.subtitleAt(6000), "Late line");

    std::filesystem::remove(path);
}

TEST(SubtitleParser, ParsesAssTextKeepingCommasAndConvertingNewlines)
{
    // Text 字段里的逗号原样保留；{\...} 覆盖标签剥离；\N 与 \n 都换成换行
    std::string content =
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,{\\an8}Hello, world\\NSecond\\nThird\n";

    std::string path = createTempFile("gtest_ass_text.ass", content);
    SubtitleParser parser;

    ASSERT_TRUE(parser.loadFile(path));
    auto entries = parser.entries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].text, "Hello, world\nSecond\nThird");

    std::filesystem::remove(path);
}

TEST(SubtitleParser, ParsesAssTimecodes)
{
    // ASS 小数部分是百分之一秒；支持小时位
    std::string content =
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:01.50,0:00:02.25,Default,,0,0,0,,First\n"
        "Dialogue: 0,1:02:03.45,1:02:04.00,Default,,0,0,0,,Second\n";

    std::string path = createTempFile("gtest_ass_time.ass", content);
    SubtitleParser parser;

    ASSERT_TRUE(parser.loadFile(path));
    auto entries = parser.entries();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].startTime, 1500);
    EXPECT_EQ(entries[0].endTime, 2250);
    EXPECT_EQ(entries[1].startTime, 3723450);
    EXPECT_EQ(entries[1].endTime, 3724000);

    std::filesystem::remove(path);
}

TEST(SubtitleParser, ParsesAssCustomFormatOrder)
{
    // Format 行决定字段顺序，Text 之后的逗号仍属于文本
    std::string content =
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "\n"
        "[Events]\n"
        "Format: Start, End, Style, Text\n"
        "Dialogue: 0:00:01.00,0:00:02.00,Default,Reordered, ok\n";

    std::string path = createTempFile("gtest_ass_format.ass", content);
    SubtitleParser parser;

    ASSERT_TRUE(parser.loadFile(path));
    auto entries = parser.entries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].text, "Reordered, ok");
    EXPECT_EQ(entries[0].startTime, 1000);
    EXPECT_EQ(entries[0].endTime, 2000);

    std::filesystem::remove(path);
}

TEST(SubtitleParser, IgnoresAssCommentAndTagOnlyDialogue)
{
    // Comment 行不参与解析；剥完标签为空的 Dialogue 被丢弃
    std::string content =
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Comment: 0,0:00:09.00,0:00:10.00,Default,,0,0,0,,ignored comment\n"
        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an8}\n"
        "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Visible\n";

    std::string path = createTempFile("gtest_ass_comment.ass", content);
    SubtitleParser parser;

    ASSERT_TRUE(parser.loadFile(path));
    auto entries = parser.entries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].text, "Visible");

    std::filesystem::remove(path);
}

TEST(SubtitleParser, StopsAssParsingAtNextSection)
{
    // [Events] 之后的下一个 section 头会终止对白解析
    std::string content =
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Visible\n"
        "\n"
        "[Fonts]\n"
        "fontname: arial.ttf\n"
        "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Should be ignored\n";

    std::string path = createTempFile("gtest_ass_section.ass", content);
    SubtitleParser parser;

    ASSERT_TRUE(parser.loadFile(path));
    auto entries = parser.entries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].text, "Visible");

    std::filesystem::remove(path);
}

TEST(SubtitleParser, AssFormatWithoutDialogueFails)
{
    std::string content =
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

    std::string path = createTempFile("gtest_ass_empty.ass", content);
    SubtitleParser parser;

    EXPECT_FALSE(parser.loadFile(path));
    EXPECT_FALSE(parser.isLoaded());

    std::filesystem::remove(path);
}

TEST(SubtitleParser, ParsesSsaExtension)
{
    std::string content =
        "[Script Info]\n"
        "ScriptType: v4.00\n"
        "\n"
        "[Events]\n"
        "Format: Marked, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: Marked=0,0:00:01.00,0:00:03.00,Default,,0,0,0,,SSA line\n";

    std::string path = createTempFile("gtest_ext.ssa", content);
    SubtitleParser parser;

    ASSERT_TRUE(parser.loadFile(path));
    EXPECT_EQ(parser.subtitleAt(2000), "SSA line");

    std::filesystem::remove(path);
}

TEST(SubtitleParser, AutoDetectsAssInTxtFile)
{
    // 未知扩展名按内容自动识别：[Script Info] 触发 ASS 解析
    std::string content =
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,Auto detected\n";

    std::string path = createTempFile("gtest_autodetect.txt", content);
    SubtitleParser parser;

    ASSERT_TRUE(parser.loadFile(path));
    EXPECT_EQ(parser.subtitleAt(2000), "Auto detected");

    std::filesystem::remove(path);
}

TEST(SubtitleParser, ParsesUtf8BomAssWithChineseText)
{
    std::string content = "\xEF\xBB\xBF";
    content +=
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "\n"
        "[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
        "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,";
    content += u8"你好，世界";
    content += "\n";

    std::string path = createTempBinaryFile("gtest_ass_bom.ass", content);
    SubtitleParser parser;

    ASSERT_TRUE(parser.loadFile(path));
    EXPECT_EQ(parser.subtitleAt(2000), std::string(u8"你好，世界"));

    std::filesystem::remove(path);
}
