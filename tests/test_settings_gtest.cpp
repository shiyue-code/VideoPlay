// Settings 读写、容错与归一化的单元测试
// Settings 是单例且配置文件路径由环境变量决定，这里通过 ::testing::Environment
// 在套件启动前把 APPDATA/HOME 指向临时目录，保证测试绝不读写用户真实配置。
#include "core/settings.h"
#include "utils/logger.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
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

// 与环境变量重定向后 Settings::getConfigPath() 的计算结果保持一致
std::string configPath()
{
#ifdef _WIN32
    const char* appData = getenv("APPDATA");
    if (appData)
    {
        return std::string(appData) + "/VideoPlay/VideoPlay.json";
    }
#else
    const char* home = getenv("HOME");
    if (home)
    {
        return std::string(home) + "/.config/VideoPlay/VideoPlay.json";
    }
#endif
    return "./VideoPlay.json";
}

class SettingsTestEnvironment : public ::testing::Environment
{
public:
    void SetUp() override
    {
        Logger::root().setEnabled(false);

        m_tempDir = std::filesystem::temp_directory_path() /
                    ("videoplay_settings_gtest_" + std::to_string(currentPid()));
        std::filesystem::create_directories(m_tempDir);
        const std::string dir = m_tempDir.string();

#ifdef _WIN32
        _putenv_s("APPDATA", dir.c_str());
        const char* envValue = getenv("APPDATA");
#else
        setenv("HOME", dir.c_str(), 1);
        const char* envValue = getenv("HOME");
#endif
        if (!envValue || dir != envValue)
        {
            std::abort();
        }

        // 强制单例在环境变量重定向后初始化，并确认配置确实落在隔离目录
        Settings::instance();
        Settings::instance().save();
        if (!std::filesystem::exists(configPath()))
        {
            // 隔离失败时直接终止：abort 跳过析构，避免写真实配置
            std::abort();
        }
    }

    // 不删除临时目录：Settings 析构函数在进程退出时还会执行 save()
private:
    std::filesystem::path m_tempDir;
};

::testing::Environment* const g_settingsTestEnvironment =
    ::testing::AddGlobalTestEnvironment(new SettingsTestEnvironment);

} // namespace

class SettingsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Settings::instance().reset();
        std::error_code ec;
        std::filesystem::remove(brokenPath(), ec);
    }

    static std::string brokenPath()
    {
        return configPath() + ".broken";
    }

    // 配置文件所在目录（隔离的 %APPDATA%/VideoPlay 目录）
    static std::filesystem::path configDir()
    {
        return std::filesystem::path(configPath()).parent_path();
    }

    // 在配置目录中创建真实文件（名称可为 UTF-8 中文），返回 UTF-8 键
    std::string createRealFile(const std::string& name)
    {
        std::filesystem::path path = configDir() / std::filesystem::u8path(name);
        std::ofstream file(path);
        file << "x";
        file.close();
        return path.u8string();
    }
};

TEST_F(SettingsTest, DefaultsWhenConfigMissing)
{
    std::error_code ec;
    std::filesystem::remove(configPath(), ec);

    Settings& s = Settings::instance();
    s.load();

    EXPECT_EQ(s.volume(), 100);
    EXPECT_FALSE(s.isMuted());
    EXPECT_DOUBLE_EQ(s.playbackSpeed(), 1.0);
    EXPECT_EQ(s.loopMode(), LoopMode::Playlist);
    EXPECT_EQ(s.aspectMode(), AspectMode::Original);
    EXPECT_TRUE(s.hardwareDecodingEnabled());
    EXPECT_TRUE(s.rememberPosition());

    WindowConfig window = s.windowConfig();
    EXPECT_EQ(window.x, 100);
    EXPECT_EQ(window.y, 100);
    EXPECT_EQ(window.width, 1280);
    EXPECT_EQ(window.height, 720);
    EXPECT_FALSE(window.maximized);
    EXPECT_TRUE(window.borderless);

    SubtitleStyle style = s.subtitleStyle();
    EXPECT_EQ(style.fontFamily, "Microsoft YaHei");
    EXPECT_EQ(style.fontSize, 24);
    EXPECT_EQ(style.fontColor, "#FFFFFF");
    EXPECT_TRUE(style.hasOutline);
    EXPECT_EQ(style.outlineColor, "#000000");
    EXPECT_EQ(style.outlineWidth, 2);

    EXPECT_EQ(s.lastPosition("anything"), 0);
    EXPECT_TRUE(s.playlist().empty());
    EXPECT_EQ(s.playlistIndex(), 0u);
    EXPECT_TRUE(s.recentFiles().empty());
}

TEST_F(SettingsTest, ClampsOutOfRangeValues)
{
    Settings& s = Settings::instance();

    s.setVolume(150);
    EXPECT_EQ(s.volume(), 100);
    s.setVolume(-5);
    EXPECT_EQ(s.volume(), 0);

    s.setPlaybackSpeed(10.0);
    EXPECT_DOUBLE_EQ(s.playbackSpeed(), 4.0);
    s.setPlaybackSpeed(0.1);
    EXPECT_DOUBLE_EQ(s.playbackSpeed(), 0.25);
}

TEST_F(SettingsTest, SaveLoadRoundTrip)
{
    Settings& s = Settings::instance();

    const std::string mediaFile = createRealFile(u8"剧集 第01集.mkv");

    s.setVolume(42);
    s.setMuted(true);
    s.setPlaybackSpeed(1.5);
    s.setLoopMode(LoopMode::Single);
    s.setAspectMode(AspectMode::R4_3);
    s.setAlwaysOnTop(true);
    s.setHardwareDecodingEnabled(false);
    s.setAudioOutputDeviceName(u8"扬声器 (测试)");
    s.setChannelMapMode(ChannelMapMode::SwapLR);
    s.setRememberPosition(false);

    WindowConfig window{10, 20, 800, 600, true, false};
    s.setWindowConfig(window);

    SubtitleStyle style;
    style.fontFamily = "SimSun";
    style.fontSize = 30;
    style.fontColor = "#FF0000";
    style.hasOutline = false;
    style.outlineColor = "#00FF00";
    style.outlineWidth = 5;
    s.setSubtitleStyle(style);

    VideoTransform transform{90, true, false, 10};
    s.setVideoTransform(transform);

    VideoFilterConfig videoFilter;
    videoFilter.enabled = true;
    videoFilter.brightness = 0.2f;
    videoFilter.contrast = 1.5f;
    videoFilter.saturation = 0.8f;
    videoFilter.hue = 30.0f;
    videoFilter.gamma = 1.2f;
    videoFilter.deinterlace = DeinterlaceMode::Bwdif;
    videoFilter.hdrToneMap = HdrToneMapMode::On;
    videoFilter.hdrAlgorithm = HdrToneMapAlgorithm::Mobius;
    s.setVideoFilterConfig(videoFilter);

    AudioFilterConfig audioFilter;
    audioFilter.enabled = true;
    audioFilter.preset = AudioFilterPreset::Night;
    audioFilter.preampDb = -2.5;
    audioFilter.limiterEnabled = true;
    audioFilter.dynamicNormalizerEnabled = false;
    audioFilter.eqBands = {{500.0, 1.0, 2.0}, {2000.0, 0.5, -3.0}};
    s.setAudioFilterConfig(audioFilter);

    s.setLastPosition(mediaFile, 12345);
    s.setLastDuration(mediaFile, 60000);

    SessionInfo session;
    session.filePath = mediaFile;
    session.position = 12345;
    session.duration = 60000;
    session.playlistIndex = 0;
    session.hasValidSession = true;
    s.setLastSession(session);

    const std::string secondFile = createRealFile("ep2_roundtrip.mkv");
    s.setPlaylist({mediaFile, secondFile}, 1);
    s.save();

    // 只用不触发 save() 的普通 setter 污染内存值，load() 后应全部恢复
    s.setVolume(1);
    s.setMuted(false);
    s.setPlaybackSpeed(3.0);
    s.setLoopMode(LoopMode::None);
    s.setAspectMode(AspectMode::FillWindow);
    s.setAlwaysOnTop(false);
    s.setHardwareDecodingEnabled(true);
    s.setAudioOutputDeviceName("changed");
    s.setChannelMapMode(ChannelMapMode::Original);
    s.setRememberPosition(true);
    s.setWindowConfig(WindowConfig{});
    s.setSubtitleStyle(SubtitleStyle{});
    s.setVideoTransform(VideoTransform{});
    s.setVideoFilterConfig(VideoFilterConfig{});
    s.setAudioFilterConfig(AudioFilterConfig{});
    s.setLastPosition(mediaFile, 999);
    s.setLastDuration(mediaFile, 999);
    s.setLastSession(SessionInfo{});

    s.load();

    EXPECT_EQ(s.volume(), 42);
    EXPECT_TRUE(s.isMuted());
    EXPECT_DOUBLE_EQ(s.playbackSpeed(), 1.5);
    EXPECT_EQ(s.loopMode(), LoopMode::Single);
    EXPECT_EQ(s.aspectMode(), AspectMode::R4_3);
    EXPECT_TRUE(s.alwaysOnTop());
    EXPECT_FALSE(s.hardwareDecodingEnabled());
    EXPECT_EQ(s.audioOutputDeviceName(), std::string(u8"扬声器 (测试)"));
    EXPECT_EQ(s.channelMapMode(), ChannelMapMode::SwapLR);
    EXPECT_FALSE(s.rememberPosition());

    WindowConfig w = s.windowConfig();
    EXPECT_EQ(w.x, 10);
    EXPECT_EQ(w.y, 20);
    EXPECT_EQ(w.width, 800);
    EXPECT_EQ(w.height, 600);
    EXPECT_TRUE(w.maximized);
    EXPECT_FALSE(w.borderless);

    SubtitleStyle st = s.subtitleStyle();
    EXPECT_EQ(st.fontFamily, "SimSun");
    EXPECT_EQ(st.fontSize, 30);
    EXPECT_EQ(st.fontColor, "#FF0000");
    EXPECT_FALSE(st.hasOutline);
    EXPECT_EQ(st.outlineColor, "#00FF00");
    EXPECT_EQ(st.outlineWidth, 5);

    VideoTransform t = s.videoTransform();
    EXPECT_EQ(t.rotation, 90);
    EXPECT_TRUE(t.flipHorizontal);
    EXPECT_FALSE(t.flipVertical);
    EXPECT_EQ(t.cropPercent, 10);

    VideoFilterConfig vf = s.videoFilterConfig();
    EXPECT_TRUE(vf.enabled);
    EXPECT_FLOAT_EQ(vf.brightness, 0.2f);
    EXPECT_FLOAT_EQ(vf.contrast, 1.5f);
    EXPECT_FLOAT_EQ(vf.saturation, 0.8f);
    EXPECT_FLOAT_EQ(vf.hue, 30.0f);
    EXPECT_FLOAT_EQ(vf.gamma, 1.2f);
    EXPECT_EQ(vf.deinterlace, DeinterlaceMode::Bwdif);
    EXPECT_EQ(vf.hdrToneMap, HdrToneMapMode::On);
    EXPECT_EQ(vf.hdrAlgorithm, HdrToneMapAlgorithm::Mobius);

    AudioFilterConfig af = s.audioFilterConfig();
    EXPECT_TRUE(af.enabled);
    EXPECT_EQ(af.preset, AudioFilterPreset::Night);
    EXPECT_DOUBLE_EQ(af.preampDb, -2.5);
    EXPECT_TRUE(af.limiterEnabled);
    EXPECT_FALSE(af.dynamicNormalizerEnabled);
    ASSERT_EQ(af.eqBands.size(), 2u);
    EXPECT_DOUBLE_EQ(af.eqBands[0].frequency, 500.0);
    EXPECT_DOUBLE_EQ(af.eqBands[0].width, 1.0);
    EXPECT_DOUBLE_EQ(af.eqBands[0].gainDb, 2.0);
    EXPECT_DOUBLE_EQ(af.eqBands[1].frequency, 2000.0);
    EXPECT_DOUBLE_EQ(af.eqBands[1].width, 0.5);
    EXPECT_DOUBLE_EQ(af.eqBands[1].gainDb, -3.0);

    EXPECT_EQ(s.lastPosition(mediaFile), 12345);
    EXPECT_EQ(s.lastDuration(mediaFile), 60000);

    SessionInfo loadedSession = s.lastSession();
    EXPECT_EQ(loadedSession.filePath, mediaFile);
    EXPECT_EQ(loadedSession.position, 12345);
    EXPECT_EQ(loadedSession.duration, 60000);
    EXPECT_EQ(loadedSession.playlistIndex, 0u);
    EXPECT_TRUE(loadedSession.hasValidSession);

    EXPECT_EQ(s.playlist(), (std::vector<std::string>{mediaFile, secondFile}));
    EXPECT_EQ(s.playlistIndex(), 1u);
}

TEST_F(SettingsTest, PrunesMissingPositionEntriesOnLoad)
{
    Settings& s = Settings::instance();

    const std::string existing = createRealFile("exists.mkv");
    const std::string missing = (configDir() / "missing.mkv").u8string();
    const std::string missingParent =
        (configDir() / "no_such_subdir" / "c.mkv").u8string();
    const std::string url = "https://example.com/v.mp4";

    s.setLastPosition(existing, 100);
    s.setLastPosition(missing, 200);
    s.setLastPosition(missingParent, 300);
    s.setLastPosition(url, 400);
    s.save();

    s.load();

    EXPECT_EQ(s.lastPosition(existing), 100);
    EXPECT_EQ(s.lastPosition(missing), 0);
    EXPECT_EQ(s.lastPosition(missingParent), 300);
    EXPECT_EQ(s.lastPosition(url), 400);
}

TEST_F(SettingsTest, SanitizesInvalidValuesOnLoad)
{
    nlohmann::json config = {
        {"playback", {
            {"loopMode", 9},
            {"aspectMode", -1},
            {"channelMapping", 9},
            {"videoTransform", {
                {"rotation", 45},
                {"cropPercent", 15}
            }},
            {"audioFilter", {
                {"preset", 7},
                {"eqBands", nlohmann::json::array({
                    nlohmann::json{{"frequency", 0.0}, {"width", 1.0}, {"gainDb", 1.0}},
                    nlohmann::json{{"frequency", 100.0}, {"width", 1.0}, {"gainDb", 3.0}}
                })}
            }},
            {"videoFilter", {
                {"deinterlace", 9},
                {"hdrToneMap", 5},
                {"hdrAlgorithm", -1}
            }}
        }}
    };

    {
        std::ofstream file(configPath(), std::ios::trunc);
        file << config.dump(4);
    }

    Settings& s = Settings::instance();
    s.load();

    EXPECT_EQ(s.loopMode(), LoopMode::Playlist);
    EXPECT_EQ(s.aspectMode(), AspectMode::Original);
    EXPECT_EQ(s.channelMapMode(), ChannelMapMode::Original);

    VideoTransform t = s.videoTransform();
    EXPECT_EQ(t.rotation, 0);
    EXPECT_EQ(t.cropPercent, 0);

    AudioFilterConfig audio = s.audioFilterConfig();
    EXPECT_EQ(audio.preset, AudioFilterPreset::Off);
    ASSERT_EQ(audio.eqBands.size(), 1u);
    EXPECT_DOUBLE_EQ(audio.eqBands[0].frequency, 100.0);

    VideoFilterConfig video = s.videoFilterConfig();
    EXPECT_EQ(video.deinterlace, DeinterlaceMode::Off);
    EXPECT_EQ(video.hdrToneMap, HdrToneMapMode::Auto);
    EXPECT_EQ(video.hdrAlgorithm, HdrToneMapAlgorithm::Hable);
}

TEST_F(SettingsTest, BrokenConfigBackedUpAndDefaultsUsed)
{
    const std::string content = "{not json";
    {
        std::ofstream file(configPath(), std::ios::trunc);
        file << content;
    }

    Settings& s = Settings::instance();
    s.load();

    std::ifstream broken(brokenPath(), std::ios::binary);
    std::stringstream ss;
    ss << broken.rdbuf();
    EXPECT_EQ(ss.str(), content);

    EXPECT_EQ(s.volume(), 100);
    EXPECT_TRUE(s.hardwareDecodingEnabled());
}

TEST_F(SettingsTest, SaveIsAtomicAndWritesValidJson)
{
    Settings& s = Settings::instance();
    s.setVolume(42);
    s.save();

    EXPECT_TRUE(std::filesystem::exists(configPath()));
    EXPECT_FALSE(std::filesystem::exists(configPath() + ".tmp"));

    std::ifstream file(configPath());
    nlohmann::json config;
    file >> config;
    EXPECT_EQ(config["playback"]["volume"].get<int>(), 42);
}

TEST_F(SettingsTest, RecentFilesLimitedAndDeduplicated)
{
    Settings& s = Settings::instance();

    std::vector<std::string> files;
    for (int i = 0; i < 12; ++i)
    {
        files.push_back("C:/videos/file" + std::to_string(i) + ".mkv");
        s.addRecentFile(files.back());
    }

    std::vector<std::string> recent = s.recentFiles();
    ASSERT_EQ(recent.size(), 10u);
    EXPECT_EQ(recent.front(), files.back());

    s.addRecentFile(files[5]);
    recent = s.recentFiles();
    ASSERT_EQ(recent.size(), 10u);
    EXPECT_EQ(recent.front(), files[5]);
    EXPECT_EQ(std::count(recent.begin(), recent.end(), files[5]), 1);

    s.clearRecentFiles();
    EXPECT_TRUE(s.recentFiles().empty());
}

TEST_F(SettingsTest, BookmarksSortedUpdatedAndRemoved)
{
    Settings& s = Settings::instance();
    const std::string file = "C:/videos/movie.mkv";

    s.addBookmark(file, {1000, "a"});
    s.addBookmark(file, {5000, "b"});
    s.addBookmark(file, {3000, "c"});

    std::vector<Bookmark> bookmarks = s.bookmarksForFile(file);
    ASSERT_EQ(bookmarks.size(), 3u);
    EXPECT_EQ(bookmarks[0].timeMs, 1000);
    EXPECT_EQ(bookmarks[1].timeMs, 3000);
    EXPECT_EQ(bookmarks[2].timeMs, 5000);

    // 500ms 以内的相同位置覆盖标题而不是新增条目
    s.addBookmark(file, {1200, "a2"});
    bookmarks = s.bookmarksForFile(file);
    ASSERT_EQ(bookmarks.size(), 3u);
    EXPECT_EQ(bookmarks[0].timeMs, 1000);
    EXPECT_EQ(bookmarks[0].title, "a2");

    s.removeBookmark(file, 4800);
    bookmarks = s.bookmarksForFile(file);
    ASSERT_EQ(bookmarks.size(), 2u);
    EXPECT_EQ(bookmarks[0].timeMs, 1000);
    EXPECT_EQ(bookmarks[1].timeMs, 3000);

    s.clearBookmarksForFile(file);
    EXPECT_TRUE(s.bookmarksForFile(file).empty());
}

TEST_F(SettingsTest, SeriesProgressRoundTrip)
{
    Settings& s = Settings::instance();

    const std::string seriesKey = "C:/videos/show::Show Name";
    const std::string ep1 = createRealFile("ep1.mkv");
    const std::string ep2 = createRealFile("ep2.mkv");
    const std::unordered_map<std::string, int64_t> positions{{ep1, 111}, {ep2, 222}};

    s.setSeriesProgress(seriesKey, 2, positions);
    s.save();
    s.load();

    SeriesProgress progress = s.seriesProgress(seriesKey);
    EXPECT_EQ(progress.lastEpisodeIndex, 2);
    ASSERT_EQ(progress.episodePositions.size(), 2u);
    EXPECT_EQ(progress.episodePositions.at(ep1), 111);
    EXPECT_EQ(progress.episodePositions.at(ep2), 222);

    SeriesProgress unknown = s.seriesProgress("no/such/key");
    EXPECT_EQ(unknown.lastEpisodeIndex, 0);
    EXPECT_TRUE(unknown.episodePositions.empty());
}

TEST_F(SettingsTest, LastSessionRoundTripAndClear)
{
    Settings& s = Settings::instance();

    SessionInfo session;
    session.filePath = "C:/videos/movie.mkv";
    session.position = 12345;
    session.duration = 60000;
    session.playlistIndex = 3;
    session.hasValidSession = true;
    s.setLastSession(session);

    SessionInfo loaded = s.lastSession();
    EXPECT_EQ(loaded.filePath, session.filePath);
    EXPECT_EQ(loaded.position, 12345);
    EXPECT_EQ(loaded.duration, 60000);
    EXPECT_EQ(loaded.playlistIndex, 3u);
    EXPECT_TRUE(loaded.hasValidSession);

    s.clearLastSession();
    SessionInfo cleared = s.lastSession();
    EXPECT_FALSE(cleared.hasValidSession);
    EXPECT_TRUE(cleared.filePath.empty());
}

TEST_F(SettingsTest, AiConfigNormalizesProviderNamesAndUrls)
{
    Settings& s = Settings::instance();

    AIConfig config;
    config.provider = "google";
    config.baseUrl = "https://generativelanguage.googleapis.com/v1beta/";
    config.apiKey = "k";
    config.model = "";
    config.analysisDetailLevel = 5;
    s.setAIConfig(config);

    AIConfig loaded = s.aiConfig();
    EXPECT_EQ(loaded.provider, "gemini");
    EXPECT_EQ(loaded.baseUrl, "https://generativelanguage.googleapis.com");
    EXPECT_EQ(loaded.apiKey, "k");
    EXPECT_EQ(loaded.model, "gemini-2.5-flash");
    EXPECT_EQ(loaded.analysisDetailLevel, 2);
    ASSERT_EQ(loaded.providers.count("mimo"), 1u);
    EXPECT_EQ(loaded.providers.at("mimo").baseUrl, "https://api.xiaomimimo.com");
    EXPECT_EQ(loaded.providers.at("mimo").apiKey, "");
    EXPECT_EQ(loaded.providers.at("mimo").model, "mimo-v2.5");

    AIConfig second;
    second.provider = "xiaomi";
    second.baseUrl = "https://api.xiaomimimo.com/v1";
    second.model = "gemini-pro";
    s.setAIConfig(second);

    loaded = s.aiConfig();
    EXPECT_EQ(loaded.provider, "mimo");
    EXPECT_EQ(loaded.baseUrl, "https://api.xiaomimimo.com");
    EXPECT_EQ(loaded.model, "mimo-v2.5");
}

TEST_F(SettingsTest, LogConfigRoundTrip)
{
    Settings& s = Settings::instance();

    LogConfig config;
    config.enabled = false;
    config.consoleOutput = false;
    config.level = "warning";
    config.moduleLevels = {{"video", "debug"}, {"audio", "error"}};
    config.filePath = "C:/logs/custom.log";
    config.maxFileSize = 1024;
    config.maxFiles = 7;
    config.flushOnWarning = false;
    s.setLogConfig(config);

    const auto expectMatches = [](const LogConfig& loaded) {
        EXPECT_FALSE(loaded.enabled);
        EXPECT_FALSE(loaded.consoleOutput);
        EXPECT_EQ(loaded.level, "warning");
        ASSERT_EQ(loaded.moduleLevels.size(), 2u);
        EXPECT_EQ(loaded.moduleLevels.at("video"), "debug");
        EXPECT_EQ(loaded.moduleLevels.at("audio"), "error");
        EXPECT_EQ(loaded.filePath, "C:/logs/custom.log");
        EXPECT_EQ(loaded.maxFileSize, 1024u);
        EXPECT_EQ(loaded.maxFiles, 7u);
        EXPECT_FALSE(loaded.flushOnWarning);
    };

    expectMatches(s.logConfig());

    s.load();
    expectMatches(s.logConfig());
}
