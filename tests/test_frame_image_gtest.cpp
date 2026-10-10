// 帧图像变换（裁剪/翻转/旋转）与 PNG 保存的单元测试
//
// 变换语义依据 SDL_RenderTextureRotated 的实现
// （3rdparty/SDL3/src/render/SDL_render.c，几何路径约 line 4369-4403）：
// 翻转先交换未旋转目标矩形的角点（minx/maxx、miny/maxy），
// 之后整个矩形绕中心做 (c -s / s c) 旋转 —— y 向下的坐标系中
// 正角度为顺时针。因此等价于「先翻转、再顺时针旋转」。
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <vector>

#include "utils/frame_image.h"

#define STB_IMAGE_IMPLEMENTATION
#include "utils/stb_image.h"

using namespace VideoPlay;

namespace {

// 生成每像素颜色唯一的测试帧（BGRA，B=i, G=64+i, R=128+i, A=255）
VideoFrame makeFrame(int width, int height)
{
    VideoFrame f;
    f.width = width;
    f.height = height;
    f.data.resize(static_cast<size_t>(width) * height * 4);
    for (int i = 0; i < width * height; ++i) {
        f.data[i * 4] = static_cast<uint8_t>(i);
        f.data[i * 4 + 1] = static_cast<uint8_t>(64 + i);
        f.data[i * 4 + 2] = static_cast<uint8_t>(128 + i);
        f.data[i * 4 + 3] = 255;
    }
    return f;
}

// 读输出帧 (x,y) 处像素对应的源像素序号（B 通道即序号）
int pixelIndex(const VideoFrame& f, int x, int y)
{
    return f.data[(static_cast<size_t>(y) * f.width + x) * 4];
}

std::vector<int> indices(const VideoFrame& f)
{
    std::vector<int> out;
    out.reserve(static_cast<size_t>(f.width) * f.height);
    for (int i = 0; i < f.width * f.height; ++i) {
        out.push_back(f.data[i * 4]);
    }
    return out;
}

} // namespace

TEST(FrameImage, Rotation0IsIdentity)
{
    // 源: 0 1 2 / 3 4 5
    VideoFrame out = transformFrame(makeFrame(3, 2), 0, false, false, 0);
    ASSERT_EQ(out.width, 3);
    ASSERT_EQ(out.height, 2);
    EXPECT_EQ(indices(out), (std::vector<int>{0, 1, 2, 3, 4, 5}));
}

TEST(FrameImage, Rotation90Clockwise)
{
    // 0 1 2        3 0
    // 3 4 5  --->  4 1
    //              5 2
    VideoFrame out = transformFrame(makeFrame(3, 2), 90, false, false, 0);
    ASSERT_EQ(out.width, 2);
    ASSERT_EQ(out.height, 3);
    EXPECT_EQ(indices(out), (std::vector<int>{3, 0, 4, 1, 5, 2}));
}

TEST(FrameImage, Rotation180)
{
    VideoFrame out = transformFrame(makeFrame(3, 2), 180, false, false, 0);
    ASSERT_EQ(out.width, 3);
    ASSERT_EQ(out.height, 2);
    EXPECT_EQ(indices(out), (std::vector<int>{5, 4, 3, 2, 1, 0}));
}

TEST(FrameImage, Rotation270Clockwise)
{
    // 0 1 2        2 5
    // 3 4 5  --->  1 4
    //              0 3
    VideoFrame out = transformFrame(makeFrame(3, 2), 270, false, false, 0);
    ASSERT_EQ(out.width, 2);
    ASSERT_EQ(out.height, 3);
    EXPECT_EQ(indices(out), (std::vector<int>{2, 5, 1, 4, 0, 3}));
}

TEST(FrameImage, FlipHorizontal)
{
    VideoFrame out = transformFrame(makeFrame(3, 2), 0, true, false, 0);
    EXPECT_EQ(indices(out), (std::vector<int>{2, 1, 0, 5, 4, 3}));
}

TEST(FrameImage, FlipVertical)
{
    VideoFrame out = transformFrame(makeFrame(3, 2), 0, false, true, 0);
    EXPECT_EQ(indices(out), (std::vector<int>{3, 4, 5, 0, 1, 2}));
}

TEST(FrameImage, FlipBeforeRotation)
{
    // SDL 语义：先在未旋转空间水平翻转 [2 1 0 / 5 4 3]，再顺时针转 90：
    //   5 2
    //   4 1
    //   3 0
    // 若顺序相反（先旋转再翻转）会得到 [0 3 / 1 4 / 2 5]
    VideoFrame out = transformFrame(makeFrame(3, 2), 90, true, false, 0);
    ASSERT_EQ(out.width, 2);
    ASSERT_EQ(out.height, 3);
    EXPECT_EQ(indices(out), (std::vector<int>{5, 2, 4, 1, 3, 0}));
}

TEST(FrameImage, FlipBothWithRotation270)
{
    // 先水平+垂直翻转 [5 4 3 / 2 1 0]，再顺时针转 270:
    // 先翻转后帧: 5 4 3 / 2 1 0
    // 270: out(x,y) = in(cw-1-y, x)
    //   out(0,0)=in(2,0)=3  out(1,0)=in(2,1)=0
    //   out(0,1)=in(1,0)=4  out(1,1)=in(1,1)=1
    //   out(0,2)=in(0,0)=5  out(1,2)=in(0,1)=2
    VideoFrame out = transformFrame(makeFrame(3, 2), 270, true, true, 0);
    ASSERT_EQ(out.width, 2);
    ASSERT_EQ(out.height, 3);
    EXPECT_EQ(indices(out), (std::vector<int>{3, 0, 4, 1, 5, 2}));
}

TEST(FrameImage, CropCenterPercent)
{
    // 10x4 帧 cropPercent=25：四边各裁 25%，剩 x∈[2,6] y∈[1,2] 的 5x2 区域
    VideoFrame src = makeFrame(10, 4);
    VideoFrame out = transformFrame(src, 0, false, false, 25);
    ASSERT_EQ(out.width, 5);
    ASSERT_EQ(out.height, 2);
    EXPECT_EQ(indices(out),
              (std::vector<int>{12, 13, 14, 15, 16, 22, 23, 24, 25, 26}));
}

TEST(FrameImage, CropClampedTo40)
{
    // cropPercent 超过 40 时按 40 处理；
    // float 精度下 10*(1-2*0.4f)=1.9999 -> 1，剩 x=4,y=1 的 1x1 区域
    VideoFrame out = transformFrame(makeFrame(10, 4), 0, false, false, 80);
    ASSERT_EQ(out.width, 1);
    ASSERT_EQ(out.height, 1);
    EXPECT_EQ(indices(out), (std::vector<int>{14}));
}

TEST(FrameImage, CropCombinedWithRotation)
{
    // 10x4 crop 25 -> 5x2 (源像素 x∈[2,6] y∈[1,2])，再顺时针 90 -> 2x5
    // 裁剪后帧: 12 13 14 15 16 / 22 23 24 25 26
    // cw90: 22 12 / 23 13 / 24 14 / 25 15 / 26 16
    VideoFrame out = transformFrame(makeFrame(10, 4), 90, false, false, 25);
    ASSERT_EQ(out.width, 2);
    ASSERT_EQ(out.height, 5);
    EXPECT_EQ(indices(out),
              (std::vector<int>{22, 12, 23, 13, 24, 14, 25, 15, 26, 16}));
}

TEST(FrameImage, EmptyInputReturnsEmpty)
{
    VideoFrame empty;
    VideoFrame out = transformFrame(empty, 0, false, false, 0);
    EXPECT_TRUE(out.data.empty());
    EXPECT_EQ(out.width, 0);
}

TEST(FrameImage, SavePngWritesCorrectRgba)
{
    VideoFrame src = makeFrame(3, 2);
    const auto dir = std::filesystem::temp_directory_path();
    const std::string path = (dir / "videoplay_frame_image_test.png").u8string();

    ASSERT_TRUE(saveFramePng(src, path));

    int w = 0, h = 0, channels = 0;
    uint8_t* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    ASSERT_NE(pixels, nullptr);
    EXPECT_EQ(w, 3);
    EXPECT_EQ(h, 2);

    // 源 BGRA (B=i, G=64+i, R=128+i) -> PNG RGBA (R=128+i, G=64+i, B=i, A=255)
    for (int i = 0; i < w * h; ++i) {
        EXPECT_EQ(pixels[i * 4], static_cast<uint8_t>(128 + i)) << "R at " << i;
        EXPECT_EQ(pixels[i * 4 + 1], static_cast<uint8_t>(64 + i)) << "G at " << i;
        EXPECT_EQ(pixels[i * 4 + 2], static_cast<uint8_t>(i)) << "B at " << i;
        EXPECT_EQ(pixels[i * 4 + 3], 255) << "A at " << i;
    }
    stbi_image_free(pixels);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(FrameImage, SavePngRejectsEmptyFrame)
{
    VideoFrame empty;
    EXPECT_FALSE(saveFramePng(empty, "should_not_exist.png"));
}
