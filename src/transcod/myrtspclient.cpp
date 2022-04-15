#include "myrtspclient.h"
#include <iostream>
#include "fmt/format.h"
#include <thread>

void MyRTSPClient::frameHandler(void *arg, RTP_FRAME_TYPE frame_type, int64_t timestamp, unsigned char *buf, int len)
{
    int ret = 0;
    MyRTSPClient *rtspClient = reinterpret_cast<MyRTSPClient *>(arg);

    // fwrite(buf, len, 1, fp_dump);
    // if (a_fp_dump && frame_type == FRAME_TYPE_AUDIO)
    //     fwrite(buf, len, 1, a_fp_dump);
    // if (d_fp_dump && frame_type == FRAME_TYPE_ETC)
    //     fwrite(buf, len, 1, d_fp_dump);

    auto unpackPkt = [buf, len](unsigned char *pktdata, int &pktdata_size)
    {
        pktdata_size += len;
        pktdata = reinterpret_cast<unsigned char *>(realloc(pktdata, pktdata_size));
        memcpy(pktdata + (pktdata_size - len), buf, len);
        return pktdata;
    };

    if (frame_type == FRAME_TYPE_VIDEO)
    {
        auto setVideoPktType = [buf, len](const char *videoCodec)
        {
            if (strcmp(videoCodec, "H264") == 0)
            {
                switch (buf[4] & 0x1f)
                {
                case 5: /*IDR帧*/
                    return VideoPktType::IDR;
                    break;
                case 6: /*sei*/
                case 7: /*sps*/
                case 8: /*pps*/
                    return VideoPktType::NotPkt;
                    break;
                default:
                    return VideoPktType::NotIDR;
                    break;
                }
            }
            else if (strcmp(videoCodec, "H265") == 0)
            {
                switch ((buf[4] & 0x7E) >> 1)
                {
                case 19: /*IDR帧*/
                    return VideoPktType::IDR;
                    break;
                case 32: /*vps*/
                case 33: /*sps*/
                case 34: /*pps*/
                case 39: /*sei*/
                    return VideoPktType::NotPkt;
                    break;

                default:
                    return VideoPktType::NotIDR;
                    break;
                }
            }
        };

        if (rtspClient->m_lastVideoTimestamp == -1)
        {
            rtspClient->m_lastVideoTimestamp = timestamp;
        }

        VideoPktType videoPktType = setVideoPktType(rtspClient->m_videoCodec);
        /*第一次初始化ffmpeg*/
        if (!rtspClient->m_isFFmpegInit)
        {
            /*未初始化Extradata*/
            if (!rtspClient->m_isExtradataInit)
            {
                if (videoPktType == IDR)
                {
                    rtspClient->m_isExtradataInit = true;
                }
                else
                {
                    /*获取vps sps pps*/
                    rtspClient->m_extradata = unpackPkt(rtspClient->m_extradata, rtspClient->m_extradata_size);
                }
            }

            /*未获取帧率*/
            if (rtspClient->m_diffVideoTimestamp == 0)
            {
                rtspClient->m_diffVideoTimestamp = timestamp - rtspClient->m_lastVideoTimestamp;
            }

            /*未获取第一帧IDR*/
            if (!rtspClient->m_isFirstIDRdata)
            {
                if (strcmp(rtspClient->m_videoCodec, "H264") == 0)
                {
                    /*H264一次会发多个IDR帧,需要全拼起来*/
                    if (rtspClient->m_lastVideoTimestamp != timestamp)
                    {
                        rtspClient->m_isFirstIDRdata = true;
                    }
                    else
                    {
                        rtspClient->m_firstIDRdata = unpackPkt(rtspClient->m_firstIDRdata, rtspClient->m_firstIDRdata_size);
                    }
                }
                else if (strcmp(rtspClient->m_videoCodec, "H265") == 0)
                {
                    rtspClient->m_firstIDRdata = unpackPkt(rtspClient->m_firstIDRdata, rtspClient->m_firstIDRdata_size);
                    if (videoPktType == IDR)
                    {
                        rtspClient->m_isFirstIDRdata = true;
                    }
                }
            }

            if (rtspClient->m_isExtradataInit && rtspClient->m_diffVideoTimestamp > 0 && rtspClient->m_isFirstIDRdata)
            {
                rtspClient->m_isFFmpegInit = true;
                Transcod::DeviceMediaData mediaData;
                mediaData.videoCodec = rtspClient->m_videoCodec;
                mediaData.audioCodec = rtspClient->m_audioCodec;
                mediaData.diffVideoTimestamp = rtspClient->m_diffVideoTimestamp;
                mediaData.extradata = rtspClient->m_extradata;
                mediaData.extradata_size = rtspClient->m_extradata_size;
                mediaData.video_timestamp_frequency = rtspClient->m_video_timestamp_frequency;
                mediaData.sample_rate = rtspClient->m_audioSampleRate;
                mediaData.channels = rtspClient->m_audioChannel;

                /*初始化转码参数*/
                ret = rtspClient->m_transcod->openDevice(rtspClient->m_rtspData, mediaData);
                /*清理*/
                /*extradata会永久保存,所以拷贝进ffmpeg,清理外层*/
                free(rtspClient->m_extradata);
                rtspClient->m_extradata = nullptr;
                rtspClient->m_extradata_size = 0;
                if (ret < 0)
                {
                    /*如果转码失败就自动退出*/
                    rtspClient->closeDevice();
                    return;
                }
                /*转码第一帧*/
                ret = rtspClient->m_transcod->sendVideoData(rtspClient->m_videoPktNum, rtspClient->m_firstIDRdata, rtspClient->m_firstIDRdata_size, 1, rtspClient->m_lastVideoTimestamp);
                rtspClient->m_videoPktNum++;
                rtspClient->m_transcod->addVideoPktNum();
                /*清理*/
                /*idr数据移动到ffmpeg了,不需要二次释放*/
                rtspClient->m_firstIDRdata = nullptr;
                rtspClient->m_firstIDRdata_size = 0;
                if (ret < 0)
                {
                    /*如果转码失败就自动退出*/
                    rtspClient->closeDevice();
                    return;
                }
            }
        }

        /*已经初始化ffmpeg*/
        if (rtspClient->m_isFFmpegInit)
        {
            auto sendData = [rtspClient](int idr, int64_t timestamp)
            {
                unsigned char *pktdata = reinterpret_cast<unsigned char *>(malloc(rtspClient->m_videoPktdata_size));
                memcpy(pktdata, rtspClient->m_videoPktdata, rtspClient->m_videoPktdata_size);
                auto fut = std::async(std::bind(&Transcod::sendVideoData, rtspClient->m_transcod, rtspClient->m_videoPktNum, pktdata, rtspClient->m_videoPktdata_size, idr, timestamp));
                rtspClient->m_videoPktNum++;
                rtspClient->m_ThreadPool->enqueue([](decltype(fut) _fut, std::shared_ptr<Transcod> transcod)
                                                  { transcod->wakeVideoTranscod();
                                                      _fut.get(); 
                                                  transcod->addVideoPktNum();
                                                  transcod->wakeVideoTranscod(); },
                                                  std::move(fut), rtspClient->m_transcod);
                rtspClient->m_videoPktdata = nullptr;
                rtspClient->m_videoPktdata_size = 0;
            };
            if (strcmp(rtspClient->m_videoCodec, "H264") == 0)
            {
                /*H264一次会发多个IDR帧,需要全拼起来*/
                if (rtspClient->m_lastVideoTimestamp != timestamp)
                {
                    if (rtspClient->m_lastVideoPktType == IDR)
                    {
                        sendData(1, rtspClient->m_lastVideoTimestamp);
                    }
                    else if (rtspClient->m_lastVideoPktType == NotIDR)
                    {
                        sendData(0, rtspClient->m_lastVideoTimestamp);
                    }
                }
                rtspClient->m_videoPktdata = unpackPkt(rtspClient->m_videoPktdata, rtspClient->m_videoPktdata_size);
                rtspClient->m_lastVideoPktType = videoPktType;
            }
            else if (strcmp(rtspClient->m_videoCodec, "H265") == 0)
            {
                rtspClient->m_videoPktdata = unpackPkt(rtspClient->m_videoPktdata, rtspClient->m_videoPktdata_size);
                if (videoPktType == IDR)
                {
                    sendData(1, timestamp);
                }
                else if (videoPktType == NotIDR)
                {
                    sendData(0, timestamp);
                }
            }
        }
        /*覆盖时间戳*/
        rtspClient->m_lastVideoTimestamp = timestamp;
    }

    if (frame_type == FRAME_TYPE_AUDIO)
    {
        if (rtspClient->m_lastAudioTimestamp == -1)
        {
            rtspClient->m_lastAudioTimestamp = timestamp;
        }
        /*未获取帧率*/
        if (rtspClient->m_diffAudioTimestamp == 0)
        {
            rtspClient->m_diffAudioTimestamp = timestamp - rtspClient->m_lastAudioTimestamp;
        }

        if (rtspClient->m_isFFmpegInit && rtspClient->m_diffAudioTimestamp > 0)
        {
            unsigned char *pktdata = reinterpret_cast<unsigned char *>(malloc(len));
            memcpy(pktdata, buf, len);
            auto fut = std::async(std::bind(&Transcod::sendAudioData, rtspClient->m_transcod, rtspClient->m_audioPktNum, pktdata, len, timestamp, rtspClient->m_diffAudioTimestamp));
            rtspClient->m_audioPktNum++;
            rtspClient->m_ThreadPool->enqueue([](decltype(fut) _fut, std::shared_ptr<Transcod> transcod)
                                              { 
                                                  transcod->wakeAudioTranscod();
                                                  _fut.get(); 
                                                  transcod->addAudioPktNum();
                                                  transcod->wakeAudioTranscod(); },
                                              std::move(fut), rtspClient->m_transcod);
        }
        rtspClient->m_lastAudioTimestamp = timestamp;
    }

}

void MyRTSPClient::closeHandler(void *arg, int err, int result)
{
    printf("RTSP session disconnected, err : %d, result : %d", err, result);
}

MyRTSPClient::MyRTSPClient()
{
    RTSPCommonEnv::SetDebugFlag(DEBUG_FLAG_RTSP);
    m_rtspClient = std::unique_ptr<RTSPClient, Deleter_RTSPClient>(std::move(new RTSPClient()));
    m_transcod = std::make_shared<Transcod>();
    m_ThreadPool = std::make_shared<ThreadPool>(4);
}

MyRTSPClient::~MyRTSPClient()
{
}

void MyRTSPClient::setMutex_Quit(std::shared_ptr<bool> isQuit, std::shared_ptr<std::mutex> mtx_quit, std::shared_ptr<std::condition_variable> cv_quit)
{
    m_isQuit = isQuit;
    m_mtx_quit = mtx_quit;
    m_cv_quit = cv_quit;
}
void MyRTSPClient::openDevice(const Json::Value &data)
{
    int ret = 0;
    m_rtspData = data;
    m_rtspUrl = data["rtsp"].asString();

    ret = m_rtspClient->openURL(m_rtspUrl.c_str(), 0);
    if (ret < 0)
    {
        std::cout << "打开url失败" << std::endl;
    }
    if (ret >= 0)
    {
        m_videoCodec = m_rtspClient->videoCodec();
        m_audioCodec = m_rtspClient->audioCodec();
        /*获取时间戳频率*/
        MediaSubsessionIterator *iter = new MediaSubsessionIterator(m_rtspClient->mediaSession());
        MediaSubsession *subsession = NULL;
        while ((subsession = iter->next()) != NULL)
        {
            if (strcmp(subsession->mediumName(), "video") == 0)
            {
                m_video_timestamp_frequency = subsession->rtpTimestampFrequency();
            }
        }
        m_audioChannel = m_rtspClient->audioChannel();
        m_audioSampleRate = m_rtspClient->audioSampleRate();
        ret = m_rtspClient->playURL(frameHandler, this, closeHandler, this);
        if (ret < 0)
        {
            std::cout << "播放url失败" << std::endl;
        }
    }

    if (ret < 0)
    {
        /*如果转码失败就自动退出*/
        {
            std::unique_lock<std::mutex> quitlock(*m_mtx_quit);
            *m_isQuit = true;
        }
        m_cv_quit->notify_all();
    }
}

void MyRTSPClient::joinRoom(const Json::Value &data)
{
    m_transcod->joinRoom(data);
}

void MyRTSPClient::quitRoom(const std::string &roomid)
{
    m_transcod->quitRoom(roomid);
    if(m_transcod->getRoomNum()==0){
        /*没有房间了自动退出*/
        closeDevice();
    }
}

void MyRTSPClient::closeDevice()
{
    std::unique_lock<std::mutex> quitlock(*m_mtx_quit);
    *m_isQuit = true;
    m_cv_quit->notify_all();
}
