project(transcod)
# 设置编译后路径
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/../bin)

# 开启调试信息
# set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_BUILD_TYPE "Release")
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb -Wl,-rpath=/usr/local/webrtclib/")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall -Wl,-rpath=/usr/local/webrtclib/")


set(3RD_DIR "${TRANSCOD_SOURCE_DIR}/3rd")
# 导入ffmpeg lib库
set(FFMPEG_DIR "${3RD_DIR}/ffmpeg")
link_directories(${FFMPEG_DIR}/lib/)
# 导入RTSPclient
set(RTSP_DIR "${3RD_DIR}/RTSP")
link_directories(${RTSP_DIR}/lib/)


set(SOURCES 
    main.cpp
    ${3RD_DIR}/easywsclient/easywsclient.cpp
    websocketclient.cpp
    transcod.cpp
    myrtspclient.cpp
)

# 主函数
add_executable(${PROJECT_NAME} ${SOURCES})

# 导入easywsclient头文件
include_directories(${3RD_DIR}/easywsclient/)

# 导入pthread
target_link_libraries(${PROJECT_NAME} pthread)

# 导入jsoncpp
include_directories(${3RD_DIR}/jsoncpp/include)
# 链接jsoncpp
target_link_libraries(${PROJECT_NAME} ${3RD_DIR}/jsoncpp/lib/libjsoncpp.a)

# 导入fmt
include_directories(${3RD_DIR}/fmt/include)
# 链接fmt
target_link_libraries(${PROJECT_NAME} ${3RD_DIR}/fmt/lib/libfmt.a)


# 导入ffmpeg头文件
include_directories(${FFMPEG_DIR}/include/)
# 链接ffmpeg
target_link_libraries(${PROJECT_NAME} libavutil.so libavcodec.so libavformat.so libavdevice.so libavfilter.so libswscale.so libswresample.so)

# 链接dl(nvdia检测)
target_link_libraries(${PROJECT_NAME} libdl.so)


# 导入RTSPClient头文件
include_directories(${RTSP_DIR}/include/)
# 链接RTSPClient
target_link_libraries(${PROJECT_NAME} libRTSPClient.so)

# 导入线程池库
include_directories(${3RD_DIR}/threadPool/include)