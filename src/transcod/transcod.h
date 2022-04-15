#ifndef TRANSCOD_H
#define TRANSCOD_H

#include <memory>
#include <mutex>
#include <condition_variable>
#include "json/json.h"

extern "C"
{
#include <libavutil/hwcontext.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/time.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}

class Transcod
{
public:
    struct DeviceMediaData
    {
        const char *videoCodec;        // 视频解码器
        const char *audioCodec;        // 音频解码器
        int diffVideoTimestamp;        // 视频帧时间差
        unsigned char *extradata;      // 视频nalu头
        int extradata_size;            // 视频nalu头长度
        int video_timestamp_frequency; // 视频时间基
        int channels;                  // 声道数
        int sample_rate;               // 音频采样率
    };

private:
    enum TranscodType
    {
        NOTSELECT,
        NVIDIA,
        INTEL,
        SOFTWARE
    };
    class Outfmt
    {
    public:
        ~Outfmt()
        {
            if (video_ofmt_ctx)
            {
                avformat_close_input(&video_ofmt_ctx);
            }
            if (audio_ofmt_ctx)
            {
                avformat_close_input(&audio_ofmt_ctx);
            }
        }

    public:
        bool audio_init = false; // 是否已经初始化过音频
        bool video_init = false; // 是否已经初始化过视频
        std::string audio_rtp_url;                 // 音频url
        std::string video_rtp_url;                 // 视频url
        AVFormatContext *video_ofmt_ctx = nullptr; // 输出视频IO上下文
        AVFormatContext *audio_ofmt_ctx = nullptr; // 输出音频IO上下文
        AVStream *video_ost = nullptr;             // 视频输出流
        AVStream *audio_ost = nullptr;             // 音频输出流
        int64_t video_last_pts = -1;               // 上个音视频包的pts
        int64_t audio_last_pts = -1;               // 上个音视频包的pts
    };

    /*包*/
    class Deleter_AVPacket
    {
    public:
        void operator()(AVPacket *obj)
        {
            av_packet_free(&obj);
        }
    };

public:
    explicit Transcod();
    ~Transcod();
    /*初始化转码*/
    int openDevice(const Json::Value &data, const DeviceMediaData &mediaData);
    /*发送转码数据*/
    int sendVideoData(const int64_t &data_num, unsigned char *data, const int &data_size, const int &idr, const int64_t &timestamp);
    int sendAudioData(const int64_t &data_num, unsigned char *data, const int &data_size, const int64_t &timestamp, const int &diffTimestamp);
    /*包计数*/
    void addVideoPktNum();
    void wakeVideoTranscod();
    void addAudioPktNum();
    void wakeAudioTranscod();

    /*加入新的房间*/
    void joinRoom(const Json::Value &data);
    /*退出房间*/
    void quitRoom(const std::string &roomid);
    /*获取房间数量*/
    int getRoomNum();
public:
    static enum AVPixelFormat get_NVIDIA_hwdevice_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts);
    static enum AVPixelFormat get_INTEL_hwdevice_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts);

private:
    /*检测显卡支持的转码类型*/
    void detectVga();
    /*判断显卡与支持的转码类型,设置硬件还是软件*/
    void setTranscodType();
    /*启动shell并读取数据*/
    int shellexec(const char *cmd, std::vector<std::string> &resvec);
    /*打开rtp地址*/
    int openRtpUrl(std::shared_ptr<Outfmt> outfmt);
    /*打开视频解码器*/
    int openVideoDecoder(const int &timestamp_diff, unsigned char *extradata, const int &extradata_size);
    /*打开音频解码器*/
    int openAudioDecoder(const int &sample_rate, const int &channels);
    /*打开视频编码器*/
    int openVideoEncoder();
    /*打开音频编码器*/
    int openAudioEncoder();
    /*打开音频重采样*/
    int openAudioSwrContext(std::shared_ptr<AVFrame> frame);
    /*视频转码*/
    int videoTranscod(std::unique_ptr<AVPacket, Deleter_AVPacket> videoPkt);
    /*打开视频滤镜*/
    int openVideoFilter(const int &width, const int &height);
    /*视频滤镜处理*/
    int videoFilter(std::shared_ptr<AVFrame> frame);
    /*视频编码*/
    int videoEncodeWrite(std::shared_ptr<AVFrame> frame);
    /*音频转码*/
    int audioTranscod(std::unique_ptr<AVPacket, Deleter_AVPacket> audioPkt);
    /*音频编码*/
    int audioEncodeWrite(std::shared_ptr<AVFrame> frame);
    /*将音频数据推入fifo*/
    int audioAddSamplesToFifo(const int &nb_samples);
    /*增加输出流*/
    void addOutfmt(const Json::Value &data);
    /*删除输出流*/
    void removeOutfmt(const std::string &uuid);

private:
    /*是否有音频输入*/
    bool m_isOpenAudio = false;
    /*输出rtp roomid 为key*/
    std::mutex m_mtx_outfmt;
    std::map<std::string, std::shared_ptr<Outfmt>> m_map_outfmt;

    /*转码方式*/
    TranscodType m_TranscodType = NOTSELECT;
    bool m_hwH264 = false, m_hwH265 = false;

    /*时间戳频率*/
    int m_video_timestamp_frequency;

    /*下一个转码*/
    std::mutex m_mtx_next_videoTranscod;
    std::condition_variable m_cv_next_videoTranscod;
    std::mutex m_mtx_next_audioTranscod;
    std::condition_variable m_cv_next_audioTranscod;
    /*下一个转码的序号*/
    int64_t m_next_videoTranscod_num = 0;
    int64_t m_next_audioTranscod_num = 0;

    /*编码器初始化*/
    bool m_video_enc_init = false;

    /*编解码器选择*/
    AVCodec *m_video_dec_codec = nullptr, *m_video_enc_codec = nullptr, *m_audio_dec_codec = nullptr, *m_audio_enc_codec = nullptr;

    /*硬件加速器*/
    class Deleter_AVBufferRef
    {
    public:
        void operator()(AVBufferRef *obj)
        {
            av_buffer_unref(&obj);
        }
    };
    std::unique_ptr<AVBufferRef, Deleter_AVBufferRef> m_hw_device_ctx = nullptr;

    /*编解码器*/
    class Deleter_AVCodecContext
    {
    public:
        void operator()(AVCodecContext *obj)
        {
            avcodec_free_context(&obj);
        }
    };
    std::unique_ptr<AVCodecContext, Deleter_AVCodecContext> m_video_decoder_ctx = nullptr, m_video_encoder_ctx = nullptr, m_audio_decoder_ctx = nullptr, m_audio_encoder_ctx = nullptr;

    /*视频滤镜,缩放使用*/
    /*滤镜*/
    class Deleter_AVFilterGraph
    {
    public:
        void operator()(AVFilterGraph *obj)
        {
            avfilter_graph_free(&obj);
        }
    };
    std::unique_ptr<AVFilterGraph, Deleter_AVFilterGraph> m_filter_graph = nullptr;
    /*滤镜的输入*/
    AVFilterContext *m_buffersrc_ctx = nullptr;
    /*滤镜的输出*/
    AVFilterContext *m_buffersink_ctx = nullptr;

    /*音频重采样*/
    class Deleter_SwrContext
    {
    public:
        void operator()(SwrContext *obj)
        {
            swr_free(&obj);
        }
    };
    std::unique_ptr<SwrContext, Deleter_SwrContext> m_resample_context = nullptr;
    bool m_audio_swrContext_init = false;

    /*转换样本的数量*/
    int m_max_dst_nb_samples, m_dst_nb_samples;
    /*重采样后的数据*/
    class Deleter_DstData
    {
    public:
        void operator()(uint8_t **obj)
        {
            av_freep(&obj[0]);
            av_freep(&obj);
        }
    };
    std::unique_ptr<uint8_t *, Deleter_DstData> m_dst_data = nullptr;
    /*重采样后的数据长度*/
    int m_dst_linesize;
    /*音频fifo*/
    class Deleter_AVAudioFifo
    {
    public:
        void operator()(AVAudioFifo *obj)
        {
            av_audio_fifo_free(obj);
        }
    };
    std::unique_ptr<AVAudioFifo, Deleter_AVAudioFifo> m_fifo = nullptr;
    /*音频pts*/
    int64_t m_audio_pts = -1;
    /*音频起始包的位置*/
    int m_audio_firstPktNum = 0;
};

#endif