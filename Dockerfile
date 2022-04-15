FROM ubuntu:20.04

# 设置工作目录
WORKDIR /webrtc

# 设置时区
ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# 安装环境
RUN sed -i 's/archive.ubuntu.com/mirrors.ustc.edu.cn/g' /etc/apt/sources.list
RUN sed -i 's/security.ubuntu.com/mirrors.ustc.edu.cn/g' /etc/apt/sources.list
RUN apt clean
RUN apt update
# 更新所有库
RUN apt upgrade -y
# add-apt-repository
RUN apt install -y software-properties-common
# gcc g++ make 
RUN apt install -y build-essential 
# gcc-11 g++-11
RUN add-apt-repository ppa:ubuntu-toolchain-r/test && \
    apt-get install -y gcc-11 g++-11
# vim
RUN apt install -y vim
# libssl
RUN apt install -y libssl-dev
# wget
RUN apt install -y wget
# htop
RUN apt install -y htop

# intel
RUN \
    apt install -y intel-media-va-driver-non-free && \
    apt install -y vainfo

# nvidia
RUN wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2004/x86_64/cuda-ubuntu2004.pin && \
    mv cuda-ubuntu2004.pin /etc/apt/preferences.d/cuda-repository-pin-600 && \
    apt-key adv --fetch-keys https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2004/x86_64/7fa2af80.pub && \
    add-apt-repository "deb https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2004/x86_64/ /" && \
    apt-get update && \
    apt-get -y install cuda

# video
RUN \
    apt install -y libx264-155 && \
    apt install -y libx265-179

# audio
RUN \
    apt install -y libfdk-aac1 && \
    apt install -y opus-tools

# http
RUN \
    apt install -y httpie && \
    apt install -y jq

# git
RUN apt install -y git

# coturn
RUN apt install -y coturn

# NodeJS 14.X and npm
RUN \
    apt install -y curl dirmngr apt-transport-https lsb-release ca-certificates && \
    curl -sL https://deb.nodesource.com/setup_14.x | bash - && \
    apt install -y nodejs 

# Python 3 and pip
RUN apt install -y python3 && \
    apt install -y python3-pip && \
    pip install --upgrade pip && \
    pip install launchpadlib 

# 更新所有库
RUN apt upgrade -y

# pm2
RUN npm install -g pm2
# gulp
RUN npm install -g gulp

# lspci
RUN apt install -y pciutils

# 拷贝文件
COPY 3rd/ffmpeg/lib/ /usr/local/webrtclib
COPY 3rd/RTSP/lib/ /usr/local/webrtclib

# mediasoup-demo
COPY mediasoup-demo/ /webrtc/mediasoup-demo
RUN cd /webrtc/mediasoup-demo/server && npm install 
RUN cd /webrtc/mediasoup-demo/app && npm install

# rtsp_demo
COPY build/src/bin/ /webrtc

# coturn配置
RUN echo "TURNSERVER_ENABLED=1" >> /etc/default/coturn
COPY doc/coturn/turnserver.conf /etc/

# 启动脚本
COPY doc/start_ffmpeg.sh /webrtc
COPY doc/start_server.sh /webrtc

# 生产环境
ENV NODE_ENV="production"
# 证书
ENV HTTPS_CERT_FULLCHAIN="/webrtc/fullchain.pem"
ENV HTTPS_CERT_PRIVKEY="/webrtc/privkey.pem"
# 端口
ENV MEDIASOUP_MIN_PORT=49152
ENV MEDIASOUP_MAX_PORT=65535

# 启动mediasoup服务端
CMD \
    openssl genrsa > /webrtc/privkey.pem && \
    openssl req -new -x509 -days 3650 -subj "/C=CN/ST=shanghai/L=shanghai/O=root/OU=root/CN=root" -key /webrtc/privkey.pem > /webrtc/fullchain.pem && \
    /webrtc/start_server.sh && \
    bash