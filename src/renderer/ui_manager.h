#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace VideoPlay {

class SDLRenderer;

class UIManager {
public:
    explicit UIManager(SDLRenderer* renderer);
    ~UIManager() = default;

    void renderUI(int64_t position, int64_t duration, int volume, bool isMuted,
                  bool isPlaying, double speed, const std::string& filename,
                  const std::string& subtitle = {},
                  const std::vector<std::string>& playlist = {}, size_t currentPlaylistIndex = 0,
                  int64_t audioPts = 0, int64_t videoPts = 0, double avDiff = 0.0,
                  bool isPreloading = false);

private:
    SDLRenderer* m_renderer = nullptr;
};

} // namespace VideoPlay
