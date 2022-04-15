#ifndef BUSINESS_H
#define BUSINESS_H

#include <stdlib.h>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>
#include <map>

#include "json/json.h"

class Business
{
public:
    static Business &getInstance()
    {
        static Business instance;
        return instance;
    }
    /*push任务*/
    void pushProducer(const Json::Value &root);
    /*get transcodData*/
    std::string getTranscodData(const std::string &key);
    /*del transcodData*/
    void delTranscodData(const std::string &key);
    /*quit all transcod*/
    void quitAllTranscod();
private:
    /*创建转码进程*/
    void createTranscod(const std::string &uuid);
    /*打开设备或加入房间*/
    void openDevice(const Json::Value& root);
    /*退出房间,如果全部退出就会关闭设备*/
    void closeDevice(const Json::Value& root);

private:
    explicit Business();
    ~Business();

    Business(const Business &sg) = delete;
    Business &operator=(const Business &sg) = delete;

private:
    bool m_quit = false;
    std::mutex m_mtx_cv;
    std::mutex m_mtx_transcodData;
    std::queue<Json::Value> m_queue_producer;
    std::condition_variable m_cv;
    std::jthread m_jt;
    std::map<std::string,std::string> m_map_transcodData;
};

#endif