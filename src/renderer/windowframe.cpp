#include "renderer/windowframe.h"

#ifdef _WIN32
#include "renderer/windowframe_win32.h"
#else
#include "renderer/windowframe_linux.h"
#endif

namespace VideoPlay {

std::unique_ptr<WindowFrame> WindowFrame::create() {
#ifdef _WIN32
    return std::make_unique<WindowFrameWin32>();
#else
    return std::make_unique<WindowFrameLinux>();
#endif
}

} // namespace VideoPlay
