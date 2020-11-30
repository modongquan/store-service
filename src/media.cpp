#include <sys/time.h>
#include "media.h"

#ifdef __cplusplus
extern "C"
{
#endif

#include "libavformat/avformat.h"

#ifdef __cplusplus
}
#endif

int32_t TranslateToMp4(std::string &file_path, std::string &url)
{
    AVFormatContext *fmt_ctx = nullptr, *out_fmt_ctx = nullptr;
    AVPacket *av_pkt = nullptr;
    AVStream *out_stream = nullptr;
    int video_idx = -1, frame_index = 0;

    std::string mp4_path = transMediaPath, file_name;
    std::string::size_type start_pos, end_pos;
    int32_t ret = 0;
    char urlStr[128];

    start_pos = file_path.rfind('/') + 1;
    end_pos = file_path.rfind('.');
    file_name = file_path.substr(start_pos, end_pos - start_pos);
    mp4_path = mp4_path + file_name + ".mp4";
    snprintf(urlStr, sizeof(urlStr), "http://%s:%d/%s.mp4", downloadMediaIp.c_str(), playback_port, file_name.c_str());
    url = urlStr;
    myprintf("store path = %s, trans path = %s, url = %s\n", file_path.c_str(), mp4_path.c_str(), urlStr);

    if (avformat_open_input(&fmt_ctx, file_path.c_str(), nullptr, nullptr) < 0)
    {
        myprintf("avformat_open_input fail\n");
        ret = -1;
        goto error;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0)
    {
        myprintf("avformat_find_stream_info fail\n");
        ret = -1;
        goto error;
    }
    av_dump_format(fmt_ctx, 0, file_path.c_str(), 0);
    myprintf("\n");

    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++)
    {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_idx = i;
        }
    }
    if (video_idx < 0)
    {
        myprintf("did not find video data\n");
        ret = -1;
        goto error;
    }

    if (fmt_ctx->streams[video_idx]->r_frame_rate.den <= 0 || fmt_ctx->streams[video_idx]->r_frame_rate.num <= 0)
    {
        myprintf("frame rate not correct\n");
        ret = -1;
        goto error;
    }

    if (avformat_alloc_output_context2(&out_fmt_ctx, nullptr, nullptr, mp4_path.c_str()) < 0)
    {
        myprintf("avformat_alloc_output_context2 fail\n");
        ret = -1;
        goto error;
    }

    out_stream = avformat_new_stream(out_fmt_ctx, nullptr);
    if (!out_stream)
    {
        myprintf("avformat_new_stream fail\n");
        ret = -1;
        goto error;
    }
    out_stream->id = fmt_ctx->nb_streams - 1;

    ret = avcodec_parameters_copy(out_stream->codecpar, fmt_ctx->streams[video_idx]->codecpar);
    if (ret < 0)
    {
        myprintf("avcodec_copy_context fail\n");
        ret = -1;
        goto error;
    }
    out_stream->codecpar->codec_tag = 0;

    if (out_fmt_ctx->oformat->flags | AVFMT_NOFILE)
    {
        unlink(mp4_path.c_str());
    }

    if (avio_open(&out_fmt_ctx->pb, mp4_path.c_str(), AVIO_FLAG_WRITE) < 0)
    {
        myprintf("avio_open %s fail\n", mp4_path.c_str());
        ret = -1;
        goto error;
    }

    av_dump_format(out_fmt_ctx, 0, mp4_path.c_str(), 1);
    myprintf("\n");

    ret = avformat_write_header(out_fmt_ctx, nullptr);
    if (ret < 0)
    {
        myprintf("%s avformat_write_header fail\n", mp4_path.c_str());
        ret = -1;
        goto error;
    }

    av_pkt = av_packet_alloc();

    while (!av_read_frame(fmt_ctx, av_pkt))
    {
        if (av_pkt->stream_index == video_idx)
        {
            av_pkt->duration = av_rescale_q(av_pkt->duration, fmt_ctx->streams[video_idx]->time_base, out_stream->time_base);
            av_pkt->pts = ++frame_index * av_pkt->duration;
            av_pkt->dts = av_pkt->pts;
            av_pkt->pos = -1;
            if (av_interleaved_write_frame(out_fmt_ctx, av_pkt) < 0)
            {
                myprintf("av_interleaved_write_frame error\n");
                ret = -1;
                break;
            }
        }
        av_packet_unref(av_pkt);
    }

    ret = av_write_trailer(out_fmt_ctx);

error:
    if (fmt_ctx)
    {
        avformat_close_input(&fmt_ctx);
    }
    if (out_fmt_ctx)
    {
        avio_closep(&out_fmt_ctx->pb);
        avformat_free_context(out_fmt_ctx);
    }
    if (av_pkt)
        av_packet_free(&av_pkt);
    return ret;
}
