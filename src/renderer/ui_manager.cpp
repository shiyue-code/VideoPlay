#include "renderer/ui_manager.h"
#include "renderer/sdlrenderer.h"

#define NOMINMAX

namespace VideoPlay {

UIManager::UIManager(SDLRenderer* renderer) : m_renderer(renderer) {
}

void UIManager::renderUI(int64_t position, int64_t duration, int volume, bool isMuted,
                         bool isPlaying, double speed, const std::string& filename,
                         const std::string& subtitle,
                         const std::vector<std::string>& playlist, size_t currentPlaylistIndex,
                         int64_t audioPts, int64_t videoPts, double avDiff,
                         bool isPreloading) {
    m_renderer->renderUIImpl(position, duration, volume, isMuted, isPlaying, speed,
                             filename, subtitle, playlist, currentPlaylistIndex,
                             audioPts, videoPts, avDiff, isPreloading);
}

} // namespace VideoPlay
