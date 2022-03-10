#include "rtsp_client.h"

Rtsp_Client::Rtsp_Client()
{
    /*设置环境*/
    m_scheduler = BasicTaskScheduler::createNew();
    m_env = BasicUsageEnvironment::createNew(*m_scheduler);
}

Rtsp_Client::~Rtsp_Client()
{
}