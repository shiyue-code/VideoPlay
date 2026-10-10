set(VIDEOPLAY_GOOGLETEST_DIR "${CMAKE_SOURCE_DIR}/3rdparty/googletest")

if(NOT EXISTS "${VIDEOPLAY_GOOGLETEST_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "GoogleTest submodule is missing. Run: git submodule update --init --recursive")
endif()

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)

if(MSVC)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()

add_subdirectory(
    "${VIDEOPLAY_GOOGLETEST_DIR}"
    "${CMAKE_BINARY_DIR}/3rdparty/googletest"
    EXCLUDE_FROM_ALL
)

include(GoogleTest)

set(VIDEOPLAY_TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/3rdparty
)

set(VIDEOPLAY_GTEST_MAIN_TARGET GTest::gtest_main)
if(NOT TARGET ${VIDEOPLAY_GTEST_MAIN_TARGET})
    set(VIDEOPLAY_GTEST_MAIN_TARGET gtest_main)
endif()

add_executable(test_subtitleparser
    test_subtitleparser_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/subtitles/subtitleparser.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)
target_include_directories(test_subtitleparser PRIVATE ${VIDEOPLAY_TEST_INCLUDE_DIRS})
target_link_libraries(test_subtitleparser PRIVATE ${VIDEOPLAY_GTEST_MAIN_TARGET})
gtest_discover_tests(test_subtitleparser)

add_executable(test_playbackcontroller
    test_playbackcontroller_gtest.cpp
)
target_include_directories(test_playbackcontroller PRIVATE ${VIDEOPLAY_TEST_INCLUDE_DIRS})
target_link_libraries(test_playbackcontroller PRIVATE ${VIDEOPLAY_GTEST_MAIN_TARGET})
gtest_discover_tests(test_playbackcontroller)

add_executable(test_menuitem
    test_menuitem_gtest.cpp
)
target_include_directories(test_menuitem PRIVATE ${VIDEOPLAY_TEST_INCLUDE_DIRS})
target_link_libraries(test_menuitem PRIVATE ${VIDEOPLAY_GTEST_MAIN_TARGET})
gtest_discover_tests(test_menuitem)

add_executable(test_audioclock
    test_audioclock_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/audioclock.cpp
)
target_include_directories(test_audioclock PRIVATE ${VIDEOPLAY_TEST_INCLUDE_DIRS})
target_link_libraries(test_audioclock PRIVATE ${VIDEOPLAY_GTEST_MAIN_TARGET})
gtest_discover_tests(test_audioclock)

# 集成测试依赖 cmd /c 与内置 ffmpeg.exe，仅 Windows 可构建运行
if(WIN32)
add_executable(test_ffmpegplayer_playback
    test_ffmpegplayer_playback_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ffmpegplayer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ffmpegplayer_filters.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ffmpegplayer_tracks.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ffmpegplayer_preview.cpp
    ${CMAKE_SOURCE_DIR}/src/core/audioclock.cpp
    ${CMAKE_SOURCE_DIR}/src/core/audioplayer.cpp
    ${CMAKE_SOURCE_DIR}/src/subtitles/subtitleparser.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)
target_include_directories(test_ffmpegplayer_playback PRIVATE
    ${VIDEOPLAY_TEST_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)
target_link_libraries(test_ffmpegplayer_playback PRIVATE
    ${VIDEOPLAY_GTEST_MAIN_TARGET}
    ${FFMPEG_LIBRARIES}
)
if(TARGET SDL3::SDL3)
    target_link_libraries(test_ffmpegplayer_playback PRIVATE SDL3::SDL3)
endif()
target_compile_definitions(test_ffmpegplayer_playback PRIVATE
    VIDEOPLAY_FFMPEG_EXE="${VIDEOPLAY_BUNDLED_FFMPEG_ROOT}/bin/ffmpeg.exe"
)

# 运行期依赖 FFmpeg / SDL3 动态库，先拷到测试目录再让 gtest 发现用例
set(VIDEOPLAY_FFMPEG_BIN_DIR "${VIDEOPLAY_BUNDLED_FFMPEG_ROOT}/bin")
if(EXISTS ${VIDEOPLAY_FFMPEG_BIN_DIR})
    file(GLOB VIDEOPLAY_TEST_FFMPEG_DLLS "${VIDEOPLAY_FFMPEG_BIN_DIR}/*.dll")
    foreach(VIDEOPLAY_TEST_DLL ${VIDEOPLAY_TEST_FFMPEG_DLLS})
        add_custom_command(TARGET test_ffmpegplayer_playback POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${VIDEOPLAY_TEST_DLL}
                $<TARGET_FILE_DIR:test_ffmpegplayer_playback>
        )
    endforeach()
endif()
if(TARGET SDL3::SDL3)
    add_custom_command(TARGET test_ffmpegplayer_playback POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:test_ffmpegplayer_playback>
    )
endif()

gtest_discover_tests(test_ffmpegplayer_playback)
endif()

