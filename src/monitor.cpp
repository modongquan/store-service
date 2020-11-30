#include <sys/statfs.h>
#include "monitor.h"
#include "mysql.h"

extern std::vector<std::map<std::string, std::string>> cameraInfoList;

void *MonitorProc(void *arg)
{
    std::string monitor_path = storePath;
    std::string cameraId;
    std::string unlink_path;
    struct statfs disk_info;
    uint64_t free_size, min_free_size = STORE_FILE_MAX_SIZE * cameraInfoList.size() * 2;

    while (sys_flag)
    {
        if (statfs(monitor_path.c_str(), &disk_info) < 0)
        {
            perror("monitor disk info error");
            sleep(1);
            continue;
        }

        free_size = disk_info.f_bavail * disk_info.f_bsize;
        if (free_size <= min_free_size)
        {
            for (int i = 0; i < cameraInfoList.size(); i++)
            {
                struct mysqlstoreinfo storeInfo;
                auto iter_id = cameraInfoList[i].find(std::string(CAMERA_ID_MYSQL_NAME));
                cameraId = iter_id->second;
                if (GetFirstStoreInfoFromDB(cameraId.c_str(), storeInfo) < 0)
                    continue;
                DeleteInfoInTable(iter_id->second, storeInfo.file_name);
                unlink_path = monitor_path + "/" + cameraId + "/" + storeInfo.file_name;
                unlink(unlink_path.c_str());
            }
        }
        sleep(1);
    }
}

int StartMonitorProc(void)
{
    pthread_t pth;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&pth, &attr, MonitorProc, nullptr) < 0)
        return -1;

    return 0;
}
