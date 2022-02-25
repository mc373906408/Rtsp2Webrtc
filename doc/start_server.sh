#!/bin/bash
cd /webrtc/mediasoup-demo/app
pm2 --name="app" start gulp 
cd /webrtc/mediasoup-demo/server
pm2 start server.js
