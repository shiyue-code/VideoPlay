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
