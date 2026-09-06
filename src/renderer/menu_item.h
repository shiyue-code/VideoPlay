#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace VideoPlay {

enum class MenuId;

class MenuItem;
using MenuItemPtr = std::shared_ptr<MenuItem>;

class MenuItem : public std::enable_shared_from_this<MenuItem> {
public:
    using Action = std::function<void()>;
    using LabelSupplier = std::function<std::string()>;
    using BoolSupplier = std::function<bool()>;

    int id = 0;                              // 兼容旧 MenuId 枚举值
    std::string staticLabel;                // 静态文本
    LabelSupplier dynamicLabel = nullptr;   // 动态文本回调（用于“调色算法(Hable)”等随状态变化的项）
    std::string shortcut;                   // 快捷键文本（如 "Ctrl+O"）
    BoolSupplier checkedSupplier = nullptr; // 动态勾选状态
    BoolSupplier enabledSupplier = nullptr; // 动态可用状态
    Action onTrigger = nullptr;             // 动作回调
    bool isSeparator = false;               // 分隔线标记

    std::vector<MenuItemPtr> children;      // 子菜单项列表（天然支持无限级级联）

    MenuItem() = default;

    // 工厂方法
    static MenuItemPtr create(const std::string& label, Action action = nullptr, const std::string& shortcut = "") {
        auto item = std::make_shared<MenuItem>();
        item->staticLabel = label;
        item->onTrigger = std::move(action);
        item->shortcut = shortcut;
        return item;
    }

    static MenuItemPtr create(int id, const std::string& label, Action action = nullptr, const std::string& shortcut = "") {
        auto item = create(label, std::move(action), shortcut);
        item->id = id;
        return item;
    }

    static MenuItemPtr create(MenuId menuId, const std::string& label, Action action = nullptr, const std::string& shortcut = "") {
        return create(static_cast<int>(menuId), label, std::move(action), shortcut);
    }

    static MenuItemPtr createDynamic(LabelSupplier supplier, Action action = nullptr, const std::string& shortcut = "") {
        auto item = std::make_shared<MenuItem>();
        item->dynamicLabel = std::move(supplier);
        item->onTrigger = std::move(action);
        item->shortcut = shortcut;
        return item;
    }

    static MenuItemPtr createDynamic(int id, LabelSupplier supplier, Action action = nullptr, const std::string& shortcut = "") {
        auto item = createDynamic(std::move(supplier), std::move(action), shortcut);
        item->id = id;
        return item;
    }

    static MenuItemPtr createDynamic(MenuId menuId, LabelSupplier supplier, Action action = nullptr, const std::string& shortcut = "") {
        return createDynamic(static_cast<int>(menuId), std::move(supplier), std::move(action), shortcut);
    }

    static MenuItemPtr separator() {
        auto item = std::make_shared<MenuItem>();
        item->isSeparator = true;
        return item;
    }

    // 链式配置方法
    MenuItemPtr addChild(MenuItemPtr child) {
        if (child) {
            children.push_back(std::move(child));
        }
        return shared_from_this();
    }

    MenuItemPtr setChecked(BoolSupplier supplier) {
        checkedSupplier = std::move(supplier);
        return shared_from_this();
    }

    MenuItemPtr setEnabled(BoolSupplier supplier) {
        enabledSupplier = std::move(supplier);
        return shared_from_this();
    }

    MenuItemPtr setAction(Action action) {
        onTrigger = std::move(action);
        return shared_from_this();
    }

    // 属性访问器
    std::string getLabel() const {
        if (dynamicLabel) {
            return dynamicLabel();
        }
        return staticLabel;
    }

    bool isChecked() const {
        if (checkedSupplier) {
            return checkedSupplier();
        }
        return false;
    }

    bool isEnabled() const {
        if (enabledSupplier) {
            return enabledSupplier();
        }
        return true;
    }

    bool hasChildren() const {
        return !children.empty();
    }
};

} // namespace VideoPlay

