#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include "easywsclient.hpp"

#include <string>
#include <mutex>
#include <thread>
#include <memory>
#include <iostream>
#include <condition_variable>

using easywsclient::WebSocket;

class WebSocket_Client
{
public:
    static WebSocket_Client &getInstance()
    {
        static WebSocket_Client instance;
        return instance;
    }
    /*共享删除锁*/
    void setMutex_Quit(std::shared_ptr<bool> isQuit, std::shared_ptr<std::mutex> mtx_quit, std::shared_ptr<std::condition_variable> cv_quit);
    /*连接websocket*/
    bool openWebSocket(const std::string &wsurl);
    /*sendname*/
    void sendUUID(const std::string &uuid);
private:
    /*发送websocket数据*/
    void send(const std::string &message);

private:
    explicit WebSocket_Client();
    ~WebSocket_Client();

    WebSocket_Client(const WebSocket_Client &sg) = delete;
    WebSocket_Client &operator=(const WebSocket_Client &sg) = delete;

private:
    std::unique_ptr<WebSocket> m_ws;
    std::shared_ptr<std::mutex> m_mtx_quit;
    std::shared_ptr<std::condition_variable> m_cv_quit;
    std::shared_ptr<bool> m_isQuit;
    std::jthread m_jt;
    std::mutex m_sendMtx;
};

#endif