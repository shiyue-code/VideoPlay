#pragma once

#include "renderer/menu_item.h"
#include <SDL3/SDL.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace VideoPlay {

class SDLRenderer;

// 级联菜单状态层级（支持无限级展开）
struct MenuCascadeLevel {
    MenuItemPtr parent;    // 当前展开的父节点（其 children 为各项）
    int x = 0;             // 菜单左上角 X
    int y = 0;             // 菜单左上角 Y
    int width = 0;         // 菜单宽度
    int height = 0;        // 菜单高度
    int hoveredIndex = -1; // 当前悬停项索引
};

class MenuManager {
public:
    explicit MenuManager(SDLRenderer* renderer);
    ~MenuManager() = default;

    // 菜单数据初始化与动态更新
    void initMenus();
    void updateTrackMenus();
    void updateChapterMenuItems();
    void updateRecentFilesMenu();

    // 渲染
    void renderMenuBar();
    void renderCascadeMenus();
    void renderContextMenu();
    void updateMenuAnimation();

    // 状态查询
    bool isMenuOpen() const;
    bool isContextMenuOpen() const { return m_showContextMenu; }
    bool isTopMenuVisible(size_t index) const;
    bool isMouseOverMenu(int x, int y) const;
    int getActiveSubmenuId() const;

    // 事件处理
    bool handleMenuClick(int x, int y);
    bool handleContextMenuClick(int x, int y);
    bool handleMouseMove(int x, int y);

    // 显示控制
    void closeAllMenus(bool animate = true);
    void showContextMenu(int x, int y);
    void hideContextMenu();

    // 树形菜单访问
    const std::vector<MenuItemPtr>& getTopMenus() const { return m_topMenus; }
    MenuItemPtr getActiveContextMenu() const { return m_activeContextMenu; }

private:
    SDLRenderer* m_renderer = nullptr;

    // 顶部菜单栏与上下文菜单树
    std::vector<MenuItemPtr> m_topMenus;
    MenuItemPtr m_activeContextMenu;
    MenuItemPtr m_playbackContextMenu;
    MenuItemPtr m_playlistContextMenu;
    int m_contextPlaylistIndex = -1;

    // 级联展开栈（通用任意深度）
    std::vector<MenuCascadeLevel> m_cascadeStack;

    // 顶部菜单栏激活状态
    int m_activeTopMenu = -1;
    int m_pendingTopMenu = -1;

    // 菜单动画
    float m_menuAnimAlpha = 0.0f;
    bool m_menuAnimating = false;
    uint64_t m_menuAnimStartTime = 0;
    static constexpr uint64_t MENU_ANIM_DURATION_MS = 120;

    // 右键菜单状态
    bool m_showContextMenu = false;
    int m_contextMenuX = 0;
    int m_contextMenuY = 0;

    // 内部排版与渲染辅助函数
    int calculateMenuWidth(const MenuItemPtr& parent);
    void renderCascadeLevel(const MenuCascadeLevel& level, float alpha);
    bool updateCascadeHover(int mouseX, int mouseY);
    void openTopMenu(int index);
    void executeItemAction(const MenuItemPtr& item);
};

} // namespace VideoPlay
