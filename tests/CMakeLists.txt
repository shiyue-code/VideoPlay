# 测试目录的 CMakeLists.txt

add_executable(test_subtitleparser
    test_subtitleparser.cpp
    ${CMAKE_SOURCE_DIR}/src/subtitles/subtitleparser.cpp
)

target_include_directories(test_subtitleparser
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_subtitleparser
    PRIVATE ${FFMPEG_LIBRARIES}
)

if(TARGET SDL3::SDL3)
    target_link_libraries(test_subtitleparser PRIVATE SDL3::SDL3)
endif()

target_compile_definitions(test_subtitleparser PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

add_test(NAME SubtitleParserTest COMMAND test_subtitleparser)
