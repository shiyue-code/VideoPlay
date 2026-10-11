// SearchEngine 转录/字幕/章节检索索引的单元测试
#include "ai/searchengine.h"
#include "utils/logger.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

using namespace VideoPlay;

namespace {

TranscriptSegment makeSegment(int64_t startMs, int64_t endMs, const std::string& text)
{
    TranscriptSegment seg;
    seg.startTime = startMs;
    seg.endTime = endMs;
    seg.text = text;
    seg.confidence = 1.0f;
    return seg;
}

SubtitleEntry makeSubtitle(int64_t startMs, int64_t endMs, const std::string& text)
{
    SubtitleEntry entry;
    entry.startTime = startMs;
    entry.endTime = endMs;
    entry.text = text;
    return entry;
}

ChapterInfo makeChapter(int64_t startMs, const std::string& title)
{
    ChapterInfo chapter;
    chapter.startTime = startMs;
    chapter.endTime = startMs + 60000;
    chapter.title = title;
    return chapter;
}

} // namespace

class SearchEngineTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 关闭日志，避免测试期间向用户目录写日志文件
        Logger::root().setEnabled(false);
    }

    SearchEngine m_engine;
};

TEST_F(SearchEngineTest, HasIndexInitiallyFalse)
{
    EXPECT_FALSE(m_engine.hasIndex());
}

TEST_F(SearchEngineTest, HasIndexTrueAfterBuild)
{
    m_engine.buildIndex("video.mkv", {makeSegment(0, 1000, "hello")});
    EXPECT_TRUE(m_engine.hasIndex());
}

TEST_F(SearchEngineTest, BuildIndexWithEmptyTranscriptStillCounts)
{
    // buildIndex 无条件置 hasIndex，即使没有任何条目
    m_engine.buildIndex("video.mkv", {});
    EXPECT_TRUE(m_engine.hasIndex());
    EXPECT_TRUE(m_engine.search("anything").empty());
}

TEST_F(SearchEngineTest, ClearIndexResetsState)
{
    m_engine.buildIndex("v", {makeSegment(0, 1000, "hello world")});
    ASSERT_TRUE(m_engine.hasIndex());
    m_engine.clearIndex();
    EXPECT_FALSE(m_engine.hasIndex());
    EXPECT_TRUE(m_engine.search("hello").empty());
}

TEST_F(SearchEngineTest, SearchWithoutIndexReturnsEmpty)
{
    EXPECT_TRUE(m_engine.search("hello").empty());
}

TEST_F(SearchEngineTest, SearchEmptyQueryReturnsEmpty)
{
    m_engine.buildIndex("v", {makeSegment(0, 1000, "hello world")});
    EXPECT_TRUE(m_engine.search("").empty());
}

TEST_F(SearchEngineTest, SearchQueryWithoutTokensReturnsEmpty)
{
    m_engine.buildIndex("v", {makeSegment(0, 1000, "hello world")});
    // 纯标点分不出任何 token
    EXPECT_TRUE(m_engine.search("!!!").empty());
}

TEST_F(SearchEngineTest, FindsEnglishKeyword)
{
    m_engine.buildIndex("v", {
        makeSegment(12000, 15000, "The player renders video frames"),
        makeSegment(20000, 25000, "Audio is decoded separately"),
    });
    auto results = m_engine.search("renders");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].text, "The player renders video frames");
    EXPECT_EQ(results[0].timestamp, 12000);
    EXPECT_EQ(results[0].source, 0);   // 0=转录
    EXPECT_EQ(results[0].context, "[0:12] The player renders video frames");
    EXPECT_GT(results[0].relevance, 0.0f);
}

TEST_F(SearchEngineTest, SearchIsCaseInsensitive)
{
    m_engine.buildIndex("v", {makeSegment(0, 1000, "The Player Renders")});
    EXPECT_EQ(m_engine.search("RENDERS").size(), 1u);
}

TEST_F(SearchEngineTest, FindsChineseByExactSubstring)
{
    m_engine.buildIndex("v", {
        makeSegment(0, 1000, u8"讲解关键动作分解"),
        makeSegment(5000, 6000, u8"其它段落内容"),
    });
    // 中文按 UTF-8 单字建倒排索引，多字查询由子串相关性兜住
    auto results = m_engine.search(u8"关键");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].text, std::string(u8"讲解关键动作分解"));
}

TEST_F(SearchEngineTest, ChineseQueryMatchesOutOfOrderCharacters)
{
    m_engine.buildIndex("v", {makeSegment(0, 1000, u8"讲解关键动作分解")});
    // 逐字倒排 + 按字计分：乱序的单字组合也能命中同一词条
    EXPECT_FALSE(m_engine.search(u8"动作").empty());
    EXPECT_FALSE(m_engine.search(u8"作动").empty());
}

TEST_F(SearchEngineTest, SubtitleEntriesBecomeSearchable)
{
    m_engine.buildIndex("v", {makeSegment(0, 1000, "spoken words here")});
    m_engine.addSubtitleEntries({makeSubtitle(3000, 4000, "subtitle line about dragons")});

    auto results = m_engine.search("dragons");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].source, 1);   // 1=外挂字幕
    EXPECT_EQ(results[0].timestamp, 3000);
    EXPECT_EQ(results[0].context, "[0:03] subtitle line about dragons");
}

TEST_F(SearchEngineTest, AddEmptySubtitlesDoesNotCreateIndex)
{
    m_engine.addSubtitleEntries({});
    EXPECT_FALSE(m_engine.hasIndex());
}

TEST_F(SearchEngineTest, AddSubtitlesOnFreshEngineCreatesIndex)
{
    m_engine.addSubtitleEntries({makeSubtitle(0, 1000, "only subtitles")});
    EXPECT_TRUE(m_engine.hasIndex());
    EXPECT_EQ(m_engine.search("subtitles").size(), 1u);
}

TEST_F(SearchEngineTest, MaxResultsTruncates)
{
    std::vector<TranscriptSegment> segs;
    for (int i = 0; i < 5; ++i)
    {
        segs.push_back(makeSegment(i * 1000, i * 1000 + 500,
                                   "apple item " + std::to_string(i)));
    }
    m_engine.buildIndex("v", segs);
    EXPECT_EQ(m_engine.search("apple", 2).size(), 2u);
    EXPECT_EQ(m_engine.search("apple", 50).size(), 5u);
}

TEST_F(SearchEngineTest, RelevanceRanksBetterMatchFirst)
{
    m_engine.buildIndex("v", {
        makeSegment(5000, 6000, "machine parts and tools"),
        makeSegment(0, 1000, "machine learning basics explained"),
    });
    auto results = m_engine.search("machine learning");
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].text, "machine learning basics explained");
    EXPECT_GT(results[0].relevance, results[1].relevance);
}

TEST_F(SearchEngineTest, BuildIndexReplacesPreviousIndex)
{
    m_engine.buildIndex("v1", {makeSegment(0, 1000, "alpha only")});
    ASSERT_EQ(m_engine.search("alpha").size(), 1u);

    m_engine.buildIndex("v2", {makeSegment(0, 1000, "beta only")});
    EXPECT_TRUE(m_engine.search("alpha").empty());
    EXPECT_EQ(m_engine.search("beta").size(), 1u);
}

TEST_F(SearchEngineTest, ChaptersAreSearchable)
{
    m_engine.buildIndex("v", {makeSegment(0, 1000, "transcript text")},
                        {makeChapter(60000, "Opening ceremony")});
    auto results = m_engine.search("ceremony");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].source, 2);   // 2=章节
    EXPECT_EQ(results[0].text, "Opening ceremony");
    EXPECT_EQ(results[0].timestamp, 60000);
    EXPECT_EQ(results[0].context, std::string(u8"章节: Opening ceremony [1:00]"));
}

TEST_F(SearchEngineTest, SourcesAreDistinguished)
{
    m_engine.buildIndex("v",
                        {makeSegment(0, 1000, "shared keyword transcript")},
                        {makeChapter(2000, "shared keyword chapter")});
    m_engine.addSubtitleEntries({makeSubtitle(4000, 5000, "shared keyword subtitle")});

    auto results = m_engine.search("shared keyword");
    ASSERT_EQ(results.size(), 3u);
    std::set<int> sources;
    for (const auto& r : results)
    {
        sources.insert(r.source);
    }
    EXPECT_EQ(sources, (std::set<int>{0, 1, 2}));
}

TEST_F(SearchEngineTest, NoMatchReturnsEmpty)
{
    m_engine.buildIndex("v", {makeSegment(0, 1000, "some english words")});
    EXPECT_TRUE(m_engine.search("qqqxyz").empty());
}
