# ffmpeg动态库
./configure --prefix=./build --arch=x86_64 --disable-x86asm --enable-vaapi --enable-libfdk_aac --enable-libopus --enable-shared --disable-static


./configure --prefix=./build --arch=x86_64 --enable-nonfree --enable-gpl --enable-version3 --disable-doc --disable-debug --enable-vaapi --enable-cuda-nvcc --enable-libnpp --extra-cflags=-I/usr/local/cuda/include --extra-ldflags=-L/usr/local/cuda/lib64 --enable-libx264 --enable-libx265 --enable-libfdk_aac --enable-libopus --enable-shared --disable-static


make -j16
make DESTDIR=./build install

# 查询链接库
ldd

# 修改ffmpeg链接库
patchelf --set-rpath /usr/local/ffmpeglib *

# H265转H264
ffmpeg -re -hwaccel qsv -c:v hevc_qsv  -i video-h265.mkv -vf 'scale_qsv=640:-1' -c:v h264_qsv -acodec copy -f flv rtmp://localhost/live/livestream

ffmpeg -re -hwaccel qsv -c:v hevc_qsv -rtsp_transport tcp -i rtsp://admin:Admin123@192.168.0.232:554/ch01.264 -vf 'scale_qsv=640:-1' -c:v h264_qsv -acodec copy -f flv rtmp://localhost/live/livestream

ffmpeg -re -hwaccel vaapi -hwaccel_output_format vaapi -rtsp_transport tcp -i rtsp://admin:Admin123@192.168.0.232:554/ch01.264 -vf 'scale_vaapi=640:-1' -c:v h264_vaapi -c:a libfdk_aac -f flv rtmp://localhost/live/livestream

ffmpeg -re -i ./source.200kbps.768x320.flv -c copy -f flv rtmp://localhost/live/livestream


ffmpeg -rtsp_transport tcp -i rtsp://admin:12345@192.168.18.252:554/h264/ch1/main/av_stream -c:a copy -c:v libx264 -f flv rtmp://stream.amaitech.com/live/4001480473625vhost=tinywan.amai8.com

ffmpeg -re -hwaccel qsv -c:v hevc_qsv -rtsp_transport tcp -i rtsp://admin:Admin123@192.168.0.232:554/ch01.264 -vf 'scale_qsv=640:-1' -c:v h264_qsv -c:a libfdk_aac -f flv rtmp://localhost/live/livestream

ffmpeg -i rtmp://127.0.0.1:1935/live/livestream


tcp
'rtsp://192.168.0.220:554/04557197457718400101?DstCode=01&ServiceType=1&ClientType=1&StreamID=1&SrcTP=2&DstTP=2&SrcPP=1&DstPP=1&MediaTransMode=0&BroadcastType=0&SV=1&Token=mbsWSrgg4cwnrd4dtrJZhXtJQqPfZLNOCyLsG61fXkU=&'

udp
rtsp://192.168.0.220:554/04557197457718400101?DstCode=01&ServiceType=1&ClientType=1&StreamID=1&SrcTP=2&DstTP=2&SrcPP=1&DstPP=1&MediaTransMode=0&BroadcastType=0&SV=1&Token=kBPPBFA03tmRySo/35pO9Puqm1j+SZ9aGhTQz7szKss=&


抓包
rtsp.request || rtsp.response

Linux抓包，windows用Wireshark
tcpdump -i eno1 -X -e -w test.cap



# srs服务器
docker run --name=eager_tesla --hostname=bd6a35d5309f --mac-address=02:42:ac:11:00:02 --env=CANDIDATE=192.168.0.20 --env=PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin --volume=/home/zkteco/Downloads:/download --workdir=/usr/local/srs --expose=10080/udp -p 1935:1935 -p 1985:1985 -p 8000:8000/udp -p 8080:8080 --restart=no --label='org.opencontainers.image.title=CentOS Base Image' --label='org.label-schema.name=CentOS Base Image' --label='org.opencontainers.image.vendor=CentOS' --label='org.label-schema.vendor=CentOS' --label='org.label-schema.license=GPLv2' --label='org.opencontainers.image.created=2020-11-13 00:00:00+00:00' --label='org.label-schema.build-date=20201113' --label='org.opencontainers.image.licenses=GPL-2.0-only' --label='org.label-schema.schema-version=1.0' --runtime=runc registry.cn-hangzhou.aliyuncs.com/ossrs/srs:4 objs/srs -c conf/rtc.confc

docker run -it -p 1935:1935 -p 1985:1985 -p 8080:8080 \
    --env CANDIDATE=$CANDIDATE -p 8000:8000/udp \
    registry.cn-hangzhou.aliyuncs.com/ossrs/srs:4 ./objs/srs -c conf/rtmp2rtc.conf





http --check-status --verify=no POST https://192.168.0.20:4443/rooms/wq8ihbhz/broadcasters id=fL4KvJdi908zgfcI6UgP3ryyqXtVFfS0 displayName=Broadcaster 'device:={"name": "FFmpeg"}'

http --check-status --verify=no POST https://192.168.0.20:4443/rooms/wq8ihbhz/broadcasters/fL4KvJdi908zgfcI6UgP3ryyqXtVFfS0/transports type=plain comedia:=true rtcpMux:=false

http --check-status --verify=no POST https://192.168.0.20:4443/rooms/wq8ihbhz/broadcasters/fL4KvJdi908zgfcI6UgP3ryyqXtVFfS0/transports type=plain comedia:=true rtcpMux:=false

http --check-status --verify=no -v POST https://192.168.0.20:4443/rooms/wq8ihbhz/broadcasters/fL4KvJdi908zgfcI6UgP3ryyqXtVFfS0/transports/401880bf-4560-40b7-a426-38b3737d3bfe/producers kind=audio 'rtpParameters:={ "codecs": [{ "mimeType":"audio/opus", "payloadType":100, "clockRate":48000, "channels":2, "parameters":{ "sprop-stereo":1 } }], "encodings": [{ "ssrc":1111 }] }'

http --check-status --verify=no -v POST https://192.168.0.20:4443/rooms/wq8ihbhz/broadcasters/fL4KvJdi908zgfcI6UgP3ryyqXtVFfS0/transports/67061f69-d608-407f-b697-80d2ea6f227e/producers kind=video 'rtpParameters:={ "codecs": [{ "mimeType":"video/h264", "payloadType":101, "clockRate":90000,"parameters":{"packetization-mode":1,"profile-level-id":"42e01f","level-asymmetry-allowed":1}}], "encodings": [{ "ssrc":2222 }] }'
