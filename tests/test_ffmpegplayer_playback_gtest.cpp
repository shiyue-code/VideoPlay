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

// BGRA 三通道整体均值
static double meanBgr(const VideoFrame& frame)
{
    if (frame.data.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    size_t count = 0;
    for (size_t i = 0; i + 3 < frame.data.size(); i += 4) {
        sum += frame.data[i] + frame.data[i + 1] + frame.data[i + 2];
        count += 3;
    }
    return count ? sum / static_cast<double>(count) : 0.0;
}

TEST_F(PlaybackTest, PausedRefreshAppliesVideoAdjust)
{
    FFmpegPlayer player;
    ASSERT_TRUE(player.loadFile(s_file3));
    player.play();

    // 泵到取到第一帧后暂停，保留该帧验证暂停态画面调节立即生效
    VideoFrame frame;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline && frame.data.empty()) {
        player.checkPreloadComplete();
        const int64_t pos = player.audioPositionMs();
        player.getVideoFrame(pos, frame);
        if (frame.data.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    ASSERT_FALSE(frame.data.empty()) << "3 秒内没有取到视频帧";
    ASSERT_TRUE(static_cast<bool>(frame.base)) << "帧未携带调节基础帧";
    player.pause();

    const int64_t pts = frame.pts;
    const double before = meanBgr(frame);

    VideoFilterConfig cfg;
    cfg.enabled = true;
    cfg.brightness = -0.4f;
    player.setVideoFilterConfig(cfg);

    EXPECT_TRUE(player.refreshVideoFrame(frame));
    const double after = meanBgr(frame);
    std::cerr << "[measured] brightness=-0.4: BGR mean " << before
              << " -> " << after << std::endl;
    EXPECT_GE(before - after, 20.0) << "before=" << before << " after=" << after;
    EXPECT_EQ(frame.pts, pts);
    // 代次未再变化，第二次刷新应直接返回 false
    EXPECT_FALSE(player.refreshVideoFrame(frame));

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

TEST_F(PlaybackTest, StepBackLandingIsPreviousFrame)
{
    // 模拟 app 逐帧步进：暂停 → seekStepBack(显示帧 pts) → getStepLandingFrame。
    // 素材是 x264 默认编码（含 B 帧），解码顺序 ≠ pts 顺序——
    // 落点必须是解码序上 anchor 的上一帧（pts 恰好少一档），
    // 而不是重排后的下一个 P 帧，也不是上一个被取用的帧。
    // 先做一次普通 seek：历史/队列被清空，第一次 -1 必然走真实补解码，
    // 之后连续 -1 命中已解码窗口（免 seek、免解码）
    FFmpegPlayer player;
    ASSERT_TRUE(player.loadFile(s_file12));
    player.play();

    // 播到 ~2s，确认出帧正常
    int64_t shownPts = -1;
    const auto t0 = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < t0 && shownPts < 2000) {
        player.checkPreloadComplete();
        const int64_t pos = player.audioPositionMs();
        VideoFrame f;
        if (player.getVideoFrame(pos, f)) {
            shownPts = f.pts;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    ASSERT_GE(shownPts, 2000) << "没取到足够的帧";
    player.pause();

    // 普通 seek 到 6s：清空已解码窗口，落点消费后 -1 必须真实补解码
    player.seek(6000);
    VideoFrame seekLanding;
    const auto d0 = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < d0 && seekLanding.data.empty()) {
        if (!player.getStepLandingFrame(seekLanding)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    }
    ASSERT_FALSE(seekLanding.data.empty()) << "暂停 seek 没等到落点";
    shownPts = seekLanding.pts;

    // -1：以显示帧 pts 为锚做回退落点 seek（真实解码路径）
    player.seekStepBack(shownPts);

    VideoFrame landing;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline && landing.data.empty()) {
        if (!player.getStepLandingFrame(landing)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    }
    ASSERT_FALSE(landing.data.empty()) << "8 秒内没有等到落点帧";
    std::cerr << "[measured] stepback: shown=" << shownPts
              << " landing=" << landing.pts << std::endl;
    // 落点必须是解码序真正的上一帧：pts 恰好少一档（40ms）
    EXPECT_EQ(landing.pts, shownPts - 40)
        << "landing=" << landing.pts << " expected=" << (shownPts - 40);

    // 落点后 +1 必须从缓冲窗口精确回到锚点帧
    VideoFrame next;
    const auto d2 = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < d2 && next.data.empty()) {
        if (!player.getStepForwardFrame(landing.pts, next)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    }
    ASSERT_FALSE(next.data.empty());
    EXPECT_EQ(next.pts, shownPts) << "next=" << next.pts;

    // 再 +1 应继续走缓冲窗口内的下一帧
    VideoFrame next2;
    const auto d3 = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < d3 && next2.data.empty()) {
        if (!player.getStepForwardFrame(next.pts, next2)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    }
    ASSERT_FALSE(next2.data.empty());
    EXPECT_EQ(next2.pts, shownPts + 40) << "next2=" << next2.pts;

    // 连续 -1：窗口里已有前帧，直接命中（免解码）。
    // 跨过刚 +1 走过的帧验证历史与队列并集取前驱的正确性
    int64_t anchor = next2.pts;
    for (int i = 1; i <= 3; ++i) {
        player.seekStepBack(anchor);
        VideoFrame back;
        const auto d4 = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < d4 && back.data.empty()) {
            if (!player.getStepLandingFrame(back)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
        }
        ASSERT_FALSE(back.data.empty()) << "第 " << i << " 次回退没落点";
        EXPECT_EQ(back.pts, anchor - 40)
            << "i=" << i << " back=" << back.pts << " anchor=" << anchor;
        anchor = back.pts;
    }
    player.stop();
}

TEST_F(PlaybackTest, StepBackAfterInstantPause)
{
    // 启动场景：play() 后立刻 pause()（预缓冲期间），随后像 app 一样
    // +1（恢复播放取最早帧再暂停）、-1（seekStepBack 取落点）
    FFmpegPlayer player;
    ASSERT_TRUE(player.loadFile(s_file12));
    player.play();
    player.pause();

    // +1 两次：恢复播放 → getStepForwardFrame 取下一帧 → 暂停
    // （第一帧 pts=0 没有上一帧，测 -1 需要展示第二帧）
    VideoFrame shown;
    for (int i = 0; i < 2 && shown.pts <= 0; ++i) {
        player.play();
        const auto d1 = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < d1 && shown.pts <= 0) {
            VideoFrame f;
            if (player.getStepForwardFrame(shown.data.empty() ? -1 : shown.pts, f)) {
                shown = std::move(f);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
        }
        player.pause();
    }
    ASSERT_FALSE(shown.data.empty()) << "恢复播放后没取到帧";
    ASSERT_GT(shown.pts, 0) << "没取到第二帧";

    // -1：落点必须是解码序真正的上一帧（pts - 40ms）
    player.seekStepBack(shown.pts);
    VideoFrame landing;
    const auto d2 = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < d2 && landing.data.empty()) {
        if (!player.getStepLandingFrame(landing)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    }
    ASSERT_FALSE(landing.data.empty()) << "启动即暂停后 -1 没落点";
    std::cerr << "[measured] instant-pause stepback: shown=" << shown.pts
              << " landing=" << landing.pts << std::endl;
    EXPECT_EQ(landing.pts, shown.pts - 40) << "landing=" << landing.pts;

    // +1 精确回位
    VideoFrame back;
    const auto d3 = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < d3 && back.data.empty()) {
        if (!player.getStepForwardFrame(landing.pts, back)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    }
    ASSERT_FALSE(back.data.empty());
    EXPECT_EQ(back.pts, shown.pts) << "back=" << back.pts;
    player.stop();
}

TEST_F(PlaybackTest, ReplayAfterEof)
{
    FFmpegPlayer player;
    ASSERT_TRUE(player.loadFile(s_file3));
    std::atomic<bool> stopped{false};
    player.setStateCallback([&stopped](PlaybackState state) {
        if (state == PlaybackState::Stopped) {
            stopped = true;
        }
    });

    player.play();
    PumpStats stats;
    pumpFor(player, stats, 15000, [&stopped] { return stopped.load(); });
    ASSERT_TRUE(stopped.load()) << "15 秒内未收到 Stopped 回调";

    // Stopped 期间发出的 seek 没有解码线程消费，重播时应被丢弃而不是
    // 被新线程执行——否则跳到结尾附近立刻再次 EOF，表现为「无法再播放」
    player.seek(2500);

    // 自然播完后再次 play：应从头重新解码出帧
    player.play();
    PumpStats stats2;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline &&
           stats2.framesTaken.load() == 0) {
        pumpOnce(player, stats2);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    std::cerr << "[measured] replay: framesTaken=" << stats2.framesTaken.load()
              << " pos=" << player.audioPositionMs()
              << " state=" << static_cast<int>(player.state()) << std::endl;
    EXPECT_GT(stats2.framesTaken.load(), 0) << "EOF 后重播取不到视频帧";
    EXPECT_EQ(player.state(), PlaybackState::Playing);
    // 残留 seek(2500) 若被消费，首帧 pts 会落在 2500 附近；从头播应远小于它
    EXPECT_LT(stats2.lastFramePts.load(), 1000)
        << "重播首帧 pts=" << stats2.lastFramePts.load();
    player.stop();
}
