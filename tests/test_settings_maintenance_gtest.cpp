// 设置文件清理逻辑的单元测试
#include <gtest/gtest.h>

#include <set>
#include <string>

#include "core/settings_maintenance.h"

using namespace VideoPlay;

namespace {

// 以「存在文件集合」模拟本地文件存在性检查
struct FakeFs {
    std::set<std::string> existing;
    bool operator()(const std::string& path) const
    {
        return existing.count(path) > 0;
    }
};

} // namespace

TEST(SettingsMaintenance, RemovesMissingLocalPaths)
{
    nlohmann::json object = {
        {"C:/Videos/exists.mkv", 1000},
        {"C:/Videos/gone.mkv", 2000},
        {"D:/gone_too.mp4", 3000}
    };
    FakeFs fs{{"C:/Videos/exists.mkv"}};

    const size_t removed = pruneMissingFileEntries(object, fs);

    EXPECT_EQ(removed, 2u);
    EXPECT_TRUE(object.contains("C:/Videos/exists.mkv"));
    EXPECT_FALSE(object.contains("C:/Videos/gone.mkv"));
    EXPECT_FALSE(object.contains("D:/gone_too.mp4"));
}

TEST(SettingsMaintenance, KeepsUrlsAndNetworkPaths)
{
    nlohmann::json object = {
        {"http://example.com/a.mp4", 1},
        {"https://example.com/b.mp4", 2},
        {"rtsp://example.com/c", 3},
        {"\\\\server\\share\\a.mkv", 4},
        {"//server/a.mkv", 5}
    };
    FakeFs fs{}; // 全部「不存在」，但都应保留

    const size_t removed = pruneMissingFileEntries(object, fs);

    EXPECT_EQ(removed, 0u);
    EXPECT_EQ(object.size(), 5u);
}

TEST(SettingsMaintenance, NonObjectReturnsZero)
{
    nlohmann::json array = nlohmann::json::array({"a", "b"});
    nlohmann::json str = "not-an-object";
    nlohmann::json null;
    FakeFs fs{};

    EXPECT_EQ(pruneMissingFileEntries(array, fs), 0u);
    EXPECT_EQ(pruneMissingFileEntries(str, fs), 0u);
    EXPECT_EQ(pruneMissingFileEntries(null, fs), 0u);
    EXPECT_TRUE(array.is_array());
    EXPECT_EQ(array.size(), 2u);
}

TEST(SettingsMaintenance, HandlesUtf8ChineseKeys)
{
    const std::string kept = u8"D:/视频/存在.mkv";
    const std::string gone = u8"D:/视频/不存在.mkv";
    nlohmann::json object = {{kept, 1}, {gone, 2}};
    FakeFs fs{{kept}};

    const size_t removed = pruneMissingFileEntries(object, fs);

    EXPECT_EQ(removed, 1u);
    EXPECT_TRUE(object.contains(kept));
    EXPECT_FALSE(object.contains(gone));
}

TEST(SettingsMaintenance, EmptyObjectAndAllKept)
{
    nlohmann::json empty = nlohmann::json::object();
    nlohmann::json object = {{"C:/a.mkv", 1}};
    FakeFs fs{{"C:/a.mkv"}};

    EXPECT_EQ(pruneMissingFileEntries(empty, fs), 0u);
    EXPECT_EQ(pruneMissingFileEntries(object, fs), 0u);
    EXPECT_TRUE(object.contains("C:/a.mkv"));
}
