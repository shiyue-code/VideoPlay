// string_utils 工具函数单元测试
#include <gtest/gtest.h>

#include "utils/string_utils.h"

using namespace VideoPlay;

TEST(StringUtils, ParseHexColorWithHash)
{
    uint8_t r = 0, g = 0, b = 0;
    EXPECT_TRUE(parseHexColor("#FF8040", r, g, b));
    EXPECT_EQ(r, 0xFF);
    EXPECT_EQ(g, 0x80);
    EXPECT_EQ(b, 0x40);
}

TEST(StringUtils, ParseHexColorWithoutHash)
{
    uint8_t r = 0, g = 0, b = 0;
    EXPECT_TRUE(parseHexColor("00a0ff", r, g, b));
    EXPECT_EQ(r, 0x00);
    EXPECT_EQ(g, 0xA0);
    EXPECT_EQ(b, 0xFF);
}

TEST(StringUtils, ParseHexColorCaseInsensitive)
{
    uint8_t r1 = 0, g1 = 0, b1 = 0, r2 = 0, g2 = 0, b2 = 0;
    EXPECT_TRUE(parseHexColor("#AbC123", r1, g1, b1));
    EXPECT_TRUE(parseHexColor("abc123", r2, g2, b2));
    EXPECT_EQ(r1, r2);
    EXPECT_EQ(g1, g2);
    EXPECT_EQ(b1, b2);
}

TEST(StringUtils, ParseHexColorRejectsInvalid)
{
    uint8_t r = 0, g = 0, b = 0;
    EXPECT_FALSE(parseHexColor("", r, g, b));
    EXPECT_FALSE(parseHexColor("#FFF", r, g, b));        // 太短
    EXPECT_FALSE(parseHexColor("FFFFFFF", r, g, b));   // 太长
    EXPECT_FALSE(parseHexColor("#GGGGGG", r, g, b));   // 非十六进制
    EXPECT_FALSE(parseHexColor("#12 456", r, g, b));   // 含空格
    EXPECT_FALSE(parseHexColor("0xFF8040", r, g, b));  // 0x 前缀不支持
}
