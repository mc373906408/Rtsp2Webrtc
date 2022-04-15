#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include <memory>

#include "App.h"
#include "json/json.h"


class WebSocket_Server
{
    public:
    enum MessageLevel
    {
        INFO,
        WARN,
        ERROR
    };
public:
    static WebSocket_Server &getInstance()
    {
        static WebSocket_Server instance;
        return instance;
    }

    void run();
    /*发送给指定uuid数据*/
    void publish(const std::string &uuid,const std::string& message);
    /*发送给server数据*/
    void publishServer(const std::string &uuid,const MessageLevel& level,const std::string& message);

private:
    explicit WebSocket_Server();
    ~WebSocket_Server();

    WebSocket_Server(const WebSocket_Server &sg) = delete;
    WebSocket_Server &operator=(const WebSocket_Server &sg) = delete;

private:
    struct PerSocketData
    {
        std::string name;
    };

    std::shared_ptr<uWS::App> m_app;

};

#endif