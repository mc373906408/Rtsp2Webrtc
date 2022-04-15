#include <iostream>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/hwcontext_qsv.h>
#include <libavutil/imgutils.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
}

// #define TTY_PATH            "/dev/tty"
// #define STTY_US             "stty raw -echo -F "
// #define STTY_DEF            "stty -raw echo -F "

typedef struct DecodeContext
{
    AVBufferRef *hw_device_ref;
} DecodeContext;

// FILE *output_file = NULL;

/*来自ffmpeg官方文档*/
static AVPixelFormat get_format(AVCodecContext *avctx, const enum AVPixelFormat *pix_fmts)
{
    while (*pix_fmts != AV_PIX_FMT_NONE)
    {
        if (*pix_fmts == AV_PIX_FMT_QSV)
        {
            DecodeContext *decode = (DecodeContext *)avctx->opaque;
            AVHWFramesContext *frames_ctx;
            AVQSVFramesContext *frames_hwctx;
            int ret;

            /* create a pool of surfaces to be used by the decoder */
            avctx->hw_frames_ctx = av_hwframe_ctx_alloc(decode->hw_device_ref);
            if (!avctx->hw_frames_ctx)
                return AV_PIX_FMT_NONE;
            frames_ctx = (AVHWFramesContext *)avctx->hw_frames_ctx->data;
            frames_hwctx = (AVQSVFramesContext *)frames_ctx->hwctx;

            frames_ctx->format = AV_PIX_FMT_QSV;
            frames_ctx->sw_format = avctx->sw_pix_fmt;
            frames_ctx->width = FFALIGN(avctx->coded_width, 32);
            frames_ctx->height = FFALIGN(avctx->coded_height, 32);
            frames_ctx->initial_pool_size = 32;

            frames_hwctx->frame_type = MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;

            ret = av_hwframe_ctx_init(avctx->hw_frames_ctx);
            if (ret < 0)
                return AV_PIX_FMT_NONE;

            return AV_PIX_FMT_QSV;
        }

        pix_fmts++;
    }

    fprintf(stderr, "The QSV pixel format not offered in get_format()\n");

    return AV_PIX_FMT_NONE;
}

static int set_hwframe_ctx(const int &width, const int &height, AVCodecContext *ctx, DecodeContext *decode)
{
    AVBufferRef *hw_frames_ref;
    AVHWFramesContext *frames_ctx = NULL;
    int err = 0;

    if (!(hw_frames_ref = av_hwframe_ctx_alloc(decode->hw_device_ref)))
    {
        return -1;
    }
    frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
    frames_ctx->format = AV_PIX_FMT_QSV;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
    frames_ctx->width = width;
    frames_ctx->height = height;
    /*对象池,不重要,就是加速构造速度*/
    frames_ctx->initial_pool_size = 32;
    err = av_hwframe_ctx_init(hw_frames_ref);
    if (err < 0)
    {
        av_buffer_unref(&hw_frames_ref);
        return err;
    }
    ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
    if (!ctx->hw_frames_ctx)
    {
        err = AVERROR(ENOMEM);
    }

    av_buffer_unref(&hw_frames_ref);
    return err;
}

/*打开rtsp流地址*/
int OpenRtspStream(const char *url, AVFormatContext **ic)
{
    int res = 0;
    AVDictionary *options = nullptr;
    /*设置以tcp方式打开rtsp*/
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    // av_dict_set(&options, "stimeout", "10000000", 0);
    //av_dict_set(&options, "buffer_size", "1024000", 0);  //设置udp的接收缓冲
    // av_dict_set(&options, "flvflags", "no_duration_filesize", 0);
    /*打开rtsp流地址*/
    res = avformat_open_input(ic, url, nullptr, &options);

    if (res < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "打开rtsp流地址失败,%d\n", res);
        return res;
    }
    /*找到视频流信息，视频流+音频流*/
    res = avformat_find_stream_info(*ic, nullptr);
    if (res < 0)
    {
        if (!(*ic))
        {
            avformat_close_input(ic);
        }
        av_log(nullptr, AV_LOG_ERROR, "未找到视频流信息,%d\n", res);
        return res;
    }

    return 0;
}

int OpenAVCodec(int &video_stream_idx, int &audio_stream_idx, DecodeContext *decode, AVCodecContext **videoCodecContext, AVFormatContext *ic)
{
    int res = 0;
    /*获取流索引*/
    video_stream_idx = av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audio_stream_idx = av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO, -1, video_stream_idx, nullptr, 0);
    if (video_stream_idx == -1)
    {
        av_log(nullptr, AV_LOG_ERROR, "未找到视频流\n");
        return -1;
    }

    /*打开硬件设备*/
    res = av_hwdevice_ctx_create(&decode->hw_device_ref, AV_HWDEVICE_TYPE_QSV, "auto", nullptr, 0);
    if (res < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "未打开硬件设备,av_hwdevice_ctx_create\n");
        return res;
    }
    /*根据编解码上下文中的编码id查找对应的解码器*/
    /*只有知道视频的编码方式，才能够根据编码方式去找到解码器*/
    AVCodecParameters *videoCodecParameters = ic->streams[video_stream_idx]->codecpar;
    // AVCodecParameters *audioCodecParameters = (*ic)->streams[audio_stream_idx]->codecpar;
    /*找到硬件解码器,英特尔版本*/
    std::string videoDecoderName = "";
    if (videoCodecParameters->codec_id == AV_CODEC_ID_HEVC)
    {
        videoDecoderName = "hevc_qsv";
    }
    else if (videoCodecParameters->codec_id == AV_CODEC_ID_H264)
    {
        videoDecoderName = "h264_qsv";
    }
    if (videoDecoderName.empty())
    {
        av_log(nullptr, AV_LOG_ERROR, "未找到硬件解码器\n");
        return -1;
    }
    AVCodec *videoCodec = avcodec_find_decoder_by_name(videoDecoderName.c_str());
    if (!videoCodec)
    {
        /*未找到解码器,主要原因是没装msdk*/
        av_log(nullptr, AV_LOG_ERROR, "未找到硬件解码器\n");
        return -1;
    }
    /*选择解码器*/
    *videoCodecContext = avcodec_alloc_context3(videoCodec);

    /*设置解码上下文*/
    res = avcodec_parameters_to_context(*videoCodecContext, videoCodecParameters);
    if (res < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "设置解码上下文失败,avcodec_parameters_to_context,%d\n", res);
        return res;
    }
    /*设置这个属性才会启用硬件加速*/
    (*videoCodecContext)->opaque = &decode->hw_device_ref;
    (*videoCodecContext)->get_format = get_format;
    /*打开解码器*/
    res = avcodec_open2(*videoCodecContext, nullptr, nullptr);
    if (res < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "打开解码器失败,%d\n", res);
        return res;
    }

    return 0;
}

int OpenVideoEnCodec(const int &width, const int &height, const int &bit_rate, const int &video_stream_idx, DecodeContext *decode, AVCodecContext **videoEnCodecContext, AVFormatContext *ic, AVFormatContext *oc)
{
    int res = 0;
    /*英特尔版本*/
    AVCodec *videoEnCodec = avcodec_find_encoder_by_name("h264_qsv");
    if (!videoEnCodec)
    {
        /*未找到编码器,主要原因是没装msdk*/
        av_log(nullptr, AV_LOG_ERROR, "未找到视频硬件编码器\n");
        return -1;
    }

    /*选择编码器*/
    *videoEnCodecContext = avcodec_alloc_context3(videoEnCodec);
    /*设置编码上下文*/
    (*videoEnCodecContext)->flags=0;
    (*videoEnCodecContext)->width = width;
    (*videoEnCodecContext)->height = height;
    /*沿用解码时比特率,帧率*/
    (*videoEnCodecContext)->bit_rate = bit_rate;
    (*videoEnCodecContext)->framerate = ic->streams[video_stream_idx]->r_frame_rate;
    (*videoEnCodecContext)->time_base = av_inv_q((*videoEnCodecContext)->framerate);
    (*videoEnCodecContext)->pix_fmt = AV_PIX_FMT_QSV;

      
     
    /*禁用B帧*/
    // (*videoEnCodecContext)->max_b_frames = 0;
    /*I帧间隔*/
    (*videoEnCodecContext)->gop_size = (*videoEnCodecContext)->time_base.den * 2;
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
    {
        /*适合流媒体??*/
        (*videoEnCodecContext)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    /*qsv特殊设置*/
    /*避免丢帧??  issues:https://github.com/Intel-Media-SDK/MediaSDK/issues/1146*/
    av_opt_set((*videoEnCodecContext)->priv_data, "async_depth", "1", 0);
    av_opt_set((*videoEnCodecContext)->priv_data, "max_dec_frame_buffering", "1", 0);
    av_opt_set((*videoEnCodecContext)->priv_data, "look_ahead", "0", 0);

    /*设置硬件buffer上下文*/
    set_hwframe_ctx(width, height, *videoEnCodecContext, decode);

    /*打开编码器*/
    res = avcodec_open2(*videoEnCodecContext, videoEnCodec, nullptr);
    if (res < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "打开视频编码器失败,%d\n", res);
        return res;
    }
    return 0;
}

int AVDecode(const int &video_stream_idx, const int &audio_stream_idx, AVFrame **hw_videoFrame, AVCodecContext *videoCodecContext, AVFormatContext *ic)
{
    int res = 0;
    AVPacket *avPacket = av_packet_alloc();
    /*从流中读取的数据包*/
    res = av_read_frame(ic, avPacket);
    if (res < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "未读到数据包,av_read_frame,%d\n", res);
        goto ERROR1;
    }
    /*视频包*/
    if (avPacket->stream_index == video_stream_idx)
    {
        /*发送 AVPacket 数据包给视频解码器*/
        res = avcodec_send_packet(videoCodecContext, avPacket);
        if (res < 0)
        {
            av_log(nullptr, AV_LOG_ERROR, "解码失败,avcodec_send_packet,%d\n", res);
            goto ERROR1;
        }
        av_packet_free(&avPacket);

        /*解码后存到GPUavFrame*/
        res = avcodec_receive_frame(videoCodecContext, *hw_videoFrame);
        if (res < 0)
        {
            if (res == AVERROR_EOF)
            {
                av_log(nullptr, AV_LOG_ERROR, "取流结束了?,avcodec_receive_frame,%d\n", res);
                goto ERROR2;
            }
            if (res == AVERROR(EAGAIN))
            {
                av_log(nullptr, AV_LOG_ERROR, "没有数据送入,avcodec_receive_frame,%d\n", res);
                goto ERROR2;
            }
            av_log(nullptr, AV_LOG_ERROR, "解码失败,avcodec_receive_frame,%d\n", res);
            goto ERROR2;
        }

        return video_stream_idx;
    }
    /*音频包*/
    else if (avPacket->stream_index == audio_stream_idx)
    {
        return audio_stream_idx;
    }

    return -1;
ERROR1:
    av_packet_free(&avPacket);
    return res;
ERROR2:
    av_frame_unref(*hw_videoFrame);
    return res;
}

int AVEnVideoDecode(AVFrame **hw_videoFrame, AVFrame **videoFrame, AVCodecContext *videoEnCodecContext)
{
    int res = 0;
    /*准备GPU硬编码空间*/
    res = av_hwframe_get_buffer(videoEnCodecContext->hw_frames_ctx, *hw_videoFrame, 0);
    if (res < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "准备GPU硬编码空间失败,%d\n", res);
        goto ERROR;
    }
    if (!(*hw_videoFrame)->hw_frames_ctx)
    {
        av_log(nullptr, AV_LOG_ERROR, "GPU硬编码hw_frames_ctx为空\n");
        goto ERROR;
    }
    /*将CPU中的数据转移到GPU中*/
    res = av_hwframe_transfer_data(*hw_videoFrame, *videoFrame, 0);
    if (res < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "CPU转移失败,%d\n", res);
        goto ERROR;
    }
    /*拷贝额外数据,主要拷贝pts和dts*/
    av_frame_copy_props(*hw_videoFrame, *videoFrame);

    /*向编码器提供数据*/
    res = avcodec_send_frame(videoEnCodecContext, *hw_videoFrame);
    if (res < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "向编码器提供数据失败,%d\n", res);
        goto ERROR;
    }
ERROR:
    av_frame_unref(*videoFrame);
    av_frame_unref(*hw_videoFrame);
    return res;
}

int OpenVideoFilter(AVFilterContext **buffersrc_ctx, AVFilterContext **buffersink_ctx, AVFilterGraph *filter_graph, const int &width, const int &height, AVCodecContext *videoCodecContext, const int &video_stream_idx, AVFormatContext *ic)
{
    int res = 0;

    AVFilter *buffersrc = nullptr;
    AVFilter *buffersink = nullptr;
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();

    buffersrc = (AVFilter *)avfilter_get_by_name("buffer");
    buffersink = (AVFilter *)avfilter_get_by_name("buffersink");

    /*创建过滤器源*/
    char args[128] = {};
    char args2[32] = {};
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d:frame_rate=%d/%d",
             videoCodecContext->width, videoCodecContext->height, AV_PIX_FMT_QSV,
             ic->streams[video_stream_idx]->time_base.num, ic->streams[video_stream_idx]->time_base.den,
             ic->streams[video_stream_idx]->codecpar->sample_aspect_ratio.num, ic->streams[video_stream_idx]->codecpar->sample_aspect_ratio.den,
             ic->streams[video_stream_idx]->r_frame_rate.num, ic->streams[video_stream_idx]->r_frame_rate.den);

    res = avfilter_graph_create_filter(buffersrc_ctx, buffersrc, "in", args, nullptr, filter_graph);
    if (res < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "创建过滤器源失败,%d\n", res);
        goto END;
    }

    //这里比初始化软件滤镜多的一步，将hw_frames_ctx传给buffersrc, 这样buffersrc就知道传给它的是硬件解码器，数据在显存内
    if (videoCodecContext->hw_frames_ctx)
    {
        AVBufferSrcParameters *par = av_buffersrc_parameters_alloc();
        par->hw_frames_ctx = videoCodecContext->hw_frames_ctx;
        res = av_buffersrc_parameters_set(*buffersrc_ctx, par);
        av_freep(&par);
        if (res < 0)
        {
            av_log(nullptr, AV_LOG_ERROR, "hw_frames_ctx传给buffersrc失败,%d\n", res);
            goto END;
        }
    }

    /*创建接收过滤器*/
    res = avfilter_graph_create_filter(buffersink_ctx, buffersink, "out", nullptr, nullptr, filter_graph);
    if (res < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "创建接收过滤器失败,%d\n", res);
        goto END;
    }

    /* Endpoints for the filter graph. */
    outputs->name = av_strdup("in");
    outputs->filter_ctx = *buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = *buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    /*通过解析过滤器字符串添加过滤器*/

    snprintf(args2, sizeof(args2),
             "vpp_qsv=w=%d:h=%d",
             width, height);
    res = avfilter_graph_parse_ptr(filter_graph, args2, &inputs, &outputs, nullptr);
    if (res < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "通过解析过滤器字符串添加过滤器失败,avfilter_graph_parse_ptr,%d\n", res);
        goto END;
    }
    /*检查过滤器的完整性*/
    res = avfilter_graph_config(filter_graph, nullptr);
    if (res < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "过滤器的不完整,%d\n", res);
        goto END;
    }
END:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return res;
}

int VideoFilter(AVFrame **hw_videoFrame, AVFilterContext *buffersrc_ctx, AVFilterContext *buffersink_ctx)
{
    int res = 0;
    /*将数据放进滤镜,拷贝一次,把pts/dts保留下来*/
    res = av_buffersrc_add_frame(buffersrc_ctx, *hw_videoFrame);
    if (res < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "滤镜处理失败,av_buffersrc_add_frame_flags,%d\n", res);
        return res;
    }
    /*取出数据*/
    res = av_buffersink_get_frame(buffersink_ctx, *hw_videoFrame);
    if (res < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "滤镜处理失败,av_buffersink_get_frame,%d\n", res);
        return res;
    }

    return 0;
}

// static int get_char()
// {
//     fd_set rfds;
//     struct timeval tv;
//     int ch = 0;

//     FD_ZERO(&rfds);
//     FD_SET(0, &rfds);
//     tv.tv_sec = 0;
//     tv.tv_usec = 10; //设置等待超时时间

//     //检测键盘是否有输入
//     if (select(1, &rfds, NULL, NULL, &tv) > 0)
//     {
//         ch = getchar(); 
//     }

//     return ch;
// }

int main(int, char **)
{
    // system(STTY_US TTY_PATH);
    // av_log_set_level(AV_LOG_DEBUG);
    int res = 0;
    /*解码器*/
    AVCodecContext *videoCodecContext = nullptr;
    /*编码器*/
    AVCodecContext *videoEnCodecContext = nullptr;

    DecodeContext decode = {nullptr};

    /*视频包*/
    AVPacket *videoPacket = nullptr;
    /*音频包*/
    AVPacket *audioPacket = nullptr;

    /*在CPU的视频帧,使用滤镜等操作是操作这个帧*/
    AVFrame *videoFrame = nullptr;
    /*在GPU中的视频帧*/
    AVFrame *hw_videoFrame = nullptr;
    /*接收滤镜后的视频帧*/
    AVFrame *filter_videoFrame = nullptr;

    /*视频流索引*/
    int video_stream_idx = -1;
    /*音频流索引*/
    int audio_stream_idx = -1;

    /*视频滤镜,缩放使用*/
    /*滤镜*/
    AVFilterGraph *filter_graph = nullptr;
    /*滤镜的输入*/
    AVFilterContext *buffersrc_ctx = nullptr;
    /*滤镜的输出*/
    AVFilterContext *buffersink_ctx = nullptr;

    /*输入封装格式上下文，保存了视频文件封装格式的相关信息*/
    AVFormatContext *ic = avformat_alloc_context();
    /*输出封装格式上下文*/
    AVFormatContext *oc = nullptr;

    /*输出流*/
    AVStream *videoOutStream = nullptr;
    AVStream *audioOutStream = nullptr;

    int64_t start_time = 0;

    const char *rtmpUrl = "rtmp://localhost/live/livestream";
    // const char *rtmpUrl = "test.mp4";

    /*打开rtsp流地址*/
    res = OpenRtspStream("rtsp://admin:Admin123@192.168.0.232:554/ch01.264", &ic);
    if (res < 0)
    {
        goto END;
    }
    /*创建输出上下文*/
    avformat_alloc_output_context2(&oc, nullptr, "flv", rtmpUrl);
    // avformat_alloc_output_context2(&oc, nullptr, "mp4", rtmpUrl);
    if (!oc)
    {
        av_log(nullptr, AV_LOG_ERROR, "创建输出上下文失败\n");
        goto END;
    }

    /*查找解码器，并打开*/
    res = OpenAVCodec(video_stream_idx, audio_stream_idx, &decode, &videoCodecContext, ic);
    if (res < 0)
    {
        goto END2;
    }
    /*rtsp拿到的时间基异常*/
    if (ic->streams[video_stream_idx]->time_base.den == ic->streams[video_stream_idx]->r_frame_rate.num)
    {
        av_log(nullptr, AV_LOG_ERROR, "rtsp拿到的时间基异常\n");
        goto END2;
    }
    // output_file = fopen("/home/zkteco/test.h264", "w+b");

    /*初始化AVFrame,这是解码后实际的一帧数据*/
    videoPacket = av_packet_alloc();
    audioPacket = av_packet_alloc();
    videoFrame = av_frame_alloc();
    hw_videoFrame = av_frame_alloc();
    filter_videoFrame = av_frame_alloc();

    int width, height;

    start_time = av_gettime();
    while (1)
    {
        /*解码*/
        res = AVDecode(video_stream_idx, audio_stream_idx, &hw_videoFrame, videoCodecContext, ic);
        if (res < 0)
        {
            /*英特尔会有个特定错误-22,解析下一次,Error initializing the MFX video decoder: invalid video parameters (-15)*/
            if (res == -22)
            {
                av_frame_unref(hw_videoFrame);
                continue;
            }
            /*没有数据送入但是可以解析下一次*/
            if (res == AVERROR(EAGAIN))
            {
                av_frame_unref(hw_videoFrame);
                continue;
            }
            /*其他异常直接结束*/
            goto END3;
        }

        /*解码出视频帧*/
        if (res == video_stream_idx)
        {
            /*降低分辨率到640P*/
            if (videoCodecContext->width > 640)
            {
                width = 640;
                height = videoCodecContext->height / (videoCodecContext->width / 640);
            }
            else
            {
                width = videoCodecContext->width;
                height = videoCodecContext->height;
            }

            if (!filter_graph)
            {
                /*滤镜初始化*/
                filter_graph = avfilter_graph_alloc();

                res = OpenVideoFilter(&buffersrc_ctx, &buffersink_ctx, filter_graph, width, height, videoCodecContext, video_stream_idx, ic);
                if (res < 0)
                {
                    goto END3;
                }
            }
            /*拷贝额外数据,主要拷贝pts和dts,备份*/
            av_frame_copy_props(filter_videoFrame, hw_videoFrame);
            /*视频数据过滤镜*/
            res = VideoFilter(&hw_videoFrame, buffersrc_ctx, buffersink_ctx);
            if (res < 0)
            {
                av_frame_unref(hw_videoFrame);
                goto END3;
            }

            /*从GPU转移到CPU*/
            res = av_hwframe_transfer_data(videoFrame, hw_videoFrame, 0);
            if (res < 0)
            {
                av_log(nullptr, AV_LOG_ERROR, "GPU转移失败,%d\n", res);
                av_frame_unref(hw_videoFrame);
                av_frame_unref(filter_videoFrame);
                goto END3;
            }
            av_frame_copy_props(videoFrame, filter_videoFrame);

            /*释放*/
            av_frame_unref(hw_videoFrame);
            av_frame_unref(filter_videoFrame);

            /*打开编码器*/
            if (!videoEnCodecContext)
            {
                /*查找编码器,并打开(视频H264)*/
                res = OpenVideoEnCodec(width, height, 800 * 1024 * 8, video_stream_idx, &decode, &videoEnCodecContext, ic, oc);
                if (res < 0)
                {
                    /*打开失败*/
                    goto END3;
                }
                /*创建输出视频流管道*/
                videoOutStream = avformat_new_stream(oc, videoEnCodecContext->codec);
                if (!videoOutStream)
                {
                    av_log(nullptr, AV_LOG_ERROR, "视频输出流创建失败\n");
                    goto END3;
                }
                /*拷贝codecpar*/
                avcodec_parameters_from_context(videoOutStream->codecpar, videoEnCodecContext);
                /*打开推流*/
                if (!(oc->oformat->flags & AVFMT_NOFILE))
                {
                    res = avio_open(&oc->pb, rtmpUrl, AVIO_FLAG_WRITE);
                    if (res < 0)
                    {
                        av_log(nullptr, AV_LOG_ERROR, "打开推流失败\n");
                        goto END3;
                    }
                }
                av_dump_format(oc, 0, rtmpUrl, 1);
                /*写入头信息*/
                res = avformat_write_header(oc, nullptr);
                if (res < 0)
                {
                    av_log(nullptr, AV_LOG_ERROR, "推流写入头信息失败\n");
                    goto END3;
                }
            }
            /*编码*/
            res = AVEnVideoDecode(&hw_videoFrame, &videoFrame, videoEnCodecContext);
            if (res < 0)
            {
                av_log(nullptr, AV_LOG_ERROR, "视频包编码失败,%d\n", res);
                goto END3;
            }

            /*将视频帧写入视频包中*/
            res = avcodec_receive_packet(videoEnCodecContext, videoPacket);
            if (res < 0)
            {
                av_log(nullptr, AV_LOG_ERROR, "包写入失败,%d\n", res);
                /*数据不够*/
                if (res == AVERROR(EAGAIN))
                {
                    av_packet_unref(videoPacket);
                    continue;
                }
                /*其他异常直接结束*/
                goto END3;
            }
            /*跳过异常帧*/
            if (videoPacket->pts < videoPacket->dts)
            {
                av_log(nullptr, AV_LOG_ERROR, "跳过异常帧\n");
                av_packet_unref(videoPacket);
                continue;
            }
            videoPacket->stream_index = video_stream_idx;

            av_packet_rescale_ts(videoPacket, ic->streams[video_stream_idx]->time_base, oc->streams[video_stream_idx]->time_base);
            videoPacket->pts = videoPacket->pts < 0 ? 0 : videoPacket->pts;
            videoPacket->dts = videoPacket->dts < 0 ? 0 : videoPacket->dts;

            av_interleaved_write_frame(oc, videoPacket);
            av_packet_unref(videoPacket);

            // if(get_char()=='0')
            // {
            //     break;
            // }
        }
    }
    // av_write_trailer(oc);
    // avformat_close_input(&oc);
END3:
    avfilter_graph_free(&filter_graph);
    av_frame_free(&videoFrame);
    av_frame_free(&hw_videoFrame);
    av_frame_free(&filter_videoFrame);
    av_packet_free(&videoPacket);
    av_packet_free(&audioPacket);
    if (videoEnCodecContext)
    {
        avcodec_free_context(&videoEnCodecContext);
    }
END2:
    if (decode.hw_device_ref)
    {
        av_buffer_unref(&decode.hw_device_ref);
    }
    if (videoCodecContext)
    {
        avcodec_free_context(&videoCodecContext);
    }
END:
    avformat_free_context(oc);
    avformat_free_context(ic);
    return 0;
}