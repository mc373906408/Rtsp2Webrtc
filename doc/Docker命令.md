## 打包
docker build -t webrtc .

## 导出docker镜像
docker save > webrtc_docker.tar webrtc

## 导入docker镜像
docker load -i webrtc_docker.tar

## 运行
docker run -itd --privileged --device=/dev/dri --net=host -e ANNOUNCED_IP=192.168.0.30 --name webrtc webrtc 

## 进入docker
docker exec -it webrtc bash

## 停止
docker stop webrtc && docker rm webrtc