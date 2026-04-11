cmake_minimum_required(VERSION 3.16)
project(VideoPlay VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 启用Qt自动功能
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 添加 Qlementine 本地库
add_subdirectory(3rdparty/qlementine)

# 添加 ElaWidgetTools
set(ELAWIDGETTOOLS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QT_SDK_DIR "D:/Qt/6.7.3/msvc2019_64" CACHE PATH "QT SDK DIR" FORCE)
# 注释掉版本检查（因为我们使用 Qt 6.7.3）
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(3rdparty/elawidgettools)

# 查找Qt6 (QAudioSink 需要 Multimedia)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui OpenGL OpenGLWidgets Multimedia)

# 查找FFmpeg (通过FIND包或FFmpeg_ROOT)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavcodec libavformat libavutil libswscale libswresample)
endif()

if(NOT FFMPEG_FOUND AND DEFINED FFmpeg_ROOT)
    # 手动设置FFmpeg路径
    find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
        PATHS ${FFmpeg_ROOT}/include
        NO_DEFAULT_PATH)
    find_library(FFMPEG_AVCODEC_LIB avcodec
        PATHS ${FFmpeg_ROOT}/lib
        NO_DEFAULT_PATH)
    find_library(FFMPEG_AVFORMAT_LIB avformat
        PATHS ${FFmpeg_ROOT}/lib
        NO_DEFAULT_PATH)
    find_library(FFMPEG_AVUTIL_LIB avutil
        PATHS ${FFmpeg_ROOT}/lib
        NO_DEFAULT_PATH)
    find_library(FFMPEG_SWSCALE_LIB swscale
        PATHS ${FFmpeg_ROOT}/lib
        NO_DEFAULT_PATH)
    find_library(FFMPEG_SWRESAMPLE_LIB swresample
        PATHS ${FFmpeg_ROOT}/lib
        NO_DEFAULT_PATH)

    if(FFMPEG_INCLUDE_DIR AND FFMPEG_AVCODEC_LIB)
        set(FFMPEG_FOUND TRUE)
        set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
        set(FFMPEG_LIBRARIES
            ${FFMPEG_AVCODEC_LIB}
            ${FFMPEG_AVFORMAT_LIB}
            ${FFMPEG_AVUTIL_LIB}
            ${FFMPEG_SWSCALE_LIB}
            ${FFMPEG_SWRESAMPLE_LIB})
    endif()
endif()

if(FFMPEG_FOUND)
    message(STATUS "FFmpeg found: ${FFMPEG_INCLUDE_DIRS}")
else()
    message(WARNING "FFmpeg not found, some features may be limited")
endif()

# 源文件
set(SOURCES
    src/main.cpp
    src/core/ffmpegplayer.cpp
    src/core/audioplaybackthread.cpp
    src/core/playerengine.cpp
    src/core/settings.cpp
    src/ui/videorenderer.cpp
    src/ui/playlistwidget.cpp
    src/subtitles/subtitleparser.cpp
    src/utils/logger.cpp
    src/ui/ela/ElaVideoWindow.cpp
)

set(HEADERS
    src/core/ffmpegplayer.h
    src/core/audioplaybackthread.h
    src/core/common.h
    src/core/playerengine.h
    src/core/settings.h
    src/ui/videorenderer.h
    src/ui/playlistwidget.h
    src/subtitles/subtitleparser.h
    src/utils/logger.h
    src/ui/ela/ElaVideoWindow.h
)

# 资源文件 (如果存在)
if(EXISTS "${CMAKE_SOURCE_DIR}/resources/icons/app_icon.png")
    set(RESOURCES src/resources.qrc)
endif()

# 创建可执行文件
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/3rdparty/qlementine/lib/include
    ${CMAKE_SOURCE_DIR}/3rdparty/elawidgettools/ElaWidgetTools
)

# Qlementine 链接
target_link_libraries(${PROJECT_NAME} PRIVATE qlementine)

# ElaWidgetTools 链接
target_link_libraries(${PROJECT_NAME} PRIVATE ElaWidgetTools)

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    Qt6::OpenGL
    Qt6::OpenGLWidgets
)

# 链接 Qt6::Multimedia (必需，用于 QAudioSink)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Multimedia)

if(TARGET Qt6::MultimediaWidgets)
    # Qt MultimediaWidgets no longer used
endif()

if(FFMPEG_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_FFMPEG)
endif()

# Windows特定设置
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE dwmapi opengl32)

    # 添加应用程序图标
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/icons/app.ico")
        set(APP_ICON_RC "${CMAKE_BINARY_DIR}/app_icon.rc")
        file(WRITE ${APP_ICON_RC} "IDI_ICON1 ICON DISCARDABLE \"${CMAKE_SOURCE_DIR}/resources/icons/app.ico\"")
        target_sources(${PROJECT_NAME} PRIVATE ${APP_ICON_RC})
    endif()
endif()

# 编译定义
target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    QT_DEPRECATED_WARNINGS
)

# 输出目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 安装规则
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
