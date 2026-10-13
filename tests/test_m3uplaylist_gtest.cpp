// M3U/M3U8 播放列表解析单元测试
#include <gtest/gtest.h>

#include "core/m3uplaylist.h"

using namespace VideoPlay;

TEST(M3uPlaylist, DetectsPlaylistExtension)
{
    EXPECT_TRUE(isM3uPlaylistPath("D:/music/list.m3u"));
    EXPECT_TRUE(isM3uPlaylistPath("D:\\music\\list.M3U8"));
    EXPECT_TRUE(isM3uPlaylistPath("http://iptv.example.com/list.m3u?token=abc"));
    EXPECT_TRUE(isM3uPlaylistPath("https://cdn.example.com/live/index.m3u8#frag"));
    EXPECT_FALSE(isM3uPlaylistPath("D:/music/song.mp3"));
    EXPECT_FALSE(isM3uPlaylistPath("http://example.com/video.mp4"));
    EXPECT_FALSE(isM3uPlaylistPath("notes.m3u.txt"));
}

TEST(M3uPlaylist, ParsesPlainUriLines)
{
    M3uPlaylist pl = parseM3uPlaylist(
        "#EXTM3U\r\n"
        "D:/videos/a.mp4\r\n"
        "b.mkv\n"
        "\n"                       // 空行跳过
        "# 注释行跳过\n"
        "c.avi");
    ASSERT_EQ(pl.entries.size(), 3u);
    EXPECT_FALSE(pl.isHlsManifest);
    EXPECT_EQ(pl.entries[0].uri, "D:/videos/a.mp4");
    EXPECT_EQ(pl.entries[1].uri, "b.mkv");
    EXPECT_EQ(pl.entries[2].uri, "c.avi");
    EXPECT_TRUE(pl.entries[0].title.empty());
}

TEST(M3uPlaylist, ParsesExtinfTitles)
{
    M3uPlaylist pl = parseM3uPlaylist(
        "#EXTM3U\n"
        "#EXTINF:-1 tvg-id=\"cctv1\" tvg-name=\"CCTV1\",CCTV-1 综合\n"
        "http://iptv.example.com/cctv1/index.m3u8\n"
        "#EXTINF:120,Channel Two\n"
        "http://iptv.example.com/ch2\n"
        "http://iptv.example.com/ch3\n");  // 无 EXTINF → 空标题
    ASSERT_EQ(pl.entries.size(), 3u);
    EXPECT_EQ(pl.entries[0].title, "CCTV-1 综合");
    EXPECT_EQ(pl.entries[1].title, "Channel Two");
    EXPECT_TRUE(pl.entries[2].title.empty());
}

TEST(M3uPlaylist, ExtinfCommaInsideQuotesNotSeparator)
{
    M3uPlaylist pl = parseM3uPlaylist(
        "#EXTINF:-1 group-title=\"新闻,财经\",频道A\n"
        "http://example.com/a\n");
    ASSERT_EQ(pl.entries.size(), 1u);
    EXPECT_EQ(pl.entries[0].title, "频道A");
}

TEST(M3uPlaylist, DetectsHlsManifest)
{
    M3uPlaylist pl = parseM3uPlaylist(
        "#EXTM3U\n"
        "#EXT-X-TARGETDURATION:10\n"
        "#EXT-X-MEDIA-SEQUENCE:0\n"
        "#EXTINF:10.0,\n"
        "seg0.ts\n");
    EXPECT_TRUE(pl.isHlsManifest);
}

TEST(M3uPlaylist, Extm3uHeaderAloneIsNotHls)
{
    M3uPlaylist pl = parseM3uPlaylist("#EXTM3U\nfile.mp4\n");
    EXPECT_FALSE(pl.isHlsManifest);
}

TEST(M3uPlaylist, ResolvesRelativeLocalUri)
{
    // 相对路径按清单所在目录解析（用 / 分隔，跨平台一致）
    const std::string src = "D:/tv/channels.m3u";
    std::string r = resolveM3uUri(src, "sub/clip.mp4");
    EXPECT_EQ(r, "D:/tv/sub/clip.mp4");
    r = resolveM3uUri(src, "../shared/clip.mp4");
    EXPECT_EQ(r, "D:/shared/clip.mp4");
}

TEST(M3uPlaylist, KeepsAbsoluteAndNetworkUri)
{
    const std::string src = "D:/tv/channels.m3u";
    EXPECT_EQ(resolveM3uUri(src, "http://live.example.com/ch1"),
              "http://live.example.com/ch1");
    const std::string abs = resolveM3uUri(src, "D:/media/movie.mkv");
    EXPECT_EQ(abs, "D:/media/movie.mkv");
    // file:// 还原为本地路径
    EXPECT_EQ(resolveM3uUri(src, "file:///D:/media/movie.mkv"), "D:/media/movie.mkv");
}

TEST(M3uPlaylist, ResolvesRemoteRelativeUri)
{
    const std::string src = "http://iptv.example.com/lists/main.m3u";
    EXPECT_EQ(resolveM3uUri(src, "ch1/stream"),
              "http://iptv.example.com/lists/ch1/stream");
    EXPECT_EQ(resolveM3uUri(src, "../root.ts"),
              "http://iptv.example.com/root.ts");
}

TEST(M3uPlaylist, EmptyAndInvalidInput)
{
    EXPECT_TRUE(parseM3uPlaylist("").entries.empty());
    EXPECT_TRUE(parseM3uPlaylist("#EXTM3U\n# 只有注释\n").entries.empty());
    EXPECT_EQ(resolveM3uUri("D:/a.m3u", "   "), "");
}
