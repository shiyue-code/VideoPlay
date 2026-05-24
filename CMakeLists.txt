cmake_minimum_required(VERSION 3.16)
project(VideoPlay VERSION 2.0.0 LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC 配置
if(MSVC)
    add_compile_options(/utf-8)
    set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Od /MDd /RTC1 /bigobj")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/DEBUG /INCREMENTAL")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /MD /DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/Zi /O2 /MD /DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "/DEBUG /INCREMENTAL:NO /OPT:REF /OPT:ICF")
endif()

# 优先使用项目内置 FFmpeg，仍允许通过 -DFFmpeg_ROOT=... 覆盖
set(VIDEOPLAY_BUNDLED_FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/3rdparty/FFmpeg")
if(NOT DEFINED FFmpeg_ROOT AND EXISTS "${VIDEOPLAY_BUNDLED_FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    set(FFmpeg_ROOT "${VIDEOPLAY_BUNDLED_FFMPEG_ROOT}" CACHE PATH "FFmpeg root directory")
endif()

# 查找 FFmpeg
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavcodec libavformat libavutil libswscale libswresample)
endif()

if(NOT FFMPEG_FOUND AND DEFINED FFmpeg_ROOT)
    unset(FFMPEG_INCLUDE_DIR CACHE)
    unset(FFMPEG_AVCODEC_LIB CACHE)
    unset(FFMPEG_AVFORMAT_LIB CACHE)
    unset(FFMPEG_AVUTIL_LIB CACHE)
    unset(FFMPEG_SWSCALE_LIB CACHE)
    unset(FFMPEG_SWRESAMPLE_LIB CACHE)

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
    message(FATAL_ERROR "FFmpeg is required but not found")
endif()

# 从源码构建 SDL3
if(EXISTS "${CMAKE_SOURCE_DIR}/3rdparty/SDL3/CMakeLists.txt")
    message(STATUS "Building SDL3 from source (submodule)")
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(3rdparty/SDL3 EXCLUDE_FROM_ALL)
    set(SDL3_FOUND TRUE)
else()
    # 尝试查找系统安装的 SDL3
    find_package(SDL3 QUIET CONFIG)
    if(NOT SDL3_FOUND)
        message(FATAL_ERROR "SDL3 is required but not found. Please run: git submodule update --init")
    endif()
endif()

# 从源码构建 SDL3_ttf
if(EXISTS "${CMAKE_SOURCE_DIR}/3rdparty/SDL3_ttf/CMakeLists.txt")
    message(STATUS "Building SDL3_ttf from source (submodule)")
    set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(3rdparty/SDL3_ttf EXCLUDE_FROM_ALL)
    set(SDL3_TTF_FOUND TRUE)
else()
    set(SDL3_TTF_FOUND FALSE)
    message(WARNING "SDL3_ttf not found, text rendering will be disabled")
endif()

# 查找 OpenGL (可选，用于高级渲染)
find_package(OpenGL QUIET)

# 查找 spdlog (可选，用于高性能日志；未安装时使用内置日志实现)
find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
    find_package(spdlog QUIET)
endif()

# HTTP 客户端使用 Windows WinHTTP API (无需额外依赖)

# 源文件
set(SOURCES
    src/main.cpp
    src/app.cpp
    src/app_playback.cpp
    src/app_playlist.cpp
    src/app_events.cpp
    src/core/ffmpegplayer.cpp
    src/core/audioplayer.cpp
    src/core/settings.cpp
    src/core/episodedetector.cpp
    src/subtitles/subtitleparser.cpp
    src/utils/logger.cpp
    src/renderer/sdlrenderer.cpp
    src/renderer/sdlrenderer_events.cpp
    src/renderer/sdlrenderer_menus.cpp
    src/renderer/sdlrenderer_ui.cpp
    src/renderer/sdlrenderer_draw.cpp
    src/renderer/windowframe.cpp
    src/renderer/custommessagebox.cpp
    src/renderer/inputfield.cpp
    src/renderer/settingsdialog.cpp
    src/ai/httpclient.cpp
    src/ai/aianalyzer.cpp
    src/ai/searchengine.cpp
)

if(WIN32)
    list(APPEND SOURCES src/renderer/windowframe_win32.cpp)
else()
    list(APPEND SOURCES src/renderer/windowframe_linux.cpp)
endif()

set(HEADERS
    src/app.h
    src/core/ffmpegplayer.h
    src/core/audioplayer.h
    src/core/settings.h
    src/core/common.h
    src/core/episodedetector.h
    src/subtitles/subtitleparser.h
    src/utils/logger.h
    src/renderer/sdlrenderer.h
    src/renderer/sdlrenderer_internal.h
    src/renderer/windowframe.h
    src/renderer/custommessagebox.h
    src/renderer/inputfield.h
    src/renderer/settingsdialog.h
    src/ai/httpclient.h
    src/ai/aianalyzer.h
    src/ai/searchengine.h
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty
    ${FFMPEG_INCLUDE_DIRS}
)

# SDL3 include (仅当未从源码构建时手动添加)
if(NOT TARGET SDL3::SDL3 AND SDL3_INCLUDE_DIRS)
    target_include_directories(${PROJECT_NAME} PRIVATE ${SDL3_INCLUDE_DIRS})
endif()

# SDL3_ttf include (仅当未从源码构建时手动添加)
if(SDL3_TTF_FOUND AND NOT TARGET SDL3_ttf::SDL3_ttf AND SDL3_TTF_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE ${SDL3_TTF_INCLUDE_DIR})
endif()

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${FFMPEG_LIBRARIES}
)

if(spdlog_FOUND)
    if(TARGET spdlog::spdlog)
        target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
    elseif(TARGET spdlog::spdlog_header_only)
        target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog_header_only)
    endif()
    target_compile_definitions(${PROJECT_NAME} PRIVATE VIDEOPLAY_HAS_SPDLOG=1)
endif()

if(TARGET SDL3::SDL3)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)
elseif(SDL3_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL3_LIBRARIES})
endif()

if(OPENGL_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GL)
endif()

if(SDL3_TTF_FOUND)
    if(TARGET SDL3_ttf::SDL3_ttf)
        target_link_libraries(${PROJECT_NAME} PRIVATE SDL3_ttf::SDL3_ttf)
    elseif(SDL3_TTF_LIBRARY)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL3_TTF_LIBRARY})
    endif()
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_SDL_TTF=1)
endif()

if(OPENGL_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GL)
endif()

# Windows 特定设置
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE opengl32)
    
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
)

# 输出目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 安装规则
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Windows: 复制 FFmpeg 运行时
if(WIN32 AND FFMPEG_FOUND AND DEFINED FFmpeg_ROOT)
    set(FFMPEG_BIN_DIR "${FFmpeg_ROOT}/bin")
    if(EXISTS ${FFMPEG_BIN_DIR})
        file(GLOB FFMPEG_RUNTIME_FILES
            "${FFMPEG_BIN_DIR}/*.dll"
            "${FFMPEG_BIN_DIR}/*.exe")
        foreach(FFMPEG_RUNTIME_FILE ${FFMPEG_RUNTIME_FILES})
            get_filename_component(RUNTIME_NAME ${FFMPEG_RUNTIME_FILE} NAME)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${FFMPEG_RUNTIME_FILE}
                    $<TARGET_FILE_DIR:${PROJECT_NAME}>
                COMMENT "Copying ${RUNTIME_NAME}"
            )
        endforeach()
    endif()
endif()

# Windows: 复制 SDL3 DLL
if(WIN32 AND TARGET SDL3::SDL3)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMENT "Copying SDL3.dll"
    )
endif()

# Windows: 复制 SDL3_ttf DLL
if(WIN32 AND TARGET SDL3_ttf::SDL3_ttf)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMENT "Copying SDL3_ttf.dll"
    )
endif()

# Windows: 复制字体文件
if(WIN32 AND SDL3_TTF_FOUND)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/resources/fonts
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/fonts
        COMMENT "Copying fonts"
    )
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/resources/icons
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/icons
        COMMENT "Copying icons"
    )
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

message(STATUS "VideoPlay configuration complete")
message(STATUS "  FFmpeg: ${FFMPEG_FOUND}")
message(STATUS "  SDL3: ${SDL3_FOUND}")
message(STATUS "  SDL3_ttf: ${SDL3_TTF_FOUND}")
message(STATUS "  OpenGL: ${OPENGL_FOUND}")
message(STATUS "  spdlog: ${spdlog_FOUND}")
