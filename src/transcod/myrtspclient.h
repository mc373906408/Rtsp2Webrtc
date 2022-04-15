#ifndef MYRTSPCLIENT_H
#define MYRTSPCLIENT_H

#include <memory>
#include <mutex>
#include <condition_variable>

#include "transcod.h"
#include "json/json.h"
#include "RTSPCommonEnv.h"
#include "RTSPClient.h"
#include "ThreadPool.h"

class MyRTSPClient
{
public:
    static MyRTSPClient &getInstance()
    {
        static MyRTSPClient instance;
        return instance;
    }
    /*共享退出锁*/
    void setMutex_Quit(std::shared_ptr<bool> isQuit, std::shared_ptr<std::mutex> mtx_quit, std::shared_ptr<std::condition_variable> cv_quit);
    /*启动转码*/
    void openDevice(const Json::Value &data);
    /*加入新的房间*/
    void joinRoom(const Json::Value &data);
    /*退出房间*/
    void quitRoom(const std::string &roomid);
    /*关闭转码*/
    void closeDevice();

public:
    static void frameHandler(void *arg, RTP_FRAME_TYPE frame_type, int64_t timestamp, unsigned char *buf, int len);

    static void closeHandler(void *arg, int err, int result);

private:
    explicit MyRTSPClient();
    ~MyRTSPClient();

    MyRTSPClient(const MyRTSPClient &sg) = delete;
    MyRTSPClient &operator=(const MyRTSPClient &sg) = delete;

private:
    enum VideoPktType
    {
        NotPkt,
        IDR,
        NotIDR
    };

    std::shared_ptr<std::mutex> m_mtx_quit;
    std::shared_ptr<std::condition_variable> m_cv_quit;
    std::shared_ptr<bool> m_isQuit;

    /*线程池*/
    std::shared_ptr<ThreadPool> m_ThreadPool;

    std::string m_rtspUrl;
    Json::Value m_rtspData;

    bool m_isFFmpegInit = false;

    /*视频处理*/
    /*初始化vps\sps\pps*/
    bool m_isExtradataInit = false;
    unsigned char *m_extradata = nullptr;
    int m_extradata_size = 0;

    /*记录上一次时间戳*/
    int64_t m_lastVideoTimestamp = -1;
    /*时间戳的差*/
    int m_diffVideoTimestamp = 0;
    /*记录上一个帧类型,H264特殊处理*/
    VideoPktType m_lastVideoPktType;

    /*打成整包*/
    unsigned char *m_videoPktdata = nullptr;
    int m_videoPktdata_size = 0;
    int64_t m_videoPktNum = 0;

    const char *m_videoCodec;
    int m_video_timestamp_frequency;

    /*第一帧IDR*/
    bool m_isFirstIDRdata = false;
    unsigned char *m_firstIDRdata = nullptr;
    int m_firstIDRdata_size = 0;

    /*音频处理*/
    /*记录上一次时间戳*/
    int64_t m_lastAudioTimestamp = -1;
    /*时间戳的差*/
    int m_diffAudioTimestamp = 0;

    /*打成整包*/
    unsigned char *m_audioPktdata = nullptr;
    int m_audioPktdata_size = 0;
    int64_t m_audioPktNum = 0;

    const char *m_audioCodec;
    int m_audioChannel;
    int m_audioSampleRate;

    class Deleter_RTSPClient
    {
    public:
        void operator()(RTSPClient *obj)
        {
            obj->closeURL();
            delete obj;
        }
    };
    std::unique_ptr<RTSPClient, Deleter_RTSPClient> m_rtspClient = nullptr;

    std::shared_ptr<Transcod> m_transcod = nullptr;
};

#endif