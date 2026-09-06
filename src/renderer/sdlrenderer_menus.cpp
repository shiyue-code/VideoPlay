#include "renderer/sdlrenderer.h"

namespace VideoPlay {

void SDLRenderer::initMenus() {
    if (m_menuManager) {
        m_menuManager->initMenus();
    }
}

void SDLRenderer::updateTrackMenus() {
    if (m_menuManager) {
        m_menuManager->updateTrackMenus();
    }
}

void SDLRenderer::renderMenuBar() {
    if (m_menuManager) {
        m_menuManager->renderMenuBar();
    }
}

void SDLRenderer::renderContextMenu() {
    if (m_menuManager) {
        m_menuManager->renderContextMenu();
    }
}

bool SDLRenderer::handleMenuClick(int x, int y) {
    if (m_menuManager) {
        return m_menuManager->handleMenuClick(x, y);
    }
    return false;
}

bool SDLRenderer::handleContextMenuClick(int x, int y) {
    if (m_menuManager) {
        return m_menuManager->handleContextMenuClick(x, y);
    }
    return false;
}

void SDLRenderer::closeAllMenus(bool animate) {
    if (m_menuManager) {
        m_menuManager->closeAllMenus(animate);
    }
}

void SDLRenderer::closeMenu() {
    if (m_menuManager) {
        m_menuManager->closeAllMenus(true);
    }
}

void SDLRenderer::closeContextMenu() {
    if (m_menuManager) {
        m_menuManager->hideContextMenu();
    }
}

bool SDLRenderer::isMenuOpen() const {
    return m_menuManager && m_menuManager->isMenuOpen();
}

bool SDLRenderer::isContextMenuOpen() const {
    return m_menuManager && m_menuManager->isContextMenuOpen();
}

bool SDLRenderer::isTopMenuVisible(size_t index) const {
    if (m_menuManager) {
        return m_menuManager->isTopMenuVisible(index);
    }
    return true;
}

void SDLRenderer::showContextMenu(int x, int y) {
    if (m_menuManager) {
        m_menuManager->showContextMenu(x, y);
    }
}

void SDLRenderer::hideContextMenu() {
    if (m_menuManager) {
        m_menuManager->hideContextMenu();
    }
}

} // namespace VideoPlay
