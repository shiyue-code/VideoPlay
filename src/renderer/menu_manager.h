#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace VideoPlay {

class SDLRenderer;

class MenuManager {
public:
    explicit MenuManager(SDLRenderer* renderer);
    ~MenuManager() = default;

    void initMenus();
    void updateTrackMenus();
    void updateChapterMenuItems();
    void updateRecentFilesMenu();

    void renderMenuBar();
    void renderContextMenu();
    bool isMenuOpen() const;
    bool isTopMenuVisible(size_t index) const;
    bool handleMenuClick(int x, int y);
    bool handleContextMenuClick(int x, int y);
    void closeAllMenus(bool animate = true);
    void updateMenuAnimation();
    void showContextMenu(int x, int y);
    void hideContextMenu();

private:
    SDLRenderer* m_renderer = nullptr;
};

} // namespace VideoPlay
