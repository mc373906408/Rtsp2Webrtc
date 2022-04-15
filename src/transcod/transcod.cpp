#include "transcod.h"
#include "fmt/format.h"

extern "C"
{
#include <ffnvcodec/dynlink_loader.h>
}

Transcod::Transcod()
{
}
Transcod::~Transcod()
{
}

int Transcod::openDevice(const Json::Value &data, const DeviceMediaData &mediaData)
{
    // av_log_set_level(AV_LOG_DEBUG);
    int ret = 0;

    if (mediaData.audioCodec)
    {
        /*启动音频转码*/
        m_isOpenAudio = true;
    }

    /*记录时间戳频率*/
    m_video_timestamp_frequency = mediaData.video_timestamp_frequency;

    /*创建新的输出通道*/
    addOutfmt(data);

    /*检测支持的转码方式*/
    detectVga();

    /*查找解码器*/
    if (strcmp(mediaData.videoCodec, "H264") == 0)
    {
        m_video_dec_codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    }
    else if (strcmp(mediaData.videoCodec, "H265") == 0)
    {
        m_video_dec_codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    }
    if (m_isOpenAudio)
    {
        if (strcmp(mediaData.audioCodec, "PCMU") == 0)
        {
            m_audio_dec_codec = avcodec_find_decoder(AV_CODEC_ID_PCM_MULAW);
        }
    }

    /*判断是否支持硬件转码*/
    setTranscodType();
    /*打开视频解码器*/
    ret = openVideoDecoder(mediaData.diffVideoTimestamp, mediaData.extradata, mediaData.extradata_size);
    if (ret < 0)
    {
        return ret;
    }

    /*打开音频解码器*/
    if (m_isOpenAudio)
    {
        ret = openAudioDecoder(mediaData.sample_rate, mediaData.channels);
        if (ret < 0)
        {
            return ret;
        }
        ret = openAudioEncoder();
        if (ret < 0)
        {
            return ret;
        }
    }

    return 0;
}

int Transcod::sendVideoData(const int64_t &data_num, unsigned char *data, const int &data_size, const int &idr, const int64_t &timestamp)
{
    // std::cout<<data_num<<"  "<<m_next_videoTranscod_num<<"  "<<std::this_thread::get_id()<<std::endl;
    int ret = 0;
    std::unique_ptr<AVPacket, Deleter_AVPacket> videoPkt = std::unique_ptr<AVPacket, Deleter_AVPacket>(std::move(av_packet_alloc()));
    /*根据数据填充pkt包*/
    ret = av_packet_from_data(videoPkt.get(), data, data_size);
    if (ret < 0)
    {
        return ret;
    }
    /*设置是否为i帧  0/1*/
    videoPkt->flags = idr;

    /*设置pts\dts*/
    videoPkt->pts = videoPkt->dts = timestamp;

    /*等待转码*/
    {
        std::unique_lock<std::mutex> lock(m_mtx_next_videoTranscod);
        while (data_num != m_next_videoTranscod_num)
        {
            m_cv_next_videoTranscod.wait(lock);
        }
    }
    /*实际转码*/
    ret = videoTranscod(std::move(videoPkt));
    if (ret < 0)
    {
        /*没有数据送入但是可以解析下一次*/
        if (ret == AVERROR(EAGAIN))
        {
            ret = 0;
        }
    }
    return ret;
}

int Transcod::sendAudioData(const int64_t &data_num, unsigned char *data, const int &data_size, const int64_t &timestamp, const int &diffTimestamp)
{
    int ret = 0;
    std::unique_ptr<AVPacket, Deleter_AVPacket> audioPkt = std::unique_ptr<AVPacket, Deleter_AVPacket>(std::move(av_packet_alloc()));
    /*根据数据填充pkt包*/
    ret = av_packet_from_data(audioPkt.get(), data, data_size);
    if (ret < 0)
    {
        return ret;
    }
    /*单声道持续时间等于数据长度*/
    if (m_audio_decoder_ctx->channels == 1)
    {
        audioPkt->duration = data_size;
    }
    audioPkt->flags = 1;
    /*pts/dts计算公式  pts = inc++ * (frame_size * 1000 / sample_rate)*/
    audioPkt->pts = audioPkt->dts = timestamp * 8;
    /*设置编码的音频起始包的位置,比如间隔40,时间戳80,按照0,40,80,来计算,这个是第三个包*/
    m_audio_firstPktNum = (timestamp / diffTimestamp) + 1;

    /*等待转码*/
    {
        std::unique_lock<std::mutex> lock(m_mtx_next_audioTranscod);
        while (data_num != m_next_audioTranscod_num)
        {
            m_cv_next_audioTranscod.wait(lock);
        }
    }

    /*实际转码*/
    ret = audioTranscod(std::move(audioPkt));
    if (ret < 0)
    {
        /*没有数据送入但是可以解析下一次*/
        if (ret == AVERROR(EAGAIN))
        {
            ret = 0;
        }
    }
    return ret;
}

void Transcod::detectVga()
{
    int ret = 0;
    std::vector<std::string> resvec;
    ret = shellexec("lspci | grep -i vga", resvec);
    if (ret <= 0)
    {
        m_TranscodType = SOFTWARE;
        return;
    }

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
                    m_TranscodType = NVIDIA;
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

        if (NVIDIAH264 && NVIDIAH265)
        {
            m_hwH264 = true;
            m_hwH265 = true;
            m_TranscodType = NVIDIA;
        }
        else if (INTELH264 && INTELH265)
        {
            m_hwH264 = true;
            m_hwH265 = true;
            m_TranscodType = INTEL;
        }
        else if (NVIDIAH264)
        {
            m_hwH264 = true;
            m_TranscodType = NVIDIA;
        }
        else if (INTELH264)
        {
            m_hwH264 = true;
            m_TranscodType = INTEL;
        }
    }
    if (m_TranscodType == NOTSELECT)
    {
        m_TranscodType = SOFTWARE;
    }
}

int Transcod::shellexec(const char *cmd, std::vector<std::string> &resvec)
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

void Transcod::setTranscodType()
{
    int ret = 0;
    if (!((m_video_dec_codec->id == AV_CODEC_ID_H264 && m_hwH264) ||
          (m_video_dec_codec->id == AV_CODEC_ID_HEVC && m_hwH265)))
    {
        m_TranscodType = SOFTWARE;
    }

    AVBufferRef *hw_device_ctx = nullptr;
    /*打开硬件设备*/
    switch (m_TranscodType)
    {
    case NVIDIA:
        ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
        if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "创建 CUDA 设备失败,%s\n", errStr);
        }
        break;
    case INTEL:
        ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0);
        if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "创建 VAAPI 设备失败,%s\n", errStr);
        }
        break;

    default:
        break;
    }
    m_hw_device_ctx = std::unique_ptr<AVBufferRef, Deleter_AVBufferRef>(std::move(hw_device_ctx));

    if (ret < 0)
    {
        m_TranscodType = SOFTWARE;
    }

    av_log(nullptr, AV_LOG_WARNING, "当前摄像机视频格式%s,使用的解码方式%s\n", m_video_dec_codec->name, m_TranscodType == NVIDIA ? "NVIDIA" : m_TranscodType == INTEL ? "INTEL"
                                                                                                                                                                      : "SOFTWARE");
}

int Transcod::openVideoDecoder(const int &timestamp_diff, unsigned char *extradata, const int &extradata_size)
{
    int ret = 0;
    /*初始化解码器*/
    m_video_decoder_ctx = std::unique_ptr<AVCodecContext, Deleter_AVCodecContext>(std::move(avcodec_alloc_context3(m_video_dec_codec)));
    if (!m_video_decoder_ctx)
    {
        return AVERROR(ENOMEM);
    }

    /*设置解码上下文*/
    m_video_decoder_ctx->extradata = reinterpret_cast<unsigned char *>(av_malloc(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE));
    memcpy(m_video_decoder_ctx->extradata, extradata, extradata_size);
    m_video_decoder_ctx->extradata_size = extradata_size;
    memset(&m_video_decoder_ctx->extradata[m_video_decoder_ctx->extradata_size], 0, AV_INPUT_BUFFER_PADDING_SIZE);

    /*码率直接设置为0*/
    m_video_decoder_ctx->bit_rate = 0;

    /*赋值video时间基*/
    m_video_decoder_ctx->framerate = {1000 / timestamp_diff, 1};
    m_video_decoder_ctx->time_base = av_inv_q(m_video_decoder_ctx->framerate);

    /*创建硬件设备引用*/
    switch (m_TranscodType)
    {
    case NVIDIA:
        m_video_decoder_ctx->hw_device_ctx = av_buffer_ref(m_hw_device_ctx.get());
        if (!m_video_decoder_ctx->hw_device_ctx)
        {
            av_log(nullptr, AV_LOG_ERROR, "硬件设备引用创建失败\n");
            return AVERROR(ENOMEM);
        }
        m_video_decoder_ctx->get_format = Transcod::get_NVIDIA_hwdevice_format;
        break;
    case INTEL:
        m_video_decoder_ctx->hw_device_ctx = av_buffer_ref(m_hw_device_ctx.get());
        if (!m_video_decoder_ctx->hw_device_ctx)
        {
            av_log(nullptr, AV_LOG_ERROR, "硬件设备引用创建失败\n");
            return AVERROR(ENOMEM);
        }
        m_video_decoder_ctx->get_format = Transcod::get_INTEL_hwdevice_format;
        break;

    default:
        break;
    }

    /*打开解码器*/
    ret = avcodec_open2(m_video_decoder_ctx.get(), m_video_dec_codec, nullptr);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "video 无法打开编解码器进行解码,%s\n", errStr);
        return ret;
    }

    return ret;
}

int Transcod::openAudioDecoder(const int &sample_rate, const int &channels)
{
    int ret = 0;
    /*初始化解码器*/
    m_audio_decoder_ctx = std::unique_ptr<AVCodecContext, Deleter_AVCodecContext>(std::move(avcodec_alloc_context3(m_audio_dec_codec)));
    if (!m_audio_decoder_ctx)
    {
        return AVERROR(ENOMEM);
    }
    /*设置解码上下文*/
    m_audio_decoder_ctx->sample_rate = sample_rate;
    m_audio_decoder_ctx->channels = channels;
    if (m_audio_dec_codec->id == AV_CODEC_ID_PCM_MULAW)
    {
        // 音频为G711或者G726编码时，音频数据的采样频率为8000，16位采样且是单通道的
        m_audio_decoder_ctx->sample_fmt = AV_SAMPLE_FMT_S16;
        // 比特率设置64000
        m_audio_decoder_ctx->bit_rate = 64000;
    }

    ret = avcodec_open2(m_audio_decoder_ctx.get(), m_audio_dec_codec, nullptr);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "audio 无法打开编解码器进行解码,%s\n", errStr);
        return ret;
    }

    return ret;
}

int Transcod::openRtpUrl(std::shared_ptr<Outfmt> outfmt)
{
    int ret = 0;
    if (outfmt->audio_init && outfmt->video_init)
    {
        return ret;
    }

    /*初始化视频输出流*/
    if (!outfmt->video_init && m_video_enc_init)
    {
        av_log(nullptr, AV_LOG_WARNING, "video rtpUrl : %s\n", outfmt->video_rtp_url.c_str());
        /*创建输出上下文*/
        ret = avformat_alloc_output_context2(&outfmt->video_ofmt_ctx, nullptr, "tee", outfmt->video_rtp_url.c_str());
        if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "创建输出上下文失败,%s\n", errStr);
            return ret;
        }
        /*添加流*/
        outfmt->video_ost = avformat_new_stream(outfmt->video_ofmt_ctx, m_video_enc_codec);
        if (!outfmt->video_ost)
        {
            av_log(nullptr, AV_LOG_ERROR, "video 为输出格式分配流失败\n");
            ret = AVERROR(ENOMEM);
            return -1;
        }

        ret = avcodec_parameters_from_context(outfmt->video_ost->codecpar, m_video_encoder_ctx.get());
        if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "video 无法复制流参数。,%s\n", errStr);
            return ret;
        }

        /*打开推流*/
        if (!(outfmt->video_ofmt_ctx->oformat->flags & AVFMT_NOFILE))
        {
            ret = avio_open(&outfmt->video_ofmt_ctx->pb, outfmt->video_rtp_url.c_str(), AVIO_FLAG_WRITE);
            if (ret < 0)
            {
                char errStr[256] = {0};
                av_strerror(ret, errStr, sizeof(errStr));
                av_log(nullptr, AV_LOG_ERROR, "打开推流失败,%s\n", errStr);
                return ret;
            }
        }

        /*写入流标头*/
        ret = avformat_write_header(outfmt->video_ofmt_ctx, nullptr);
        if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "写入流标头时出错,%s\n", errStr);
            return ret;
        }
        outfmt->video_init = true;
    }

    /*如果找不到音频流,就不用初始化音频通道*/
    if (!outfmt->audio_init && m_isOpenAudio && !outfmt->audio_rtp_url.empty())
    {
        av_log(nullptr, AV_LOG_WARNING, "audio rtpUrl : %s\n", outfmt->audio_rtp_url.c_str());
        /*创建输出上下文*/
        ret = avformat_alloc_output_context2(&outfmt->audio_ofmt_ctx, nullptr, "tee", outfmt->audio_rtp_url.c_str());
        if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "创建输出上下文失败,%s\n", errStr);
            return ret;
        }

        /*添加流*/
        outfmt->audio_ost = avformat_new_stream(outfmt->audio_ofmt_ctx, m_audio_enc_codec);
        if (!outfmt->audio_ost)
        {
            av_log(nullptr, AV_LOG_ERROR, "audio 为输出格式分配流失败\n");
            ret = AVERROR(ENOMEM);
            return -1;
        }

        ret = avcodec_parameters_from_context(outfmt->audio_ost->codecpar, m_audio_encoder_ctx.get());
        if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "audio 无法复制流参数。,%s\n", errStr);
            return ret;
        }
        outfmt->audio_ost->time_base = m_audio_encoder_ctx->time_base;

        /*打开推流*/
        if (!(outfmt->audio_ofmt_ctx->oformat->flags & AVFMT_NOFILE))
        {
            ret = avio_open(&outfmt->audio_ofmt_ctx->pb, outfmt->audio_rtp_url.c_str(), AVIO_FLAG_WRITE);
            if (ret < 0)
            {
                char errStr[256] = {0};
                av_strerror(ret, errStr, sizeof(errStr));
                av_log(nullptr, AV_LOG_ERROR, "打开推流失败,%s\n", errStr);
                return ret;
            }
        }

        /*写入流标头*/
        ret = avformat_write_header(outfmt->audio_ofmt_ctx, nullptr);
        if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "写入流标头时出错,%s\n", errStr);
            return ret;
        }
        outfmt->audio_init = true;
    }
    return ret;
}

int Transcod::openVideoEncoder()
{
    int ret = 0;

    /*打开编码器*/
    switch (m_TranscodType)
    {
    case NVIDIA:
        m_video_enc_codec = avcodec_find_encoder_by_name("h264_nvenc");
        break;
    case INTEL:
        m_video_enc_codec = avcodec_find_encoder_by_name("h264_vaapi");
        break;
    default:
        m_video_enc_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        break;
    }
    if (!m_video_enc_codec)
    {
        av_log(nullptr, AV_LOG_ERROR, "video 创建编码器失败\n");
        return -1;
    }

    /*初始化编码器*/
    m_video_encoder_ctx = std::unique_ptr<AVCodecContext, Deleter_AVCodecContext>(std::move(avcodec_alloc_context3(m_video_enc_codec)));
    if (!m_video_encoder_ctx)
    {
        return AVERROR(ENOMEM);
    }
    int width, height;
    /*降低分辨率到640P*/
    if (m_video_decoder_ctx->width > 640)
    {
        width = 640;
        height = m_video_decoder_ctx->height / (m_video_decoder_ctx->width / 640);
    }
    else
    {
        width = m_video_decoder_ctx->width;
        height = m_video_decoder_ctx->height;
    }

    /*我们需要引用解码器的 hw_frames_ctx 来初始化编码器的编解码器。只有得到解码后的帧，才能得到它的hw_frames_ctx*/
    switch (m_TranscodType)
    {
    case NVIDIA:
    case INTEL:
        m_video_encoder_ctx->hw_frames_ctx = av_buffer_ref(m_video_decoder_ctx->hw_frames_ctx);
        if (!m_video_encoder_ctx->hw_frames_ctx)
        {
            ret = AVERROR(ENOMEM);
            return ret;
        }
        break;

    default:
        break;
    }

    /*为编码器设置 AVCodecContext 参数，这里我们保留它们与解码器相同,降低分辨率*/
    m_video_encoder_ctx->framerate = m_video_decoder_ctx->framerate;
    m_video_encoder_ctx->time_base = av_inv_q(m_video_decoder_ctx->framerate);
    m_video_encoder_ctx->width = width;
    switch (m_TranscodType)
    {
    case NVIDIA:
        m_video_encoder_ctx->pix_fmt = AV_PIX_FMT_CUDA;
        break;
    case INTEL:
        m_video_encoder_ctx->pix_fmt = AV_PIX_FMT_VAAPI;
        break;
    default:
        m_video_encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        /*使用Baseline Profile + Level 3.1编码*/
        av_opt_set(m_video_encoder_ctx->priv_data, "profile", "baseline", AV_OPT_SEARCH_CHILDREN);
        av_opt_set(m_video_encoder_ctx->priv_data, "level", "31", AV_OPT_SEARCH_CHILDREN);
        break;
    }

    m_video_encoder_ctx->height = height;

    /*禁用B帧*/
    m_video_encoder_ctx->max_b_frames = 0;

    /*I帧间隔*/
    m_video_encoder_ctx->gop_size = m_video_encoder_ctx->time_base.den * 2;
    /*必须要加头,否则无法解析*/
    m_video_encoder_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    /*打开编码器*/
    ret = avcodec_open2(m_video_encoder_ctx.get(), m_video_enc_codec, nullptr);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "video 无法打开编码器,%s\n", errStr);
        return ret;
    }

    /*滤镜初始化*/
    ret = openVideoFilter(width, height);
    if (ret < 0)
    {
        return ret;
    }

    return ret;
}

int Transcod::openAudioEncoder()
{
    int ret = 0;
    /*打开编码器*/
    m_audio_enc_codec = avcodec_find_encoder_by_name("libopus");
    if (!m_audio_enc_codec)
    {
        av_log(nullptr, AV_LOG_ERROR, "audio 创建编码器失败\n");
        return -1;
    }

    /*初始化编码器*/
    m_audio_encoder_ctx = std::unique_ptr<AVCodecContext, Deleter_AVCodecContext>(std::move(avcodec_alloc_context3(m_audio_enc_codec)));
    if (!m_audio_encoder_ctx)
    {
        return AVERROR(ENOMEM);
    }

    /*为编码器设置 AVCodecContext 参数，这里我们保留它们与解码器相同*/
    m_audio_encoder_ctx->bit_rate = m_audio_decoder_ctx->bit_rate;                                      //比特率
    m_audio_encoder_ctx->sample_rate = 48000;                                                           //采样率
    m_audio_encoder_ctx->channels = 2;                                                                  //声道数
    m_audio_encoder_ctx->channel_layout = av_get_default_channel_layout(m_audio_encoder_ctx->channels); //声道布局
    m_audio_decoder_ctx->channel_layout = av_get_default_channel_layout(m_audio_decoder_ctx->channels);
    m_audio_encoder_ctx->sample_fmt = m_audio_enc_codec->sample_fmts[0];    //采样格式
    m_audio_encoder_ctx->time_base = {1, m_audio_encoder_ctx->sample_rate}; //时间基
    /*必须要加头,否则无法解析*/
    m_audio_encoder_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    /*打开编码器*/
    ret = avcodec_open2(m_audio_encoder_ctx.get(), m_audio_enc_codec, NULL);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "audio 无法打开编码器,%s\n", errStr);
        return ret;
    }

    return ret;
}

int Transcod::openAudioSwrContext(std::shared_ptr<AVFrame> frame)
{
    int ret = 0;
    /*创建重采样*/
    m_resample_context = std::unique_ptr<SwrContext, Deleter_SwrContext>(std::move(swr_alloc_set_opts(nullptr, m_audio_encoder_ctx->channel_layout, m_audio_encoder_ctx->sample_fmt, m_audio_encoder_ctx->sample_rate, m_audio_decoder_ctx->channel_layout, m_audio_decoder_ctx->sample_fmt, m_audio_decoder_ctx->sample_rate, 0, nullptr)));
    if (!m_resample_context)
    {
        av_log(nullptr, AV_LOG_ERROR, "audio 创建重采样失败\n");
        return -1;
    }

    ret = swr_init(m_resample_context.get());
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "audio 无法打开重采样,%s\n", errStr);
        return ret;
    }

    m_max_dst_nb_samples = m_dst_nb_samples = av_rescale_rnd(frame->nb_samples, m_audio_encoder_ctx->sample_rate, frame->sample_rate, AV_ROUND_UP);

    uint8_t **dst_data = nullptr;
    ret = av_samples_alloc_array_and_samples(&dst_data, &m_dst_linesize, m_audio_encoder_ctx->channels, m_dst_nb_samples, m_audio_encoder_ctx->sample_fmt, 0);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "audio  av_samples_alloc_array_and_samples 分配dst_data失败,%s\n", errStr);
        return ret;
    }
    m_dst_data = std::unique_ptr<uint8_t *, Deleter_DstData>(std::move(dst_data));

    /*重采样一次,计算出重采样延迟*/
    swr_convert(m_resample_context.get(), m_dst_data.get(), m_dst_nb_samples, const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);

    m_fifo = std::unique_ptr<AVAudioFifo, Deleter_AVAudioFifo>(std::move(av_audio_fifo_alloc(m_audio_encoder_ctx->sample_fmt, m_audio_encoder_ctx->channels, m_audio_encoder_ctx->frame_size)));
    if (!m_fifo)
    {
        av_log(nullptr, AV_LOG_ERROR, "audio 创建fifo失败\n");
        return -1;
    }
    return ret;
}

int Transcod::videoTranscod(std::unique_ptr<AVPacket, Deleter_AVPacket> videoPkt)
{
    int ret = 0;
    /*发送 AVPacket 数据包给视频解码器*/
    ret = avcodec_send_packet(m_video_decoder_ctx.get(), videoPkt.get());
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "video 解码失败,%s\n", errStr);
        return ret;
    }
    while (ret >= 0)
    {
        auto deleteFrame = [](AVFrame *obj)
        {
            av_frame_free(&obj);
        };
        std::shared_ptr<AVFrame> frame(std::move(av_frame_alloc()), deleteFrame);
        if (!frame)
        {
            return AVERROR(ENOMEM);
        }
        /*解码后存到显存(软解在内存)*/
        ret = avcodec_receive_frame(m_video_decoder_ctx.get(), frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            return 0;
        }
        else if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "video 解码时出错,%s\n", errStr);
            return ret;
        }

        if (!m_video_enc_init)
        {
            /*解码第一帧后初始化编码器和滤镜*/
            ret = openVideoEncoder();
            if (ret < 0)
            {
                return ret;
            }
            m_video_enc_init = true;
        }

        /*打开rtp地址*/
        {
            std::unique_lock<std::mutex> lock(m_mtx_outfmt);
            for (const auto &[key, value] : m_map_outfmt)
            {
                ret = openRtpUrl(value);
                if (ret < 0)
                {
                    return ret;
                }
            }
        }
        /*将数据送入滤镜*/
        ret = videoFilter(frame);
        if (ret < 0)
        {
            return ret;
        }
        /*编码*/
        ret = videoEncodeWrite(frame);
        if (ret < 0)
        {
            av_log(nullptr, AV_LOG_ERROR, "video 编码失败\n");
            return ret;
        }
    }
    return 0;
}

int Transcod::openVideoFilter(const int &width, const int &height)
{
    int ret = 0;

    m_filter_graph = std::unique_ptr<AVFilterGraph, Deleter_AVFilterGraph>(std::move(avfilter_graph_alloc()));
    const AVFilter *buffersrc = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");

    /*创建过滤器源*/
    char args[128] = {};
    char args2[128] = {};
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             m_video_decoder_ctx->width, m_video_decoder_ctx->height, m_video_decoder_ctx->pix_fmt,
             1, m_video_timestamp_frequency,
             m_video_decoder_ctx->sample_aspect_ratio.num, m_video_decoder_ctx->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&m_buffersrc_ctx, buffersrc, "in", args, nullptr, m_filter_graph.get());
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "创建过滤器源失败,%s\n", errStr);
        return ret;
    }

    //这里比初始化软件滤镜多的一步，将hw_frames_ctx传给buffersrc, 这样buffersrc就知道传给它的是硬件解码器，数据在显存内
    if (m_video_decoder_ctx->hw_frames_ctx)
    {
        AVBufferSrcParameters *par = av_buffersrc_parameters_alloc();
        par->hw_frames_ctx = m_video_decoder_ctx->hw_frames_ctx;
        ret = av_buffersrc_parameters_set(m_buffersrc_ctx, par);
        av_freep(&par);
        if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "hw_frames_ctx传给buffersrc失败,%s\n", errStr);
            return ret;
        }
    }

    /*创建接收过滤器*/
    ret = avfilter_graph_create_filter(&m_buffersink_ctx, buffersink, "out", nullptr, nullptr, m_filter_graph.get());
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "创建接收过滤器失败,%s\n", errStr);
        return ret;
    }

    AVFilterInOut *inputs = avfilter_inout_alloc();
    AVFilterInOut *outputs = avfilter_inout_alloc();
    /* Endpoints for the filter graph. */
    inputs->name = av_strdup("out");
    inputs->filter_ctx = m_buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    outputs->name = av_strdup("in");
    outputs->filter_ctx = m_buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    /*通过解析过滤器字符串添加过滤器*/
    switch (m_TranscodType)
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
    ret = avfilter_graph_parse_ptr(m_filter_graph.get(), args2, &inputs, &outputs, nullptr);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "通过解析过滤器字符串添加过滤器失败,%s\n", errStr);
        goto end;
    }

    /*检查过滤器的完整性*/
    ret = avfilter_graph_config(m_filter_graph.get(), nullptr);
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

int Transcod::videoFilter(std::shared_ptr<AVFrame> frame)
{
    int ret = 0;
    /*将数据放进滤镜*/
    ret = av_buffersrc_add_frame(m_buffersrc_ctx, frame.get());
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "滤镜处理失败,av_buffersrc_add_frame,%s\n", errStr);
        return ret;
    }
    /*取出数据*/
    ret = av_buffersink_get_frame(m_buffersink_ctx, frame.get());
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "滤镜处理失败,av_buffersink_get_frame,%s\n", errStr);
        return ret;
    }
    return ret;
}

int Transcod::videoEncodeWrite(std::shared_ptr<AVFrame> frame)
{
    int ret = 0;

    /*送进编码器*/
    ret = avcodec_send_frame(m_video_encoder_ctx.get(), frame.get());
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "video 编码失败,avcodec_send_frame,%s\n", errStr);
        goto end;
    }

    while (1)
    {
        std::unique_ptr<AVPacket, Deleter_AVPacket> videoPkt = std::unique_ptr<AVPacket, Deleter_AVPacket>(std::move(av_packet_alloc()));
        /*编码数据压入包*/
        ret = avcodec_receive_packet(m_video_encoder_ctx.get(), videoPkt.get());
        if (ret)
        {
            break;
        }
        /*跳过异常帧*/
        if (videoPkt->pts < videoPkt->dts)
        {
            av_log(nullptr, AV_LOG_ERROR, "video 跳过异常帧\n");
            continue;
        }

        std::unique_lock<std::mutex> lock(m_mtx_outfmt);
        for (const auto &[key, value] : m_map_outfmt)
        {
            if (!value->video_init)
            {
                continue;
            }
            /*拷贝pkt*/
            std::unique_ptr<AVPacket, Deleter_AVPacket> clonedPkt(av_packet_clone(videoPkt.get()));

            clonedPkt->stream_index = 0;
            av_packet_rescale_ts(clonedPkt.get(), {1, m_video_timestamp_frequency},
                                 value->video_ofmt_ctx->streams[0]->time_base);

            clonedPkt->pts = clonedPkt->pts < 0 ? 0 : clonedPkt->pts;
            clonedPkt->dts = clonedPkt->dts < 0 ? 0 : clonedPkt->dts;

            /*本次推流包的pts要比上次推流包大*/
            if (clonedPkt->pts > value->video_last_pts)
            {
                value->video_last_pts = clonedPkt->pts;
                ret = av_interleaved_write_frame(value->video_ofmt_ctx, clonedPkt.get());
                if (ret < 0)
                {
                    char errStr[256] = {0};
                    av_strerror(ret, errStr, sizeof(errStr));
                    av_log(nullptr, AV_LOG_ERROR, "video 将数据写入输出流时出错,%s\n", errStr);
                    return -1;
                }
            }
        }
    }

end:
    if (ret == AVERROR_EOF)
    {
        return 0;
    }
    if (ret < 0)
    {
        ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);
    }
    return ret;
}

int Transcod::audioTranscod(std::unique_ptr<AVPacket, Deleter_AVPacket> audioPkt)
{
    int ret = 0;

    /*发送 AVPacket 数据包给音频解码器*/
    ret = avcodec_send_packet(m_audio_decoder_ctx.get(), audioPkt.get());
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "audio 解码失败,%s\n", errStr);
        return ret;
    }

    while (ret >= 0)
    {
        auto deleteFrame = [](AVFrame *obj)
        {
            av_frame_free(&obj);
        };
        std::shared_ptr<AVFrame> frame(std::move(av_frame_alloc()), deleteFrame);
        if (!frame)
        {
            return AVERROR(ENOMEM);
        }
        /*解码后存到内存*/
        ret = avcodec_receive_frame(m_audio_decoder_ctx.get(), frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            return 0;
        }
        else if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "audio 解码时出错,%s\n", errStr);
            return ret;
        }

        /*打开rtp地址*/
        {
            std::unique_lock<std::mutex> lock(m_mtx_outfmt);
            for (const auto &[key, value] : m_map_outfmt)
            {
                ret = openRtpUrl(value);
                if (ret < 0)
                {
                    return ret;
                }
            }
        }
        if (!m_audio_swrContext_init)
        {
            /*解码第一帧后初始化重采样*/
            ret = openAudioSwrContext(frame);
            if (ret < 0)
            {
                return ret;
            }
            m_audio_swrContext_init = true;
        }

        /*编码*/
        ret = audioEncodeWrite(frame);
        if (ret < 0)
        {
            av_log(nullptr, AV_LOG_ERROR, "audio 编码失败\n");
            return ret;
        }
    }

    return 0;
}

int Transcod::audioEncodeWrite(std::shared_ptr<AVFrame> frame)
{
    int ret = 0;
    int nb_samples = 0;

    /*重采样*/
    m_dst_nb_samples = av_rescale_rnd(swr_get_delay(m_resample_context.get(), frame->sample_rate) + frame->nb_samples,
                                      m_audio_encoder_ctx->sample_rate, frame->sample_rate, AV_ROUND_UP);

    if (m_dst_nb_samples > m_max_dst_nb_samples)
    {
        av_freep(&m_dst_data.get()[0]);
        ret = av_samples_alloc(m_dst_data.get(), &m_dst_linesize, m_audio_encoder_ctx->channels, m_dst_nb_samples, m_audio_encoder_ctx->sample_fmt, 0);
        if (ret < 0)
        {
            char errStr[256] = {0};
            av_strerror(ret, errStr, sizeof(errStr));
            av_log(nullptr, AV_LOG_ERROR, "audio  av_samples_alloc 分配dst_data失败,%s\n", errStr);
            return ret;
        }
        m_max_dst_nb_samples = m_dst_nb_samples;
    }

    nb_samples = swr_convert(m_resample_context.get(), m_dst_data.get(), m_dst_nb_samples, const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
    if (nb_samples < 0)
    {
        av_log(nullptr, AV_LOG_ERROR, "audio 重采样失败\n");
        return -1;
    }

    /*将数据推入fifo*/
    ret = audioAddSamplesToFifo(nb_samples);
    if (ret < 0)
    {
        return ret;
    }
    if (av_audio_fifo_size(m_fifo.get()) < m_audio_encoder_ctx->frame_size)
    {
        /*fifo数据不满足一帧,等下次输入*/
        return 0;
    }

    /*数据长度满足一帧,编码*/
    while (av_audio_fifo_size(m_fifo.get()) >= m_audio_encoder_ctx->frame_size)
    {
        int frame_size = FFMIN(av_audio_fifo_size(m_fifo.get()), m_audio_encoder_ctx->frame_size);

        /*创建临时的输出frame*/
        AVFrame *outframe = av_frame_alloc();
        if (!outframe)
        {
            av_log(nullptr, AV_LOG_ERROR, "audio 无法创建临时frame\n");
            return -1;
        }
        outframe->nb_samples = frame_size;
        outframe->channel_layout = m_audio_encoder_ctx->channel_layout;
        outframe->format = m_audio_encoder_ctx->sample_fmt;
        outframe->sample_rate = m_audio_encoder_ctx->sample_rate;

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
        if (av_audio_fifo_read(m_fifo.get(), reinterpret_cast<void **>(outframe->data), frame_size) < frame_size)
        {
            av_frame_free(&outframe);
            av_log(nullptr, AV_LOG_ERROR, "audio 无法从 FIFO 读取数据\n");
            return -1;
        }

        /*给frame设置pts*/
        if (outframe)
        {
            if (m_audio_pts == -1)
            {
                m_audio_pts = m_audio_firstPktNum * outframe->nb_samples;
            }
            outframe->pts = m_audio_pts;
            m_audio_pts += outframe->nb_samples;
        }

        /*编码*/
        ret = avcodec_send_frame(m_audio_encoder_ctx.get(), outframe);
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
            std::unique_ptr<AVPacket, Deleter_AVPacket> audioPkt = std::unique_ptr<AVPacket, Deleter_AVPacket>(std::move(av_packet_alloc()));
            /*编码数据压入包*/
            ret = avcodec_receive_packet(m_audio_encoder_ctx.get(), audioPkt.get());
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
                goto end;
            }

            std::unique_lock<std::mutex> lock(m_mtx_outfmt);
            for (const auto &[key, value] : m_map_outfmt)
            {
                if (!value->audio_init || value->audio_rtp_url.empty())
                {
                    continue;
                }
                /*拷贝pkt*/
                std::unique_ptr<AVPacket, Deleter_AVPacket> clonedPkt(av_packet_clone(audioPkt.get()));

                clonedPkt->stream_index = 0;

                /*如果全部准备完毕，就开始推流,并且,本次推流包的pts要比上次推流包大*/
                if (clonedPkt->pts > 0 && clonedPkt->pts > value->audio_last_pts)
                {
                    value->audio_last_pts = clonedPkt->pts;
                    ret = av_interleaved_write_frame(value->audio_ofmt_ctx, clonedPkt.get());
                    if (ret < 0)
                    {
                        char errStr[256] = {0};
                        av_strerror(ret, errStr, sizeof(errStr));
                        av_log(nullptr, AV_LOG_ERROR, "audio 将数据写入输出流时出错,%s\n", errStr);

                        return -1;
                    }
                }
            }
        }
    }

end:
    if (ret == AVERROR_EOF)
    {
        return 0;
    }

    if (ret < 0)
    {
        ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);
    }
    return ret;
}

int Transcod::audioAddSamplesToFifo(const int &nb_samples)
{
    int ret = 0;
    ret = av_audio_fifo_realloc(m_fifo.get(), av_audio_fifo_size(m_fifo.get()) + nb_samples);
    if (ret < 0)
    {
        char errStr[256] = {0};
        av_strerror(ret, errStr, sizeof(errStr));
        av_log(nullptr, AV_LOG_ERROR, "audio 无法重新分配 FIFO,%s\n", errStr);
        return ret;
    }
    if (av_audio_fifo_write(m_fifo.get(), reinterpret_cast<void **>(m_dst_data.get()), nb_samples) < nb_samples)
    {
        av_log(nullptr, AV_LOG_ERROR, "audio 无法将数据写入 FIFO\n");
        return ret;
    }
    return ret;
}

void Transcod::addOutfmt(const Json::Value &data)
{
    std::unique_lock<std::mutex> lock(m_mtx_outfmt);
    std::string lanIP = data["webrtcLanIP"].asString();
    m_map_outfmt[data["roomid"].asString()] = std::unique_ptr<Outfmt>(std::make_unique<Outfmt>());
    if (data.isMember("audioPort"))
    {
        std::pair<size_t, size_t> audioPort = {-1, -1};
        audioPort.first = data["audioPort"][0].asInt();
        audioPort.second = data["audioPort"][1].asInt();
        if (audioPort.first > 0 && audioPort.second > 0)
        {
            m_map_outfmt[data["roomid"].asString()]->audio_rtp_url = fmt::format("[select=a:f=rtp:ssrc=1111:payload_type=100]rtp://{}:{}?rtcpport={}", lanIP, audioPort.first, audioPort.second);
        }
    }
    m_map_outfmt[data["roomid"].asString()]->video_rtp_url = fmt::format("[select=v:f=rtp:ssrc=2222:payload_type=101]rtp://{}:{}?rtcpport={}", lanIP, data["videoPort"][0].asInt(), data["videoPort"][1].asInt());
}

void Transcod::removeOutfmt(const std::string &roomid)
{
    std::unique_lock<std::mutex> lock(m_mtx_outfmt);
    m_map_outfmt.erase(roomid);
}

void Transcod::joinRoom(const Json::Value &data)
{
    addOutfmt(data);
}

void Transcod::quitRoom(const std::string &roomid)
{
    removeOutfmt(roomid);
}

int Transcod::getRoomNum()
{
    std::unique_lock<std::mutex> lock(m_mtx_outfmt);
    return m_map_outfmt.size();
}


void Transcod::addVideoPktNum()
{
    std::unique_lock<std::mutex> lock(m_mtx_next_videoTranscod);
    m_next_videoTranscod_num++;
}
void Transcod::wakeVideoTranscod()
{
    std::unique_lock<std::mutex> lock(m_mtx_next_videoTranscod);
    m_cv_next_videoTranscod.notify_all();
}

void Transcod::addAudioPktNum()
{
    std::unique_lock<std::mutex> lock(m_mtx_next_audioTranscod);
    m_next_audioTranscod_num++;
}
void Transcod::wakeAudioTranscod()
{
    std::unique_lock<std::mutex> lock(m_mtx_next_audioTranscod);
    m_cv_next_audioTranscod.notify_all();
}

enum AVPixelFormat Transcod::get_NVIDIA_hwdevice_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;
    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++)
    {
        if (*p == AV_PIX_FMT_CUDA)
            return *p;
    }
    av_log(nullptr, AV_LOG_ERROR, "无法使用 CUDA 解码此文件\n");

    return AV_PIX_FMT_NONE;
}

enum AVPixelFormat Transcod::get_INTEL_hwdevice_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;

    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++)
    {
        if (*p == AV_PIX_FMT_VAAPI)
            return *p;
    }
    av_log(nullptr, AV_LOG_ERROR, "无法使用 VA-API 解码此文件\n");

    return AV_PIX_FMT_NONE;
}
