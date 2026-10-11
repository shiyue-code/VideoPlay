#include <gtest/gtest.h>
#include "core/fileassoc.h"
#include "core/common.h"

#include <algorithm>

using namespace VideoPlay;

// fileassoc.h 中的纯逻辑：ProgId、扩展名默认值接管决策、扩展名列表。
// 不触碰注册表，任何平台都可运行。

TEST(FileAssocTest, ProgIdIsStable) {
    // ProgId 写入注册表，随意改动会破坏已注册用户的关联
    EXPECT_EQ(std::string(fileAssocProgId()), "VideoPlay.Video");
}

TEST(FileAssocTest, MayTakeDefaultWhenEmpty) {
    EXPECT_TRUE(fileAssocMayTakeDefault(""));
}

TEST(FileAssocTest, MayTakeDefaultWhenOurs) {
    // 已经指向本应用时允许重写（重复注册/修复场景）
    EXPECT_TRUE(fileAssocMayTakeDefault("VideoPlay.Video"));
}

TEST(FileAssocTest, MayNotTakeForeignDefault) {
    // 其它应用占用的每用户默认值不能被抢占
    EXPECT_FALSE(fileAssocMayTakeDefault("SomeOtherPlayer.mp4"));
    EXPECT_FALSE(fileAssocMayTakeDefault("Applications\\notepad.exe"));
    // 大小写不同也算外来值（ProgId 比较按精确匹配）
    EXPECT_FALSE(fileAssocMayTakeDefault("videoplay.video"));
}

TEST(FileAssocTest, MediaExtensionsAreWellFormed) {
    const auto& exts = mediaExtensions();
    ASSERT_FALSE(exts.empty());
    for (const auto& ext : exts) {
        // 必须含点且小写（注册表 value name 与扩展名匹配依赖这个约定）
        EXPECT_EQ(ext.front(), '.') << ext;
        EXPECT_EQ(ext, toLower(ext)) << ext;
    }
    // 无重复项
    auto sorted = exts;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(std::adjacent_find(sorted.begin(), sorted.end()), sorted.end());
}

TEST(FileAssocTest, MediaExtensionsCoverCommonFormats) {
    const auto& exts = mediaExtensions();
    auto has = [&exts](const char* e) {
        return std::find(exts.begin(), exts.end(), e) != exts.end();
    };
    EXPECT_TRUE(has(".mp4"));
    EXPECT_TRUE(has(".mkv"));
    EXPECT_TRUE(has(".mp3"));
    EXPECT_TRUE(has(".flac"));
}
