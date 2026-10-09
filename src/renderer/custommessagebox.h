#ifndef CUSTOMMESSAGEBOX_H
#define CUSTOMMESSAGEBOX_H

#include "renderer/modaldialog.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <functional>
#include <vector>
#include <cstdint>

namespace VideoPlay {

class CustomMessageBox : public ModalDialog {
public:
    using TimestampClickCallback = std::function<void(int64_t timestampMs)>;

    CustomMessageBox(SDL_Window* parentWindow, TTF_Font* font);
    ~CustomMessageBox() override = default;

    void show(const std::string& title, const std::string& message, bool isError = false,
              TimestampClickCallback timestampCallback = nullptr);

private:
    std::string m_message;
    TimestampClickCallback m_timestampClickCallback;
    bool m_isError = false;

    struct TimestampRect {
        SDL_Rect rect;
        int64_t timestampMs = 0;
    };
    std::vector<TimestampRect> m_timestampRects;

    bool onEvent(const SDL_Event& event) override;
    void renderContent() override;
    void calculateSize();
};

} // namespace VideoPlay

#endif // CUSTOMMESSAGEBOX_H
