FROM ubuntu:20.04

# 设置工作目录
WORKDIR /webrtc

# 安装环境
RUN  sed -i s@/archive.ubuntu.com/@/mirrors.aliyun.com/@g /etc/apt/sources.list
RUN  apt clean
RUN apt update
# gcc g++ make 
RUN apt install -y build-essential 
# vim
RUN apt install -y vim
# libssl
RUN apt install -y libssl-dev

# Python 3.8 and pip
RUN apt install -y python3 python3-pip

# NodeJS 14.X and npm
RUN \
    apt install -y curl dirmngr apt-transport-https lsb-release ca-certificates && \
    curl -sL https://deb.nodesource.com/setup_14.x | bash - && \
    apt install -y nodejs

# intel
RUN \
    apt install -y intel-media-va-driver-non-free && \
    apt install -y vainfo

# audio
RUN \
    apt install -y libfdk-aac1 && \
    apt install -y opus-tools

# http
RUN \
    apt install -y httpie && \
    apt install -y jq



# 拷贝文件
# ffmpeg
COPY 3rd/ffmpeg/lib/ /usr/local/ffmpeglib

# rtsp_demo
COPY build/rtsp_demo /webrtc

# mediasoup-demo
COPY mediasoup-demo/ /webrtc/mediasoup-demo

# pm2
RUN npm install -g pm2
# gulp
RUN npm install -g gulp
RUN npm install /webrtc/mediasoup-demo/app
RUN npm install /webrtc/mediasoup-demo/server

# 启动脚本
COPY doc/start_ffmpeg.sh /webrtc
COPY doc/start_server.sh /webrtc

# 生产环境
ENV NODE_ENV="production"

# 启动mediasoup服务端
CMD /webrtc/start_server.sh && bash