#include <sys/time.h>
#include <signal.h>
#include "common.h"
#include "rtmpsrv.h"
#include "store.h"
#include "http.h"
#include "mysql.h"
#include "stream.h"
#include "cJSON.h"
#include "backup.h"
#include "monitor.h"

char pid_path[128] = {"/var/run/store-service.pid"};

std::vector<std::map<std::string, std::string>> cameraInfoList;

extern int32_t StartServer(std::string &ip, uint32_t port, uint32_t rd_buf_size, client_cb clientCB);
extern std::string grpcClientStart(std::string &mainId, std::string &subId);

void StorePlannedStream(const char *streamIp, uint8_t *data, uint32_t size, uint32_t flag)
{
    if (!streamIp || !data)
        return;

    std::string devIp(streamIp);
    std::string type(RECODE_TYPE_PLANNED);

    WriteFrameToBuf(devIp, type, data, size, flag);
}

void StoreManualStream(const char *streamIp, uint8_t *data, uint32_t size, uint32_t flag)
{
    if (!streamIp || !data)
        return;

    std::string devIp(streamIp);
    std::string type(RECODE_TYPE_MANUAL);

    WriteFrameToBuf(devIp, type, data, size, flag);
}

void StoreAlarmStream(const char *streamIp, uint8_t *data, uint32_t size, uint32_t flag)
{
    if (!streamIp || !data)
        return;

    std::string devIp(streamIp);
    std::string type(RECODE_TYPE_ALARM);

    WriteFrameToBuf(devIp, type, data, size, flag);
}

void StoreFileInfo(const char *cameraIp, time_t startTime, const char *type, const char *file_name)
{
    if (!cameraIp || !file_name)
        return;

    std::string deviceIp(cameraIp);
    std::string fileName(file_name);
    std::string recode_type(type);
    std::string status(RECODE_FILE_STATUS_UNLOCK);

    if (InsertInfoToTable(deviceIp, startTime, recode_type, fileName, status) < 0)
    {
        myprintf("camera %s insert file %s fail\n", cameraIp, file_name);
    }

    BackupScheHandl(deviceIp, fileName);
}

void DeleteCameraInfoInList(std::string &camera_id)
{
    for (auto iter = cameraInfoList.begin(); iter != cameraInfoList.end(); iter++)
    {
        std::map<std::string, std::string> camera_info = *iter;
        std::string streamId = camera_info[std::string("id")];
        if (streamId == camera_id)
        {
            cameraInfoList.erase(iter);
            break;
        }
    }
}

int UpdataPidFile(void)
{
    char buf[128];

    unlink(pid_path);

    int fd = open(pid_path, O_CREAT | O_WRONLY, 0777);
    if (fd < 0)
    {
        snprintf(buf, 128, "open %s fail\n", pid_path);
        perror("");
        goto quit;
    }

    snprintf(buf, 128, "%d", getpid());
    write(fd, buf, strlen(buf));
    return 0;

quit:
    write(STDERR_FILENO, buf, strlen(buf));
    return -1;
}

void RemovePidFile(void)
{
    unlink(pid_path);
}

void SignalHdl(int signal)
{
    char buf[128];
    snprintf(buf, 128, "signal = %d\n", signal);
    write(STDERR_FILENO, buf, strlen(buf));
    RemovePidFile();
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    // std::string devIp("10.0.60.67");
    // std::string devIp2("10.0.160.33");
    // const char rtsp_path[] = {"rtsp://10.0.60.67:554/cam/realmonitor?channel=1&subtype=0&unicast=true&proto=Onvif"};
    // const char rtsp_path2[] = {"rtsp://admin:teamway123@10.0.160.33:554/LiveMedia/ch1/Media1"};
    int ret = 0;

    signal(SIGINT, SignalHdl);
    signal(SIGSEGV, SignalHdl);
    signal(SIGTERM, SignalHdl);
    signal(SIGQUIT, SignalHdl);

    setbuf(stdout, nullptr);

    if (UpdataPidFile() < 0)
        goto quit;

#ifdef REDEF_PRINTF
    if (PrintfRefef("./store_service.log") < 0)
        goto quit;
#endif

    if (LoadInfoFile() < 0)
        goto quit;

    if (DatabaseInit(serverIp, databaseUser, databasePasswd, databaseName) < 0)
        goto quit;

    if (GetCameraInfoFromDB(cameraInfoList) < 0)
        goto quit;

    if (StoreInit(StoreFileInfo) < 0)
        goto quit;

    if (StreamInit() < 0)
        goto quit;

    for (int i = 0; i < cameraInfoList.size(); i++)
    {
        auto iter_id = cameraInfoList[i].find(std::string(CAMERA_ID_MYSQL_NAME));
        auto iter_rec = cameraInfoList[i].find(std::string(REC_SCHE_JSON_NAME));
        auto iter_bak = cameraInfoList[i].find(std::string(BAK_SCHE_JSON_NAME));
        std::vector<std::vector<std::vector<struct tm>>> rec_schedule;
        std::vector<std::vector<std::vector<struct tm>>> bak_schedule;
        std::string storeType(RECODE_TYPE_PLANNED);

        if (!iter_id->second.size() || !iter_rec->second.size())
            goto quit;

        if (ParseSchedule(iter_rec->second.c_str(), rec_schedule) < 0)
            goto quit;

        if (ParseSchedule(iter_bak->second.c_str(), bak_schedule) < 0)
            goto quit;

        if (CreateCameraTable(iter_id->second) < 0)
            goto quit;

        if (AddBakSchedule(iter_id->second, bak_schedule) < 0)
            goto quit;

        if (AddStoreDevice(iter_id->second, storeType) < 0)
            goto quit;

        if (AddStream(iter_id->second.c_str(), rec_schedule, StorePlannedStream) < 0)
            goto quit;
    }

    if (StartServer(serverIp, server_port, 10 * 1024, (client_cb)ResponseClient) < 0)
        goto quit;
    RtmpServerStart();
    StartMonitorProc();

    while (sys_flag)
    {
        std::string mainId = "34020000001320000211";
        std::string subId = "34020000001320000211";
        std::string rtspUrl;
        rtspUrl = grpcClientStart(mainId, subId);
        printf("rtsp url = %s\n", rtspUrl.c_str());
        sleep(1);
    }

quit:
    RemovePidFile();
    return ret;
}
