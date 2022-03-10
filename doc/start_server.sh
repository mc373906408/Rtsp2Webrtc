#!/bin/bash
sed -i "s%MEDIASOUP_MIN_PORT%$MEDIASOUP_MIN_PORT%g" /etc/turnserver.conf
sed -i "s%MEDIASOUP_MAX_PORT%$MEDIASOUP_MAX_PORT%g" /etc/turnserver.conf
sed -i "s%ANNOUNCED_IP%ANNOUNCED_IP%g" /etc/turnserver.conf
turnserver -o
cd /webrtc/mediasoup-demo/app
pm2 --name="app" start gulp 
cd /webrtc/mediasoup-demo/server
pm2 start server.js
