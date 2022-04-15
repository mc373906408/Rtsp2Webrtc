#include "business.h"
#include "websocket_server.h"
#include "fmt/format.h"
#include "debugDefine.h"
#include <iostream>

Business::Business()
{
    auto consumer = [this]()
    {
        while (!m_quit)
        {
            {
                std::unique_lock<std::mutex> lock(m_mtx_cv);
                while (m_queue_producer.empty())
                {
                    m_cv.wait(lock);
                }
            }
            Json::Value root;
            {
                std::unique_lock<std::mutex> lock(m_mtx_cv);
                root = m_queue_producer.front();
                m_queue_producer.pop();
            }

            auto command = root["command"].asString();
            if (command == "OpenDevice")
            {
                /*启动转码或加入房间*/
                openDevice(root);
            }
            else if (command == "CloseDevice")
            {
                /*退出房间*/
                closeDevice(root);
            }
        }
    };
    // 启动线程
    m_jt = std::jthread{consumer};
}
Business::~Business()
{
    m_quit = true;
    m_jt.request_stop();
}

void Business::pushProducer(const Json::Value &root)
{
    std::unique_lock<std::mutex> lock(m_mtx_cv);
    m_queue_producer.push(root);
    m_cv.notify_all();
}

void Business::openDevice(const Json::Value &root)
{
    auto data = root["data"];
    std::string uuid = data["uuid"].asString();
    std::string roomid = data["roomid"].asString();
    std::string rtsp = data["rtsp"].asString();
    std::string webrtcLanIP = data["webrtcLanIP"].asString();
    std::pair<size_t, size_t> videoPort = {-1, -1};
    if (data.isMember("videoPort"))
    {
        videoPort.first = data["videoPort"][0].asInt();
        videoPort.second = data["videoPort"][1].asInt();
    }
    /*判断参数完整性,因为音频可能为空不判断音频端口*/
    if (uuid.empty() || roomid.empty() || rtsp.empty() || webrtcLanIP.empty() || videoPort.first <= 0 || videoPort.second <= 0)
    {
        WebSocket_Server::getInstance().publishServer(uuid, WebSocket_Server::ERROR, "Error_Parameter");
        return;
    }

    bool transcodIsStart = false;
    {
        std::unique_lock<std::mutex> lock(m_mtx_transcodData);
        /*判断转码子程序是否已经启动*/
        transcodIsStart = (m_map_transcodData.count(uuid) == 1);
        /*记录*/
        Json::FastWriter fwriter;
        m_map_transcodData[uuid] = fwriter.write(root);
    }

    if (transcodIsStart)
    {
        /*如果转码已经启动,就下发增加房间命令*/
        Json::Value _root = root;
        _root["command"] = "JoinRoom";
        Json::FastWriter fwriter;
        WebSocket_Server::getInstance().publish(uuid, fwriter.write(_root));
    }
    else
    {
#ifndef Debug
        /*启动转码子程序*/
        createTranscod(uuid);
#endif
    }
}

void Business::closeDevice(const Json::Value &root)
{
    auto data = root["data"];
    std::string uuid = data["uuid"].asString();
    std::string roomid = data["roomid"].asString();
    /*判断参数完整性*/
    if (uuid.empty() || roomid.empty())
    {
        WebSocket_Server::getInstance().publishServer(uuid, WebSocket_Server::ERROR, "Error_Parameter");
        return;
    }
    Json::FastWriter fwriter;
    Json::Value _root = root;
    _root["command"] = "QuitRoom";
    WebSocket_Server::getInstance().publish(uuid, fwriter.write(_root));
}

void Business::createTranscod(const std::string &uuid)
{
    int res = 0;
    res = system(fmt::format("./transcod {} &", uuid).c_str());
    if (res == -1 || (WIFEXITED(res) && WEXITSTATUS(res) != 0))
    {
        /*转码程序启动失败*/
        WebSocket_Server::getInstance().publishServer(uuid, WebSocket_Server::ERROR, "Error_createTranscod");
        return;
    }
}

std::string Business::getTranscodData(const std::string &key)
{
    std::unique_lock<std::mutex> lock(m_mtx_transcodData);
    std::string transcodData = m_map_transcodData[key];
    return transcodData;
}

void Business::delTranscodData(const std::string &key)
{
    {
        std::unique_lock<std::mutex> lock(m_mtx_transcodData);
#ifndef Debug
        m_map_transcodData.erase(key);
#endif
    }
    WebSocket_Server::getInstance().publishServer(key, WebSocket_Server::WARN, "WARN_CloseDevice");
}

void Business::quitAllTranscod()
{
    std::vector<std::string> keys;
    {
        std::unique_lock<std::mutex> lock(m_mtx_transcodData);
        for (const auto &[key, value] : m_map_transcodData)
        {
            keys.push_back(key);
        }
    }

    for (auto key : keys)
    {
        Json::FastWriter fwriter;
        Json::Value root;
        root["command"] = "CloseDevice";
        WebSocket_Server::getInstance().publish(key, fwriter.write(root));
    }
}