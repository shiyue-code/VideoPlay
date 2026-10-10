#pragma once

#include "renderer/inputdialog.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace VideoPlay {

class SDLRenderer;

class DialogManager {
public:
    explicit DialogManager(SDLRenderer* renderer);
    ~DialogManager();

    void openFileDialog(std::function<void(const std::string&)> callback, const std::vector<std::string>& filters = {});
    void openSubtitleDialog(std::function<void(const std::string&)> callback);
    void openFolderDialog(std::function<void(const std::string&)> callback);
    void showInputDialog(const std::string& title, const std::string& placeholder,
                         std::function<void(const std::string&)> callback);
    void processPendingResult();

private:
    SDLRenderer* m_sdlRenderer = nullptr;

    std::unique_ptr<InputDialog> m_inputDialog;
    std::mutex m_dialogMutex;
    std::string m_pendingDialogResult;
    bool m_dialogResultReady = false;
    std::function<void(const std::string&)> m_dialogCallback;
};

} // namespace VideoPlay
