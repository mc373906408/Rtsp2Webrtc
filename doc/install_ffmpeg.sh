#!/bin/bash
echo "准备安装"
echo "请输入root密码（默认是登录密码）"
sudo -i echo
echo "正在拷贝ffmpeg"
sudo mkdir /usr/local/ffmpeglib
sudo rm -r /usr/local/ffmpeglib/*
sudo cp -r ./ffmpeglib/* /usr/local/ffmpeglib
if [ -e "/usr/local/ffmpeglib/libavcodec.so" ]
then
echo
else
echo "拷贝失败"
exit
fi
echo "正在安装驱动"
sudo apt-get install -y intel-media-va-driver-non-free
sudo apt-get install -y vainfo
sudo apt-get install -y libfdk-aac1
sudo apt-get install -y libopus-dev
sudo apt-get install -y libva-dev
sudo apt-get install -y httpie
sudo apt-get install -y jq

wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2004/x86_64/cuda-ubuntu2004.pin && \
    mv cuda-ubuntu2004.pin /etc/apt/preferences.d/cuda-repository-pin-600 && \
    apt-key adv --fetch-keys https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2004/x86_64/7fa2af80.pub && \
    add-apt-repository "deb https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2004/x86_64/ /" && \
    apt-get update && \
    apt-get -y install cuda