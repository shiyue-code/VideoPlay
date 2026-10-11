#include "core/videoadjuster.h"
#include "utils/logger.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

using namespace VideoPlay;

namespace {

constexpr int kFrameSize = 64;

struct FrameDeleter {
    void operator()(AVFrame* frame) const
    {
        if (frame)
        {
            av_frame_free(&frame);
        }
    }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

// 构造 64x64 常量 yuv420p 帧（有限范围 BT709）
FramePtr makeYuvFrame(int y, int u, int v)
{
    AVFrame* raw = av_frame_alloc();
    if (!raw)
    {
        return {};
    }
    raw->format = AV_PIX_FMT_YUV420P;
    raw->width = kFrameSize;
    raw->height = kFrameSize;
    raw->color_range = AVCOL_RANGE_MPEG;
    raw->colorspace = AVCOL_SPC_BT709;
    if (av_frame_get_buffer(raw, 32) < 0)
    {
        av_frame_free(&raw);
        return {};
    }
    for (int row = 0; row < raw->height; ++row)
    {
        std::memset(raw->data[0] + row * raw->linesize[0], y,
                    static_cast<size_t>(kFrameSize));
    }
    for (int row = 0; row < raw->height / 2; ++row)
    {
        std::memset(raw->data[1] + row * raw->linesize[1], u,
                    static_cast<size_t>(kFrameSize / 2));
        std::memset(raw->data[2] + row * raw->linesize[2], v,
                    static_cast<size_t>(kFrameSize / 2));
    }
    return FramePtr(raw);
}

// BGRA 三通道的整体均值
double meanBgr(const VideoFrame& frame)
{
    if (frame.data.empty())
    {
        return 0.0;
    }
    double sum = 0.0;
    size_t count = 0;
    for (size_t i = 0; i + 3 < frame.data.size(); i += 4)
    {
        sum += frame.data[i] + frame.data[i + 1] + frame.data[i + 2];
        count += 3;
    }
    return count ? sum / static_cast<double>(count) : 0.0;
}

} // namespace

class VideoAdjusterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Logger::root().setEnabled(false);
    }
};

TEST_F(VideoAdjusterTest, IdentityConfigProducesNeutralBgra)
{
    VideoAdjuster adjuster;
    FramePtr base = makeYuvFrame(126, 128, 128);
    ASSERT_TRUE(base);

    VideoFrame out = adjuster.convert(base.get(), VideoFilterConfig{});
    ASSERT_EQ(out.width, kFrameSize);
    ASSERT_EQ(out.height, kFrameSize);
    ASSERT_EQ(out.data.size(), static_cast<size_t>(kFrameSize) * kFrameSize * 4);

    // Y=126、有限范围 BT709 → RGB 应为接近 128 的中性灰
    EXPECT_NEAR(out.data[0], 128, 3);   // B
    EXPECT_NEAR(out.data[1], 128, 3);   // G
    EXPECT_NEAR(out.data[2], 128, 3);   // R
    EXPECT_EQ(out.data[3], 255);        // A
}

TEST_F(VideoAdjusterTest, BrightnessRaisesLuma)
{
    VideoAdjuster adjuster;
    FramePtr base = makeYuvFrame(126, 128, 128);
    ASSERT_TRUE(base);

    VideoFrame identity = adjuster.convert(base.get(), VideoFilterConfig{});

    VideoFilterConfig bright;
    bright.enabled = true;
    bright.brightness = 0.3f;
    VideoFrame boosted = adjuster.convert(base.get(), bright);
    ASSERT_FALSE(boosted.data.empty());

    const double identityMean = meanBgr(identity);
    const double boostedMean = meanBgr(boosted);
    std::cerr << "[measured] identity mean=" << identityMean
              << " brightness=0.3 mean=" << boostedMean << std::endl;
    EXPECT_GT(boostedMean, identityMean + 30.0)
        << "identity=" << identityMean << " boosted=" << boostedMean;
}

TEST_F(VideoAdjusterTest, ZeroSaturationYieldsGray)
{
    VideoAdjuster adjuster;
    FramePtr base = makeYuvFrame(126, 90, 200);
    ASSERT_TRUE(base);

    VideoFilterConfig desat;
    desat.enabled = true;
    desat.saturation = 0.0f;
    VideoFrame gray = adjuster.convert(base.get(), desat);
    ASSERT_FALSE(gray.data.empty());
    const int gB = gray.data[0];
    const int gG = gray.data[1];
    const int gR = gray.data[2];
    EXPECT_LE(std::abs(gR - gG), 3) << "R=" << gR << " G=" << gG;
    EXPECT_LE(std::abs(gG - gB), 3) << "G=" << gG << " B=" << gB;

    // 同一帧默认参数应保留明显色度
    VideoFrame chroma = adjuster.convert(base.get(), VideoFilterConfig{});
    ASSERT_FALSE(chroma.data.empty());
    const int cB = chroma.data[0];
    const int cR = chroma.data[2];
    std::cerr << "[measured] sat=0 R=" << gR << " G=" << gG << " B=" << gB
              << "; default R=" << cR << " B=" << cB << std::endl;
    EXPECT_GT(std::abs(cR - cB), 40) << "R=" << cR << " B=" << cB;
}

TEST_F(VideoAdjusterTest, SameInstanceRecoversIdentity)
{
    VideoAdjuster adjuster;
    FramePtr base = makeYuvFrame(126, 128, 128);
    ASSERT_TRUE(base);

    VideoFilterConfig bright;
    bright.enabled = true;
    bright.brightness = 0.3f;
    adjuster.convert(base.get(), bright);

    // 调节回默认后应与全新调节器的恒等输出逐字节一致
    VideoFrame restored = adjuster.convert(base.get(), VideoFilterConfig{});
    VideoAdjuster fresh;
    VideoFrame identity = fresh.convert(base.get(), VideoFilterConfig{});
    ASSERT_FALSE(restored.data.empty());
    ASSERT_FALSE(identity.data.empty());
    EXPECT_EQ(restored.data, identity.data);
}

TEST_F(VideoAdjusterTest, ConvertDoesNotModifyBase)
{
    VideoAdjuster adjuster;
    FramePtr base = makeYuvFrame(126, 90, 200);
    ASSERT_TRUE(base);

    VideoFilterConfig cfg;
    cfg.enabled = true;
    cfg.brightness = 0.3f;
    cfg.saturation = 0.5f;
    adjuster.convert(base.get(), cfg);
    adjuster.convert(base.get(), VideoFilterConfig{});

    int mismatches = 0;
    for (int row = 0; row < kFrameSize; ++row)
    {
        for (int col = 0; col < kFrameSize; ++col)
        {
            if (base->data[0][row * base->linesize[0] + col] != 126)
            {
                ++mismatches;
            }
        }
    }
    EXPECT_EQ(mismatches, 0);
}

TEST_F(VideoAdjusterTest, NullBaseReturnsEmpty)
{
    VideoAdjuster adjuster;
    VideoFrame out = adjuster.convert(nullptr, VideoFilterConfig{});
    EXPECT_TRUE(out.data.empty());
}
