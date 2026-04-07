# VideoPlay Agent Development Guide

This file provides essential information for agents working on this codebase.

**Platform**: Windows + MSVC2019 + Qt 6.7.3

## Build Commands

### Configure
```bash
cmake -B build -G "Visual Studio 16 2019" -A x64 \
  -DCMAKE_PREFIX_PATH="D:/Qt/6.7.3/msvc2019_64;D:/ffmpeg/ffmpeg-master-latest-win64-gpl-shared" \
  -DFFmpeg_ROOT="D:/ffmpeg/ffmpeg-master-latest-win64-gpl-shared"
```

### Build
```bash
cmake --build build --config Release --target VideoPlay
```

### Run
```bash
start build/bin/Release/VideoPlay.exe
```

### Post-Build DLL Copy (MANDATORY)
CMake automatic DLL copying does NOT work reliably with MSVC. After every build:
```powershell
# Qt DLLs
Copy-Item D:/Qt/6.7.3/msvc2019_64/bin/Qt6*.dll build/bin/Release/
# FFmpeg DLLs (if using)
Copy-Item D:/ffmpeg/ffmpeg-master-latest-win64-gpl-shared/bin/*.dll build/bin/Release/
# Qt Plugins
New-Item -ItemType Directory -Force build/bin/Release/platforms
Copy-Item D:/Qt/6.7.3/msvc2019_64/plugins/platforms/qwindows.dll build/bin/Release/platforms/
```

### Tests
```bash
cd build
ctest -C Release --output-on-failure
```
**Note**: Tests are currently broken (linkage issues). `tests/CMakeLists.txt` references non-existent `video_core` and `video_plugins` targets. Fix by linking against `VideoPlay` target instead.

## Code Organization

```
src/
├── main.cpp                     # Entry point (sets QT_MEDIA_BACKEND=windows)
├── core/
│   ├── playerengine.h/cpp       # QMediaPlayer/QAudioOutput wrapper
│   ├── settings.h/cpp           # Singleton QSettings wrapper
│   └── common.h                 # Enums, formatTime() helper
├── ui/
│   ├── mainwindow.h/cpp         # Main window, menus, drag-drop
│   ├── controls.h/cpp           # Playback bar (play/pause/seek/volume/speed)
│   ├── videowidget.h/cpp        # Video rendering (QVideoSink -> QImage)
│   └── playlistwidget.h/cpp     # Playlist management
├── subtitles/
│   ├── subtitleparser.h/cpp     # SRT/ASS/VTT parsing
│   └── subtitleoverlay.h/cpp    # Subtitle rendering overlay
└── utils/
    └── logger.h/cpp             # Singleton file/console logger
```

## Code Style

- **Classes**: `PascalCase` (e.g., `PlayerEngine`)
- **Methods**: `camelCase` (e.g., `loadFile()`)
- **Variables**: `camelCase`
- **Members**: `m_` prefix (e.g., `m_engine`)
- **Constants**: `kPascalCase` or `SCREAMING_SNAKE_CASE`
- **Enums**: `PascalCase` values (e.g., `PlaybackState::Playing`)
- **Indentation**: 4 spaces (no tabs)
- **Braces**: Allman style (opening brace on new line)
- **Max line length**: ~100 characters

### Include Order
```cpp
#include "own_header.h"     // Project headers first
#include <QtHeader>         // Qt headers (angle brackets)
#include "other/module.h"   // Other project headers
```

### Signal-Slot
Use new syntax only: `connect(sender, &Sender::signal, receiver, &Receiver::slot);`

## Critical Gotchas

1. **Media Backend**: `QT_MEDIA_BACKEND=windows` is set in `main.cpp`. Do NOT remove.
2. **Subtitle Overlay on Windows**: `QVideoWidget` creates a native window (HWND) that always renders on top of Qt child widgets. The `SubtitleOverlay` must be a **separate top-level window** with `Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint` flags, positioned to match the video container via `mapToGlobal()`.
3. **QStackedLayout StackAll does NOT work** with `QVideoWidget` on Windows due to native window z-order.
4. **Progress Bar Seek**: Seek only on `sliderReleased`, not during drag. The slider's `sliderPressed`/`sliderReleased` block position updates during drag.
5. **Path Handling**: Always use `QUrl::fromLocalFile()` for file paths. Use `QFileInfo` for absolute paths.
6. **Adding new files**: `CMakeLists.txt` explicitly lists all sources (no `file(GLOB)`). You must manually add `.h` and `.cpp` to both `SOURCES` and `HEADERS` lists.
7. **FFmpeg is optional**: Guard FFmpeg-specific code with `#ifdef HAS_FFMPEG`.

## Memory Management
- Use Qt parent hierarchy: pass `this` as parent to child widgets/objects.
- No manual `delete` needed for parented objects.

## Error Handling
- Emit signals for errors: `PlayerEngine::errorOccurred(QString)`
- Log via `Logger::instance().error()`
- Show `QMessageBox::critical` in `main.cpp` for fatal errors.

## Debugging
- Build Debug config for debugging
- Use `qDebug()` for console output
- Use `Logger::instance().debug()` for file logs (`%APPDATA%/VideoPlay/logs/`)
