@echo off
REM VideoPlay 部署脚本 - 构建后运行此脚本复制所有依赖

set APP_DIR=%~dp0bin\Release
set QT_DIR=D:\Qt\6.7.3\msvc2019_64
set FFMPEG_DIR=D:\ffmpeg\ffmpeg-master-latest-win64-gpl-shared

echo 正在复制依赖到 %APP_DIR%...

REM 复制 Qt DLLs
echo 复制 Qt 核心库...
copy /Y "%QT_DIR%\bin\Qt6Core.dll" "%APP_DIR%\" 2>nul
copy /Y "%QT_DIR%\bin\Qt6Gui.dll" "%APP_DIR%\" 2>nul
copy /Y "%QT_DIR%\bin\Qt6Widgets.dll" "%APP_DIR%\" 2>nul
copy /Y "%QT_DIR%\bin\Qt6Multimedia.dll" "%APP_DIR%\" 2>nul
copy /Y "%QT_DIR%\bin\Qt6Network.dll" "%APP_DIR%\" 2>nul

REM 复制 Qt 平台插件
echo 复制 Qt 平台插件...
if not exist "%APP_DIR%\platforms" mkdir "%APP_DIR%\platforms"
copy /Y "%QT_DIR%\plugins\platforms\qwindows.dll" "%APP_DIR%\platforms\" 2>nul

REM 复制 Qt 多媒体插件
echo 复制 Qt 多媒体插件...
if not exist "%APP_DIR%\multimedia" mkdir "%APP_DIR%\multimedia"
copy /Y "%QT_DIR%\plugins\multimedia\*.dll" "%APP_DIR%\multimedia\" 2>nul

REM 复制 FFmpeg DLLs
echo 复制 FFmpeg 库...
if exist "%FFMPEG_DIR%\bin" (
    xcopy /Y /Q "%FFMPEG_DIR%\bin\*.*" "%APP_DIR%\" 2>nul
)

echo 完成! 所有依赖已复制到 %APP_DIR%
pause
