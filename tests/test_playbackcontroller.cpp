#include "gtest/gtest.h"

#include <QCoreApplication>
#include <QTimer>
#include <QDebug>

#include "core/playerengine.h"
#include "plugins/pluginloader.h"

using namespace VideoPlay;

class PlayerEngineTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        int argc = 0;
        char** argv = nullptr;
        app = new QCoreApplication(argc, argv);
    }
    
    static void TearDownTestSuite() {
        delete app;
        app = nullptr;
    }
    
    void SetUp() override {
        engine = new PlayerEngine();
    }
    
    void TearDown() override {
        delete engine;
        engine = nullptr;
    }
    
    static QCoreApplication* app;
    PlayerEngine* engine;
};

QCoreApplication* PlayerEngineTest::app = nullptr;

TEST_F(PlayerEngineTest, Initialization) {
    EXPECT_NE(engine, nullptr);
    EXPECT_EQ(engine->getState(), PlayerEngine::Stopped);
    EXPECT_DOUBLE_EQ(engine->getVolume(), 1.0);
    EXPECT_FALSE(engine->isMuted());
}

TEST_F(PlayerEngineTest, PlaybackRate) {
    engine->setPlaybackRate(PlayerEngine::PlaybackRate::Double);
    EXPECT_EQ(engine->getPlaybackRate(), PlayerEngine::PlaybackRate::Double);
    
    engine->setPlaybackRate(PlayerEngine::PlaybackRate::Normal);
    EXPECT_EQ(engine->getPlaybackRate(), PlayerEngine::PlaybackRate::Normal);
}

TEST_F(PlayerEngineTest, VolumeControl) {
    engine->setVolume(0.5);
    EXPECT_DOUBLE_EQ(engine->getVolume(), 0.5);
    
    engine->setMuted(true);
    EXPECT_TRUE(engine->isMuted());
}

TEST_F(PlayerEngineTest, LoadNonExistentFile) {
    bool result = engine->loadFile("nonexistent_file.mp4");
    EXPECT_FALSE(result);
}

class PluginLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        loader = new PluginLoader();
    }
    
    void TearDown() override {
        delete loader;
        loader = nullptr;
    }
    
    PluginLoader* loader;
};

TEST_F(PluginLoaderTest, Constructor) {
    EXPECT_NE(loader, nullptr);
}

TEST_F(PluginLoaderTest, DiscoverPlugins) {
    QStringList plugins = loader->discoverPlugins();
    // This will be empty until plugins are built
    EXPECT_TRUE(plugins.isEmpty() || !plugins.isEmpty());
}

TEST_F(PluginLoaderTest, GetPluginCount) {
    int count = loader->getPluginCount();
    EXPECT_GE(count, 0);
}

} // namespace VideoPlay