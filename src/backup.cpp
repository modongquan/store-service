#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#include <pthread.h>
#include "backup.h"
#include "mysql.h"

#define REQ_START_TRANS_BAK_FILE "start transfer bak file"
#define REQ_DOING_TRANS_BAK_FILE "doing transfer bak file"
#define REQ_FINISH_TRANS_BAK_FILE "finish transfer bak file"

#define HAED_MAX_SIZE 128

struct BakStream
{
    std::string bak_path;
};

struct StartTransBakDef
{
    const char head[sizeof(REQ_START_TRANS_BAK_FILE)] = {REQ_START_TRANS_BAK_FILE};
    char bak_id[sizeof("34020000001320000301-34020000001320000301")];
    char bak_name[sizeof("2020-09-09 11:51:37.store")];
};

struct StartTransBakResp
{
    char head[sizeof(REQ_START_TRANS_BAK_FILE)] = {REQ_START_TRANS_BAK_FILE};
    uint8_t result;
    uint32_t bak_stream_id;
};

struct DoingTransBakDef
{
    const char head[sizeof(REQ_DOING_TRANS_BAK_FILE)] = {REQ_DOING_TRANS_BAK_FILE};
    uint32_t bak_stream_id;
    uint32_t stream_chunk_size;
    uint8_t *stream_chunk;
};

struct DoingTransBakResp
{
    char head[sizeof(REQ_DOING_TRANS_BAK_FILE)] = {REQ_DOING_TRANS_BAK_FILE};
    uint8_t result;
};

struct FinishTransBakDef
{
    const char head[sizeof(REQ_FINISH_TRANS_BAK_FILE)] = {REQ_FINISH_TRANS_BAK_FILE};
    uint32_t bak_stream_id;
};

struct FinishTransBakResp
{
    char head[sizeof(REQ_FINISH_TRANS_BAK_FILE)] = {REQ_FINISH_TRANS_BAK_FILE};
    uint8_t result;
};

struct BakSchedule
{
    std::vector<std::vector<std::vector<struct tm>>> bakSchedule;
    volatile int is_in_use;
};

struct thread_info
{
    const char *pBakId;
    const char *pBakName;
    int is_thread_created;
};

typedef int (*ReqRespond)(uint8_t *, uint32_t, uint8_t *, uint32_t);

static std::map<std::string, void *> bakReqHandle;
static std::map<uint32_t, struct BakStream> bakStreamQueue;
static uint32_t bakStreamIdx = 0;

static std::map<std::string, struct BakSchedule> bakScheduleList;

int TransBakFile(uint8_t *in, uint32_t in_size, uint8_t *out, uint32_t out_size)
{
    if (!in || !out)
        return -1;

    int ret = 0;
    int fd = 0;

    std::string store_path = backupPath;
    struct StartTransBakDef *pTransBak = (struct StartTransBakDef *)in;
    struct StartTransBakResp *pTransBakResp = (struct StartTransBakResp *)out;
    struct StartTransBakResp resp;
    struct BakStream bakStream;

    store_path += pTransBak->bak_id;

    if (access(store_path.c_str(), F_OK) < 0)
    {
        myprintf("%s director not exist\n", store_path.c_str());

        if (mkdir(store_path.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) < 0)
        {
            myprintf("create directory %s fail : ", store_path.c_str());
            perror("");
            ret = -1;
            goto quit;
        }
    }
    else
    {
        myprintf("%s director exist\n", store_path.c_str());
    }

    store_path += "/";
    store_path += pTransBak->bak_name;

    if (access(store_path.c_str(), F_OK) < 0)
    {
        fd = open(store_path.c_str(), O_CREAT, 0777);
        if (fd < 0)
        {
            myprintf("create backup file(%s) fail\n", store_path.c_str());
            perror("");
            ret = -1;
            goto quit;
        }
        close(fd);
        myprintf("create backup file(%s) successful\n", store_path.c_str());
    }
    else
    {
        myprintf("backup file(%s) already exist\n", store_path.c_str());
    }

    bakStream.bak_path = store_path;
    bakStreamQueue.insert(bakStreamQueue.end(), std::pair<uint32_t, struct BakStream>(bakStreamIdx, bakStream));

    pTransBakResp->bak_stream_id = bakStreamIdx++;

quit:
    strcpy(pTransBakResp->head, resp.head);
    pTransBakResp->result = ret;

    return sizeof(struct StartTransBakResp);
}

int DoingRecvBakFile(uint8_t *in, uint32_t in_size, uint8_t *out, uint32_t out_size)
{
    if (!in || !out)
        return -1;

    struct DoingTransBakDef *pDoingTransBak = (struct DoingTransBakDef *)in;
    struct DoingTransBakResp *pDoingTransResp = (struct DoingTransBakResp *)out;
    struct DoingTransBakResp resp;
    uint8_t *pChunck = in + sizeof(struct DoingTransBakDef) - sizeof(pDoingTransBak->stream_chunk);
    uint32_t bakStreamId = pDoingTransBak->bak_stream_id;
    int ret = 0;
    int fd = 0;

    std::string bak_path = bakStreamQueue[bakStreamId].bak_path;

    fd = open(bak_path.c_str(), O_WRONLY);
    if (fd < 0)
    {
        myprintf("open backup file(%s) fail\n", bak_path.c_str());
        perror("");
        ret = -1;
        goto quit;
    }

    ret = lseek(fd, 0, SEEK_END);
    ret = write(fd, pChunck, pDoingTransBak->stream_chunk_size);
    if (ret < 0)
    {
        myprintf("[%s][%s] write %s fail : ", __FILE__, __FUNCTION__, bak_path.c_str());
        perror("");
        goto quit;
    }
    close(fd);

quit:
    strcpy(pDoingTransResp->head, resp.head);
    pDoingTransResp->result = ret;
    return sizeof(struct DoingTransBakResp);
}

int FinishBakFile(uint8_t *in, uint32_t in_size, uint8_t *out, uint32_t out_size)
{
    if (!in || !out)
        return -1;

    struct FinishTransBakDef *pFinishBak = (struct FinishTransBakDef *)in;
    struct FinishTransBakResp *pFinishBakResp = (struct FinishTransBakResp *)out;
    struct FinishTransBakResp resp;
    uint32_t bakStreamId = pFinishBak->bak_stream_id;
    int ret = 0;
    auto iter = bakStreamQueue.find(bakStreamId);
    if (iter == bakStreamQueue.end())
    {
        ret = -1;
        goto quit;
    }

    bakStreamQueue.erase(iter);

quit:
    strcpy(pFinishBakResp->head, resp.head);
    pFinishBakResp->result = ret;
    return sizeof(uint8_t);
}

void BackupServerInit(void)
{
    bakReqHandle.insert(bakReqHandle.end(), std::pair<std::string, void *>(std::string(REQ_START_TRANS_BAK_FILE), (void *)TransBakFile));
    bakReqHandle.insert(bakReqHandle.end(), std::pair<std::string, void *>(std::string(REQ_DOING_TRANS_BAK_FILE), (void *)DoingRecvBakFile));
    bakReqHandle.insert(bakReqHandle.end(), std::pair<std::string, void *>(std::string(REQ_FINISH_TRANS_BAK_FILE), (void *)FinishBakFile));
}

void BackupRespond(int sockfd, uint8_t *data, uint32_t size)
{
    if (!data)
        return;

    char *pHead = (char *)data;
    uint8_t res[128];
    int res_size;
    struct timeval startTime, endTime;
    uint64_t startMs, endMs;

    std::string reqHead(pHead);

    if (strlen(pHead) > HAED_MAX_SIZE)
        return;

    if (!bakReqHandle.count(reqHead))
    {
        myprintf("can't not find request /'%s/'\n", pHead);
        return;
    }

    gettimeofday(&startTime, nullptr);
    ReqRespond req_respond = (ReqRespond)bakReqHandle[reqHead];
    if (!req_respond)
        return;
    res_size = req_respond(data, size, res, 128);
    write(sockfd, res, res_size);
    gettimeofday(&endTime, nullptr);
    startMs = startTime.tv_sec * 1000 + startTime.tv_usec / 1000;
    endMs = endTime.tv_sec * 1000 + endTime.tv_usec / 1000;
    myprintf("bak resp cast %ld ms\n", endMs - startMs);
}

int ConnectBackupServer(void)
{
    int sockfd = 0;
    struct sockaddr_in addr;

    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0)
    {
        perror("socket() fail");
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(backupIp.c_str());
    addr.sin_port = htons(backup_port);

    if (connect(sockfd, (const sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connet fail\n");
        return -1;
    }

    return sockfd;
}

int StartTransferBakFile(std::string &backup_id, std::string &backup_name, uint32_t &bakStreamId)
{
    int sockfd = 0, ret;
    uint8_t result;
    struct StartTransBakDef transBak;
    struct StartTransBakResp transBakResp;

    fd_set rd_fds;
    struct timeval timeout;

    if ((sockfd = ConnectBackupServer()) <= 0)
    {
        result = -1;
        goto quit;
    }

    strcpy(transBak.bak_id, backup_id.c_str());
    strcpy(transBak.bak_name, backup_name.c_str());
    write(sockfd, (void *)&transBak, sizeof(transBak));

    FD_ZERO(&rd_fds);
    FD_SET(sockfd, &rd_fds);
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    ret = select(sockfd + 1, &rd_fds, nullptr, nullptr, &timeout);
    if (ret > 0)
    {
        read(sockfd, (void *)&transBakResp, sizeof(transBakResp));
        if (strcmp(transBakResp.head, REQ_START_TRANS_BAK_FILE))
        {
            myprintf("[%s][%s] resp error\n", __FILE__, __FUNCTION__);
            result = -1;
            goto quit;
        }
        result = transBakResp.result;
        bakStreamId = transBakResp.bak_stream_id;
    }
    else if (!ret)
    {
        myprintf("backup client connect timeout\n");
        result = -1;
    }
    else
    {
        myprintf("backup client connect error\n");
        result = -1;
    }

quit:
    if (sockfd)
        close(sockfd);
    return result;
}

int DoingTransBakFile(std::string &backup_id, std::string &backup_name, uint32_t &bakStreamId)
{
    struct DoingTransBakDef doingTransBak;
    struct DoingTransBakResp resp;

    std::string backup_path = storePath + backup_id + "/" + backup_name;

    size_t head_size, rd_size, wr_size;
    int sockfd = 0;
    int mss;
    int streamfd;
    int ret = 0;
    socklen_t len = sizeof(mss);

    fd_set rd_fds;
    struct timeval timeout, startTime, endTime;
    uint64_t startMs, endMs;

    doingTransBak.stream_chunk = nullptr;

    if ((streamfd = open(backup_path.c_str(), O_RDONLY)) < 0)
    {
        myprintf("open file '%s' fail\n", backup_path.c_str());
        perror("");
        ret = -1;
        goto quit;
    }

    if ((sockfd = ConnectBackupServer()) < 0)
    {
        ret = -1;
        goto quit;
    }

    if (getsockopt(sockfd, IPPROTO_TCP, TCP_MAXSEG, &mss, &len) < 0)
    {
        myprintf("[%s][%s] get tcp mss fail\n", __FILE__, __FUNCTION__);
        perror("");
        ret = -1;
        goto quit;
    }
    myprintf("tcp mss = %d\n", mss);
    mss = mss * 100;
    if ((doingTransBak.stream_chunk = (uint8_t *)malloc(mss)) == nullptr)
    {
        ret = -1;
        goto quit;
    }

    doingTransBak.bak_stream_id = bakStreamId;
    head_size = sizeof(doingTransBak) - sizeof(doingTransBak.stream_chunk);

    while ((rd_size = read(streamfd, doingTransBak.stream_chunk, mss - head_size)) > 0)
    {
        gettimeofday(&startTime, nullptr);
        doingTransBak.stream_chunk_size = rd_size;

        if (!sockfd && (sockfd = ConnectBackupServer()) <= 0)
        {
            ret = -1;
            goto quit;
        }

        if (write(sockfd, (void *)&doingTransBak, head_size) < 0 || write(sockfd, doingTransBak.stream_chunk, doingTransBak.stream_chunk_size) < 0)
        {
            myprintf("[%s][%s] socket write error\n", __FILE__, __FUNCTION__);
            perror("");
            ret = -1;
            goto quit;
        }

        FD_ZERO(&rd_fds);
        FD_SET(sockfd, &rd_fds);
        timeout.tv_sec = 3;
        timeout.tv_usec = 0;

        int ret = select(sockfd + 1, &rd_fds, nullptr, nullptr, &timeout);
        if (ret > 0)
        {
            read(sockfd, (void *)&resp, sizeof(resp));
            if (strcmp(resp.head, REQ_DOING_TRANS_BAK_FILE))
            {
                myprintf("[%s][%s] resp error\n", __FILE__, __FUNCTION__);
                ret = -1;
                goto quit;
            }
            if (resp.result < 0)
            {
                ret = -1;
                goto quit;
            }
        }
        else if (!ret)
        {
            myprintf("backup client connect timeout\n");
            ret = -1;
            goto quit;
        }
        else
        {
            myprintf("backup client connect error\n");
            ret = -1;
            goto quit;
        }
        close(sockfd);
        sockfd = 0;

        gettimeofday(&endTime, nullptr);
        startMs = startTime.tv_sec * 1000 + startTime.tv_usec / 1000;
        endMs = endTime.tv_sec * 1000 + endTime.tv_usec / 1000;
        myprintf("trans mss cast %ld ms\n", endMs - startMs);
    }

quit:
    close(streamfd);
    if (doingTransBak.stream_chunk)
        free(doingTransBak.stream_chunk);

    return ret;
}

int FinishTransBakFile(uint32_t &bakStreamId)
{
    int sockfd = 0;
    int result = -1;
    struct FinishTransBakDef finishTransBak;
    struct FinishTransBakResp resp;

    fd_set rd_fds;
    struct timeval timeout;

    if ((sockfd = ConnectBackupServer()) <= 0)
    {
        return -1;
    }

    finishTransBak.bak_stream_id = bakStreamId;
    write(sockfd, (void *)&finishTransBak, sizeof(finishTransBak));

    FD_ZERO(&rd_fds);
    FD_SET(sockfd, &rd_fds);
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    int ret = select(sockfd + 1, &rd_fds, nullptr, nullptr, &timeout);
    if (ret > 0)
    {
        read(sockfd, (void *)&resp, sizeof(resp));
        if (strcmp(resp.head, REQ_FINISH_TRANS_BAK_FILE))
        {
            myprintf("[%s][%s] resp error\n", __FILE__, __FUNCTION__);
            goto quit;
        }
        if (resp.result < 0)
            goto quit;
    }
    else if (!ret)
    {
        myprintf("backup client connect timeout\n");
        goto quit;
    }
    else
    {
        myprintf("backup client connect error\n");
        goto quit;
    }
    result = 0;

quit:
    if (sockfd)
        close(sockfd);
    return result;
}

int BackupClientStart(std::string &backup_id, std::string &backup_name)
{
    uint32_t bakStreamId;
    std::string backup("yes");

    if (StartTransferBakFile(backup_id, backup_name, bakStreamId) < 0)
        return -1;

    if (DoingTransBakFile(backup_id, backup_name, bakStreamId) < 0)
        return -1;

    if (FinishTransBakFile(bakStreamId) < 0)
        return -1;

    myprintf("device \"%s\" backup file \"%s\" successfully\n", backup_id.c_str(), backup_name.c_str());

    UpdateBackupToTable(backup_id, backup_name, backup);

    return 0;
}

void *BackupScheProc(void *arg)
{
    struct thread_info *pThreadInfo = (struct thread_info *)arg;
    std::string backup_id = pThreadInfo->pBakId;
    std::string backup_name = pThreadInfo->pBakName;

    pThreadInfo->is_thread_created = 1;

    auto iter = bakScheduleList.find(backup_id);
    if (iter == bakScheduleList.end())
    {
        myprintf("[%s][%s] do not find device id \"%s\" \n", __FILE__, __FUNCTION__, backup_id.c_str());
        return nullptr;
    }

    time_t now_time;
    struct tm *tm_now, *tm_start, *tm_end;
    uint32_t sec_now, sec_start, sec_end;

    time(&now_time);
    tm_now = localtime(&now_time);
    sec_now = tm_now->tm_hour * 3600 + tm_now->tm_min * 60 + tm_now->tm_sec;

    iter->second.is_in_use = 1;
    std::vector<std::vector<struct tm>> &daySche = iter->second.bakSchedule[tm_now->tm_wday];

    for (int i = 0; i < daySche.size(); i++)
    {
        tm_start = &daySche[i][0];
        tm_end = &daySche[i][1];

        sec_start = tm_start->tm_hour * 3600 + tm_start->tm_min * 60 + tm_start->tm_sec;
        sec_end = tm_end->tm_hour * 3600 + tm_end->tm_min * 60 + tm_end->tm_sec;

        if (sec_now >= sec_start && sec_now <= sec_end)
        {
            BackupClientStart(backup_id, backup_name);
            break;
        }
    }
    iter->second.is_in_use = 0;

    return 0;
}

int BackupScheHandl(std::string &backup_id, std::string &backup_name)
{
    struct thread_info threadInfo;
    pthread_t pth;
    pthread_attr_t attr;

    threadInfo.pBakId = backup_id.c_str();
    threadInfo.pBakName = backup_name.c_str();
    threadInfo.is_thread_created = 0;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&pth, &attr, BackupScheProc, (void *)&threadInfo) < 0)
        return -1;

    while(!threadInfo.is_thread_created)
        usleep(10000);
        
    return 0;
}

int AddBakSchedule(std::string &backup_id, std::vector<std::vector<std::vector<struct tm>>> &schedule)
{
    struct BakSchedule bakSche = {schedule, 0};
    int ret = 0;

    if (bakScheduleList.find(backup_id) == bakScheduleList.end())
    {
        bakScheduleList.insert(bakScheduleList.end(), std::pair<std::string, struct BakSchedule>(backup_id, bakSche));
    }
    else
    {
        myprintf("device \"%s\" already exist\n", backup_id.c_str());
        SetError(device_already_exist);
        ret = -1;
    }

    return ret;
}

int RemoveBakSchedule(std::string &backup_id)
{
    auto iter = bakScheduleList.find(backup_id);
    if (iter != bakScheduleList.end())
    {
        while (iter->second.is_in_use)
            usleep(10000);
        bakScheduleList.erase(iter);
    }

    return 0;
}

int UpdateBakSchedule(std::string &backup_id, std::vector<std::vector<std::vector<struct tm>>> &schedule)
{
    struct BakSchedule bakSche = {schedule, 0};
    int ret = 0;

    auto iter = bakScheduleList.find(backup_id);
    if (iter != bakScheduleList.end())
    {
        iter->second.is_in_use = 1;
        iter->second.bakSchedule = schedule;
        iter->second.is_in_use = 0;
    }
    else
    {
        myprintf("do not fild camera \'%s\'\n", backup_id.c_str());
        SetError(do_not_find_camera);
        ret = -1;
    }

    return ret;    
}