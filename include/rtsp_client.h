#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

class Rtsp_Client
{
public:
    static Rtsp_Client& getInstance()
    {
        static Rtsp_Client instance;
        return instance;
    }

    /*打开RTSP地址*/
    int openRTSP();
private:
    explicit Rtsp_Client();
    ~Rtsp_Client();
private:
    /*任务调度器*/
    TaskScheduler *m_scheduler=nullptr;
    /*环境*/
    UsageEnvironment *m_env=nullptr;
};