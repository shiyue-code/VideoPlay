#include <gtest/gtest.h>
#include "renderer/menu_item.h"

using namespace VideoPlay;

TEST(MenuItemTest, BasicProperties) {
    bool triggered = false;
    auto item = MenuItem::create(1, "Open", [&triggered]() { triggered = true; }, "Ctrl+O");

    EXPECT_EQ(item->id, 1);
    EXPECT_EQ(item->getLabel(), "Open");
    EXPECT_EQ(item->shortcut, "Ctrl+O");
    EXPECT_TRUE(item->isEnabled());
    EXPECT_FALSE(item->isChecked());
    EXPECT_FALSE(item->isSeparator);
    EXPECT_FALSE(item->hasChildren());

    if (item->onTrigger) {
        item->onTrigger();
    }
    EXPECT_TRUE(triggered);
}

TEST(MenuItemTest, DynamicLabel) {
    std::string currentAlgo = "Hable";
    auto item = MenuItem::createDynamic([&currentAlgo]() {
        return "调色算法（" + currentAlgo + "）";
    });

    EXPECT_EQ(item->getLabel(), "调色算法（Hable）");
    currentAlgo = "Mobius";
    EXPECT_EQ(item->getLabel(), "调色算法（Mobius）");
}

TEST(MenuItemTest, DynamicCheckedAndEnabled) {
    int loopMode = 0;
    bool canPlay = false;

    auto item = MenuItem::create("列表循环")
        ->setChecked([&loopMode]() { return loopMode == 2; })
        ->setEnabled([&canPlay]() { return canPlay; });

    EXPECT_FALSE(item->isChecked());
    EXPECT_FALSE(item->isEnabled());

    loopMode = 2;
    canPlay = true;

    EXPECT_TRUE(item->isChecked());
    EXPECT_TRUE(item->isEnabled());
}

TEST(MenuItemTest, HierarchyAndChildren) {
    auto root = MenuItem::create("画面");
    auto toneMap = MenuItem::create("HDR 色调映射");
    auto algoMenu = MenuItem::create("调色算法");

    algoMenu->addChild(MenuItem::create("Hable (推荐)"));
    algoMenu->addChild(MenuItem::create("Mobius"));
    algoMenu->addChild(MenuItem::create("Reinhard"));

    toneMap->addChild(MenuItem::create("自动 (推荐)"));
    toneMap->addChild(MenuItem::create("开启"));
    toneMap->addChild(MenuItem::create("关闭"));
    toneMap->addChild(algoMenu);

    root->addChild(toneMap);

    EXPECT_TRUE(root->hasChildren());
    EXPECT_EQ(root->children.size(), 1u);

    EXPECT_TRUE(root->children[0]->hasChildren());
    EXPECT_EQ(root->children[0]->children.size(), 4u);

    EXPECT_TRUE(root->children[0]->children[3]->hasChildren());
    EXPECT_EQ(root->children[0]->children[3]->children.size(), 3u);
    EXPECT_EQ(root->children[0]->children[3]->children[0]->getLabel(), "Hable (推荐)");
}

TEST(MenuItemTest, Separator) {
    auto sep = MenuItem::separator();
    EXPECT_TRUE(sep->isSeparator);
    EXPECT_FALSE(sep->hasChildren());
}

