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
