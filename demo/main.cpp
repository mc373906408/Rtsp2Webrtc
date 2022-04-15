#include <string>
#include <stdio.h>
#include <errno.h>
#include <vector>
#include <iostream>
extern "C"
{
#include <ffnvcodec/dynlink_loader.h>
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

#define Debug

enum VGADEVICE
{
    NOTSELECT,
    NVIDIA,
    INTEL,
    SOFTWARE
};

VGADEVICE _VGADEVICE = NOTSELECT;

struct Runner
{
    time_t lasttime;
};

enum AVPixelFormat get_hwdevice_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{ 
    const enum AVPixelFormat *p;
    switch (_VGADEVICE)
    {
    case NVIDIA:
        for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++)
        {
            if (*p == AV_PIX_FMT_CUDA)
                return *p;
        }
        av_log(nullptr, AV_LOG_ERROR, "无法使用 CUDA 解码此文件\n");
        break;
    case INTEL:
        for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++)
        {
            if (*p == AV_PIX_FMT_VAAPI)
                return *p;
        }
        av_log(nullptr, AV_LOG_ERROR, "无法使用 VA-API 解码此文件\n");
        break;
    default:
        break;
    }

    return AV_PIX_FMT_NONE;
}

int open_input_file(const char *filename, AVFormatContext **ifmt_ctx, int &in_audio_stream, int &in_video_stream, AVCodec **audio_dec_codec, AVCodec **video_dec_codec)
{
    int ret;
    AVDictionary *options = nullptr;
    /*设置以tcp方式打开rtsp*/
    // av_dict_set(&options, "rtsp_transport", "tcp", 0);
    /*如果 TCP 可用作 RTSP RTP 传输，请先尝试 TCP 进行 RTP 传输。*/
    // av_dict_set(&options, "rtsp_flags", "prefer_tcp", 0);
    /*设置以udp方式打开rtsp*/
    av_dict_set(&options, "rtsp_transport", "udp", 0);
    /*最大延迟0.5s*/
    av_dict_set(&options, "max_delay", "500000", 0);

    /*打开超时3s*/
    av_dict_set(&options, "stimeout", "3000000", 0);

    /*最大缓存??*/
    // av_dict_set(&options, "buffer_size", "9000000", 0);
    // av_dict_set(&options, "fifo_size", "9000000", 0);
    /*打开rtsp流地址*/
    ret = avformat_open_input(ifmt_ctx, filename, NULL, NULL);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "打开rtsp流地址失败,%s\n", errStr);
        return ret;
    }
    /*找到视频流信息,视频流+音频流*/
    ret = avformat_find_stream_info(*ifmt_ctx, nullptr);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "未找到视频流信息,%d\n", errStr);
        return ret;
    }
    /*获取流索引*/
    /*视频流*/
    ret = av_find_best_stream(*ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, video_dec_codec, 0);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "未找到视频流,%s\n", errStr);
        return ret;
    }
    in_video_stream = ret;

    /*音频流,音频不存在就跳过*/
    ret = av_find_best_stream(*ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, audio_dec_codec, 0);
    if (ret < 0)
    {
        in_audio_stream = -1;
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "未找到音频流,%s\n", errStr);
        return 0;
    }
    in_audio_stream = ret;

    return ret;
}

int open_video_decoder(AVCodecContext **video_decoder_ctx, AVCodec *video_dec_codec, AVFormatContext *ifmt_ctx, int in_video_stream, AVBufferRef *hw_device_ctx)
{
    int ret = 0;
    AVStream *video = nullptr;

    /*初始化解码器*/
    *video_decoder_ctx = avcodec_alloc_context3(video_dec_codec);
    if (!video_decoder_ctx)
    {
        return AVERROR(ENOMEM);
    }
    /*设置解码上下文*/
    video = ifmt_ctx->streams[in_video_stream];

    ret = avcodec_parameters_to_context(*video_decoder_ctx, video->codecpar);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "video avcodec_parameters_to_context 失败,%s\n", errStr);
        return ret;
    }

    /*赋值video时间基*/
    (*video_decoder_ctx)->framerate = video->r_frame_rate;
    (*video_decoder_ctx)->time_base = av_inv_q((*video_decoder_ctx)->framerate);

    /*创建硬件设备引用*/
    switch (_VGADEVICE)
    {
    case NVIDIA:
    case INTEL:
        (*video_decoder_ctx)->hw_device_ctx = av_buffer_ref(hw_device_ctx);
        if (!(*video_decoder_ctx)->hw_device_ctx)
        {
            av_log(nullptr, AV_LOG_ERROR, "硬件设备引用创建失败\n");
            return AVERROR(ENOMEM);
        }
        (*video_decoder_ctx)->get_format = get_hwdevice_format;
        break;

    default:
        break;
    }

    /*打开解码器*/
    ret = avcodec_open2(*video_decoder_ctx, video_dec_codec, NULL);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "video 无法打开编解码器进行解码,%s\n", errStr);
    }
    return ret;
}

int open_audio_decoder(AVCodecContext **audio_decoder_ctx, AVCodec *audio_dec_codec, AVFormatContext *ifmt_ctx, int in_audio_stream)
{
    int ret = 0;
    AVStream *audio = nullptr;

    /*初始化解码器*/
    *audio_decoder_ctx = avcodec_alloc_context3(audio_dec_codec);
    if (!(*audio_decoder_ctx))
    {
        return AVERROR(ENOMEM);
    }
    /*设置解码上下文*/
    audio = ifmt_ctx->streams[in_audio_stream];

    ret = avcodec_parameters_to_context(*audio_decoder_ctx, audio->codecpar);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "audio avcodec_parameters_to_context 失败,%s\n", errStr);
        return ret;
    }

    ret = avcodec_open2(*audio_decoder_ctx, audio_dec_codec, NULL);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "audio 无法打开编解码器进行解码,%s\n", errStr);
    }
    return ret;
}

/*打开推流*/
int open_ofmt_ctx(const int &audio_initialized, const int &video_initialized, int &initialized, AVFormatContext **ofmt_ctx, const char *outUrl)
{
    int ret = 0;
    if (!initialized)
    {
        if (audio_initialized && video_initialized)
        {
            /*打开推流*/
            if (!((*ofmt_ctx)->oformat->flags & AVFMT_NOFILE))
            {
                ret = avio_open(&(*ofmt_ctx)->pb, outUrl, AVIO_FLAG_WRITE);
                if (ret < 0)
                {
                    char errStr[256] = {0};
                    av_strerror(ret, errStr, sizeof(errStr));
                    av_log(nullptr, AV_LOG_ERROR, "打开推流失败,%s\n", errStr);
                    return ret;
                }
            }

            /*写入流标头*/
            ret = avformat_write_header(*ofmt_ctx, nullptr);
            if (ret < 0)
            {
                char errStr[256] = {0};
                av_strerror(ret, errStr, sizeof(errStr));
                av_log(nullptr, AV_LOG_ERROR, "写入流标头时出错,%s\n", errStr);
                return ret;
            }
            initialized = 1;
        }
    }
    return ret;
}

int video_encode_write(AVPacket *enc_pkt, AVFrame *frame, AVCodecContext **video_encoder_ctx, AVFormatContext **ifmt_ctx, AVFormatContext **ofmt_ctx, const int &in_video_stream, const int &out_video_stream, const int &initialized, int64_t &video_last_pts)
{
    int ret = 0;

    av_packet_unref(enc_pkt);

    /*送进编码器*/
    ret = avcodec_send_frame(*video_encoder_ctx, frame);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "video 编码失败,avcodec_send_frame,%s\n", errStr);
        goto end;
    }
    while (1)
    {
        /*编码数据压入包*/
        ret = avcodec_receive_packet(*video_encoder_ctx, enc_pkt);
        if (ret)
        {
            break;
        }
        /*跳过异常帧*/
        if (enc_pkt->pts < enc_pkt->dts)
        {
            av_log(nullptr, AV_LOG_ERROR, "video 跳过异常帧\n");
            continue;
        }
        enc_pkt->stream_index = out_video_stream;
        av_packet_rescale_ts(enc_pkt, (*ifmt_ctx)->streams[in_video_stream]->time_base,
                             (*ofmt_ctx)->streams[out_video_stream]->time_base);

        enc_pkt->pts = enc_pkt->pts < 0 ? 0 : enc_pkt->pts;
        enc_pkt->dts = enc_pkt->dts < 0 ? 0 : enc_pkt->dts;

        /*如果全部准备完毕，就开始推流,并且,本次推流包的pts要比上次推流包大*/
        if (initialized && enc_pkt->pts > video_last_pts)
        {
            video_last_pts = enc_pkt->pts;
            ret = av_interleaved_write_frame(*ofmt_ctx, enc_pkt);
            if (ret < 0)
            {
                char errStr[256] = {0};
                av_strerror(ret, errStr, sizeof(errStr));
                av_log(nullptr, AV_LOG_ERROR, "video 将数据写入输出流时出错,%s\n", errStr);
                return -1;
            }
        }
    }

end:
    if (ret == AVERROR_EOF)
    {
        return 0;
    }

    ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);
    return ret;
}

int add_samples_to_fifo(AVAudioFifo **fifo,
                        uint8_t ***dst_data,
                        const int &nb_samples)
{
    int ret = 0;
    ret = av_audio_fifo_realloc(*fifo, av_audio_fifo_size(*fifo) + nb_samples);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "audio 无法重新分配 FIFO,%s\n", errStr);
        return ret;
    }
    if (av_audio_fifo_write(*fifo, (void **)(*dst_data), nb_samples) < nb_samples)
    {
        av_log(nullptr, AV_LOG_ERROR, "audio 无法将数据写入 FIFO\n");
        return ret;
    }
    return 0;
}

int audio_encode_write(AVPacket *enc_pkt, AVFrame *srcFrame, AVCodecContext **audio_encoder_ctx, AVFormatContext **ofmt_ctx, const int &out_audio_stream, SwrContext **resample_context, int64_t &audio_pts, int &max_dst_nb_samples, int &dst_nb_samples, uint8_t ***dst_data, int &dst_linesize, AVAudioFifo **fifo, const int &initialized, int64_t &audio_last_pts)
{
    av_packet_unref(enc_pkt);
    int ret = 0;
    int nb_samples = 0;

    /*重采样*/
    dst_nb_samples = av_rescale_rnd(swr_get_delay(*resample_context, srcFrame->sample_rate) + srcFrame->nb_samples,
                                    (*audio_encoder_ctx)->sample_rate, srcFrame->sample_rate, AV_ROUND_UP);

    if (dst_nb_samples > max_dst_nb_samples)
    {
        av_freep(dst_data[0]);
        ret = av_samples_alloc(*dst_data, &dst_linesize, (*audio_encoder_ctx)->channels, dst_nb_samples, (*audio_encoder_ctx)->sample_fmt, 0);
        if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "audio  av_samples_alloc 分配dst_data失败,%s\n", errStr);
            return ret;
        }
        max_dst_nb_samples = dst_nb_samples;
    }

    nb_samples = swr_convert(*resample_context, *dst_data, dst_nb_samples, (const uint8_t **)srcFrame->extended_data, srcFrame->nb_samples);
    if (nb_samples < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "audio 重采样失败\n");
        return -1;
    }

    /*将数据推入fifo*/
    add_samples_to_fifo(fifo, dst_data, nb_samples);
    if (av_audio_fifo_size(*fifo) < (*audio_encoder_ctx)->frame_size)
    {
        /*fifo数据不满足一帧,等下次输入*/
        return 0;
    }

    /*数据长度满足一帧,编码*/
    while (av_audio_fifo_size(*fifo) >= (*audio_encoder_ctx)->frame_size)
    {
        int frame_size = FFMIN(av_audio_fifo_size(*fifo), (*audio_encoder_ctx)->frame_size);

        /*创建临时的输出frame*/
        AVFrame *outframe = av_frame_alloc();
        if (!outframe)
        {
            av_log(nullptr, AV_LOG_ERROR, "audio 无法创建临时frame\n");
            return -1;
        }
        outframe->nb_samples = frame_size;
        outframe->channel_layout = (*audio_encoder_ctx)->channel_layout;
        outframe->format = (*audio_encoder_ctx)->sample_fmt;
        outframe->sample_rate = (*audio_encoder_ctx)->sample_rate;

        /*分配缓存*/
        ret = av_frame_get_buffer(outframe, 0);
        if (ret < 0)
        {
            av_frame_free(&outframe);
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "audio 分配临时frame缓存失败,%s\n", errStr);
            return ret;
        }

        /*从fifo读取缓存*/
        if (av_audio_fifo_read(*fifo, (void **)outframe->data, frame_size) < frame_size)
        {
            av_frame_free(&outframe);
            av_log(nullptr, AV_LOG_ERROR, "audio 无法从 FIFO 读取数据\n");
            return -1;
        }

        /*给frame设置pts*/
        if (outframe)
        {
            outframe->pts = audio_pts;
            audio_pts += outframe->nb_samples;
        }

        /*编码*/
        ret = avcodec_send_frame(*audio_encoder_ctx, outframe);
        if (ret < 0)
        {
            av_frame_free(&outframe);
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "audio 编码失败,avcodec_send_frame,%s\n", errStr);
            return ret;
        }
        av_frame_free(&outframe);

        while (1)
        {
            /*编码数据压入包*/
            ret = avcodec_receive_packet(*audio_encoder_ctx, enc_pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                ret = 0;
                break;
            }
            else if (ret < 0)
            {
                char errStr[256] = {0};
                av_strerror(ret, errStr, sizeof(errStr));
                av_log(nullptr, AV_LOG_ERROR, "audio 编码失败,avcodec_receive_packet,%s\n", errStr);
                break;
            }

            enc_pkt->stream_index = out_audio_stream;

            /*如果全部准备完毕，就开始推流,并且,本次推流包的pts要比上次推流包大*/
            if (initialized && enc_pkt->pts > 0 && enc_pkt->pts > audio_last_pts)
            {
                audio_last_pts = enc_pkt->pts;
                ret = av_interleaved_write_frame(*ofmt_ctx, enc_pkt);
                if (ret < 0)
                {
                    char errStr[256] = {0};
                    av_strerror(ret, errStr, sizeof(errStr));
                    av_log(nullptr, AV_LOG_ERROR, "audio 将数据写入输出流时出错,%s\n", errStr);
                    break;
                }
            }
        }
        if (ret < 0)
        {
            return ret;
        }
    }

    return ret;
}

int openVideoFilter(AVCodecContext **video_decoder_ctx, AVFormatContext **ifmt_ctx, const int &video_stream, AVFilterContext **buffersrc_ctx, AVFilterContext **buffersink_ctx, AVFilterGraph **filter_graph, const int &width, const int &height)
{
    int ret = 0;

    AVFilter *buffersrc = nullptr;
    AVFilter *buffersink = nullptr;
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();

    buffersrc = (AVFilter *)avfilter_get_by_name("buffer");
    buffersink = (AVFilter *)avfilter_get_by_name("buffersink");

    /*创建过滤器源*/
    char args[128] = {};
    char args2[128] = {};
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             (*video_decoder_ctx)->width, (*video_decoder_ctx)->height, (*video_decoder_ctx)->pix_fmt,
             (*ifmt_ctx)->streams[video_stream]->time_base.num, (*ifmt_ctx)->streams[video_stream]->time_base.den,
             (*video_decoder_ctx)->sample_aspect_ratio.num, (*video_decoder_ctx)->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(buffersrc_ctx, buffersrc, "in", args, nullptr, *filter_graph);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "创建过滤器源失败,%s\n", errStr);
        goto end;
    }

    //这里比初始化软件滤镜多的一步，将hw_frames_ctx传给buffersrc, 这样buffersrc就知道传给它的是硬件解码器，数据在显存内
    if ((*video_decoder_ctx)->hw_frames_ctx)
    {
        AVBufferSrcParameters *par = av_buffersrc_parameters_alloc();
        par->hw_frames_ctx = (*video_decoder_ctx)->hw_frames_ctx;
        ret = av_buffersrc_parameters_set(*buffersrc_ctx, par);
        av_freep(&par);
        if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "hw_frames_ctx传给buffersrc失败,%s\n", errStr);
            goto end;
        }
    }

    /*创建接收过滤器*/
    ret = avfilter_graph_create_filter(buffersink_ctx, buffersink, "out", nullptr, nullptr, *filter_graph);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "创建接收过滤器失败,%s\n", errStr);
        goto end;
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
    switch (_VGADEVICE)
    {
    case NVIDIA:
        snprintf(args2, sizeof(args2),
                 "scale_npp=w=%d:h=%d",
                 width, height);
        break;
    case INTEL:
        snprintf(args2, sizeof(args2),
                 "scale_vaapi=w=%d:h=%d",
                 width, height);
        break;

    default:
        snprintf(args2, sizeof(args2),
                 "scale=w=%d:h=%d",
                 width, height);
        break;
    }

    ret = avfilter_graph_parse_ptr(*filter_graph, args2, &inputs, &outputs, nullptr);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "通过解析过滤器字符串添加过滤器失败,%s\n", errStr);
        goto end;
    }

    /*检查过滤器的完整性*/
    ret = avfilter_graph_config(*filter_graph, nullptr);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "过滤器的不完整,%s\n", errStr);
        goto end;
    }
end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return ret;
}

int videoFilter(AVFrame **frame, AVFilterContext **buffersrc_ctx, AVFilterContext **buffersink_ctx)
{
    int ret = 0;
    /*将数据放进滤镜*/
    ret = av_buffersrc_add_frame(*buffersrc_ctx, *frame);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "滤镜处理失败,av_buffersrc_add_frame,%s\n", errStr);
        return ret;
    }
    /*取出数据*/
    ret = av_buffersink_get_frame(*buffersink_ctx, *frame);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "滤镜处理失败,av_buffersink_get_frame,%s\n", errStr);
        return ret;
    }

    return 0;
}

int video_dec_enc(AVPacket *pkt, AVCodec *video_enc_codec, AVBufferRef **hw_device_ctx, AVCodecContext **video_decoder_ctx, AVCodecContext **video_encoder_ctx, const int &audio_initialized, int &video_initialized, int &initialized, const char *outUrl, AVStream **video_ost, AVFormatContext **ifmt_ctx, AVFormatContext **ofmt_ctx, const int &in_video_stream, int &out_video_stream, const int &out_audio_stream, AVFilterContext **buffersrc_ctx, AVFilterContext **buffersink_ctx, AVFilterGraph **filter_graph, int64_t &video_last_pts)
{
    AVFrame *frame;
    int ret = 0;

    /*发送 AVPacket 数据包给视频解码器*/
    ret = avcodec_send_packet(*video_decoder_ctx, pkt);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "video 解码失败,%s\n", errStr);
        return ret;
    }

    while (ret >= 0)
    {
        frame = av_frame_alloc();
        if (!frame)
        {
            return AVERROR(ENOMEM);
        }
        /*解码后存到显存*/
        ret = avcodec_receive_frame(*video_decoder_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            av_frame_free(&frame);
            return 0;
        }
        else if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "video 解码时出错,%s\n", errStr);
            goto fail;
        }

        /*解码第一帧后初始化编码器和滤镜*/
        if (!video_initialized)
        {
            int width, height;
            /*降低分辨率到640P*/
            if ((*video_decoder_ctx)->width > 640)
            {
                width = 640;
                height = (*video_decoder_ctx)->height / ((*video_decoder_ctx)->width / 640);
            }
            else
            {
                width = (*video_decoder_ctx)->width;
                height = (*video_decoder_ctx)->height;
            }

            /*我们需要引用解码器的 hw_frames_ctx 来初始化编码器的编解码器。只有得到解码后的帧，才能得到它的hw_frames_ctx*/
            switch (_VGADEVICE)
            {
            case NVIDIA:
            case INTEL:
                (*video_encoder_ctx)->hw_frames_ctx = av_buffer_ref((*video_decoder_ctx)->hw_frames_ctx);
                if (!(*video_encoder_ctx)->hw_frames_ctx)
                {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                break;

            default:
                break;
            }

            /*为编码器设置 AVCodecContext 参数，这里我们保留它们与解码器相同,降低分辨率*/
            (*video_encoder_ctx)->framerate = (*video_decoder_ctx)->framerate;
            (*video_encoder_ctx)->time_base = av_inv_q((*video_decoder_ctx)->framerate);
            (*video_encoder_ctx)->width = width;
            switch (_VGADEVICE)
            {
            case NVIDIA:
                (*video_encoder_ctx)->pix_fmt = AV_PIX_FMT_CUDA;
                break;
            case INTEL:
                (*video_encoder_ctx)->pix_fmt = AV_PIX_FMT_VAAPI;
                break;
            default:
                (*video_encoder_ctx)->pix_fmt = AV_PIX_FMT_YUV420P;
                /*使用Baseline Profile + Level 3.1编码*/
                av_opt_set((*video_encoder_ctx)->priv_data, "profile", "baseline", AV_OPT_SEARCH_CHILDREN);
                av_opt_set((*video_encoder_ctx)->priv_data, "level", "31", AV_OPT_SEARCH_CHILDREN);
                break;
            }

            (*video_encoder_ctx)->height = height;

            /*禁用B帧*/
            (*video_encoder_ctx)->max_b_frames = 0;

            /*I帧间隔*/
            (*video_encoder_ctx)->gop_size = (*video_encoder_ctx)->time_base.den * 2;
            /*必须要加头,否则无法解析*/
            if ((*ofmt_ctx)->oformat->flags & AVFMT_GLOBALHEADER)
            {
                (*video_encoder_ctx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }

            /*打开编码器*/
            ret = avcodec_open2((*video_encoder_ctx), video_enc_codec, NULL);
            if (ret < 0)
            {
                char errStr[256] = {0};
                av_strerror(ret, errStr, sizeof(errStr));
                av_log(nullptr, AV_LOG_ERROR, "video 无法打开编码器,%s\n", errStr);
                goto fail;
            }

            /*添加流*/
            *video_ost = avformat_new_stream(*ofmt_ctx, video_enc_codec);
            if (!(*video_ost))
            {
                av_log(nullptr, AV_LOG_ERROR, "video 为输出格式分配流失败\n");
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            if (out_audio_stream == 0)
            {
                out_video_stream = 1;
            }
            else
            {
                out_video_stream = 0;
            }

            ret = avcodec_parameters_from_context((*video_ost)->codecpar, *video_encoder_ctx);
            if (ret < 0)
            {
                char errStr[256] = {0};
                av_strerror(ret, errStr, sizeof(errStr));
                av_log(nullptr, AV_LOG_ERROR, "video 无法复制流参数。,%s\n", errStr);
                goto fail;
            }

            /*滤镜初始化*/
            *filter_graph = avfilter_graph_alloc();

            ret = openVideoFilter(video_decoder_ctx, ifmt_ctx, in_video_stream, buffersrc_ctx, buffersink_ctx, filter_graph, width, height);
            if (ret < 0)
            {
                goto fail;
            }

            video_initialized = 1;
        }

        /*打开推流*/
        ret = open_ofmt_ctx(audio_initialized, video_initialized, initialized, ofmt_ctx, outUrl);
        if (ret < 0)
        {
            goto fail;
        }

        /*将数据送入滤镜*/
        videoFilter(&frame, buffersrc_ctx, buffersink_ctx);

        /*编码*/
        ret = video_encode_write(pkt, frame, video_encoder_ctx, ifmt_ctx, ofmt_ctx, in_video_stream, out_video_stream, initialized, video_last_pts);
        if (ret < 0)
        {
            av_log(nullptr, AV_LOG_ERROR, "video 编码失败\n");
        }

    fail:
        av_frame_free(&frame);
        if (ret < 0)
        {
            return ret;
        }
    }
    return 0;
}

int audio_dec_enc(AVPacket *pkt, AVCodec *audio_enc_codec, AVCodecContext **audio_decoder_ctx, AVCodecContext **audio_encoder_ctx, int &audio_initialized, const int &video_initialized, int &initialized, const char *outUrl, AVStream **audio_ost, AVFormatContext **ofmt_ctx, int &out_audio_stream, const int &out_video_stream, SwrContext **resample_context, int64_t &audio_pts, int &max_dst_nb_samples, int &dst_nb_samples, uint8_t ***dst_data, int &dst_linesize, AVAudioFifo **fifo, int64_t &audio_last_pts)
{
    AVFrame *srcFrame;
    int ret = 0;

    /*发送 AVPacket 数据包给音频解码器*/
    ret = avcodec_send_packet(*audio_decoder_ctx, pkt);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "audio 解码失败,%s\n", errStr);
        return ret;
    }
    while (ret >= 0)
    {
        srcFrame = av_frame_alloc();
        if (!srcFrame)
        {
            return AVERROR(ENOMEM);
        }
        /*解码后存到内存*/
        ret = avcodec_receive_frame(*audio_decoder_ctx, srcFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            av_frame_free(&srcFrame);
            return 0;
        }
        else if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "audio 解码时出错,%s\n", errStr);
            goto fail;
        }

        /*解码第一帧后初始化编码器*/
        if (!audio_initialized)
        {
            /*为编码器设置 AVCodecContext 参数，这里我们保留它们与解码器相同*/
            (*audio_encoder_ctx)->bit_rate = (*audio_decoder_ctx)->bit_rate; //比特率
            // (*audio_encoder_ctx)->bit_rate = 128000;
            (*audio_encoder_ctx)->sample_rate = 48000; //采样率
            // (*audio_encoder_ctx)->channels = (*audio_decoder_ctx)->channels;                                                                   //声道数
            (*audio_encoder_ctx)->channels = 2;
            (*audio_encoder_ctx)->channel_layout = av_get_default_channel_layout((*audio_encoder_ctx)->channels); //声道布局
            (*audio_decoder_ctx)->channel_layout = av_get_default_channel_layout((*audio_decoder_ctx)->channels);
            (*audio_encoder_ctx)->sample_fmt = (*audio_enc_codec).sample_fmts[0];                 //采样格式
            (*audio_encoder_ctx)->time_base = (AVRational){1, (*audio_encoder_ctx)->sample_rate}; //时间基

            /*必须要加头,否则无法解析*/
            if ((*ofmt_ctx)->oformat->flags & AVFMT_GLOBALHEADER)
            {
                (*audio_encoder_ctx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }

            /*打开编码器*/
            ret = avcodec_open2((*audio_encoder_ctx), audio_enc_codec, NULL);
            if (ret < 0)
            {
                char errStr[256] = {0};
                av_strerror(ret, errStr, sizeof(errStr));
                av_log(nullptr, AV_LOG_ERROR, "audio 无法打开编码器,%s\n", errStr);
                goto fail;
            }

            /*添加流*/
            *audio_ost = avformat_new_stream(*ofmt_ctx, audio_enc_codec);
            if (!(*audio_ost))
            {
                av_log(nullptr, AV_LOG_ERROR, "为输出格式分配流失败\n");
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            if (out_video_stream == 0)
            {
                out_audio_stream = 1;
            }
            else
            {
                out_audio_stream = 0;
            }

            ret = avcodec_parameters_from_context((*audio_ost)->codecpar, *audio_encoder_ctx);
            if (ret < 0)
            {
                char errStr[256] = {0};
                av_strerror(ret, errStr, sizeof(errStr));
                av_log(nullptr, AV_LOG_ERROR, "audio 无法复制流参数。,%s\n", errStr);
                goto fail;
            }

            (*audio_ost)->time_base = (*audio_encoder_ctx)->time_base;

            /*创建重采样*/
            *resample_context = swr_alloc_set_opts(nullptr, (*audio_encoder_ctx)->channel_layout, (*audio_encoder_ctx)->sample_fmt, (*audio_encoder_ctx)->sample_rate, (*audio_decoder_ctx)->channel_layout, (*audio_decoder_ctx)->sample_fmt, (*audio_decoder_ctx)->sample_rate, 0, nullptr);
            if (!(*resample_context))
            {
                av_log(nullptr, AV_LOG_ERROR, "audio 创建重采样失败\n");
                goto fail;
            }

            ret = swr_init(*resample_context);
            if (ret < 0)
            {
                char errStr[256] = {0};
                av_strerror(ret, errStr, sizeof(errStr));
                av_log(nullptr, AV_LOG_ERROR, "audio 无法打开重采样,%s\n", errStr);
                goto fail;
            }

            max_dst_nb_samples = dst_nb_samples = av_rescale_rnd(srcFrame->nb_samples, (*audio_encoder_ctx)->sample_rate, srcFrame->sample_rate, AV_ROUND_UP);

            int ret = av_samples_alloc_array_and_samples(dst_data, &dst_linesize, (*audio_encoder_ctx)->channels, dst_nb_samples, (*audio_encoder_ctx)->sample_fmt, 0);
            if (ret < 0)
            {
                char errStr[256] = {0};
                av_strerror(ret, errStr, sizeof(errStr));
                av_log(nullptr, AV_LOG_ERROR, "audio  av_samples_alloc_array_and_samples 分配dst_data失败,%s\n", errStr);
                goto fail;
            }

            /*重采样一次,计算出重采样延迟*/
            swr_convert(*resample_context, *dst_data, dst_nb_samples, (const uint8_t **)srcFrame->extended_data, srcFrame->nb_samples);

            *fifo = av_audio_fifo_alloc((*audio_encoder_ctx)->sample_fmt, (*audio_encoder_ctx)->channels, (*audio_encoder_ctx)->frame_size);
            if (!(*fifo))
            {
                av_log(nullptr, AV_LOG_ERROR, "audio 创建fifo失败\n");
                goto fail;
            }
            audio_initialized = 1;
        }

        /*打开推流*/
        ret = open_ofmt_ctx(audio_initialized, video_initialized, initialized, ofmt_ctx, outUrl);
        if (ret < 0)
        {
            goto fail;
        }
        /*编码*/
        ret = audio_encode_write(pkt, srcFrame, audio_encoder_ctx, ofmt_ctx, out_audio_stream, resample_context, audio_pts, max_dst_nb_samples, dst_nb_samples, dst_data, dst_linesize, fifo, initialized, audio_last_pts);
        if (ret < 0)
        {
            av_log(nullptr, AV_LOG_ERROR, "audio 编码失败\n");
            goto fail;
        }

    fail:
        av_frame_free(&srcFrame);
        if (ret < 0)
        {
            return ret;
        }
    }

    return 0;
}

int interruptCallback(void *_input_runner)
{
    Runner *input_runner = (Runner *)_input_runner;
    if (input_runner->lasttime > 0)
    {
        if (time(NULL) - input_runner->lasttime > 3)
        {
            /*超时3秒退出*/
            av_log(nullptr, AV_LOG_ERROR, "读取超时\n");
            return 1;
        }
    }
    return 0;
}

int shellexec(const char *cmd, std::vector<std::string> &resvec)
{
    resvec.clear();
    FILE *pp = popen(cmd, "r"); //建立管道
    if (!pp)
    {
        return -1;
    }
    char tmp[1024]; //设置一个合适的长度，以存储每一行输出
    while (fgets(tmp, sizeof(tmp), pp) != NULL)
    {
        if (tmp[strlen(tmp) - 1] == '\n')
        {
            tmp[strlen(tmp) - 1] = '\0'; //去除换行符
        }
        resvec.push_back(tmp);
    }
    pclose(pp); //关闭管道
    return resvec.size();
}

int main(int argc, char **argv)
{
    int ret = 0;

    bool HWH264 = false, HWH265 = false;

    /*判断显卡与支持的解码类型*/
    std::vector<std::string> resvec;
    ret = shellexec("lspci | grep -i vga", resvec);
    if (ret > 0)
    {
        bool NVIDIAH264 = false, NVIDIAH265 = false;
        bool INTELH264 = false, INTELH265 = false;
        for (auto item : resvec)
        {
            if (item.find("NVIDIA") != std::string::npos)
            {
                /*检测cuda的解码能力*/
                CUVIDDECODECAPS caps = {};

                CudaFunctions *cudl = nullptr;
                ret = cuda_load_functions(&cudl, nullptr);
                if (ret != 0)
                {
                    continue;
                }

                CuvidFunctions *cvdl = nullptr;
                ret = cuvid_load_functions(&cvdl, nullptr);
                if (ret != 0)
                {
                    cuda_free_functions(&cudl);
                    continue;
                }

                cudl->cuInit(0);
                CUdevice dev;
                cudl->cuDeviceGet(&dev, 0);
                char name[255];
                cudl->cuDeviceGetName(name, 255, dev);
                CUcontext cuda_ctx;
                cudl->cuCtxCreate(&cuda_ctx, CU_CTX_SCHED_BLOCKING_SYNC, dev);

                caps.eCodecType = cudaVideoCodec_H264;
                caps.eChromaFormat = cudaVideoChromaFormat_420;
                caps.nBitDepthMinus8 = 0;
                ret = cvdl->cuvidGetDecoderCaps(&caps);
                if (ret != 0)
                {
                    cuda_free_functions(&cudl);
                    cuvid_free_functions(&cvdl);
                    continue;
                }
                if (caps.bIsSupported)
                {
                    NVIDIAH264 = true;
                }

                caps.eCodecType = cudaVideoCodec_HEVC;
                ret = cvdl->cuvidGetDecoderCaps(&caps);
                if (ret != 0)
                {
                    if (NVIDIAH264)
                    {
                        _VGADEVICE = NVIDIA;
                    }
                    cuda_free_functions(&cudl);
                    cuvid_free_functions(&cvdl);
                    continue;
                }
                if (caps.bIsSupported)
                {
                    NVIDIAH265 = true;
                }
                cuda_free_functions(&cudl);
                cuvid_free_functions(&cvdl);
            }
            if (item.find("Intel") != std::string::npos)
            {
                std::vector<std::string> vainfoResvec;
                ret = shellexec("vainfo", vainfoResvec);
                if (ret > 0)
                {
                    for (auto vainfoItem : vainfoResvec)
                    {
                        if (vainfoItem.find("H264") != std::string::npos)
                        {
                            INTELH264 = true;
                        }
                        if (vainfoItem.find("HEVC") != std::string::npos)
                        {
                            INTELH265 = true;
                        }
                    };
                }
            }
        }

        if (NVIDIAH264 && NVIDIAH265)
        {
            HWH264 = true;
            HWH265 = true;
            _VGADEVICE = NVIDIA;
        }
        else if (INTELH264 && INTELH265)
        {
            HWH264 = true;
            HWH265 = true;
            _VGADEVICE = INTEL;
        }
        else if (NVIDIAH264)
        {
            HWH264 = true;
            _VGADEVICE = NVIDIA;
        }
        else if (INTELH264)
        {
            HWH264 = true;
            _VGADEVICE = INTEL;
        }
    }
    if (_VGADEVICE == NOTSELECT)
    {
        _VGADEVICE = SOFTWARE;
    }

#ifdef Debug
    // av_log_set_level(AV_LOG_DEBUG);
    av_log_set_level(AV_LOG_INFO);
#else
    av_log_set_level(AV_LOG_INFO);
    if (argc < 6)
    {
        av_log(nullptr, AV_LOG_ERROR, "参数太少,请调用start.sh启动\n");
        return -1;
    }
    if (strcasecmp(argv[7], "Y") == 0)
    {
        HWDEVICE = true;
    }

#endif

    /*输入输出IO上下文*/
    AVFormatContext *ifmt_ctx = nullptr, *ofmt_ctx = nullptr;
    /*ifmt_ctx的索引*/
    int in_video_stream = -1;
    int in_audio_stream = -1;
    /*ofmt_ctx的索引*/
    int out_video_stream = -1;
    int out_audio_stream = -1;
    /*硬件加速器*/
    AVBufferRef *hw_device_ctx = nullptr;
    /*编码器初始化*/
    int initialized = 0;
    int video_initialized = 0;
    int audio_initialized = 0;

    /*编解码器选择*/
    AVCodec *video_dec_codec = nullptr, *video_enc_codec = nullptr;
    AVCodec *audio_dec_codec = nullptr, *audio_enc_codec = nullptr;
    /*编解码器*/
    AVCodecContext *video_decoder_ctx = nullptr, *video_encoder_ctx = nullptr;
    AVCodecContext *audio_decoder_ctx = nullptr, *audio_encoder_ctx = nullptr;
    /*输出流*/
    AVStream *video_ost = nullptr;
    AVStream *audio_ost = nullptr;
    /*输出包*/
    AVPacket *dec_pkt = nullptr;

    /*上个音视频包的pts*/
    int64_t video_last_pts = -1;
    int64_t audio_last_pts = -1;

    /*视频滤镜,缩放使用*/
    /*滤镜*/
    AVFilterGraph *filter_graph = nullptr;
    /*滤镜的输入*/
    AVFilterContext *buffersrc_ctx = nullptr;
    /*滤镜的输出*/
    AVFilterContext *buffersink_ctx = nullptr;

    /*音频重采样*/
    SwrContext *resample_context = nullptr;
    /*音频pts*/
    int64_t audio_pts = 0;
    /*转换样本的数量*/
    int max_dst_nb_samples, dst_nb_samples;
    /*重采样后的数据*/
    uint8_t **dst_data = nullptr;
    /*重采样后的数据长度*/
    int dst_linesize;
    /*音频fifo*/
    AVAudioFifo *fifo = nullptr;

    /*read超时机制*/
    Runner input_runner = {0};

    /*输入*/
#ifdef Debug
    // const char *rtspUrl = "rtsp://admin:Admin123@192.168.0.232:554/ch01.264";
    // const char *rtspUrl = "rtsp://192.168.0.227/LiveMedia/ch1/Media1";
    const char *rtspUrl = "rtsp://admin:Admin123@192.168.0.232:554/ch01.264";
    // const char *rtspUrl = "rtsp://10.8.14.108:554/ch04.264?ptype=udp";
    // const char *rtspUrl = "rtsp://10.8.14.108:554/ch01.264?ptype=udp";
#else
    const char *rtspUrl = argv[1];

#endif

/*输出*/
#ifdef Debug
    // const char *rtpUrl = "[select=a:f=rtp:ssrc=1111:payload_type=100]rtp://10.8.14.118:58438?rtcpport=50203|[select=v:f=rtp:ssrc=2222:payload_type=101]rtp://10.8.14.118:59470?rtcpport=53956";
    // const char *rtpUrl = "[select=a:f=rtp:ssrc=1111:payload_type=100]rtp://192.168.0.11:58438?rtcpport=50203|[select=v:f=rtp:ssrc=2222:payload_type=101]rtp://192.168.0.11:59470?rtcpport=53956";
    const char *rtpUrl = "[select=v:f=rtp:ssrc=2222:payload_type=101]rtp://192.168.0.11:59470?rtcpport=53956";
#else
    char rtpUrl[512];
#endif

    /*打开文件流*/
    ret = open_input_file(rtspUrl, &ifmt_ctx, in_audio_stream, in_video_stream, &audio_dec_codec, &video_dec_codec);
    if (ret < 0)
    {
        goto end;
    }

    /*判断是否支持硬件解码*/
    if (!((video_dec_codec->id == AV_CODEC_ID_H264 && HWH264) ||
          (video_dec_codec->id == AV_CODEC_ID_HEVC && HWH265)))
    {
        _VGADEVICE = SOFTWARE;
    }

    av_log(nullptr, AV_LOG_WARNING, "当前摄像机视频格式%s,使用的解码方式%s\n", video_dec_codec->name, _VGADEVICE == NVIDIA ? "NVIDIA" : _VGADEVICE == INTEL ? "INTEL"
                                                                                                                                                            : "SOFTWARE");

    /*打开硬件设备*/
    switch (_VGADEVICE)
    {
    case NVIDIA:
        ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
        if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "创建 CUDA 设备失败,%s\n", errStr);
            return -1;
        }
        break;
    case INTEL:
        ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0);
        if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "创建 VAAPI 设备失败,%s\n", errStr);
            return -1;
        }
        break;

    default:
        break;
    }

    /*打开解码器*/
    ret = open_video_decoder(&video_decoder_ctx, video_dec_codec, ifmt_ctx, in_video_stream, hw_device_ctx);
    if (ret < 0)
    {
        goto end;
    }
    if (in_audio_stream > -1)
    {
        ret = open_audio_decoder(&audio_decoder_ctx, audio_dec_codec, ifmt_ctx, in_audio_stream);
        if (ret < 0)
        {
            goto end;
        }
    }

#ifdef Debug
    if (in_audio_stream < 0)
    {
        audio_initialized = 1;
    }
#else
    /*如果找不到音频流,就不用初始化音频通道*/
    if (in_audio_stream < 0)
    {
        audio_initialized = 1;
        sprintf(rtpUrl, "[select=v:f=rtp:ssrc=2222:payload_type=101]rtp://%s:%s?rtcpport=%s", argv[2], argv[5], argv[6]);
    }
    else
    {
        sprintf(rtpUrl, "[select=a:f=rtp:ssrc=1111:payload_type=100]rtp://%s:%s?rtcpport=%s|[select=v:f=rtp:ssrc=2222:payload_type=101]rtp://%s:%s?rtcpport=%s", argv[2], argv[3], argv[4], argv[2], argv[5], argv[6]);
    }
    av_log(nullptr, AV_LOG_WARNING, "rtpUrl : %s\n", rtpUrl);
#endif

    /*rtsp拿到的时间基异常*/
    if (ifmt_ctx->streams[in_video_stream]->time_base.den == ifmt_ctx->streams[in_video_stream]->r_frame_rate.num)
    {
        av_log(nullptr, AV_LOG_ERROR, "rtsp拿到的时间基异常\n");
        goto end;
    }

    /*打开编码器*/
    switch (_VGADEVICE)
    {
    case NVIDIA:
        video_enc_codec = avcodec_find_encoder_by_name("h264_nvenc");
        break;
    case INTEL:
        video_enc_codec = avcodec_find_encoder_by_name("h264_vaapi");
        break;
    default:
        video_enc_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        break;
    }

    if (!video_enc_codec)
    {
        av_log(nullptr, AV_LOG_ERROR, "video 创建编码器失败\n");
        ret = -1;
        goto end;
    }

    if (in_audio_stream >= 0)
    {
        audio_enc_codec = avcodec_find_encoder_by_name("libopus");
        if (!audio_enc_codec)
        {
            av_log(nullptr, AV_LOG_ERROR, "audio 创建编码器失败\n");
            ret = -1;
            goto end;
        }
    }

    /*创建输出上下文*/
    ret = (avformat_alloc_output_context2(&ofmt_ctx, nullptr, "tee", rtpUrl));
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "创建输出上下文失败,%s\n", errStr);
        goto end;
    }

    /*初始化编码器*/
    video_encoder_ctx = avcodec_alloc_context3(video_enc_codec);
    if (!video_encoder_ctx)
    {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (in_audio_stream > -1)
    {
        audio_encoder_ctx = avcodec_alloc_context3(audio_enc_codec);
        if (!audio_encoder_ctx)
        {
            ret = AVERROR(ENOMEM);
            goto end;
        }
    }

    /*读流超时回调*/
    ifmt_ctx->interrupt_callback.opaque = &input_runner;
    ifmt_ctx->interrupt_callback.callback = interruptCallback;

    /*分配解码包*/
    dec_pkt = av_packet_alloc();
    if (!dec_pkt)
    {
        av_log(nullptr, AV_LOG_ERROR, "分配解码包失败\n");
        goto end;
    }
    /*开始转码*/
    while (ret >= 0)
    {
        input_runner.lasttime = time(NULL);
        /*从rtsp流中读取包*/
        ret = av_read_frame(ifmt_ctx, dec_pkt);
        if (ret < 0)
        {
            break;
        }

        /*视频包*/
        if (in_video_stream == dec_pkt->stream_index)
        {
            ret = video_dec_enc(dec_pkt, video_enc_codec, &hw_device_ctx, &video_decoder_ctx, &video_encoder_ctx, audio_initialized, video_initialized, initialized, rtpUrl, &video_ost, &ifmt_ctx, &ofmt_ctx, in_video_stream, out_video_stream, out_audio_stream, &buffersrc_ctx, &buffersink_ctx, &filter_graph, video_last_pts);
            if (ret < 0)
            {
                /*没有数据送入但是可以解析下一次*/
                if (ret == AVERROR(EAGAIN))
                {
                    ret = 0;
                }
            }
        }
        /*音频包*/
        if (in_audio_stream >= 0 && in_audio_stream == dec_pkt->stream_index)
        {
            ret = audio_dec_enc(dec_pkt, audio_enc_codec, &audio_decoder_ctx, &audio_encoder_ctx, audio_initialized, video_initialized, initialized, rtpUrl, &audio_ost, &ofmt_ctx, out_audio_stream, out_video_stream, &resample_context, audio_pts, max_dst_nb_samples, dst_nb_samples, &dst_data, dst_linesize, &fifo, audio_last_pts);
            if (ret < 0)
            {
                /*没有数据送入但是可以解析下一次*/
                if (ret == AVERROR(EAGAIN))
                {
                    ret = 0;
                }
            }
        }
        av_packet_unref(dec_pkt);
    }

    // /* flush decoder */
    // av_packet_unref(dec_pkt);
    // ret = dec_enc(dec_pkt, video_enc_codec, &video_decoder_ctx, &video_encoder_ctx, initialized, &video_ost, &ifmt_ctx, &ofmt_ctx, video_stream);

    // /* flush encoder */
    // ret = encode_write(dec_pkt, nullptr, &video_encoder_ctx, &ifmt_ctx, &ofmt_ctx, video_stream);

    /* write the trailer for output stream */
    av_write_trailer(ofmt_ctx);

end:
    if (!fifo)
    {
        av_audio_fifo_free(fifo);
    }
    if (dst_data)
    {
        av_freep(&dst_data[0]);
    }

    swr_free(&resample_context);
    avformat_close_input(&ifmt_ctx);
    avformat_close_input(&ofmt_ctx);
    avfilter_graph_free(&filter_graph);
    avcodec_free_context(&video_decoder_ctx);
    avcodec_free_context(&video_encoder_ctx);
    avcodec_free_context(&audio_decoder_ctx);
    avcodec_free_context(&audio_encoder_ctx);
    avfilter_graph_free(&filter_graph);
    av_buffer_unref(&hw_device_ctx);
    av_packet_free(&dec_pkt);
    return ret;
}