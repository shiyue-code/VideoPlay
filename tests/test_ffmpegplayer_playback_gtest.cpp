#include "core/ffmpegplayer.h"

#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

using namespace VideoPlay;

// 集成测试：用 ffmpeg.exe 生成素材，SDL dummy 音频驱动按实时消耗音频，
// 模拟主循环驱动 FFmpegPlayer，验证倍速/seek 音画同步/EOF 排空。
class PlaybackTest : public ::testing::Test
{
protected:
    static std::string s_ffmpegExe;
    static std::filesystem::path s_dir;
    static std::string s_file12;
    static std::string s_file3;
    static bool s_sdlReady;

    static void SetUpTestSuite()
    {
        // 转成反斜杠：cmd /c 不接受带引号的正斜杠可执行路径
        s_ffmpegExe = std::filesystem::path(VIDEOPLAY_FFMPEG_EXE).make_preferred().string();
        s_dir = std::filesystem::temp_directory_path() / "videoplay_playback_test";
        std::filesystem::create_directories(s_dir);
        s_file12 = (s_dir / "av_sync_12s.mp4").string();
        s_file3 = (s_dir / "av_sync_3s.mp4").string();
        ASSERT_TRUE(generateAsset(s_file12, 12));
        ASSERT_TRUE(generateAsset(s_file3, 3));

        // dummy 驱动按实时消耗音频（SDL_dummyaudio 的 WaitDevice 睡眠
        // sample_frames/freq），无需真实声卡
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        s_sdlReady = SDL_Init(SDL_INIT_AUDIO);
        ASSERT_TRUE(s_sdlReady) << SDL_GetError();
    }

    static void TearDownTestSuite()
    {
        if (s_sdlReady) {
            SDL_Quit();
        }
    }

    static bool runFfmpeg(const std::string& args)
    {
        const std::string logPath = (s_dir / "ffmpeg_log.txt").string();
        // cmd /c 会剥掉首尾的引号，外层再包一层保持内部引号完整
        std::string cmd = "\"\"" + s_ffmpegExe + "\" -hide_banner -loglevel error -y " +
                          args + " 2> \"" + logPath + "\"\"";
        const int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::ifstream log(logPath);
            std::string err((std::istreambuf_iterator<char>(log)),
                            std::istreambuf_iterator<char>());
            std::cerr << "ffmpeg ret=" << ret << " err=" << err << std::endl;
        }
        return ret == 0;
    }

    static bool generateAsset(const std::string& out, int seconds)
    {
        const std::string inputs =
            "-f lavfi -i testsrc2=size=320x240:rate=25 "
            "-f lavfi -i sine=frequency=440:sample_rate=48000 ";
        const std::string tail = " -t " + std::to_string(seconds) +
                                 " \"" + out + "\"";
        // GOP 10s（25fps * -g 250）：关键帧只在 0，用于验证 seek 音频裁剪
        if (runFfmpeg(inputs + "-c:v libx264 -g 250 -c:a aac" + tail) &&
            std::filesystem::exists(out)) {
            return true;
        }
        return runFfmpeg(inputs + "-c:v mpeg4 -g 250 -c:a aac" + tail) &&
               std::filesystem::exists(out);
    }

    struct PumpStats {
        std::atomic<int64_t> lastFramePts{-1};
        std::atomic<int64_t> posAtLastTake{0};
        std::atomic<int> framesTaken{0};
    };

    // 模拟一次主循环：预缓冲检查 + 取当前时钟取视频帧
    static void pumpOnce(FFmpegPlayer& player, PumpStats& stats)
    {
        player.checkPreloadComplete();
        const int64_t pos = player.audioPositionMs();
        VideoFrame frame;
        if (player.getVideoFrame(pos, frame)) {
            stats.lastFramePts = frame.pts;
            stats.posAtLastTake = pos;
            stats.framesTaken++;
        }
    }

    static void pumpFor(FFmpegPlayer& player, PumpStats& stats, int ms,
                      const std::function<bool()>& shouldStop = {})
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            if (shouldStop && shouldStop()) {
                break;
            }
            pumpOnce(player, stats);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
};

std::string PlaybackTest::s_ffmpegExe;
std::filesystem::path PlaybackTest::s_dir;
std::string PlaybackTest::s_file12;
std::string PlaybackTest::s_file3;
bool PlaybackTest::s_sdlReady = false;

TEST_F(PlaybackTest, SpeedAffectsAudioClock)
{
    {
        FFmpegPlayer player;
        ASSERT_TRUE(player.loadFile(s_file12));
        player.setPlaybackSpeed(2.0);
        player.play();
        PumpStats stats;
        pumpFor(player, stats, 200);
        const int64_t pos0 = player.audioPositionMs();
        pumpFor(player, stats, 3000);
        const int64_t delta = player.audioPositionMs() - pos0;
        // 2x：3 秒挂钟应推进约 6000ms 媒体时间
        std::cerr << "[measured] 2x delta=" << delta << "ms" << std::endl;
        EXPECT_GE(delta, 5500) << "2x delta=" << delta;
        EXPECT_LE(delta, 6500) << "2x delta=" << delta;
        player.stop();
    }
    {
        FFmpegPlayer player;
        ASSERT_TRUE(player.loadFile(s_file12));
        player.setPlaybackSpeed(1.0);
        player.play();
        PumpStats stats;
        pumpFor(player, stats, 200);
        const int64_t pos0 = player.audioPositionMs();
        pumpFor(player, stats, 3000);
        const int64_t delta = player.audioPositionMs() - pos0;
        // 1x：3 秒挂钟应推进约 3000ms
        std::cerr << "[measured] 1x delta=" << delta << "ms" << std::endl;
        EXPECT_GE(delta, 2600) << "1x delta=" << delta;
        EXPECT_LE(delta, 3400) << "1x delta=" << delta;
        player.stop();
    }
}

TEST_F(PlaybackTest, SeekAudioVideoSync)
{
    FFmpegPlayer player;
    ASSERT_TRUE(player.loadFile(s_file12));
    player.play();
    PumpStats stats;
    pumpFor(player, stats, 800);

    player.seek(5000);

    // 记录 seek 后首个取到的帧与当时的媒体时钟
    int64_t firstPts = -1;
    int64_t posAtFirst = -1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        player.checkPreloadComplete();
        const int64_t pos = player.audioPositionMs();
        VideoFrame frame;
        if (player.getVideoFrame(pos, frame)) {
            firstPts = frame.pts;
            posAtFirst = pos;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    ASSERT_GE(firstPts, 0) << "seek 后 3 秒内没有取到视频帧";
    std::cerr << "[measured] seek(5000): firstPts=" << firstPts
              << " posAtFirst=" << posAtFirst << std::endl;
    EXPECT_GE(firstPts, 4900) << "firstPts=" << firstPts;
    EXPECT_LE(firstPts, 5300) << "firstPts=" << firstPts;
    EXPECT_GE(posAtFirst, 4900) << "posAtFirst=" << posAtFirst;
    EXPECT_LE(posAtFirst, 5400) << "posAtFirst=" << posAtFirst;

    pumpFor(player, stats, 1000);
    const int64_t pos = player.audioPositionMs();
    ASSERT_GT(stats.lastFramePts.load(), 0);
    std::cerr << "[measured] seek+1s: pos=" << pos
              << " lastFramePts=" << stats.lastFramePts.load()
              << " diff=" << std::abs(pos - stats.lastFramePts.load()) << "ms" << std::endl;
    EXPECT_LT(std::abs(pos - stats.lastFramePts.load()), 150)
        << "pos=" << pos << " lastFramePts=" << stats.lastFramePts.load();
    player.stop();
}

TEST_F(PlaybackTest, EofDrainsPlayback)
{
    FFmpegPlayer player;
    ASSERT_TRUE(player.loadFile(s_file3));
    std::atomic<bool> stopped{false};
    player.setStateCallback([&stopped](PlaybackState state) {
        if (state == PlaybackState::Stopped) {
            stopped = true;
        }
    });

    const auto t0 = std::chrono::steady_clock::now();
    player.play();
    PumpStats stats;
    pumpFor(player, stats, 15000, [&stopped] { return stopped.load(); });
    const auto wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    ASSERT_TRUE(stopped.load()) << "15 秒内未收到 Stopped 回调";
    // 3s 素材应播完才 Stopped：挂钟 >= 2.7s，尾帧 pts >= 2800
    std::cerr << "[measured] EOF: wallMs=" << wallMs
              << " lastFramePts=" << stats.lastFramePts.load() << std::endl;
    EXPECT_GE(wallMs, 2700) << "wallMs=" << wallMs;
    ASSERT_GT(stats.lastFramePts.load(), 0) << "没有取到任何视频帧";
    EXPECT_GE(stats.lastFramePts.load(), 2800)
        << "lastFramePts=" << stats.lastFramePts.load();
    player.stop();
}
