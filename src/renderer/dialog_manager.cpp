#include "renderer/dialog_manager.h"
#include "renderer/sdlrenderer.h"

#define NOMINMAX

#include <SDL3/SDL.h>

namespace VideoPlay {

DialogManager::DialogManager(SDLRenderer* renderer) : m_sdlRenderer(renderer) {
}

DialogManager::~DialogManager() {
    m_messageBox.reset();
}

void DialogManager::showMessageBox(const std::string& title, const std::string& message,
                                   bool isError,
                                   std::function<void(int64_t timestampMs)> timestampCallback) {
    if (!m_messageBox) {
        m_messageBox = std::make_unique<CustomMessageBox>(m_sdlRenderer->getWindow(), m_sdlRenderer->getFont());
    }
    m_messageBox->show(title, message, isError, std::move(timestampCallback));
}

void DialogManager::openSubtitleDialog(std::function<void(const std::string&)> callback) {
    {
        std::lock_guard<std::mutex> lock(m_dialogMutex);
        m_dialogResultReady = false;
        m_pendingDialogResult.clear();
        m_dialogCallback = std::move(callback);
    }

    auto sdlCallback = [](void* userdata, const char* const* filelist, int /*filter*/) {
        auto* dialogManager = static_cast<DialogManager*>(userdata);
        std::lock_guard<std::mutex> lock(dialogManager->m_dialogMutex);
        if (filelist && filelist[0]) {
            dialogManager->m_pendingDialogResult = filelist[0];
        }
        dialogManager->m_dialogResultReady = true;
    };

    SDL_DialogFileFilter sdlFilters[] = {
        { "字幕文件", "srt;ass;ssa;vtt" }
    };

    SDL_ShowOpenFileDialog(sdlCallback, this, nullptr, sdlFilters, 1, nullptr, false);
}

void DialogManager::openFolderDialog(std::function<void(const std::string&)> callback) {
    {
        std::lock_guard<std::mutex> lock(m_dialogMutex);
        m_dialogResultReady = false;
        m_pendingDialogResult.clear();
        m_dialogCallback = std::move(callback);
    }

    auto sdlCallback = [](void* userdata, const char* const* filelist, int /*filter*/) {
        auto* dialogManager = static_cast<DialogManager*>(userdata);
        std::lock_guard<std::mutex> lock(dialogManager->m_dialogMutex);
        if (filelist && filelist[0]) {
            dialogManager->m_pendingDialogResult = filelist[0];
        }
        dialogManager->m_dialogResultReady = true;
    };

    SDL_ShowOpenFolderDialog(sdlCallback, this, nullptr, nullptr, false);
}

void DialogManager::openFileDialog(std::function<void(const std::string&)> callback, const std::vector<std::string>& /*filters*/) {
    {
        std::lock_guard<std::mutex> lock(m_dialogMutex);
        m_dialogResultReady = false;
        m_pendingDialogResult.clear();
        m_dialogCallback = std::move(callback);
    }

    auto sdlCallback = [](void* userdata, const char* const* filelist, int /*filter*/) {
        auto* dialogManager = static_cast<DialogManager*>(userdata);
        std::lock_guard<std::mutex> lock(dialogManager->m_dialogMutex);
        if (filelist && filelist[0]) {
            dialogManager->m_pendingDialogResult = filelist[0];
        }
        dialogManager->m_dialogResultReady = true;
    };

    SDL_DialogFileFilter sdlFilters[] = {
        { "媒体文件", "mp4;mkv;avi;mov;wmv;flv;webm;m4v;ts;m2ts;mpeg;mpg;vob;3gp;ogv;asf;rm;rmvb;mp3;aac;wav;flac;ogg;m4a;wma;opus;ape;ac3;dts;eac3;wv;weba;srt;ass;ssa;vtt" }
    };

    SDL_ShowOpenFileDialog(sdlCallback, this, nullptr, sdlFilters, 1, nullptr, false);
}

void DialogManager::processPendingResult() {
    std::lock_guard<std::mutex> lock(m_dialogMutex);
    if (m_dialogResultReady) {
        if (m_dialogCallback) {
            m_dialogCallback(m_pendingDialogResult);
        }
        m_dialogResultReady = false;
        m_pendingDialogResult.clear();
        m_dialogCallback = nullptr;
    }
}

} // namespace VideoPlay
