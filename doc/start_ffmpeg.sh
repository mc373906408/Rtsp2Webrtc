#!/bin/bash
echo "准备将摄像机加入房间,请先确保房间已经创建"
# echo "请输入mediasoup服务器url地址(例:192.168.0.20):"
# read ANNOUNCED_IP
echo "请输入房间ID(例:wq8ihbhz):"
read ROOM_ID
echo "请输入摄像机rtsp地址(例:rtsp://admin:Admin123@192.168.0.232:554/ch01.264):"
read RTSP_URL


# ANNOUNCED_IP=192.168.0.30
# ROOM_ID=ifiuvv6s
# RTSP_URL=rtsp://admin:Admin123@192.168.0.232:554/ch01.264

SERVER_URL="https://${ANNOUNCED_IP}:4443"

BROADCASTER_ID=$(LC_CTYPE=C tr -dc A-Za-z0-9 < /dev/urandom | fold -w ${1:-32} | head -n 1)
# BROADCASTER_ID="DMokG1qBxRC1WGbuLr1AopJlFBLfu23R"
HTTPIE_COMMAND="http --check-status --verify=no"
AUDIO_SSRC=1111
AUDIO_PT=100
VIDEO_SSRC=2222
VIDEO_PT=101


# echo "删除上一个摄像机"
# ${HTTPIE_COMMAND} \
#     DELETE ${SERVER_URL}/rooms/${ROOM_ID}/broadcasters/${BROADCASTER_ID} \
#     > /dev/null

echo "加入新的摄像机"
${HTTPIE_COMMAND} \
	POST ${SERVER_URL}/rooms/${ROOM_ID}/broadcasters \
	id="${BROADCASTER_ID}" \
	displayName="Broadcaster" \
	device:='{"name": "FFmpeg"}' \
    > /dev/null

echo "打开音频通道"
res=$(${HTTPIE_COMMAND} \
	POST ${SERVER_URL}/rooms/${ROOM_ID}/broadcasters/${BROADCASTER_ID}/transports \
	type="plain" \
	comedia:=true \
	rtcpMux:=false \
	2> /dev/null)

eval "$(echo ${res} | jq -r '@sh "audioTransportId=\(.id) audioTransportIp=\(.ip) audioTransportPort=\(.port) audioTransportRtcpPort=\(.rtcpPort)"')"

${HTTPIE_COMMAND} -v \
	POST ${SERVER_URL}/rooms/${ROOM_ID}/broadcasters/${BROADCASTER_ID}/transports/${audioTransportId}/producers \
	kind="audio" \
	rtpParameters:="{ \"codecs\": [{ \"mimeType\":\"audio/opus\", \"payloadType\":${AUDIO_PT}, \"clockRate\":48000, \"channels\":2, \"parameters\":{ \"sprop-stereo\":1 } }], \"encodings\": [{ \"ssrc\":${AUDIO_SSRC} }] }" \
	> /dev/null

echo "打开视频通道"
res=$(${HTTPIE_COMMAND} \
	POST ${SERVER_URL}/rooms/${ROOM_ID}/broadcasters/${BROADCASTER_ID}/transports \
	type="plain" \
	comedia:=true \
	rtcpMux:=false \
	2> /dev/null)
eval "$(echo ${res} | jq -r '@sh "videoTransportId=\(.id) videoTransportIp=\(.ip) videoTransportPort=\(.port) videoTransportRtcpPort=\(.rtcpPort)"')"

${HTTPIE_COMMAND} -v \
	POST ${SERVER_URL}/rooms/${ROOM_ID}/broadcasters/${BROADCASTER_ID}/transports/${videoTransportId}/producers \
	kind="video" \
	rtpParameters:="{ \"codecs\": [{ \"mimeType\":\"video/h264\", \"payloadType\":${VIDEO_PT}, \"clockRate\":90000,\"parameters\":{\"packetization-mode\":1,\"profile-level-id\":\"42e01f\",\"level-asymmetry-allowed\":1} }], \"encodings\": [{ \"ssrc\":${VIDEO_SSRC} }] }" \
	> /dev/null

echo "音频rtp端口:${audioTransportPort} 音频rtcp端口:${audioTransportRtcpPort}"
echo "视频rtp端口:${videoTransportPort} 视频rtcp端口:${videoTransportRtcpPort}"

echo "打开摄像机"
./rtsp_demo ${RTSP_URL} ${ANNOUNCED_IP} ${audioTransportPort} ${audioTransportRtcpPort} ${videoTransportPort} ${videoTransportRtcpPort}