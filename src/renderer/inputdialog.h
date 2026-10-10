#ifndef INPUTDIALOG_H
#define INPUTDIALOG_H

#include "renderer/modaldialog.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <functional>
#include <memory>
#include <string>

namespace VideoPlay {

class InputField;

// 单行文本输入对话框：独立无边框窗口模态运行。
// 确定/回车返回输入文本，取消/Esc 返回空串（通过回调区分）。
class InputDialog : public ModalDialog {
public:
    InputDialog(SDL_Window* parentWindow, TTF_Font* font, TTF_Font* titleFont);
    ~InputDialog() override = default;

    // 阻塞显示对话框；确定时以输入文本调用 callback，取消时调用 callback("")
    void show(const std::string& title, const std::string& placeholder,
              const std::string& initialValue,
              std::function<void(const std::string&)> callback);

private:
    std::unique_ptr<InputField> m_input;
    std::function<void(const std::string&)> m_callback;
    bool m_accepted = false;

    bool onEvent(const SDL_Event& event) override;
    void renderContent() override;
    void finish(bool accepted);
};

} // namespace VideoPlay

#endif // INPUTDIALOG_H
