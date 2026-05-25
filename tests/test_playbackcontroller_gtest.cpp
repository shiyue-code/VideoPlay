#include "core/common.h"

#include <gtest/gtest.h>

using namespace VideoPlay;

TEST(PlaybackController, FormatTime)
{
    EXPECT_EQ(formatTime(0), "0:00");
    EXPECT_EQ(formatTime(65'000), "1:05");
    EXPECT_EQ(formatTime(3'661'000), "1:01:01");
}

TEST(PlaybackController, PlaybackSpeedMapping)
{
    EXPECT_DOUBLE_EQ(playbackSpeedToDouble(PlaybackSpeed::Speed_0_25), 0.25);
    EXPECT_DOUBLE_EQ(playbackSpeedToDouble(PlaybackSpeed::Speed_1_0), 1.0);
    EXPECT_DOUBLE_EQ(playbackSpeedToDouble(PlaybackSpeed::Speed_4_0), 4.0);

    EXPECT_EQ(doubleToPlaybackSpeed(0.20), PlaybackSpeed::Speed_0_25);
    EXPECT_EQ(doubleToPlaybackSpeed(1.00), PlaybackSpeed::Speed_1_0);
    EXPECT_EQ(doubleToPlaybackSpeed(1.60), PlaybackSpeed::Speed_1_5);
    EXPECT_EQ(doubleToPlaybackSpeed(3.50), PlaybackSpeed::Speed_4_0);
}

TEST(PlaybackController, DefaultFrameState)
{
    VideoFrame frame;
    EXPECT_TRUE(frame.data.empty());
    EXPECT_EQ(frame.width, 0);
    EXPECT_EQ(frame.height, 0);
    EXPECT_EQ(frame.pts, 0);
}

TEST(PlaybackController, AudioFilterPresets)
{
    auto off = audioFilterConfigForPreset(AudioFilterPreset::Off);
    EXPECT_FALSE(off.enabled);
    EXPECT_TRUE(off.eqBands.empty());

    auto voice = audioFilterConfigForPreset(AudioFilterPreset::Voice);
    EXPECT_TRUE(voice.enabled);
    EXPECT_FALSE(voice.eqBands.empty());
    EXPECT_TRUE(voice.limiterEnabled);

    auto night = audioFilterConfigForPreset(AudioFilterPreset::Night);
    EXPECT_TRUE(night.enabled);
    EXPECT_TRUE(night.dynamicNormalizerEnabled);
    EXPECT_LT(night.preampDb, 0.0);
}
