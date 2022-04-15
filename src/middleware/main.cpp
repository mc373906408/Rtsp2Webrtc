#include "App.h"
#include "business.h"
#include "json/json.h"
#include "websocket_server.h"

int main()
{
    /*启动业务单例*/
    Business::getInstance();
    /*启动*/
    WebSocket_Server::getInstance().run();
    return 0;
}