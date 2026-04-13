#include "app.h"
#include "utils/logger.h"
#include <iostream>

int main(int argc, char* argv[]) {
    VideoPlay::Logger::instance().info("VideoPlay v" APP_VERSION " starting...");
    
    try {
        VideoPlay::VideoPlayerApp app;
        return app.run(argc, argv);
    } catch (const std::exception& e) {
        VideoPlay::Logger::instance().error("Fatal error: " + std::string(e.what()));
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
