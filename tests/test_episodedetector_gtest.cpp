// 剧集识别（EpisodeDetector）的单元测试
#include "core/episodedetector.h"
#include "utils/logger.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

using namespace VideoPlay;

namespace {

int currentPid()
{
#ifdef _WIN32
    return _getpid();
#else
    return static_cast<int>(getpid());
#endif
}

} // namespace

class EpisodeDetectorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 关闭日志，避免测试期间向用户目录写日志文件
        Logger::root().setEnabled(false);

        const std::string testName =
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
        m_dir = std::filesystem::temp_directory_path() /
                ("videoplay_episode_gtest_" + testName + "_" +
                 std::to_string(currentPid()));
        std::filesystem::remove_all(m_dir);
        std::filesystem::create_directories(m_dir);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_dir, ec);
    }

    // 以 UTF-8 文件名创建空文件，返回可传给 detectFromFile 的路径
    std::string createFile(const std::string& name)
    {
        std::filesystem::path path = m_dir / std::filesystem::u8path(name);
        std::ofstream file(path);
        file.close();
        return path.u8string();
    }

    void createFiles(std::initializer_list<std::string> names)
    {
        for (const auto& name : names)
        {
            createFile(name);
        }
    }

    static std::vector<int> episodeNumbers(const SeriesGroup& group)
    {
        std::vector<int> numbers;
        for (const auto& episode : group.episodes)
        {
            numbers.push_back(episode.episodeNumber);
        }
        return numbers;
    }

    std::filesystem::path m_dir;
};

TEST_F(EpisodeDetectorTest, DetectsSxxEyyGroup)
{
    std::string e01 = createFile("Show.Name.S01E01.mkv");
    std::string e02 = createFile("Show.Name.S01E02.mkv");
    createFile("Show.Name.S01E10.mkv");

    auto group = EpisodeDetector::detectFromFile(e02);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(group->seasonNumber, 1);
    EXPECT_EQ(group->currentIndex, 1u);
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2, 10}));
    EXPECT_EQ(group->episodes[0].title, "S1E1");
    EXPECT_EQ(group->episodes[1].title, "S1E2");
    EXPECT_EQ(group->episodes[2].title, "S1E10");
    EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::u8path(group->episodes[0].path),
                                          std::filesystem::u8path(e01)));
    EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::u8path(group->episodes[1].path),
                                          std::filesystem::u8path(e02)));
}

TEST_F(EpisodeDetectorTest, FiltersEpisodesBySeason)
{
    std::string s01e01 = createFile("Show S01E01.mkv");
    createFile("Show S01E02.mkv");
    std::string s02e01 = createFile("Show S02E01.mkv");

    auto seasonOne = EpisodeDetector::detectFromFile(s01e01);
    ASSERT_TRUE(seasonOne.has_value());
    EXPECT_EQ(seasonOne->seasonNumber, 1);
    EXPECT_EQ(episodeNumbers(*seasonOne), (std::vector<int>{1, 2}));

    // S02 只有一集，达不到成组的最少两集
    EXPECT_FALSE(EpisodeDetector::detectFromFile(s02e01).has_value());
}

TEST_F(EpisodeDetectorTest, NormalizesSeriesNameAndExcludesOtherSeries)
{
    std::string first = createFile("show.name.S01E01.mkv");
    createFile("Show_Name_S01E02.mkv");
    createFile("Other S01E03.mkv");

    auto group = EpisodeDetector::detectFromFile(first);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2}));
}

TEST_F(EpisodeDetectorTest, AppliesSubstringNameRule)
{
    std::string first = createFile("Show S01E01.mkv");
    createFile("Shows S01E02.mkv");
    createFile("Showtime S01E03.mkv");

    auto group = EpisodeDetector::detectFromFile(first);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2}));
}

TEST_F(EpisodeDetectorTest, IgnoresNonMediaFiles)
{
    std::string first = createFile("Show S01E01.mkv");
    createFile("Show S01E02.MP4");
    createFile("Show S01E03.srt");
    createFile("Show S01E04.txt");

    auto group = EpisodeDetector::detectFromFile(first);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2}));
}

TEST_F(EpisodeDetectorTest, ReturnsNulloptForNonexistentPath)
{
    std::filesystem::path missing = m_dir / "no_such_file.mkv";
    EXPECT_FALSE(EpisodeDetector::detectFromFile(missing.u8string()).has_value());
}

TEST_F(EpisodeDetectorTest, ReturnsNulloptForSingleEpisode)
{
    std::string only = createFile("Show S01E01.mkv");
    EXPECT_FALSE(EpisodeDetector::detectFromFile(only).has_value());
}

TEST_F(EpisodeDetectorTest, ReturnsNulloptWithoutEpisodePattern)
{
    std::string first = createFile("holiday.mp4");
    createFile("holiday2.mp4");
    EXPECT_FALSE(EpisodeDetector::detectFromFile(first).has_value());
}

TEST_F(EpisodeDetectorTest, DetectsNxEPattern)
{
    createFile("Show 1x01.mp4");
    std::string e02 = createFile("Show 1x02.mp4");

    auto group = EpisodeDetector::detectFromFile(e02);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(group->seasonNumber, 1);
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2}));
    EXPECT_EQ(group->episodes[1].title, "S1E2");
}

TEST_F(EpisodeDetectorTest, DetectsEpPatternWithSeasonZero)
{
    createFile("Show EP01.mp4");
    std::string e02 = createFile("Show EP02.mp4");

    auto group = EpisodeDetector::detectFromFile(e02);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(group->seasonNumber, 0);
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2}));
    EXPECT_EQ(group->episodes[1].title, u8"第 2 集");
}

TEST_F(EpisodeDetectorTest, DetectsChineseEpisodeMarker)
{
    std::string first = createFile(u8"Show 第01集.mp4");
    createFile(u8"Show 第02集.mp4");

    auto group = EpisodeDetector::detectFromFile(first);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2}));
}

TEST_F(EpisodeDetectorTest, DetectsBracketedNumbers)
{
    std::string first = createFile("Show [01].mkv");
    createFile("Show [02].mkv");

    auto group = EpisodeDetector::detectFromFile(first);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2}));
}

TEST_F(EpisodeDetectorTest, DetectsTrailingNumbersAfterSpace)
{
    std::string first = createFile("Show 01.mp4");
    createFile("Show 02.mp4");

    auto group = EpisodeDetector::detectFromFile(first);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2}));
}

TEST_F(EpisodeDetectorTest, DetectsUnderscoreSeparatedNumbers)
{
    std::string first = createFile("clip_01.mp4");
    createFile("clip_02.mp4");

    auto group = EpisodeDetector::detectFromFile(first);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2}));
}

TEST_F(EpisodeDetectorTest, DetectsDotSeparatedNumbers)
{
    std::string first = createFile("rec.01.mp4");
    createFile("rec.02.mp4");

    auto group = EpisodeDetector::detectFromFile(first);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2}));
}

TEST_F(EpisodeDetectorTest, DoesNotTreatYearsAsEpisodes)
{
    std::string first = createFile("Movie 2023.mp4");
    createFile("Movie 2024.mp4");
    EXPECT_FALSE(EpisodeDetector::detectFromFile(first).has_value());
}

TEST_F(EpisodeDetectorTest, DetectsPureChineseSeriesName)
{
    std::string e01 = createFile(u8"剧名 第01集.mp4");
    createFile(u8"剧名 第02集.mp4");
    createFile(u8"别的剧 第03集.mp4");

    auto group = EpisodeDetector::detectFromFile(e01);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2}));
    EXPECT_EQ(group->seriesName, std::string(u8"剧名"));
}

TEST_F(EpisodeDetectorTest, DetectsChineseHuaMarker)
{
    std::string e01 = createFile(u8"剧名 第01话.mp4");
    createFile(u8"剧名 第02话.mp4");

    auto group = EpisodeDetector::detectFromFile(e01);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2}));
}

TEST_F(EpisodeDetectorTest, IgnoresFullWidthPunctuationInSeriesName)
{
    // 「剧名：」与「剧名 」归一化后应相同，才能归入同组
    std::string e01 = createFile(u8"剧名：第01集.mp4");
    createFile(u8"剧名 第02集.mp4");

    auto group = EpisodeDetector::detectFromFile(e01);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2}));
}

TEST_F(EpisodeDetectorTest, FullWidthBracketsKeepValidSeriesName)
{
    std::string e01 = createFile(u8"剧名【01】.mkv");
    createFile(u8"剧名【02】.mkv");

    auto group = EpisodeDetector::detectFromFile(e01);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2}));
    EXPECT_EQ(group->seriesName, std::string(u8"剧名"));

    // 回归用例：旧正则按字节匹配【，系列名会残留半个 UTF-8 字符
    std::string s01 = createFile(u8"Show【01】.mkv");
    createFile(u8"Show【02】.mkv");

    auto show = EpisodeDetector::detectFromFile(s01);
    ASSERT_TRUE(show.has_value());
    EXPECT_EQ(show->seriesName, "Show");
    EXPECT_EQ(episodeNumbers(*show), (std::vector<int>{1, 2}));
}

TEST_F(EpisodeDetectorTest, DetectsChineseTrailingNumber)
{
    std::string e01 = createFile(u8"剧名 01.mp4");
    createFile(u8"剧名 02.mp4");

    auto group = EpisodeDetector::detectFromFile(e01);
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(episodeNumbers(*group), (std::vector<int>{1, 2}));
}
