#include <thread>
#include <condition_variable>

#include "debugDefine.h"
#include "websocketclient.h"

#include "myrtspclient.h"

int main(int argc, char **argv)
{
#ifndef Debug
    if (argc < 2)
    {
        /*启动参数少*/
        return -1;
    }
#endif

    std::shared_ptr<bool> isQuit(std::make_shared<bool>(false));
    std::shared_ptr<std::condition_variable> cv_quit(std::make_shared<std::condition_variable>());
    std::shared_ptr<std::mutex> mtx_quit(std::make_shared<std::mutex>());

    /*启动websocket client*/
    WebSocket_Client::getInstance().setMutex_Quit(isQuit, mtx_quit, cv_quit);
    if (!WebSocket_Client::getInstance().openWebSocket("ws://localhost:9001"))
    {
        /*无法链接websocket server*/
        return -1;
    }
    /*设置转码的退出锁*/
    MyRTSPClient::getInstance().setMutex_Quit(isQuit, mtx_quit, cv_quit);

#ifndef Debug
    /*通知中间件,并获取转码参数*/
    WebSocket_Client::getInstance().sendUUID(argv[1]);
#else
    WebSocket_Client::getInstance().sendUUID("333");
#endif

    {
        std::unique_lock<std::mutex> lock(*mtx_quit);
        while (!*isQuit)
        {
            cv_quit->wait(lock);
        };
    }

    return 0;
}