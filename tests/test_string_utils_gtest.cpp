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

TEST(StringUtils, SplitTimestampTokensPlainText)
{
    auto tokens = splitTimestampTokens("没有标记的一行");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].text, "没有标记的一行");
    EXPECT_FALSE(tokens[0].isTimestamp);
}

TEST(StringUtils, SplitTimestampTokensSingle)
{
    auto tokens = splitTimestampTokens("[01:23] 介绍开始");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_TRUE(tokens[0].isTimestamp);
    EXPECT_EQ(tokens[0].text, "[01:23]");
    EXPECT_EQ(tokens[0].ms, 83000);
    EXPECT_EQ(tokens[1].text, " 介绍开始");
    EXPECT_FALSE(tokens[1].isTimestamp);
}

TEST(StringUtils, SplitTimestampTokensMultiple)
{
    auto tokens = splitTimestampTokens("[00:05] 开场，中间 [1:02:03] 又一段");
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_TRUE(tokens[0].isTimestamp);
    EXPECT_EQ(tokens[0].ms, 5000);
    EXPECT_EQ(tokens[1].text, " 开场，中间 ");
    EXPECT_TRUE(tokens[2].isTimestamp);
    EXPECT_EQ(tokens[2].ms, 3723000);
    EXPECT_EQ(tokens[3].text, " 又一段");
}

TEST(StringUtils, SplitTimestampTokensNonTimestampStaysPlain)
{
    auto tokens = splitTimestampTokens("[abc] 普通文本");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].text, "[abc] 普通文本");
    EXPECT_FALSE(tokens[0].isTimestamp);
}

TEST(StringUtils, SplitTimestampTokensUnclosedBracket)
{
    auto tokens = splitTimestampTokens("前面 [12:34");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].text, "前面 [12:34");
    EXPECT_FALSE(tokens[0].isTimestamp);
}

TEST(StringUtils, SplitTimestampTokensInvalidThenValid)
{
    // 解析失败的 "[...]" 归入普通文本，后面的合法时间戳仍被识别
    auto tokens = splitTimestampTokens("[abc] [0:05]");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_FALSE(tokens[0].isTimestamp);
    EXPECT_EQ(tokens[0].text, "[abc] ");
    EXPECT_TRUE(tokens[1].isTimestamp);
    EXPECT_EQ(tokens[1].ms, 5000);
}

TEST(StringUtils, SplitTimestampTokensAdjacent)
{
    auto tokens = splitTimestampTokens("[0:05][1:10] 文本");
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_TRUE(tokens[0].isTimestamp);
    EXPECT_EQ(tokens[0].ms, 5000);
    EXPECT_TRUE(tokens[1].isTimestamp);
    EXPECT_EQ(tokens[1].ms, 70000);
    EXPECT_EQ(tokens[2].text, " 文本");
    EXPECT_FALSE(tokens[2].isTimestamp);
}

TEST(StringUtils, ParseBracketTimestampMsMinutesSeconds)
{
    int64_t ms = 0;
    EXPECT_TRUE(parseBracketTimestampMs("[0:07]", ms));
    EXPECT_EQ(ms, 7000);
    EXPECT_TRUE(parseBracketTimestampMs("[01:23]", ms));
    EXPECT_EQ(ms, 83000);
}

TEST(StringUtils, ParseBracketTimestampMsHours)
{
    int64_t ms = 0;
    EXPECT_TRUE(parseBracketTimestampMs("[1:02:03]", ms));
    EXPECT_EQ(ms, 3723000);
}

TEST(StringUtils, ParseBracketTimestampMsRejectsMalformed)
{
    int64_t ms = -1;
    EXPECT_FALSE(parseBracketTimestampMs("", ms));
    EXPECT_FALSE(parseBracketTimestampMs("[]", ms));
    EXPECT_FALSE(parseBracketTimestampMs("[abc]", ms));
    EXPECT_FALSE(parseBracketTimestampMs("[ab:cd]", ms));
    EXPECT_FALSE(parseBracketTimestampMs("[1:2:3:4]", ms));  // 多于两个冒号
    EXPECT_FALSE(parseBracketTimestampMs("1:23", ms));        // 缺括号
}

TEST(StringUtils, ParseBracketTimestampMsUnclosedStillMatches)
{
    // sscanf 只统计转换成功的个数，结尾 ']' 未匹配不影响返回值：
    // 直接调用时未闭合的 "[mm:ss" 也返回 true（splitTimestampTokens 只喂闭合片段）
    int64_t ms = 0;
    EXPECT_TRUE(parseBracketTimestampMs("[12:34", ms));
    EXPECT_EQ(ms, 754000);
}

TEST(StringUtils, ParseHmsTimestampMs)
{
    int ms = 0;
    EXPECT_TRUE(parseHmsTimestampMs("1:05", ms));
    EXPECT_EQ(ms, 65000);
    EXPECT_TRUE(parseHmsTimestampMs("01:02:03", ms));
    EXPECT_EQ(ms, 3723000);
    EXPECT_TRUE(parseHmsTimestampMs("0:07", ms));
    EXPECT_EQ(ms, 7000);
}

TEST(StringUtils, ParseHmsTimestampMsToleratesMarkers)
{
    int ms = 0;
    // 首尾非数字字符与 '*' 会被剥掉
    EXPECT_TRUE(parseHmsTimestampMs("*1:05*", ms));
    EXPECT_EQ(ms, 65000);
    EXPECT_TRUE(parseHmsTimestampMs("abc2:30xyz", ms));
    EXPECT_EQ(ms, 150000);
}

TEST(StringUtils, ParseHmsTimestampMsRejectsInvalid)
{
    int ms = -1;
    EXPECT_FALSE(parseHmsTimestampMs("", ms));
    EXPECT_FALSE(parseHmsTimestampMs("abc", ms));
    EXPECT_FALSE(parseHmsTimestampMs("12", ms));       // 没有冒号
    EXPECT_FALSE(parseHmsTimestampMs("1:2:3:4", ms));  // 多于两个冒号
}
