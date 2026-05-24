add_executable(test_subtitleparser
    test_subtitleparser.cpp
    ${CMAKE_SOURCE_DIR}/src/subtitles/subtitleparser.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/logger.cpp
)

target_include_directories(test_subtitleparser
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/3rdparty
)

add_test(NAME SubtitleParserTest COMMAND test_subtitleparser)

add_executable(test_playbackcontroller
    test_playbackcontroller.cpp
)

target_include_directories(test_playbackcontroller
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/3rdparty
)

add_test(NAME PlaybackControllerTest COMMAND test_playbackcontroller)
