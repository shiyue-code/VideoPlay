@echo off
chcp 65001 >nul
echo ==========================================
echo VideoPlay Qt 迁移清理脚本
echo ==========================================
echo.
echo 此脚本将删除旧的 Qt 相关文件
echo 请在确认新架构正常工作后再运行
echo.
pause

echo.
echo [1/4] 删除旧的 Qt UI 文件...
if exist "src\ui\ela" (
    rmdir /s /q "src\ui\ela"
    echo     已删除 src\ui\ela
)
if exist "src\ui\controls.cpp" del "src\ui\controls.cpp"
if exist "src\ui\controls.h" del "src\ui\controls.h"
if exist "src\ui\mainwindow.cpp" del "src\ui\mainwindow.cpp"
if exist "src\ui\mainwindow.h" del "src\ui\mainwindow.h"
if exist "src\ui\playlistwidget.cpp" del "src\ui\playlistwidget.cpp"
if exist "src\ui\playlistwidget.h" del "src\ui\playlistwidget.h"
if exist "src\ui\videorenderer.cpp" del "src\ui\videorenderer.cpp"
if exist "src\ui\videorenderer.h" del "src\ui\videorenderer.h"
if exist "src\ui\videowidget.cpp" del "src\ui\videowidget.cpp"
if exist "src\ui\videowidget.h" del "src\ui\videowidget.h"

echo.
echo [2/4] 删除旧的音频线程文件...
if exist "src\core\audioplaybackthread.cpp" del "src\core\audioplaybackthread.cpp"
if exist "src\core\audioplaybackthread.h" del "src\core\audioplaybackthread.h"
if exist "src\core\playerengine.cpp" del "src\core\playerengine.cpp"
if exist "src\core\playerengine.h" del "src\core\playerengine.h"

echo.
echo [3/4] 删除旧的字幕覆盖层文件...
if exist "src\subtitles\subtitleoverlay.cpp" del "src\subtitles\subtitleoverlay.cpp"
if exist "src\subtitles\subtitleoverlay.h" del "src\subtitles\subtitleoverlay.h"

echo.
echo [4/4] 删除旧的资源文件...
if exist "src\resources.qrc" del "src\resources.qrc"

echo.
echo ==========================================
echo 清理完成！
echo ==========================================
echo.
echo 注意：如果需要完全移除 ElaWidgetTools，请手动删除：
echo   - 3rdparty\elawidgettools 目录
echo   - build 目录（清理缓存）
echo.
pause
