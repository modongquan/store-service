#ifndef BACKUP_H
#define BACKUP_H

#include "common.h"

void BackupServerInit(void);
void BackupRespond(int sockfd, uint8_t *data, uint32_t size);
int BackupClientStart(std::string &backup_id, std::string &backup_name);
int BackupScheHandl(std::string &backup_id, std::string &backup_name);

int AddBakSchedule(std::string &backup_id, std::vector<std::vector<std::vector<struct tm>>> &schedule);
int RemoveBakSchedule(std::string &backup_id);
int UpdateBakSchedule(std::string &backup_id, std::vector<std::vector<std::vector<struct tm>>> &schedule);
#endif // BACKUP_H
