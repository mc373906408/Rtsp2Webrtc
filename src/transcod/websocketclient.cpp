#include "websocketclient.h"
#include "json/json.h"
#include "myrtspclient.h"

WebSocket_Client::WebSocket_Client()
{
}

WebSocket_Client::~WebSocket_Client()
{
}

void WebSocket_Client::setMutex_Quit(std::shared_ptr<bool> isQuit, std::shared_ptr<std::mutex> mtx_quit, std::shared_ptr<std::condition_variable> cv_quit)
{
    m_isQuit = isQuit;
    m_mtx_quit = mtx_quit;
    m_cv_quit = cv_quit;
}

bool WebSocket_Client::openWebSocket(const std::string &wsurl)
{
    m_ws = std::move(std::unique_ptr<WebSocket>(WebSocket::from_url(wsurl)));
    if (!m_ws)
    {
        return false;
    }

    auto producer = [this]()
    {
        while (m_ws->getReadyState() != WebSocket::CLOSED)
        {
            /*避免过高的CPU占用*/
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            {
                std::unique_lock<std::mutex> quitlock(*m_mtx_quit);
                if (*m_isQuit)
                {
                    return;
                }
            }
            std::unique_lock<std::mutex> sendlock(m_sendMtx);
            WebSocket::pointer wsp = &*m_ws;
            m_ws->poll();
            m_ws->dispatch([wsp, this](const std::string &message)
                           {
                            Json::Reader reader; 
                            Json::Value root;
                            if(reader.parse(message,root)){
                                auto command = root["command"].asString();
                                if(command == "OpenDevice"){
                                    MyRTSPClient::getInstance().openDevice(root["data"]);
                                }
                                else if(command=="JoinRoom"){
                                    MyRTSPClient::getInstance().joinRoom(root["data"]);
                                }else if(command=="QuitRoom"){
                                    MyRTSPClient::getInstance().quitRoom(root["data"]["roomid"].asString());
                                }else if(command=="CloseDevice"){
                                    MyRTSPClient::getInstance().closeDevice();
                                }
                            } });
        }
        /*如果websokcet断开就自动退出*/
        {
            std::unique_lock<std::mutex> quitlock(*m_mtx_quit);
            *m_isQuit = true;
        }
        m_cv_quit->notify_all();
    };
    // 启动线程
    m_jt = std::jthread{producer};
    return true;
}

void WebSocket_Client::send(const std::string &message)
{
    std::unique_lock<std::mutex> sendlock(m_sendMtx);
    m_ws->send(message);
}

void WebSocket_Client::sendUUID(const std::string &uuid)
{
    Json::FastWriter fwriter;
    Json::Value wRoot;
    wRoot["command"] = "SetName";
    wRoot["data"]["name"] = uuid;
    send(fwriter.write(wRoot));
}