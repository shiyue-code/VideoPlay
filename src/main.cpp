#include "app.h"
#include "core/settings.h"
#include "utils/logger.h"
#include <iostream>

namespace {
VideoPlay::Logger& logger() {
    static auto logger = VideoPlay::Logger::get("main");
    return *logger;
}
}

int main(int argc, char* argv[]) {
    VideoPlay::Logger::root().configure(VideoPlay::Settings::instance().logConfig());
    logger().info("VideoPlay v" APP_VERSION " starting...");
    
    try {
        VideoPlay::VideoPlayerApp app;
        return app.run(argc, argv);
    } catch (const std::exception& e) {
        logger().error("Fatal error: " + std::string(e.what()));
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
