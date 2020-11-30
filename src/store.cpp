#include <sys/time.h>
#include <pthread.h>
#include "common.h"
#include "store.h"

#define STORE_BUF_MAX_SIZE STORE_FILE_MAX_SIZE
#define WRITE_FILE_BLOCK_SIZE FILE_IO_BLOCK_SIZE
#define STORE_INFO_HEAD 0x7a5d9e3b

struct storeinfo
{
    uint32_t head;
    uint64_t frame_start_time;
    uint32_t frame_idx;
    uint32_t frame_offset;
    uint32_t frame_size;
    uint8_t reverse[64];
};

struct storebuf
{
    uint8_t *store_buf;
    uint32_t store_size;
    uint32_t store_offset;
    uint32_t store_info_offset;
    time_t store_start_time;
};

struct storedevice
{
    std::string store_path;
    struct storebuf pingStoreBuf;
    struct storebuf pongStoreBuf;
    struct storebuf *pCurStoreBuf;
    struct storebuf *pStoreBufWr;
    uint32_t frame_index = 0;
    std::string store_type;
};

static std::map<std::string, struct storedevice> storeDeviceList;
static std::map<std::string, struct storedevice> storeManualList;
static std::map<std::string, struct storedevice> storeAlarmList;
static pthread_mutex_t storeDevListMutex;
static store_cb storeCallback = nullptr;

struct storeinfo *MoveFramesToNextBuf(struct storebuf *srcStoreBuf, struct storebuf *dstStoreBuf)
{
    if (!srcStoreBuf || !dstStoreBuf)
        return nullptr;

    struct storeinfo *ret_addr = nullptr;
    struct storeinfo *srcStoreInfo = (struct storeinfo *)(srcStoreBuf->store_buf + srcStoreBuf->store_info_offset + sizeof(struct storeinfo));
    struct storeinfo *dstStoreInfo = (struct storeinfo *)(dstStoreBuf->store_buf + dstStoreBuf->store_info_offset);

    while (STORE_INFO_HEAD == srcStoreInfo->head)
    {
        if (ParseKeyFrame(srcStoreBuf->store_buf + srcStoreInfo->frame_offset, srcStoreInfo->frame_size))
            srcStoreInfo++;
        else
        {
            ret_addr = srcStoreInfo;
            break;
        }
    }

    dstStoreInfo->frame_offset = 0;
    while (STORE_INFO_HEAD == srcStoreInfo->head)
    {
        uint32_t dst_offset = dstStoreInfo->frame_offset;

        memcpy(dstStoreBuf->store_buf + dst_offset, srcStoreBuf->store_buf + srcStoreInfo->frame_offset, srcStoreInfo->frame_size);
        *dstStoreInfo = *srcStoreInfo;
        dstStoreInfo->frame_offset = dst_offset;
        dstStoreBuf->store_offset += srcStoreInfo->frame_size;

        dstStoreInfo--;
        dstStoreInfo->frame_offset = dst_offset + srcStoreInfo->frame_size;
        srcStoreInfo--;
    }
    dstStoreBuf->store_info_offset = (uint64_t)dstStoreInfo - (uint64_t)dstStoreBuf->store_buf;

    return ret_addr;
}

void *WriteToFileProc(void *arg)
{
    struct tm date_time;
    char file_name[128], file_path[128];

    struct timeval startTime, endTime;
    uint64_t start_ms, end_ms;
    uint32_t total_wr_size = 0;

    std::map<std::string, struct storedevice> *storeDeviceType[] = {&storeDeviceList, &storeManualList, &storeAlarmList};

    while (sys_flag)
    {
        for (int i = 0; i < 3; i++)
        {
            for (auto iter = storeDeviceType[i]->begin(); iter != storeDeviceType[i]->end(); iter++)
            {
                pthread_mutex_lock(&storeDevListMutex);

                struct storedevice *pStoreDevice = (struct storedevice *)&iter->second;
                if (!pStoreDevice->pStoreBufWr)
                {
                    pthread_mutex_unlock(&storeDevListMutex);
                    continue;
                }

                struct storeinfo *pStoreInfo = (struct storeinfo *)(pStoreDevice->pStoreBufWr->store_buf +
                                                                    pStoreDevice->pStoreBufWr->store_size - sizeof(struct storeinfo));
                if (pStoreInfo->head != STORE_INFO_HEAD)
                {
                    myprintf("store info head err\n");
                    pStoreDevice->pStoreBufWr = nullptr;
                    pthread_mutex_unlock(&storeDevListMutex);
                    continue;
                }

                localtime_r(&pStoreDevice->pStoreBufWr->store_start_time, &date_time);
                snprintf(file_name, 128, "%4d-%02d-%02d %02d:%02d:%02d.store", date_time.tm_year + 1900, date_time.tm_mon + 1, date_time.tm_mday,
                         date_time.tm_hour, date_time.tm_min, date_time.tm_sec);
                snprintf(file_path, 128, "%s/%s", pStoreDevice->store_path.c_str(), file_name);

                int fd = open(file_path, O_RDWR | O_CREAT, 0777);
                if (fd < 0)
                {
                    pStoreDevice->pStoreBufWr = nullptr;
                    pthread_mutex_unlock(&storeDevListMutex);
                    continue;
                }

                gettimeofday(&startTime, nullptr);
                total_wr_size = 0;
                while (total_wr_size < STORE_BUF_MAX_SIZE)
                {
                    int32_t wr_size = write(fd, pStoreDevice->pStoreBufWr->store_buf + total_wr_size, WRITE_FILE_BLOCK_SIZE);
                    if (wr_size < 0)
                    {
                        myprintf("%s write file fail : ", file_path);
                        perror("");
                        pthread_mutex_unlock(&storeDevListMutex);
                        break;
                    }
                    total_wr_size += wr_size;
                }
                gettimeofday(&endTime, nullptr);
                start_ms = startTime.tv_sec * 1000 + startTime.tv_usec / 1000;
                end_ms = endTime.tv_sec * 1000 + endTime.tv_usec / 1000;
                myprintf("write store file \"%s\" cast %ld ms\n", file_path, end_ms - start_ms);

                close(fd);

                if (storeCallback)
                    storeCallback(iter->first.c_str(), pStoreDevice->pStoreBufWr->store_start_time,
                                  pStoreDevice->store_type.c_str(), file_name);
                pStoreDevice->pStoreBufWr = nullptr;

                pthread_mutex_unlock(&storeDevListMutex);
                usleep(10000);
            }
        }
        sleep(1);
    }

    return nullptr;
}

int32_t StoreInit(store_cb storeCB)
{
    pthread_t pth;
    int32_t nRet = pthread_create(&pth, nullptr, WriteToFileProc, nullptr);
    if (nRet < 0)
    {
        perror("create pthread fail : ");
        SetError(create_thread_fail);
        return -1;
    }

    pthread_mutex_init(&storeDevListMutex, nullptr);

    storeCallback = storeCB;

    return 0;
}

int32_t AddStoreDevice(std::string &devId, std::string &type)
{
    std::string devPath = storePath;
    struct storedevice storeDevice, *pStoreDevice;
    int ret = 0;

    pthread_mutex_lock(&storeDevListMutex);

    if (storeDeviceList.count(devId) > 0)
    {
        myprintf("device(%s) already exist\n", devId.c_str());
        SetError(device_already_exist);
        ret = -1;
        goto quit;
    }

    storeDevice.store_path = devPath + devId;
    if (access(storeDevice.store_path.c_str(), F_OK) < 0)
    {
        myprintf("%s director not exist\n", storeDevice.store_path.c_str());

        if (mkdir(storeDevice.store_path.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) < 0)
        {
            myprintf("create directory %s fail : ", storeDevice.store_path.c_str());
            perror("");
            SetError(create_store_directory_fail);
            ret = -1;
            goto quit;
        }
    }
    else
    {
        myprintf("%s director exist\n", storeDevice.store_path.c_str());
    }

    storeDeviceList.insert(storeDeviceList.end(), std::pair<std::string, struct storedevice>(devId, storeDevice));
    pStoreDevice = &storeDeviceList[devId];

    pStoreDevice->pingStoreBuf.store_buf = (uint8_t *)malloc(STORE_BUF_MAX_SIZE);
    if (!pStoreDevice->pingStoreBuf.store_buf)
    {
        SetError(malloc_memory_fail);
        ret = -1;
        goto quit;
    }

    pStoreDevice->pongStoreBuf.store_buf = (uint8_t *)malloc(STORE_BUF_MAX_SIZE);
    if (!pStoreDevice->pongStoreBuf.store_buf)
    {
        SetError(malloc_memory_fail);
        ret = -1;
        goto quit;
    }

    pStoreDevice->pingStoreBuf.store_size = STORE_BUF_MAX_SIZE;
    pStoreDevice->pingStoreBuf.store_offset = 0;
    pStoreDevice->pingStoreBuf.store_info_offset = STORE_BUF_MAX_SIZE - sizeof(struct storeinfo);

    pStoreDevice->pongStoreBuf.store_size = STORE_BUF_MAX_SIZE;
    pStoreDevice->pongStoreBuf.store_offset = 0;
    pStoreDevice->pongStoreBuf.store_info_offset = STORE_BUF_MAX_SIZE - sizeof(struct storeinfo);

    pStoreDevice->pCurStoreBuf = &pStoreDevice->pingStoreBuf;
    pStoreDevice->pCurStoreBuf->store_start_time = 0;
    pStoreDevice->pStoreBufWr = nullptr;

    pStoreDevice->store_type = type;

quit:
    pthread_mutex_unlock(&storeDevListMutex);
    return ret;
}

int32_t AddStoreEvent(std::string &devId, std::string &type)
{
    std::string devPath = storePath;
    struct storedevice storeDevice, *pStoreDevice;
    int ret = 0;

    pthread_mutex_lock(&storeDevListMutex);

    storeDevice.store_path = devPath + devId;
    if (access(storeDevice.store_path.c_str(), F_OK) < 0)
    {
        myprintf("%s director not exist\n", storeDevice.store_path.c_str());

        if (mkdir(storeDevice.store_path.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) < 0)
        {
            myprintf("create directory %s fail : ", storeDevice.store_path.c_str());
            perror("");
            SetError(create_store_directory_fail);
            ret = -1;
            goto quit;
        }
    }
    else
    {
        myprintf("%s director exist\n", storeDevice.store_path.c_str());
    }

    if (type == std::string(RECODE_TYPE_MANUAL))
    {
        storeManualList.insert(storeManualList.end(), std::pair<std::string, struct storedevice>(devId, storeDevice));
        pStoreDevice = &storeManualList[devId];
    }
    else if (type == std::string(RECODE_TYPE_ALARM))
    {
        storeAlarmList.insert(storeAlarmList.end(), std::pair<std::string, struct storedevice>(devId, storeDevice));
        pStoreDevice = &storeAlarmList[devId];
    }
    else
    {
        goto quit;
    }

    pStoreDevice->pingStoreBuf.store_buf = (uint8_t *)malloc(STORE_BUF_MAX_SIZE);
    if (!pStoreDevice->pingStoreBuf.store_buf)
    {
        SetError(malloc_memory_fail);
        ret = -1;
        goto quit;
    }

    pStoreDevice->pongStoreBuf.store_buf = (uint8_t *)malloc(STORE_BUF_MAX_SIZE);
    if (!pStoreDevice->pongStoreBuf.store_buf)
    {
        SetError(malloc_memory_fail);
        ret = -1;
        goto quit;
    }

    pStoreDevice->pingStoreBuf.store_size = STORE_BUF_MAX_SIZE;
    pStoreDevice->pingStoreBuf.store_offset = 0;
    pStoreDevice->pingStoreBuf.store_info_offset = STORE_BUF_MAX_SIZE - sizeof(struct storeinfo);

    pStoreDevice->pongStoreBuf.store_size = STORE_BUF_MAX_SIZE;
    pStoreDevice->pongStoreBuf.store_offset = 0;
    pStoreDevice->pongStoreBuf.store_info_offset = STORE_BUF_MAX_SIZE - sizeof(struct storeinfo);

    pStoreDevice->pCurStoreBuf = &pStoreDevice->pingStoreBuf;
    pStoreDevice->pCurStoreBuf->store_start_time = 0;
    pStoreDevice->pStoreBufWr = nullptr;

    pStoreDevice->store_type = type;

quit:
    pthread_mutex_unlock(&storeDevListMutex);
    return ret;
}

int32_t RemoveStoreDevice(std::string &devId)
{
    std::string devPath = storePath;
    struct storedevice storeDevice;
    auto iter = storeDeviceList.find(devId);
    int ret = 0;

    if (iter == storeDeviceList.end())
    {
        myprintf("device(%s) do not found\n", devId.c_str());
        SetError(do_not_find_camera);
        ret = -1;
        goto quit;
    }

    // storeDevice.store_path = devPath + devIp;
    // if (!rmdir(storeDevice.store_path.c_str()))
    // {
    //     myprintf("remove %s directory successful\n", storeDevice.store_path.c_str());
    // }
    // else
    // {
    //     myprintf("remove %s directory fail : \n", storeDevice.store_path.c_str());
    //     perror("");
    // }

    while (nullptr != iter->second.pStoreBufWr)
        usleep(100000);

    iter->second.pStoreBufWr = iter->second.pCurStoreBuf;
    while (nullptr != iter->second.pStoreBufWr)
        usleep(100000);

    pthread_mutex_lock(&storeDevListMutex);

    free(iter->second.pingStoreBuf.store_buf);
    free(iter->second.pongStoreBuf.store_buf);
    storeDeviceList.erase(iter);

    pthread_mutex_unlock(&storeDevListMutex);
quit:
    return ret;
}

int32_t RemoveStoreEvent(std::string &devId, std::string &type)
{
    std::map<std::string, struct storedevice> *pStoreEventList;
    struct storedevice *pStoreEvent;
    int ret = 0;

    if (type == std::string(RECODE_TYPE_MANUAL))
    {
        pStoreEventList = &storeManualList;
    }
    else
    {
        pStoreEventList = &storeAlarmList;
    }
    auto iter = pStoreEventList->find(devId);
    if (iter == pStoreEventList->end())
        return -1;
    pStoreEvent = &iter->second;

    free(pStoreEvent->pingStoreBuf.store_buf);
    free(pStoreEvent->pongStoreBuf.store_buf);
    pStoreEventList->erase(iter);

    return 0;
}

// int32_t UpdateBakSchedule(std::string &devId, std::vector<std::vector<std::vector<struct tm>>> &schedule)
// {
//     auto iter = storeDeviceList.find(devId);
//     int ret = 0;

//     if (iter == storeDeviceList.end())
//     {
//         myprintf("device(%s) do not found\n", devId.c_str());
//         SetError(do_not_find_camera);
//         ret = -1;
//         goto quit;
//     }

//     pthread_mutex_lock(&storeDevListMutex);

//     iter->second.bakSchedule = schedule;

//     pthread_mutex_unlock(&storeDevListMutex);
// quit:
//     return ret;
// }

int32_t WriteFrameToBuf(std::string &devId, std::string &type, uint8_t *frameData, uint32_t frameSize, uint32_t flag)
{
    int32_t nRet = 0;
    uint32_t remain;
    uint64_t frame_start_ms;
    uint8_t *wr_start_addr;

    struct storedevice *pStoreDevice;
    struct storeinfo storeInfo;
    struct timeval startTimeVal;

    if (type == std::string(RECODE_TYPE_PLANNED))
        pStoreDevice = &storeDeviceList[devId];
    else if (type == std::string(RECODE_TYPE_MANUAL))
        pStoreDevice = &storeManualList[devId];
    else if (type == std::string(RECODE_TYPE_ALARM))
        pStoreDevice = &storeAlarmList[devId];
    else
        return -1;

    remain = pStoreDevice->pCurStoreBuf->store_info_offset - pStoreDevice->pCurStoreBuf->store_offset;

    if (flag)
    {
        while (nullptr != pStoreDevice->pStoreBufWr)
            usleep(10000);

        if (remain < (frameSize + sizeof(struct storeinfo)))
        {
            goto store_stop;
        }
        else
        {
            goto store;
        }
    }

    if (remain < (frameSize + sizeof(struct storeinfo)))
    {
        if (ParseKeyFrame(frameData, frameSize))
        {
            struct storebuf *pCpyBuf;
            if (pStoreDevice->pCurStoreBuf == &pStoreDevice->pingStoreBuf)
            {
                pCpyBuf = &pStoreDevice->pongStoreBuf;
            }
            else
            {
                pCpyBuf = &pStoreDevice->pingStoreBuf;
            }
            pCpyBuf->store_offset = 0;
            pCpyBuf->store_info_offset = STORE_BUF_MAX_SIZE - sizeof(struct storeinfo);
            pCpyBuf->store_start_time = time(nullptr);
            struct storeinfo *ret_addr = MoveFramesToNextBuf(pStoreDevice->pCurStoreBuf, pCpyBuf);
            uint8_t *pIFrame = pStoreDevice->pCurStoreBuf->store_buf + ret_addr->frame_offset;
            memset(pIFrame, 0, (uint64_t)(ret_addr + 1) - (uint64_t)pIFrame);
            pStoreDevice->pStoreBufWr = pStoreDevice->pCurStoreBuf;
            pStoreDevice->pCurStoreBuf = pCpyBuf;
        }
        else
        {
            memset(pStoreDevice->pCurStoreBuf->store_buf + pStoreDevice->pCurStoreBuf->store_offset, 0, remain + sizeof(struct storeinfo));
            if (pStoreDevice->pCurStoreBuf == &pStoreDevice->pingStoreBuf)
            {
                pStoreDevice->pCurStoreBuf = &pStoreDevice->pongStoreBuf;
                pStoreDevice->pStoreBufWr = &pStoreDevice->pingStoreBuf;
            }
            else
            {
                pStoreDevice->pCurStoreBuf = &pStoreDevice->pingStoreBuf;
                pStoreDevice->pStoreBufWr = &pStoreDevice->pongStoreBuf;
            }
            pStoreDevice->pCurStoreBuf->store_offset = 0;
            pStoreDevice->pCurStoreBuf->store_info_offset = STORE_BUF_MAX_SIZE - sizeof(struct storeinfo);
            pStoreDevice->pCurStoreBuf->store_start_time = time(nullptr);
        }
    }

    if (!pStoreDevice->pCurStoreBuf->store_start_time)
        pStoreDevice->pCurStoreBuf->store_start_time = time(nullptr);

store:
    storeInfo.head = STORE_INFO_HEAD;
    gettimeofday(&startTimeVal, nullptr);
    frame_start_ms = startTimeVal.tv_sec * 1000 + startTimeVal.tv_usec / 1000;
    storeInfo.frame_start_time = frame_start_ms;
    storeInfo.frame_idx = pStoreDevice->frame_index++;
    storeInfo.frame_offset = pStoreDevice->pCurStoreBuf->store_offset;
    storeInfo.frame_size = frameSize;
    memset((void *)storeInfo.reverse, 0, sizeof(storeInfo.reverse));

    wr_start_addr = pStoreDevice->pCurStoreBuf->store_buf + pStoreDevice->pCurStoreBuf->store_offset;
    memcpy(wr_start_addr, frameData, frameSize);
    pStoreDevice->pCurStoreBuf->store_offset += frameSize;

    wr_start_addr = pStoreDevice->pCurStoreBuf->store_buf + pStoreDevice->pCurStoreBuf->store_info_offset;
    memcpy(wr_start_addr, (void *)&storeInfo, sizeof(struct storeinfo));
    pStoreDevice->pCurStoreBuf->store_info_offset -= sizeof(struct storeinfo);

store_stop:
    if (flag)
    {
        memset(pStoreDevice->pCurStoreBuf->store_buf + pStoreDevice->pCurStoreBuf->store_offset, 0, remain + sizeof(struct storeinfo));
        pStoreDevice->pStoreBufWr = pStoreDevice->pCurStoreBuf;
        while (nullptr != pStoreDevice->pStoreBufWr)
            usleep(10000);

        if (type == std::string(RECODE_TYPE_PLANNED))
        {
            pStoreDevice->pCurStoreBuf->store_offset = 0;
            pStoreDevice->pCurStoreBuf->store_info_offset = STORE_BUF_MAX_SIZE - sizeof(struct storeinfo);
            pStoreDevice->pCurStoreBuf->store_start_time = 0;
        }
        else
        {
            RemoveStoreEvent(devId, type);
        }
    }

    if (pStoreDevice->pCurStoreBuf->store_info_offset < pStoreDevice->pCurStoreBuf->store_offset)
    {
        myprintf("store info offset is bigger than store offset\n");
    }
    return nRet;
}

int32_t ReadStoreFileFrame(std::string &file_path, uint8_t *frameBuf, uint32_t buf_size)
{
    if (!frameBuf)
        return -1;

    int fd = open(file_path.c_str(), O_RDONLY);
    if (fd < 0)
        return -1;

    uint32_t total_rd_size = 0;
    uint32_t read_block_size = 0;
    while ((read_block_size = read(fd, frameBuf + total_rd_size, WRITE_FILE_BLOCK_SIZE)) > 0)
    {
        total_rd_size += read_block_size;
        if (total_rd_size > buf_size)
        {
            myprintf("%s : frame buf is not enough to read store file\n", __FUNCTION__);
            close(fd);
            return -1;
        }
    }
    close(fd);

    // struct storeinfo *pStoreInfo = (struct storeinfo *)(frameBuf + total_rd_size - sizeof(struct storeinfo));
    // while (pStoreInfo->head == STORE_INFO_HEAD)
    // {
    //     myprintf("frame idx = %u, start time = %lu\n", pStoreInfo->frame_idx, pStoreInfo->frame_start_time);
    //     pStoreInfo--;
    // }
}

int32_t GetOneFrameByIndex(uint32_t idx, uint8_t *frames, uint32_t frames_size, uint8_t *out, uint32_t out_size, uint64_t *sys_time)
{
    if (!frames || !out || !sys_time)
        return -1;

    struct storeinfo *pStoreInfo = (struct storeinfo *)(frames + frames_size - sizeof(struct storeinfo));
    pStoreInfo -= idx;

    if (pStoreInfo->head != STORE_INFO_HEAD)
    {
        myprintf("%s : frame info head from store file err\n", __FUNCTION__);
        return -1;
    }

    if (pStoreInfo->frame_size > out_size)
    {
        myprintf("%s : out buffer size not enough\n", __FUNCTION__);
        return -1;
    }

    uint8_t *frame_start = frames + pStoreInfo->frame_offset;
    memcpy(out, frame_start, pStoreInfo->frame_size);
    *sys_time = pStoreInfo->frame_start_time;

    return pStoreInfo->frame_size;
}
