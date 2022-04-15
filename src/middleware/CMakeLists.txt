
project(middleware)

set(CMAKE_BUILD_TYPE "Release")

# 设置编译后路径
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/../bin)

set(SOURCES 
    main.cpp
    business.cpp
    websocket_server.cpp
)

# 主函数
add_executable(${PROJECT_NAME} ${SOURCES})

# 导入pthread
target_link_libraries(${PROJECT_NAME} pthread)

# 导入uwebsocket头文件
include_directories(${TRANSCOD_SOURCE_DIR}/3rd/uWebSockets/include)
# 链接uwebsocket
target_link_libraries(${PROJECT_NAME} ${TRANSCOD_SOURCE_DIR}/3rd/uWebSockets/lib/uSockets.a)

# 导入zlib
find_package(ZLIB REQUIRED)
# 链接zlib
target_link_libraries(${PROJECT_NAME} ZLIB::ZLIB)

# 导入jsoncpp
include_directories(${TRANSCOD_SOURCE_DIR}/3rd/jsoncpp/include)
# 链接jsoncpp
target_link_libraries(${PROJECT_NAME} ${TRANSCOD_SOURCE_DIR}/3rd/jsoncpp/lib/libjsoncpp.a)

# 导入fmt
include_directories(${TRANSCOD_SOURCE_DIR}/3rd/fmt/include)
# 链接fmt
target_link_libraries(${PROJECT_NAME} ${TRANSCOD_SOURCE_DIR}/3rd/fmt/lib/libfmt.a)
