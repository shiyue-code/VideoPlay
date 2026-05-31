// 字幕解析器单元测试
#include "subtitles/subtitleparser.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>

using namespace VideoPlay;

namespace {

int testCount = 0;
int passCount = 0;

void CHECK(bool condition, const char* name) {
    testCount++;
    if (condition) {
        passCount++;
        std::cout << "  PASS: " << name << std::endl;
    } else {
        std::cout << "  FAIL: " << name << std::endl;
    }
}

std::string createTempFile(const std::string& name, const std::string& content) {
    std::string path = std::filesystem::temp_directory_path().string() + "/videoplay_test_" + name;
    std::ofstream f(path);
    f << content;
    f.close();
    return path;
}

void testSrtParsing() {
    std::cout << "[TEST] SRT parsing" << std::endl;
    std::string content =
        "1\n"
        "00:00:01,000 --> 00:00:03,000\n"
        "Hello World\n"
        "\n"
        "2\n"
        "00:00:04,000 --> 00:00:06,000\n"
        "Second subtitle\n";

    std::string path = createTempFile("test.srt", content);
    SubtitleParser parser;
    CHECK(parser.loadFile(path), "Load SRT file");
    CHECK(parser.isLoaded(), "Parser reports loaded");

    CHECK(parser.subtitleAt(0) == "", "No subtitle at 0ms");
    CHECK(parser.subtitleAt(1500) == "Hello World", "Subtitle at 1500ms");
    CHECK(parser.subtitleAt(5000) == "Second subtitle", "Subtitle at 5000ms");
    CHECK(parser.subtitleAt(7000) == "", "No subtitle at 7000ms");

    std::filesystem::remove(path);
}

void testVttParsing() {
    std::cout << "[TEST] VTT parsing" << std::endl;
    std::string content =
        "WEBVTT\n"
        "\n"
        "00:00:01.000 --> 00:00:03.000\n"
        "VTT subtitle\n"
        "\n"
        "00:00:05.000 --> 00:00:07.000\n"
        "Another line\n";

    std::string path = createTempFile("test.vtt", content);
    SubtitleParser parser;
    CHECK(parser.loadFile(path), "Load VTT file");
    CHECK(parser.subtitleAt(2000) == "VTT subtitle", "VTT subtitle at 2000ms");
    CHECK(parser.subtitleAt(6000) == "Another line", "VTT subtitle at 6000ms");

    std::filesystem::remove(path);
}

void testOffset() {
    std::cout << "[TEST] Subtitle offset" << std::endl;
    std::string content =
        "1\n"
        "00:00:02,000 --> 00:00:04,000\n"
        "Offset test\n";

    std::string path = createTempFile("test_offset.srt", content);
    SubtitleParser parser;
    parser.loadFile(path);

    CHECK(parser.subtitleAt(2500) == "Offset test", "Before offset");
    parser.adjustOffset(1000);
    CHECK(parser.offset() == 1000, "Offset is +1000ms");
    CHECK(parser.subtitleAt(1500) == "", "After +1000ms offset, 1500ms should be empty");
    CHECK(parser.subtitleAt(3500) == "Offset test", "After +1000ms offset");

    parser.setOffset(-500);
    CHECK(parser.subtitleAt(3000) == "Offset test", "After -500ms offset");

    std::filesystem::remove(path);
}

void testClear() {
    std::cout << "[TEST] Clear" << std::endl;
    std::string content =
        "1\n"
        "00:00:01,000 --> 00:00:03,000\n"
        "Test\n";

    std::string path = createTempFile("test_clear.srt", content);
    SubtitleParser parser;
    parser.loadFile(path);
    CHECK(parser.isLoaded(), "Loaded before clear");
    parser.clear();
    CHECK(!parser.isLoaded(), "Not loaded after clear");
    CHECK(parser.subtitleAt(2000) == "", "No subtitle after clear");

    std::filesystem::remove(path);
}

void testInvalidFile() {
    std::cout << "[TEST] Invalid file" << std::endl;
    SubtitleParser parser;
    CHECK(!parser.loadFile("/nonexistent/path.srt"), "Load nonexistent file fails");
    CHECK(!parser.isLoaded(), "Not loaded after failure");
}

} // anonymous namespace

int main() {
    std::cout << "=== VideoPlay SubtitleParser Tests ===" << std::endl;

    testSrtParsing();
    testVttParsing();
    testOffset();
    testClear();
    testInvalidFile();

    std::cout << "\n=== Results: " << passCount << "/" << testCount << " passed ===" << std::endl;
    return (passCount == testCount) ? 0 : 1;
}
