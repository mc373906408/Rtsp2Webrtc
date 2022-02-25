#!/bin/bash
echo "准备安装docker"
echo "请输入root密码（默认是登录密码）"
sudo -i echo
sudo dpkg -i containerd.io.deb
sudo dpkg -i docker-ce-cli.deb
sudo dpkg -i docker-ce.deb

echo "加载docker镜像"
sudo docker load -i webrtc_docker.tar

echo "创建docker组"
sudo groupadd docker
sudo usermod -aG docker $USER
newgrp docker

