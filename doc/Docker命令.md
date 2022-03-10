## 打包
docker build -t webrtc .

## 导出docker镜像
docker save > webrtc_docker.tar webrtc

## 导入docker镜像
docker load -i webrtc_docker.tar

## 运行
### intel gpu 命令
- LAN要改成本机局域网IP
- ANNOUNCED_IP要改成公网IP
   - docker stop webrtc && docker rm webrtc && docker run -itd --privileged --device=/dev/dri --net=host -e LAN_IP=192.168.0.11 -e ANNOUNCED_IP=192.168.0.11 --name webrtc webrtc 
### nvidia gpu 命令
- LAN要改成本机局域网IP
- ANNOUNCED_IP要改成公网ip
   - docker stop webrtc && docker rm webrtc && docker run -itd --privileged --gpus=all --net=host e LAN_IP=192.168.0.11 -e ANNOUNCED_IP=192.168.0.30 --name webrtc webrtc 

## 进入docker
docker exec -it webrtc bash

## 停止
docker stop webrtc && docker rm webrtc