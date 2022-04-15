#include "websocket_server.h"
#include "business.h"

WebSocket_Server::WebSocket_Server()
{
    m_app = std::make_shared<uWS::App>();

    m_app->ws<PerSocketData>("/*", {/* Settings */
                                    .compression = uWS::CompressOptions(uWS::DEDICATED_COMPRESSOR_4KB | uWS::DEDICATED_DECOMPRESSOR),
                                    .maxPayloadLength = 100 * 1024 * 1024,
                                    .idleTimeout = 16,
                                    .maxBackpressure = 100 * 1024 * 1024,
                                    .closeOnBackpressureLimit = false,
                                    .resetIdleTimeoutOnSend = false,
                                    .sendPingsAutomatically = true,
                                    /* Handlers */
                                    .upgrade = nullptr,
                                    .open = [](auto * /*ws*/) {},
                                    .message = [](auto *ws, std::string_view message, uWS::OpCode opCode)
                                    {
                                            PerSocketData *user = static_cast<PerSocketData *>(ws->getUserData());
                                            Json::Reader reader;
                                            Json::Value root;
                                            if (reader.parse(std::string{message}, root))
                                            {
                                                auto command = root["command"].asString();
                                                if (command == "SetName")
                                                {
                                                    user->name = root["data"]["name"].asString();
                                                    /*订阅它*/
                                                    ws->subscribe(user->name);
                                                    if(user->name!="server"){
                                                        /*转码数据,下发data数据*/
                                                        std::string transcodData = Business::getInstance().getTranscodData(user->name);
                                                        ws->send(transcodData,uWS::TEXT);
                                                    }
                                                }
                                                
                                                if(user->name=="server"){
                                                    /*所有server端的请求,将输入推入缓存,消费者自动消费*/
                                                    Business::getInstance().pushProducer(root);
                                                }
                                            } },
                                    .drain = [](auto * /*ws*/) {},
                                    .ping = [](auto * /*ws*/, std::string_view) {},
                                    .pong = [](auto * /*ws*/, std::string_view) {},
                                    .close = [](auto * ws, int /*code*/, std::string_view /*message*/) {
                                        PerSocketData *user = static_cast<PerSocketData *>(ws->getUserData());
                                        if(user->name=="server"){
                                            /*如果webrtc server关闭ws,就需要退出所有转码子程序*/
                                            Business::getInstance().quitAllTranscod();
                                        }else{
                                            Business::getInstance().delTranscodData(user->name);
                                        }
                                    }})
        .listen(9001, [](auto *listen_socket)
                {
            if (listen_socket)
            {
                std::cout << "Listening on port " << 9001 << std::endl;
            } });
}

WebSocket_Server::~WebSocket_Server()
{
    uWS::Loop::get()->free();
}

void WebSocket_Server::run()
{
    m_app->run();
}

void WebSocket_Server::publish(const std::string &uuid, const std::string &message)
{
    m_app->publish(uuid, std::string_view(message), uWS::TEXT);
}

void WebSocket_Server::publishServer(const std::string &uuid, const MessageLevel &level, const std::string &message)
{
    Json::FastWriter fwriter;
    Json::Value wRoot;
    switch (level)
    {
    case INFO:
        wRoot["command"] = "Info";
        break;
    case WARN:
        wRoot["command"] = "Warn";
        break;
    case ERROR:
        wRoot["command"] = "Error";
        break;
    default:
        break;
    }
    wRoot["data"]["uuid"] = uuid;
    wRoot["data"]["message"] = message;
    publish("server", fwriter.write(wRoot));
}