# docker环境
## nvidia gpu环境
- 必须联网
- 安装显卡驱动
   - 打开软件和更新
   - 附加驱动
   - 选择NVIDIA-driver中版本最高的
- 安装容器运行时
   - install_nvidia.sh
## 安装
- 执行install_docker.sh
   - chmod +x ./install_docker.sh && ./install_docker.sh
## 运行docker
### intel gpu 命令
- LAN要改成本机局域网IP
- ANNOUNCED_IP要改成公网IP
   - docker stop webrtc && docker rm webrtc && docker run -itd --privileged --device=/dev/dri --net=host -e LAN_IP=192.168.0.11 -e ANNOUNCED_IP=zkteco.uuui.icu --name webrtc webrtc 
### nvidia gpu 命令
- LAN要改成本机局域网IP
- ANNOUNCED_IP要改成公网ip
   - docker stop webrtc && docker rm webrtc && docker run -itd --privileged --gpus=all --net=host e LAN_IP=192.168.0.11 -e ANNOUNCED_IP=192.168.0.30 --name webrtc webrtc 

# 运行demo
- demo证书是自定义的,所以最好使用chrome浏览器,ios端使用safari浏览器
- 打开网页 https://127.0.0.1:3000
- 进入docker环境执行脚本
   - docker exec -it webrtc bash
- 执行start_ffmpeg.sh脚本
   - chmod +x ./start_ffmpeg.sh && ./start_ffmpeg.sh
   - 输入房间ID和摄像机rtsp地址,房间ID在网页端url roomId后面
      - https://192.168.0.30:3000/?roomId=ifiuvv6s
