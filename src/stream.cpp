#include <pthread.h>
#include "stream.h"

#ifdef __cplusplus
extern "C"
{
#endif

#include "libavformat/avformat.h"

#ifdef __cplusplus
}
#endif

enum
{
    thread_creating = 0,
    thread_running,
    thread_stopping,
    thread_stopped,
    thread_cancel
};

struct stream_thread
{
    std::string stream_id;
    std::string stream_path;
    StreamCB stream_cb;
    int thread_state;
    std::vector<std::vector<std::vector<struct tm>>> rec_schedule;
};

static std::map<std::string, struct stream_thread> streamThreadList;
static volatile int isThreadListUsed = 0;

extern std::string grpcClientStart(std::string &mainId, std::string &subId);

void *StreamProc(void *arg)
{
    AVFormatContext *pFmtCtx = nullptr;
    AVCodecContext *pCodecCtx = nullptr;
    AVCodec *pCodec = nullptr;
    AVPacket *av_pkt = nullptr;
    int video_idx = -1;
    struct stream_thread *in = (struct stream_thread *)arg;
#if 0
    std::string streamId = in->stream_id;
    std::string streamPath = in->stream_path;
    StreamCB stream_cb = in->stream_cb;
    uint32_t isStreamStop = 0;
#else
    std::string streamId = in->stream_id, mainId, subId, streamPath;
    StreamCB stream_cb = in->stream_cb;
    uint32_t isStreamStop = 0;
    std::size_t pos;

    pos = streamId.find('-', 0);
    if (pos == std::string::npos)
    {
        myprintf("%s %s stream id format error\n", __FILE__, __FUNCTION__);
        return nullptr;
    }
    mainId = streamId.substr(0, pos);
    subId = streamId.substr(pos + 1, streamId.size());

#endif
    for (std::vector<unsigned long>::size_type i = 0; i < in->rec_schedule.size(); i++)
    {
        for (std::vector<unsigned long>::size_type j = 0; j < in->rec_schedule[i].size(); j++)
        {
            struct tm *start_tm = &in->rec_schedule[i][j][0];
            struct tm *end_tm = &in->rec_schedule[i][j][1];
            myprintf("%02d:%02d:%02d-%02d:%02d:%02d ",
                   start_tm->tm_hour, start_tm->tm_min, start_tm->tm_sec,
                   end_tm->tm_hour, end_tm->tm_min, end_tm->tm_sec);
        }
        myprintf("\n");
    }

    in->thread_state = thread_running;
    myprintf("stream thread create successful. streamId = %s, stream_cb = %p\n", streamId.c_str(), stream_cb);

    while (sys_flag)
    {
        streamPath = grpcClientStart(mainId, subId);
        if(streamPath.empty())
        {
            sleep(1);
            continue;
        }
        myprintf("(%s) stream path = %s\n", streamId.c_str(), streamPath.c_str());

        do
        {
            pFmtCtx = avformat_alloc_context();
            if (!pFmtCtx)
            {
                myprintf("avformat_alloc_context fail\n");
                fflush(stdout);
                break;
            }

            if (avformat_open_input(&pFmtCtx, streamPath.c_str(), nullptr, nullptr) < 0)
            {
                myprintf("avformat_open_input fail\n");
                fflush(stdout);
                break;
            }

            if (avformat_find_stream_info(pFmtCtx, nullptr) < 0)
            {
                myprintf("avformat_find_stream_info fail\n");
                fflush(stdout);
                break;
            }

            for (unsigned int i = 0; i < pFmtCtx->nb_streams; i++)
            {
                if (pFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                {
                    video_idx = i;
                }
            }
            if (video_idx < 0)
            {
                myprintf("did not find video data\n");
                fflush(stdout);
                break;
            }

            pCodec = avcodec_find_decoder(pFmtCtx->streams[video_idx]->codecpar->codec_id);
            if (!pCodec)
            {
                myprintf("did not find codec\n");
                fflush(stdout);
                break;
            }

            pCodecCtx = avcodec_alloc_context3(pCodec);
            if (!pCodecCtx)
            {
                myprintf("avcodec_alloc_context3 fail\n");
                fflush(stdout);
                break;
            }

            if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0)
            {
                fprintf(stderr, "Could not open codec\n");
                fflush(stderr);
                break;
            }

            if (pFmtCtx->streams[video_idx]->r_frame_rate.den <= 0 || pFmtCtx->streams[video_idx]->r_frame_rate.num <= 0)
            {
                myprintf("rtsp frame rate not correct\n");
                fflush(stdout);
                break;
            }

            av_pkt = av_packet_alloc();

            while (!av_read_frame(pFmtCtx, av_pkt))
            {
                if (av_pkt->stream_index == video_idx)
                {
                    if (stream_cb)
                    {
                        if (thread_stopping == in->thread_state)
                        {
                            isStreamStop = 1;
                        }
                        stream_cb(streamId.c_str(), av_pkt->data, av_pkt->size, isStreamStop);
                    }
                }
                av_packet_unref(av_pkt);

                if (thread_stopping == in->thread_state)
                    break;
            }
        } while (0);

        if (av_pkt)
            av_packet_free(&av_pkt);
        if (pCodecCtx)
        {
            avcodec_close(pCodecCtx);
            avcodec_free_context(&pCodecCtx);
        }
        if (pFmtCtx)
        {
            avformat_close_input(&pFmtCtx);
            avformat_free_context(pFmtCtx);
        }

        if (thread_stopping == in->thread_state)
            break;

        sleep(5);
    }

    in->thread_state = thread_stopped;

    return nullptr;
}

void StartStreamThread(struct stream_thread *stream_thread_in)
{
    if (!stream_thread_in)
        return;

    if (thread_stopped != stream_thread_in->thread_state)
        return;

    pthread_t pth;
    pthread_attr_t attr;

    stream_thread_in->thread_state = thread_creating;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int ret = pthread_create(&pth, &attr, StreamProc, (void *)stream_thread_in);
    if (ret < 0)
        return;

    while (thread_creating == stream_thread_in->thread_state)
        usleep(100000);
}

void StopStreamThread(struct stream_thread *stream_thread_in)
{
    if (!stream_thread_in)
        return;

    if (thread_running != stream_thread_in->thread_state)
        return;

    stream_thread_in->thread_state = thread_stopping;

    while (thread_stopping == stream_thread_in->thread_state)
        usleep(100000);
}

void *StreamScheduleProc(void *arg)
{
    time_t now_time;
    struct tm *tm_now, *tm_start, *tm_end;
    uint32_t sec_now, sec_start, sec_end;

    while (sys_flag)
    {
        if (isThreadListUsed)
        {
            sleep(1);
            continue;
        }

        for (auto iter = streamThreadList.begin(); iter != streamThreadList.end(); iter++)
        {
            if (thread_cancel == iter->second.thread_state)
                continue;

            isThreadListUsed = 1;

            time(&now_time);
            tm_now = localtime(&now_time);
            sec_now = tm_now->tm_hour * 3600 + tm_now->tm_min * 60 + tm_now->tm_sec;

            std::vector<std::vector<struct tm>> &day_schedule = iter->second.rec_schedule[tm_now->tm_wday];
            int j;

            for (j = 0; j < day_schedule.size(); j++)
            {
                tm_start = &day_schedule[j][0];
                tm_end = &day_schedule[j][1];

                sec_start = tm_start->tm_hour * 3600 + tm_start->tm_min * 60 + tm_start->tm_sec;
                sec_end = tm_end->tm_hour * 3600 + tm_end->tm_min * 60 + tm_end->tm_sec;

                if (sec_now >= sec_start && sec_now <= sec_end)
                {
                    StartStreamThread(&iter->second);
                    break;
                }
            }
            if (j == day_schedule.size())
                StopStreamThread(&iter->second);

            isThreadListUsed = 0;
            usleep(100000);
        }
        sleep(1);
    }
}

void *StreamEventProc(void *arg)
{
    if (!arg)
        return nullptr;

    struct stream_thread *pStreamThread = (struct stream_thread *)arg;
    struct stream_thread streamThread;

    std::size_t pos;
    std::string mainId, subId;

    struct tm startTime = pStreamThread->rec_schedule[0][0][0];
    struct tm endTime = pStreamThread->rec_schedule[0][0][1];
    time_t start_time_s = mktime(&startTime);
    time_t end_time_s = mktime(&endTime);
    time_t now_time_s;

    streamThread.stream_id = pStreamThread->stream_id;
    streamThread.stream_cb = pStreamThread->stream_cb;

    pStreamThread->thread_state = thread_running;
    streamThread.thread_state = thread_stopped;

    pos = streamThread.stream_id.find('-', 0);
    if (pos == std::string::npos)
    {
        myprintf("%s %s stream id format error\n", __FILE__, __FUNCTION__);
        return nullptr;
    }
    mainId = streamThread.stream_id.substr(0, pos);
    subId = streamThread.stream_id.substr(pos + 1, streamThread.stream_id.size());

    streamThread.stream_path = grpcClientStart(mainId, subId);

    while (sys_flag)
    {
        time(&now_time_s);
        if(now_time_s >= start_time_s && now_time_s <= end_time_s)
        {
            StartStreamThread(&streamThread);
        }
        else if(now_time_s > end_time_s)
        {
            StopStreamThread(&streamThread);
            break;
        }
        sleep(1);
    }

    return 0;
}

int32_t StreamInit(void)
{
    pthread_t pth;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&pth, &attr, StreamScheduleProc, nullptr) < 0)
        return -1;

    return 0;
}

int32_t AddStreamEvent(std::string &streamId, struct tm &startTime, struct tm &endTime, StreamCB stream_cb)
{
    pthread_t pth;
    pthread_attr_t attr;

    struct stream_thread streamThread;

    streamThread.stream_id = streamId;
    streamThread.stream_cb = stream_cb;
    streamThread.rec_schedule.resize(1);
    streamThread.rec_schedule[0].resize(1);
    streamThread.rec_schedule[0][0].push_back(startTime);
    streamThread.rec_schedule[0][0].push_back(endTime);
    streamThread.thread_state = thread_creating;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&pth, &attr, StreamEventProc, (void *)&streamThread) < 0)
        return -1;

    while (thread_creating == streamThread.thread_state)
        usleep(10000);

    return 0;
}

int32_t AddStream(const char *streamId, std::vector<std::vector<std::vector<struct tm>>> &schedule, StreamCB stream_cb)
{
    if (!streamId || !stream_cb)
    {
        SetError(func_input_null_addr);
        return -1;
    }

    std::string stream_id = streamId;
    int ret = 0;

    while (isThreadListUsed)
        usleep(100000);

    isThreadListUsed = 1;

    auto iter = streamThreadList.find(stream_id);
    if (iter != streamThreadList.end())
    {
        if (thread_cancel == iter->second.thread_state)
        {
            iter->second.stream_id = stream_id;
            iter->second.stream_cb = stream_cb;
            iter->second.thread_state = thread_stopped;
            iter->second.rec_schedule = schedule;
        }
        else
        {
            SetError(device_already_exist);
            ret = -1;
        }
    }
    else
    {
        struct stream_thread streamThread;

        streamThread.stream_id = stream_id;
        streamThread.stream_cb = stream_cb;
        streamThread.thread_state = thread_stopped;
        streamThread.rec_schedule = schedule;

        streamThreadList.insert(streamThreadList.end(), std::pair<std::string, struct stream_thread>(stream_id, streamThread));
    }

quit:
    isThreadListUsed = 0;
    return ret;
}

int32_t DeleteStream(const char *streamIp)
{
    if (!streamIp)
    {
        return -1;
    }

    std::string stream_ip = streamIp;
    int ret = 0;

    while (isThreadListUsed)
        usleep(100000);

    isThreadListUsed = 1;

    auto iter = streamThreadList.find(stream_ip);
    if (iter != streamThreadList.end())
    {
        if (thread_cancel != iter->second.thread_state)
        {
            if (thread_stopped != iter->second.thread_state)
            {
                iter->second.thread_state = thread_stopping;
                while (thread_stopping == iter->second.thread_state)
                    usleep(100000);
            }
            iter->second.thread_state = thread_cancel;
        }
        else
        {
            SetError(do_not_find_camera);
            ret = -1;
        }
    }
    else
    {
        SetError(do_not_find_camera);
        ret = -1;
    }

quit:
    isThreadListUsed = 0;
    return ret;
}

int32_t UpdateRecodeSchedule(std::string &streamIp, std::vector<std::vector<std::vector<struct tm>>> &schedule)
{
    int ret = 0;

    while (isThreadListUsed)
        usleep(100000);
    isThreadListUsed = 1;

    auto iter = streamThreadList.find(streamIp);
    if (iter == streamThreadList.end())
    {
        SetError(do_not_find_camera);
        ret = -1;
        goto quit;
    }

    iter->second.rec_schedule = schedule;

quit:
    isThreadListUsed = 0;
    return ret;
}
