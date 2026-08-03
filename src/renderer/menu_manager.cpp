#include "renderer/menu_manager.h"
#include "renderer/sdlrenderer.h"

#define NOMINMAX

namespace VideoPlay {

MenuManager::MenuManager(SDLRenderer* renderer) : m_renderer(renderer) {
}

void MenuManager::initMenus() {
    m_renderer->initMenus();
}

void MenuManager::updateTrackMenus() {
    m_renderer->updateTrackMenus();
}

void MenuManager::updateChapterMenuItems() {
    m_renderer->updateChapterMenuItems();
}

void MenuManager::updateRecentFilesMenu() {
    m_renderer->updateRecentFilesMenuImpl();
}

void MenuManager::renderMenuBar() {
    m_renderer->renderMenuBar();
}

void MenuManager::renderContextMenu() {
    m_renderer->renderContextMenu();
}

bool MenuManager::isMenuOpen() const {
    return m_renderer->isMenuOpen();
}

bool MenuManager::isTopMenuVisible(size_t index) const {
    return m_renderer->isTopMenuVisible(index);
}

bool MenuManager::handleMenuClick(int x, int y) {
    return m_renderer->handleMenuClick(x, y);
}

bool MenuManager::handleContextMenuClick(int x, int y) {
    return m_renderer->handleContextMenuClick(x, y);
}

void MenuManager::closeAllMenus(bool animate) {
    m_renderer->closeAllMenus(animate);
}

void MenuManager::updateMenuAnimation() {
    m_renderer->updateMenuAnimation();
}

void MenuManager::showContextMenu(int x, int y) {
    m_renderer->showContextMenu(x, y);
}

void MenuManager::hideContextMenu() {
    m_renderer->hideContextMenu();
}

} // namespace VideoPlay
