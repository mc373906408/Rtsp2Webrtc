#!/bin/bash
sed -i "s%MEDIASOUP_MIN_PORT%$MEDIASOUP_MIN_PORT%g" /etc/turnserver.conf
sed -i "s%MEDIASOUP_MAX_PORT%$MEDIASOUP_MAX_PORT%g" /etc/turnserver.conf
sed -i "s%ANNOUNCED_IP%$ANNOUNCED_IP%g" /etc/turnserver.conf
sed -i "s%ANNOUNCED_IP%$ANNOUNCED_IP%g" /webrtc/mediasoup-demo/app/lib/RoomClient.js
sed -i "s%HTTPS_CERT_FULLCHAIN%$HTTPS_CERT_FULLCHAIN%g" /etc/turnserver.conf
sed -i "s%HTTPS_CERT_PRIVKEY%$HTTPS_CERT_PRIVKEY%g" /etc/turnserver.conf

SERCRET=$(LC_CTYPE=C tr -dc A-Za-z0-9 < /dev/urandom | fold -w ${1:-32} | head -n 1)
sed -i "s%SERCRET%$SERCRET%g" /etc/turnserver.conf
sed -i "s%SERCRET%$SERCRET%g" /webrtc/mediasoup-demo/app/lib/RoomClient.js

turnserver -o

nohup ./middleware &

cd /webrtc/mediasoup-demo/app
pm2 --name="app" start gulp 
cd /webrtc/mediasoup-demo/server
pm2 start server.js

