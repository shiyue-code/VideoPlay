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

add_executable(test_frame_image
    test_frame_image_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/frame_image.cpp
)
target_include_directories(test_frame_image PRIVATE ${VIDEOPLAY_TEST_INCLUDE_DIRS})
target_link_libraries(test_frame_image PRIVATE ${VIDEOPLAY_GTEST_MAIN_TARGET})
gtest_discover_tests(test_frame_image)

add_executable(test_string_utils
    test_string_utils_gtest.cpp
)
target_include_directories(test_string_utils PRIVATE ${VIDEOPLAY_TEST_INCLUDE_DIRS})
target_link_libraries(test_string_utils PRIVATE ${VIDEOPLAY_GTEST_MAIN_TARGET})
gtest_discover_tests(test_string_utils)

add_executable(test_common
    test_common_gtest.cpp
)
target_include_directories(test_common PRIVATE ${VIDEOPLAY_TEST_INCLUDE_DIRS})
target_link_libraries(test_common PRIVATE ${VIDEOPLAY_GTEST_MAIN_TARGET})
gtest_discover_tests(test_common)

add_executable(test_ai_utils
    test_ai_utils_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)
target_include_directories(test_ai_utils PRIVATE ${VIDEOPLAY_TEST_INCLUDE_DIRS})
target_link_libraries(test_ai_utils PRIVATE ${VIDEOPLAY_GTEST_MAIN_TARGET})
gtest_discover_tests(test_ai_utils)

add_executable(test_searchengine
    test_searchengine_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/searchengine.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)
target_include_directories(test_searchengine PRIVATE ${VIDEOPLAY_TEST_INCLUDE_DIRS})
target_link_libraries(test_searchengine PRIVATE ${VIDEOPLAY_GTEST_MAIN_TARGET})
gtest_discover_tests(test_searchengine)

add_executable(test_settings_maintenance
    test_settings_maintenance_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings_maintenance.cpp
)
target_include_directories(test_settings_maintenance PRIVATE ${VIDEOPLAY_TEST_INCLUDE_DIRS})
target_link_libraries(test_settings_maintenance PRIVATE ${VIDEOPLAY_GTEST_MAIN_TARGET})
gtest_discover_tests(test_settings_maintenance)

add_executable(test_episodedetector
    test_episodedetector_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/episodedetector.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)
target_include_directories(test_episodedetector PRIVATE ${VIDEOPLAY_TEST_INCLUDE_DIRS})
target_link_libraries(test_episodedetector PRIVATE ${VIDEOPLAY_GTEST_MAIN_TARGET})
gtest_discover_tests(test_episodedetector)

add_executable(test_settings
    test_settings_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settings_maintenance.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)
target_include_directories(test_settings PRIVATE ${VIDEOPLAY_TEST_INCLUDE_DIRS})
target_link_libraries(test_settings PRIVATE ${VIDEOPLAY_GTEST_MAIN_TARGET})
gtest_discover_tests(test_settings)

# FFmpeg 相关测试仅 Windows 构建：集成测试依赖 cmd /c 与内置 ffmpeg.exe，
# 且目标文件需拷 FFmpeg/SDL3 DLL 才能跑
if(WIN32)

# 运行期依赖 FFmpeg / SDL3 动态库，构建后拷到目标目录再让 gtest 发现用例
function(videoplay_copy_test_runtime target)
    set(VIDEOPLAY_FFMPEG_BIN_DIR "${VIDEOPLAY_BUNDLED_FFMPEG_ROOT}/bin")
    if(EXISTS ${VIDEOPLAY_FFMPEG_BIN_DIR})
        file(GLOB VIDEOPLAY_TEST_FFMPEG_DLLS "${VIDEOPLAY_FFMPEG_BIN_DIR}/*.dll")
        foreach(VIDEOPLAY_TEST_DLL ${VIDEOPLAY_TEST_FFMPEG_DLLS})
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${VIDEOPLAY_TEST_DLL}
                    $<TARGET_FILE_DIR:${target}>
            )
        endforeach()
    endif()
    if(TARGET SDL3::SDL3)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3>
                $<TARGET_FILE_DIR:${target}>
        )
    endif()
endfunction()

add_executable(test_ffmpegplayer_playback
    test_ffmpegplayer_playback_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ffmpegplayer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ffmpegplayer_filters.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ffmpegplayer_tracks.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ffmpegplayer_preview.cpp
    ${CMAKE_SOURCE_DIR}/src/core/videoadjuster.cpp
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

videoplay_copy_test_runtime(test_ffmpegplayer_playback)

gtest_discover_tests(test_ffmpegplayer_playback)

add_executable(test_videoadjuster
    test_videoadjuster_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/videoadjuster.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)
target_include_directories(test_videoadjuster PRIVATE
    ${VIDEOPLAY_TEST_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)
target_link_libraries(test_videoadjuster PRIVATE
    ${VIDEOPLAY_GTEST_MAIN_TARGET}
    ${FFMPEG_LIBRARIES}
)

videoplay_copy_test_runtime(test_videoadjuster)

gtest_discover_tests(test_videoadjuster)
endif()

