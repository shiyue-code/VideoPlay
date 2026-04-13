cmake_minimum_required(VERSION 3.16)
project(VideoPlay VERSION 2.0.0 LANGUAGES C CXX)

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

# 查找 FFmpeg
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavcodec libavformat libavutil libswscale libswresample)
endif()

if(NOT FFMPEG_FOUND AND DEFINED FFmpeg_ROOT)
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

# 查找 SDL2
find_package(SDL2 QUIET)

if(NOT SDL2_FOUND AND DEFINED SDL2_ROOT)
    find_path(SDL2_INCLUDE_DIR SDL.h
        PATHS ${SDL2_ROOT}/include
        PATH_SUFFIXES SDL2
        NO_DEFAULT_PATH)
    find_library(SDL2_LIBRARY SDL2
        PATHS ${SDL2_ROOT}/lib
        NO_DEFAULT_PATH)
    
    if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
        set(SDL2_FOUND TRUE)
        set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
        set(SDL2_LIBRARIES ${SDL2_LIBRARY})
    endif()
endif()

# Windows: 尝试从常见位置查找 SDL2
if(NOT SDL2_FOUND AND WIN32)
    # 检查 3rdparty 目录
    if(EXISTS "${CMAKE_SOURCE_DIR}/3rdparty/SDL2")
        set(SDL2_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/3rdparty/SDL2/include)
        set(SDL2_LIBRARIES ${CMAKE_SOURCE_DIR}/3rdparty/SDL2/lib/x64/SDL2.lib)
        set(SDL2_FOUND TRUE)
        message(STATUS "SDL2 found in 3rdparty: ${SDL2_INCLUDE_DIRS}")
    endif()
endif()

if(SDL2_FOUND)
    message(STATUS "SDL2 found: ${SDL2_INCLUDE_DIRS}")
else()
    message(FATAL_ERROR "SDL2 is required but not found. Please set SDL2_ROOT or install SDL2.")
endif()

# 查找 SDL2_ttf
set(SDL2_TTF_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/3rdparty/SDL2_ttf/include)
set(SDL2_TTF_LIBRARY ${CMAKE_SOURCE_DIR}/3rdparty/SDL2_ttf/lib/x64/SDL2_ttf.lib)

if(EXISTS ${SDL2_TTF_INCLUDE_DIR} AND EXISTS ${SDL2_TTF_LIBRARY})
    set(SDL2_TTF_FOUND TRUE)
    message(STATUS "SDL2_ttf found: ${SDL2_TTF_INCLUDE_DIR}")
else()
    set(SDL2_TTF_FOUND FALSE)
    message(WARNING "SDL2_ttf not found, text rendering will be disabled")
endif()

# 查找 OpenGL (可选，用于高级渲染)
find_package(OpenGL QUIET)

# 源文件
set(SOURCES
    src/main.cpp
    src/app.cpp
    src/core/ffmpegplayer.cpp
    src/core/audioplayer.cpp
    src/core/settings.cpp
    src/subtitles/subtitleparser.cpp
    src/utils/logger.cpp
    src/renderer/sdlrenderer.cpp
    3rdparty/tinyfiledialogs/tinyfiledialogs.c
)

set(HEADERS
    src/app.h
    src/core/ffmpegplayer.h
    src/core/audioplayer.h
    src/core/settings.h
    src/core/common.h
    src/subtitles/subtitleparser.h
    src/utils/logger.h
    src/renderer/sdlrenderer.h
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SDL2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/SDL2_ttf/include
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/tinyfiledialogs
    ${FFMPEG_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
)

if(SDL2_TTF_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_TTF_INCLUDE_DIR})
endif()

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${FFMPEG_LIBRARIES}
    ${SDL2_LIBRARIES}
)

if(SDL2_TTF_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_TTF_LIBRARY})
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

# Windows: 复制 FFmpeg DLLs
if(WIN32 AND FFMPEG_FOUND AND DEFINED FFmpeg_ROOT)
    set(FFMPEG_BIN_DIR "${FFmpeg_ROOT}/bin")
    if(EXISTS ${FFMPEG_BIN_DIR})
        file(GLOB FFMPEG_DLLS "${FFMPEG_BIN_DIR}/*.dll")
        foreach(FFMPEG_DLL ${FFMPEG_DLLS})
            get_filename_component(DLL_NAME ${FFMPEG_DLL} NAME)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${FFMPEG_DLL}
                    $<TARGET_FILE_DIR:${PROJECT_NAME}>
                COMMENT "Copying ${DLL_NAME}"
            )
        endforeach()
    endif()
endif()

# Windows: 复制 SDL2 DLL
if(WIN32 AND SDL2_FOUND)
    # 尝试找到 SDL2.dll
    get_filename_component(SDL2_LIB_DIR ${SDL2_LIBRARIES} DIRECTORY)
    set(SDL2_DLL "${SDL2_LIB_DIR}/SDL2.dll")
    
    if(EXISTS ${SDL2_DLL})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SDL2_DLL}
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Copying SDL2.dll"
        )
    elseif(DEFINED SDL2_ROOT)
        set(SDL2_DLL "${SDL2_ROOT}/lib/x64/SDL2.dll")
        if(EXISTS ${SDL2_DLL})
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${SDL2_DLL}
                    $<TARGET_FILE_DIR:${PROJECT_NAME}>
                COMMENT "Copying SDL2.dll"
            )
        endif()
    endif()
endif()

# Windows: 复制 SDL2_ttf DLL
if(WIN32 AND SDL2_TTF_FOUND)
    set(SDL2_TTF_DLL "${CMAKE_SOURCE_DIR}/3rdparty/SDL2_ttf/lib/x64/SDL2_ttf.dll")
    if(EXISTS ${SDL2_TTF_DLL})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SDL2_TTF_DLL}
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Copying SDL2_ttf.dll"
        )
    endif()
    
    # 复制字体文件
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/resources/fonts
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/fonts
        COMMENT "Copying fonts"
    )
endif()

message(STATUS "VideoPlay configuration complete")
message(STATUS "  FFmpeg: ${FFMPEG_FOUND}")
message(STATUS "  SDL2: ${SDL2_FOUND}")
message(STATUS "  SDL2_ttf: ${SDL2_TTF_FOUND}")
message(STATUS "  OpenGL: ${OPENGL_FOUND}")
