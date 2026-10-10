#include "core/audioclock.h"

#include <gtest/gtest.h>

using namespace VideoPlay;

TEST(AudioClock, EmptyReturnsMinusOne)
{
    AudioClock clock;
    EXPECT_TRUE(clock.empty());
    EXPECT_DOUBLE_EQ(clock.positionMs(0), -1.0);
    EXPECT_DOUBLE_EQ(clock.positionMs(100), -1.0);
}

TEST(AudioClock, SingleSegmentPosition)
{
    AudioClock clock;
    // 100 帧，起始 100ms，每帧 0.5ms 媒体时长
    clock.push(100, 100.0, 0.5);
    // 已消费 0 帧：位于段首
    EXPECT_DOUBLE_EQ(clock.positionMs(100), 100.0);
    // 已消费 50 帧：段中
    EXPECT_DOUBLE_EQ(clock.positionMs(50), 125.0);
    // 已消费 99 帧
    EXPECT_DOUBLE_EQ(clock.positionMs(1), 149.5);
    // 欠载：停在段尾
    EXPECT_DOUBLE_EQ(clock.positionMs(0), 150.0);
}

TEST(AudioClock, MultiSegmentSpeedChange)
{
    AudioClock clock;
    // 模拟 1x -> 2x：第一段每帧 1ms，第二段每帧 2ms
    clock.push(100, 0.0, 1.0);    // 覆盖 0-100ms
    clock.push(100, 100.0, 2.0);  // 覆盖 100-300ms

    // queued 超过总量：首段起点
    EXPECT_DOUBLE_EQ(clock.positionMs(500), 0.0);
    // 第一 段中
    EXPECT_DOUBLE_EQ(clock.positionMs(150), 50.0);
    // 第一段恰好播完：进入第二段起点
    EXPECT_DOUBLE_EQ(clock.positionMs(100), 100.0);
    // 第二段中
    EXPECT_DOUBLE_EQ(clock.positionMs(50), 200.0);
    // 欠载：停在最后一段末尾
    EXPECT_DOUBLE_EQ(clock.positionMs(0), 300.0);
}

TEST(AudioClock, PruneKeepsPosition)
{
    AudioClock clock;
    clock.push(100, 0.0, 1.0);
    clock.push(100, 100.0, 2.0);

    // queued=80：已消费 120 帧，第一段已完整播完可被裁剪
    const double before = clock.positionMs(80);
    EXPECT_DOUBLE_EQ(before, 100.0 + 20.0 * 2.0);

    clock.prune(80);
    // 裁剪后位置不变
    EXPECT_DOUBLE_EQ(clock.positionMs(80), before);

    // 未完整播完的段保留：queued 大于剩余总量时回到该段起点
    clock.prune(200);
    EXPECT_DOUBLE_EQ(clock.positionMs(200), 100.0);
    EXPECT_FALSE(clock.empty());
}

TEST(AudioClock, Reset)
{
    AudioClock clock;
    clock.push(100, 0.0, 1.0);
    clock.reset();
    EXPECT_TRUE(clock.empty());
    EXPECT_DOUBLE_EQ(clock.positionMs(0), -1.0);
}
