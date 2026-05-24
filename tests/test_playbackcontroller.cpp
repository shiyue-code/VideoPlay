#include "core/common.h"

#include <cmath>
#include <iostream>
#include <string>

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

void testFormatTime() {
    std::cout << "[TEST] Time formatting" << std::endl;

    CHECK(formatTime(0) == "0:00", "Zero time");
    CHECK(formatTime(65'000) == "1:05", "Minute and seconds");
    CHECK(formatTime(3'661'000) == "1:01:01", "Hour, minutes, seconds");
}

void testPlaybackSpeedMapping() {
    std::cout << "[TEST] Playback speed mapping" << std::endl;

    CHECK(playbackSpeedToDouble(PlaybackSpeed::Speed_0_25) == 0.25, "0.25x enum");
    CHECK(playbackSpeedToDouble(PlaybackSpeed::Speed_1_0) == 1.0, "1.0x enum");
    CHECK(playbackSpeedToDouble(PlaybackSpeed::Speed_4_0) == 4.0, "4.0x enum");

    CHECK(doubleToPlaybackSpeed(0.20) == PlaybackSpeed::Speed_0_25, "Low rate clamps to 0.25x bucket");
    CHECK(doubleToPlaybackSpeed(1.00) == PlaybackSpeed::Speed_1_0, "1.0 maps to normal speed");
    CHECK(doubleToPlaybackSpeed(1.60) == PlaybackSpeed::Speed_1_5, "1.6 maps to 1.5x bucket");
    CHECK(doubleToPlaybackSpeed(3.50) == PlaybackSpeed::Speed_4_0, "High rate maps to 4.0x bucket");
}

void testDefaultFrameState() {
    std::cout << "[TEST] Default video frame" << std::endl;

    VideoFrame frame;
    CHECK(frame.data.empty(), "Frame data is empty");
    CHECK(frame.width == 0, "Frame width defaults to 0");
    CHECK(frame.height == 0, "Frame height defaults to 0");
    CHECK(frame.pts == 0, "Frame pts defaults to 0");
}

} // namespace

int main() {
    std::cout << "=== VideoPlay PlaybackController Tests ===" << std::endl;

    testFormatTime();
    testPlaybackSpeedMapping();
    testDefaultFrameState();

    std::cout << "\n=== Results: " << passCount << "/" << testCount << " passed ===" << std::endl;
    return (passCount == testCount) ? 0 : 1;
}
