// common.h 内联工具函数单元测试（formatTime、倍速映射、音频滤镜预设见 test_playbackcontroller）
#include <gtest/gtest.h>

#include "core/common.h"

using namespace VideoPlay;

namespace {

TrackInfo makeTrack(const std::string& language)
{
    TrackInfo t;
    t.language = language;
    return t;
}

} // namespace

TEST(Common, TrackLanguageNameBasics)
{
    EXPECT_EQ(trackLanguageName(""), std::string(u8"未知"));
    EXPECT_EQ(trackLanguageName("und"), std::string(u8"未指定"));
    EXPECT_EQ(trackLanguageName("eng"), std::string(u8"英语"));
    EXPECT_EQ(trackLanguageName("en"), std::string(u8"英语"));
    EXPECT_EQ(trackLanguageName("jpn"), std::string(u8"日语"));
    EXPECT_EQ(trackLanguageName("kor"), std::string(u8"韩语"));
    EXPECT_EQ(trackLanguageName("fra"), std::string(u8"法语"));
    EXPECT_EQ(trackLanguageName("deu"), std::string(u8"德语"));
    EXPECT_EQ(trackLanguageName("spa"), std::string(u8"西班牙语"));
    EXPECT_EQ(trackLanguageName("rus"), std::string(u8"俄语"));
    // 未知代码原样回传
    EXPECT_EQ(trackLanguageName("xyz"), "xyz");
}

TEST(Common, TrackLanguageNameChineseVariants)
{
    EXPECT_EQ(trackLanguageName("chi"), std::string(u8"中文"));
    EXPECT_EQ(trackLanguageName("zho"), std::string(u8"中文"));
    EXPECT_EQ(trackLanguageName("zh"), std::string(u8"中文"));
    EXPECT_EQ(trackLanguageName("zh-CN"), std::string(u8"简体中文"));
    EXPECT_EQ(trackLanguageName("zh-Hans"), std::string(u8"简体中文"));
    EXPECT_EQ(trackLanguageName("zh-TW"), std::string(u8"繁体中文"));
    EXPECT_EQ(trackLanguageName("zh-Hant"), std::string(u8"繁体中文"));
    EXPECT_EQ(trackLanguageName("zh-HK"), std::string(u8"繁体中文"));
}

TEST(Common, TrackLanguageNameCaseQuirk)
{
    // 非中文语言的匹配用原始大小写比较：大写代码原样回传
    EXPECT_EQ(trackLanguageName("ENG"), "ENG");
    // 中文判定走小写比较，大小写不敏感
    EXPECT_EQ(trackLanguageName("ZH-tw"), std::string(u8"繁体中文"));
}

TEST(Common, RefineChineseNameByTitle)
{
    EXPECT_EQ(refineChineseName(u8"中文", u8"繁體字幕"), std::string(u8"繁体中文"));
    EXPECT_EQ(refineChineseName(u8"中文", "Traditional"), std::string(u8"繁体中文"));
    EXPECT_EQ(refineChineseName(u8"中文", u8"简体字幕"), std::string(u8"简体中文"));
    EXPECT_EQ(refineChineseName(u8"中文", ""), std::string(u8"中文"));
    EXPECT_EQ(refineChineseName(u8"中文", u8"双语"), std::string(u8"中文"));
    // base 不是「中文」时不做精化
    EXPECT_EQ(refineChineseName(u8"英语", u8"简体"), std::string(u8"英语"));
}

TEST(Common, TrackLabelComposesAllParts)
{
    TrackInfo t = makeTrack("eng");
    EXPECT_EQ(trackLabel(t, 1), std::string(u8"1. 英语"));

    t.title = "Director's Cut";
    EXPECT_EQ(trackLabel(t, 1), std::string(u8"1. 英语 (Director's Cut)"));

    t.isForced = true;
    t.channels = 2;
    EXPECT_EQ(trackLabel(t, 1), std::string(u8"1. 英语 (Director's Cut) [强制] [2ch]"));
}

TEST(Common, TrackLabelFallbacks)
{
    TrackInfo t;
    EXPECT_EQ(trackLabel(t, 3), std::string(u8"3. 未知"));      // 空语言
    t.language = "und";
    EXPECT_EQ(trackLabel(t, 3), std::string(u8"3. 未指定"));
    t.language = "xyz";
    EXPECT_EQ(trackLabel(t, 3), "3. xyz");                      // 未知代码原样显示
}

TEST(Common, TrackLabelRefinesChineseByTitle)
{
    TrackInfo t = makeTrack("chi");
    EXPECT_EQ(trackLabel(t, 1), std::string(u8"1. 中文"));

    t.title = u8"繁體";
    EXPECT_EQ(trackLabel(t, 1), std::string(u8"1. 繁体中文 (繁體)"));

    // BCP-47 直接区分简繁，不依赖标题
    t = makeTrack("zh-TW");
    EXPECT_EQ(trackLabel(t, 2), std::string(u8"2. 繁体中文"));
}

TEST(Common, NetworkStateText)
{
    EXPECT_STREQ(networkStateText(NetworkState::Idle), "");
    EXPECT_STREQ(networkStateText(NetworkState::Connecting), u8"正在连接");
    EXPECT_STREQ(networkStateText(NetworkState::Buffering), u8"正在缓冲");
    EXPECT_STREQ(networkStateText(NetworkState::Playing), "");
    EXPECT_STREQ(networkStateText(NetworkState::Reconnecting), u8"正在重连");
    EXPECT_STREQ(networkStateText(NetworkState::Failed), u8"网络错误");
}

TEST(Common, DeinterlaceModeName)
{
    EXPECT_STREQ(deinterlaceModeName(DeinterlaceMode::Off), u8"关闭");
    EXPECT_STREQ(deinterlaceModeName(DeinterlaceMode::Auto), u8"自动");
    EXPECT_STREQ(deinterlaceModeName(DeinterlaceMode::Yadif), u8"线性");
    EXPECT_STREQ(deinterlaceModeName(DeinterlaceMode::Bwdif), "Bob");
}

TEST(Common, HdrToneMapNames)
{
    EXPECT_STREQ(hdrToneMapModeName(HdrToneMapMode::Auto), u8"自动");
    EXPECT_STREQ(hdrToneMapModeName(HdrToneMapMode::On), u8"开启");
    EXPECT_STREQ(hdrToneMapModeName(HdrToneMapMode::Off), u8"关闭");

    EXPECT_STREQ(hdrToneMapAlgorithmName(HdrToneMapAlgorithm::Hable), "Hable");
    EXPECT_STREQ(hdrToneMapAlgorithmName(HdrToneMapAlgorithm::Mobius), "Mobius");
    EXPECT_STREQ(hdrToneMapAlgorithmName(HdrToneMapAlgorithm::Reinhard), "Reinhard");
}

TEST(Common, AudioFilterPresetName)
{
    EXPECT_STREQ(audioFilterPresetName(AudioFilterPreset::Off), u8"关闭");
    EXPECT_STREQ(audioFilterPresetName(AudioFilterPreset::Voice), u8"语音增强");
    EXPECT_STREQ(audioFilterPresetName(AudioFilterPreset::Bass), u8"低音增强");
    EXPECT_STREQ(audioFilterPresetName(AudioFilterPreset::Night), u8"夜间模式");
}

TEST(Common, EqIsIdentityAndIsDefault)
{
    VideoFilterConfig config;
    EXPECT_TRUE(config.eqIsIdentity());
    EXPECT_TRUE(config.isDefault());

    config.brightness = 0.2f;
    EXPECT_FALSE(config.eqIsIdentity());
    // enabled=false 时 isDefault 不看 eq 参数
    EXPECT_TRUE(config.isDefault());

    config.enabled = true;
    EXPECT_FALSE(config.isDefault());
}

TEST(Common, VideoFilterConfigNonEqAffectsIsDefault)
{
    VideoFilterConfig config;
    config.deinterlace = DeinterlaceMode::Auto;
    EXPECT_FALSE(config.isDefault());

    config.deinterlace = DeinterlaceMode::Off;
    config.hdrToneMap = HdrToneMapMode::On;
    EXPECT_FALSE(config.isDefault());
}

TEST(Common, VideoTransformIsDefault)
{
    VideoTransform t;
    EXPECT_TRUE(t.isDefault());

    t.rotation = 90;
    EXPECT_FALSE(t.isDefault());

    t = VideoTransform{};
    t.flipHorizontal = true;
    EXPECT_FALSE(t.isDefault());

    t = VideoTransform{};
    t.cropPercent = 10;
    EXPECT_FALSE(t.isDefault());
}

TEST(Common, IsNetworkUrl)
{
    EXPECT_TRUE(isNetworkUrl("http://example.com/v.mp4"));
    EXPECT_TRUE(isNetworkUrl("https://example.com/v.mp4"));
    EXPECT_TRUE(isNetworkUrl("rtsp://cam/live"));
    EXPECT_TRUE(isNetworkUrl("rtmp://server/app"));
    // 大小写不敏感
    EXPECT_TRUE(isNetworkUrl("HTTP://EXAMPLE.COM"));
    EXPECT_FALSE(isNetworkUrl("C:\\videos\\a.mp4"));
    EXPECT_FALSE(isNetworkUrl("ftp://example.com/a"));
    EXPECT_FALSE(isNetworkUrl(""));
}
