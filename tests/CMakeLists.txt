# 测试目录的CMakeLists.txt

# 确保测试可以访问主项目的包含目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 测试核心功能 - 只包含实际存在的测试文件
add_executable(test_core
    test_playbackcontroller.cpp
)

target_include_directories(test_core
    PRIVATE ${CMAKE_SOURCE_DIR}
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(test_core
    PRIVATE video_core
    PRIVATE video_plugins
    PRIVATE Qt6::Core Qt6::Test
)

# 添加测试
add_test(NAME PlaybackControllerTest COMMAND test_core)