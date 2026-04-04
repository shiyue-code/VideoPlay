# VideoPlay Development Guide

This file provides essential information for agents working on this codebase.

## Build Commands

### Quick Build (Windows with MSVC)
```bash
# Configure
cmake -B build -G "Visual Studio 16 2019" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt/6.7.3/msvc2019_64;D:/ffmpeg/ffmpeg-master-latest-win64-gpl-shared" -DFFmpeg_ROOT="D:/ffmpeg/ffmpeg-master-latest-win64-gpl-shared"

# Build Release
cmake --build build --config Release

# Build Debug
cmake --build build --config Debug
```

### Single File Rebuild
After editing a source file, rebuild just the affected target:
```bash
cmake --build build --config Release --target VideoPlay
```

### Clean Build
```bash
rm -rf build
cmake -B build -G "Visual Studio 16 2019" -A x64 ...
cmake --build build --config Release
```

### Post-Build DLL Copy (Required for Running)
After each build, you MUST copy required DLLs manually:

1. **Qt DLLs**:
```bash
cp D:/Qt/6.7.3/msvc2019_64/bin/Qt6*.dll build/bin/Release/
```

2. **FFmpeg DLLs**:
```bash
cp D:/ffmpeg/ffmpeg-master-latest-win64-gpl-shared/bin/*.dll build/bin/Release/
```

3. **Qt Plugins**:
```bash
mkdir -p build/bin/Release/platforms
cp D:/Qt/6.7.3/msvc2019_64/plugins/platforms/qwindows.dll build/bin/Release/platforms/

mkdir -p build/bin/Release/multimedia
cp D:/Qt/6.7.3/msvc2019_64/plugins/multimedia/*.dll build/bin/Release/multimedia/
```

**Note**: CMake automatic DLL copying doesn't work reliably with MSVC generator on Windows. Manual copy is required after each build.

### Run the Application
```bash
cd build/bin/Release
start VideoPlay.exe
# or
cmd.exe /c "start VideoPlay.exe"
```

## Code Style Guidelines

### General
- **Language**: C++17 with Qt6
- **Encoding**: UTF-8
- **Line endings**: LF (Unix-style)

### Naming Conventions
- **Classes**: `PascalCase` (e.g., `PlayerEngine`, `MainWindow`)
- **Methods/Functions**: `camelCase` (e.g., `loadFile()`, `seekToPosition()`)
- **Variables**: `camelCase` (e.g., `m_mediaPlayer`, `m_volume`)
- **Member variables**: Prefix with `m_` (e.g., `m_engine`, `m_controls`)
- **Constants**: `kPascalCase` or `SCREAMING_SNAKE_CASE`
- **Enums**: `PascalCase` with values in `PascalCase` or `SCREAMING_SNAKE_CASE`

### File Organization
- Header files: `.h` extension
- Implementation files: `.cpp` extension
- One class per file (unless tightly coupled)
- Include order:
  1. Project headers (quotes)
  2. Qt headers (angle brackets)
  3. System headers (angle brackets)

```cpp
#include "playerengine.h"

#include <QApplication>
#include <QMenuBar>
#include <QDebug>

#include "common.h"
```

### Qt-Specific Guidelines

#### Signals and Slots
- Use new Qt5+ syntax (not old `SIGNAL`/`SLOT` macros):
```cpp
connect(m_engine, &PlayerEngine::stateChanged, this, &MainWindow::onStateChanged);
```

#### Memory Management
- Use parent pointers for widget ownership
- Use smart pointers (`QScopedPointer`, `std::unique_ptr`) for non-parented objects
- Avoid raw `new`/`delete` when possible

#### Qt Types
- Use Qt types: `QString`, `qint64`, `QList`, `QVector`, etc.
- Use `Q_NULLPTR` instead of `NULL`
- Use `override` specifier for virtual overrides

### Error Handling
- Use `qDebug()` for debug output
- Use `qWarning()` for warnings
- Use `qCritical()` for critical errors
- Use signals for error reporting to UI:
```cpp
emit errorOccurred(tr("Failed to load file"));
```

### Formatting
- Indentation: 4 spaces (no tabs)
- Braces: Allman style (opening brace on new line)
- Maximum line length: 100 characters
- Add spaces around operators: `a + b` not `a+b`

### Namespace Usage
- Core classes in `VideoPlay` namespace
- UI components may use global namespace
- Keep namespace usage consistent within files

## Project Structure

```
VideoPlay/
├── CMakeLists.txt           # Main build config
├── src/
│   ├── main.cpp            # Entry point
│   ├── mainwindow.h/cpp   # Main window
│   ├── playerengine.h/cpp # Media player engine
│   ├── controls.h/cpp     # Playback controls
│   ├── videowidget.h/cpp  # Video display
│   ├── playlistwidget.h/cpp # Playlist
│   ├── subtitleparser.h/cpp # Subtitle parsing
│   └── common.h            # Common definitions
├── 3rdparty/
│   └── qlementine/        # Qlementine style library
└── build/                  # Build output
    └── bin/Release/       # Executable
```

## Common Development Tasks

### Adding New Source Files
1. Add `.h` and `.cpp` files to `src/`
2. Add files to `SOURCES` and `HEADERS` in `CMakeLists.txt`
3. Run CMake configure and build

### Testing
- Build and run the application
- Test video playback with various formats
- Test keyboard shortcuts
- Check for memory leaks

### Debugging
```bash
# Build Debug version
cmake --build build --config Debug

# Run with output
cd build/bin/Debug
VideoPlay.exe
```

## Notes
- This project uses Qt6 Multimedia with Windows backend
- Qlementine style is integrated from `3rdparty/qlementine`
- FFmpeg DLLs are required at runtime
- Qt platform plugins must be in `platforms/` subdirectory
