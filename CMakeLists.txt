cmake_minimum_required(VERSION 3.6.3)

# 项目信息
project(rtsp_demo VERSION 0.1.0)

set(FFMPEG_LIBS_DIR "3rd/ffmpeg/lib/")


# 导入ffmpeg lib库
link_directories(${FFMPEG_LIBS_DIR})
# 导入live555 lib库
link_directories(3rd/live555/BasicUsageEnvironment/)
link_directories(3rd/live555/groupsock/)
link_directories(3rd/live555/liveMedia/)
link_directories(3rd/live555/UsageEnvironment/)
# 导入ssl
find_package(OpenSSL REQUIRED)

# 导入ffmpeg头文件
include_directories(3rd/ffmpeg/include)
# 导入live555头文件
include_directories(3rd/live555/BasicUsageEnvironment/include)
include_directories(3rd/live555/groupsock/include)
include_directories(3rd/live555/liveMedia/include)
include_directories(3rd/live555/UsageEnvironment/include)


# 主函数
add_executable(${PROJECT_NAME} main.cpp)

# 链接ffmpeg
target_link_libraries(${PROJECT_NAME} libavutil.so libavcodec.so libavformat.so libavdevice.so libavfilter.so libswscale.so libswresample.so)
# 链接live555
target_link_libraries(${PROJECT_NAME} libliveMedia.a libBasicUsageEnvironment.a libgroupsock.a libUsageEnvironment.a)
# 链接ssl
target_link_libraries(${PROJECT_NAME} OpenSSL::SSL OpenSSL::Crypto)

# 开启调试信息
set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb -Wl,-rpath=/usr/local/ffmpeglib/")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall -Wl,-rpath=/usr/local/ffmpeglib/")


# 开启所有警告
set(CMAKE_CXX_FLAGS "-Wall")

# 拷贝ffmpeg lib库
file(COPY ${FFMPEG_LIBS_DIR} DESTINATION ffmpeglib FOLLOW_SYMLINK_CHAIN)

# 拷贝安装脚本
file(COPY doc/install_ffmpeg.sh DESTINATION ./ FOLLOW_SYMLINK_CHAIN)

file(COPY doc/start_ffmpeg.sh DESTINATION ./ FOLLOW_SYMLINK_CHAIN)
