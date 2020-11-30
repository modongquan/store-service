#ifndef MYSQL_H
#define MYSQL_H

#include "common.h"

struct mysqlstoreinfo
{
    std::string file_name;
    std::string file_type;
    std::string file_status;
    std::string file_backup;
};

int32_t DatabaseInit(std::string &ip, std::string &user, std::string &passwd, std::string &name);

int32_t CreateCameraTable(std::string &cameraId);
int32_t GetFirstStoreInfoFromDB(const char *tableName, struct mysqlstoreinfo &result);
int32_t QueryInfoByDate(std::string &cameraId, std::string &start, std::string &end, std::vector<struct mysqlstoreinfo> &result);
int32_t InsertInfoToTable(std::string &cameraId, time_t start_time_ms, std::string &type, std::string &name, std::string &status);
int32_t UpdateStatusToTable(std::string &cameraId, std::string &name, std::string &status);
int32_t UpdateBackupToTable(std::string &cameraId, std::string &name, std::string &backup);
int32_t DeleteCameraTable(std::string &cameraId);
int32_t DeleteInfoInTable(std::string &cameraId, std::string &name);

int32_t CreateCameraInfoTable(void);
int32_t GetCameraInfoFromDB(std::vector<std::map<std::string, std::string>> &info);
int32_t InsertCameraInfoToTable(std::string &cameraId, std::string &name, std::string &rec_schedule, std::string &bak_schedule);
int32_t UpdateRecScheduleToCameraTable(std::string &cameraId, std::string &rec_schedule);
int32_t UpdateBakScheduleToCameraTable(std::string &cameraId, std::string &bak_schedule);
int32_t DeleteCameraInfoInTable(std::string &cameraId);

#endif