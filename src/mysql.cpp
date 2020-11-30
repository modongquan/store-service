#include <mysql/mysql.h>
#include <unistd.h>
#include <vector>
#include <map>
#include <sys/time.h>
#include "mysql.h"
#include "common.h"

MYSQL *ptrMysql;
MYSQL *ptrConnect;

std::map<std::string, MYSQL_FIELD_OFFSET> storeInfoName;
std::vector<std::map<std::string, std::string>> storeInfoFromDB;

std::string camera_info_key[] = {
    CAMERA_ID_MYSQL_NAME,
    CAMERA_NAME_MYSQL_NAME,
    REC_SCHE_JSON_NAME,
    BAK_SCHE_JSON_NAME,
};

extern void ExtraStr(std::string &in, std::vector<char> &ch, std::vector<std::string> &out);

int32_t CheckConnState(void)
{
    int ret = 0;

    if ((ret = mysql_ping(ptrConnect)))
    {
        myprintf("reconnct mysql %d\n", ret);
    }

    return 0;
}

int32_t GetStoreInfoFromDB(const char *tableName, uint64_t start_idx, uint64_t end_idx,
                           std::vector<struct mysqlstoreinfo> &result)
{
    char QueryCmd[64];
    snprintf(QueryCmd, sizeof(QueryCmd), "select * from `%s`", tableName);

    CheckConnState();

    if (mysql_real_query(ptrConnect, "set names utf8", 15))
    {
        myprintf("mysql set names utf8 fail : %s\n", mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    if (mysql_real_query(ptrConnect, static_cast<const char *>(QueryCmd), strlen(QueryCmd)))
    {
        myprintf("%s fail : %s\n", QueryCmd, mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    MYSQL_RES *ptrTables = mysql_store_result(ptrConnect);
    if (ptrTables == nullptr)
    {
        myprintf("mysql_store_result fail : %s\n", mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    MYSQL_FIELD *ptrFields;
    int file_name_idx = -1, file_type_idx = -1, file_status_idx = -1, file_backup_idx = -1;
    while ((ptrFields = mysql_fetch_field(ptrTables)))
    {
        if (!strcmp(ptrFields->name, CAMERA_NAME_MYSQL_NAME))
        {
            file_name_idx = mysql_field_tell(ptrTables) - 1;
        }
        else if (!strcmp(ptrFields->name, CAMERA_TYPE_MYSQL_NAME))
        {
            file_type_idx = mysql_field_tell(ptrTables) - 1;
        }
        else if (!strcmp(ptrFields->name, CAMERA_STATUS_MYSQL_NAME))
        {
            file_status_idx = mysql_field_tell(ptrTables) - 1;
        }
        else if (!strcmp(ptrFields->name, CAMERA_BACKUP_MYSQL_NAME))
        {
            file_backup_idx = mysql_field_tell(ptrTables) - 1;
        }
    }
    if (file_name_idx < 0)
    {
        myprintf("%s : did not fine %s fields\n", __FUNCTION__, CAMERA_NAME_MYSQL_NAME);
        mysql_free_result(ptrTables);
        return -1;
    }
    if (file_type_idx < 0)
    {
        myprintf("%s : did not fine %s fields\n", __FUNCTION__, CAMERA_TYPE_MYSQL_NAME);
        mysql_free_result(ptrTables);
        return -1;
    }
    if (file_status_idx < 0)
    {
        myprintf("%s : did not fine %s fields\n", __FUNCTION__, CAMERA_STATUS_MYSQL_NAME);
        mysql_free_result(ptrTables);
        return -1;
    }
    if (file_backup_idx < 0)
    {
        myprintf("%s : did not fine %s fields\n", __FUNCTION__, CAMERA_BACKUP_MYSQL_NAME);
        mysql_free_result(ptrTables);
        return -1;
    }

    uint64_t cnt_idx = start_idx;
    mysql_data_seek(ptrTables, start_idx);
    while (cnt_idx <= end_idx)
    {
        MYSQL_ROW Row = mysql_fetch_row(ptrTables);
        if (!Row)
        {
            myprintf("mysql_fetch_row null\n");
            return -1;
        }
        struct mysqlstoreinfo storeInfo;
        storeInfo.file_name = Row[file_name_idx];
        storeInfo.file_type = Row[file_type_idx];
        storeInfo.file_status = Row[file_status_idx];
        storeInfo.file_backup = Row[file_backup_idx];
        result.push_back(storeInfo);
        cnt_idx++;
    }

    mysql_free_result(ptrTables);

    return 0;
}

int32_t GetFirstStoreInfoFromDB(const char *tableName, struct mysqlstoreinfo &result)
{
    char QueryCmd[128];
    snprintf(QueryCmd, sizeof(QueryCmd), "select * from `%s` ORDER BY `%s` ASC LIMIT 1", tableName, CAMERA_NAME_MYSQL_NAME);

    CheckConnState();

    if (mysql_real_query(ptrConnect, "set names utf8", 15))
    {
        myprintf("mysql set names utf8 fail : %s\n", mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    if (mysql_real_query(ptrConnect, static_cast<const char *>(QueryCmd), strlen(QueryCmd)))
    {
        myprintf("%s fail : %s\n", QueryCmd, mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    MYSQL_RES *ptrTables = mysql_store_result(ptrConnect);
    if (ptrTables == nullptr)
    {
        myprintf("mysql_store_result fail : %s\n", mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    MYSQL_FIELD *ptrFields;
    int file_name_idx = -1, file_type_idx = -1, file_status_idx = -1, file_backup_idx = -1;
    while ((ptrFields = mysql_fetch_field(ptrTables)))
    {
        if (!strcmp(ptrFields->name, CAMERA_NAME_MYSQL_NAME))
        {
            file_name_idx = mysql_field_tell(ptrTables) - 1;
        }
        else if (!strcmp(ptrFields->name, CAMERA_TYPE_MYSQL_NAME))
        {
            file_type_idx = mysql_field_tell(ptrTables) - 1;
        }
        else if (!strcmp(ptrFields->name, CAMERA_STATUS_MYSQL_NAME))
        {
            file_status_idx = mysql_field_tell(ptrTables) - 1;
        }
        else if (!strcmp(ptrFields->name, CAMERA_BACKUP_MYSQL_NAME))
        {
            file_backup_idx = mysql_field_tell(ptrTables) - 1;
        }
    }
    if (file_name_idx < 0)
    {
        myprintf("%s : did not fine %s fields\n", __FUNCTION__, CAMERA_NAME_MYSQL_NAME);
        mysql_free_result(ptrTables);
        return -1;
    }
    if (file_type_idx < 0)
    {
        myprintf("%s : did not fine %s fields\n", __FUNCTION__, CAMERA_TYPE_MYSQL_NAME);
        mysql_free_result(ptrTables);
        return -1;
    }
    if (file_status_idx < 0)
    {
        myprintf("%s : did not fine %s fields\n", __FUNCTION__, CAMERA_STATUS_MYSQL_NAME);
        mysql_free_result(ptrTables);
        return -1;
    }
    if (file_backup_idx < 0)
    {
        myprintf("%s : did not fine %s fields\n", __FUNCTION__, CAMERA_BACKUP_MYSQL_NAME);
        mysql_free_result(ptrTables);
        return -1;
    }

    MYSQL_ROW Row = mysql_fetch_row(ptrTables);
    if (!Row)
    {
        myprintf("mysql_fetch_row null\n");
        return -1;
    }

    result.file_name = Row[file_name_idx];
    result.file_type = Row[file_type_idx];
    result.file_status = Row[file_status_idx];
    result.file_backup = Row[file_backup_idx];

    mysql_free_result(ptrTables);

    return 0;
}

int32_t GetCameraInfoFromDB(std::vector<std::map<std::string, std::string>> &info)
{
    char QueryCmd[64];

    info.clear();

    CheckConnState();

    snprintf(QueryCmd, 64, "select * from `camera_info`");
    if (mysql_real_query(ptrConnect, "set names utf8", 15))
    {
        myprintf("mysql set names utf8 fail : %s\n", mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    if (mysql_real_query(ptrConnect, static_cast<const char *>(QueryCmd), strlen(QueryCmd)))
    {
        myprintf("%s fail : %s\n", QueryCmd, mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    MYSQL_RES *ptrTables = mysql_store_result(ptrConnect);
    if (ptrTables == nullptr)
    {
        myprintf("mysql_store_result fail : %s\n", mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    MYSQL_FIELD *ptrFields;
    std::map<std::string, int> camera_info_idx;
    for (int i = 0; i < sizeof(camera_info_key) / sizeof(std::string); i++)
        camera_info_idx.insert(camera_info_idx.end(), std::pair<std::string, int>(camera_info_key[i], -1));

    while ((ptrFields = mysql_fetch_field(ptrTables)))
    {
        std::string key = ptrFields->name;
        if (camera_info_idx.count(std::string(key)) > 0)
        {
            camera_info_idx[key] = mysql_field_tell(ptrTables) - 1;
        }
        else
        {
            myprintf("'%s' is invalid from database\n", ptrFields->name);
            return -1;
        }
    }

    MYSQL_ROW Row;
    while ((Row = mysql_fetch_row(ptrTables)))
    {
        std::map<std::string, std::string> camera_info;

        for (auto iter = camera_info_idx.begin(); iter != camera_info_idx.end(); iter++)
        {
            std::string key = iter->first;
            std::string value = Row[iter->second];

            camera_info.insert(camera_info.end(), std::pair<std::string, std::string>(key, value));
        }

        info.push_back(camera_info);
    }

    mysql_free_result(ptrTables);

    return 0;
}

int32_t CreateCameraTable(std::string &cameraId)
{
    char QueryCmd[256];

    snprintf(QueryCmd, 256, "CREATE TABLE IF NOT EXISTS `%s` "
                            "(`%s` BIGINT UNSIGNED, "
                            "`%s` VARCHAR(32) NOT NULL, "
                            "`%s` VARCHAR(32) NOT NULL, "
                            "`%s` VARCHAR(32) NOT NULL, "
                            "`%s` VARCHAR(32) NOT NULL"
                            ")CHARSET=utf8",
             cameraId.c_str(), CAMERA_TM_MYSQL_NAME, CAMERA_NAME_MYSQL_NAME, CAMERA_TYPE_MYSQL_NAME, CAMERA_STATUS_MYSQL_NAME, CAMERA_BACKUP_MYSQL_NAME);

    CheckConnState();

    if (mysql_real_query(ptrConnect, static_cast<const char *>(QueryCmd), strlen(QueryCmd)))
    {
        myprintf("%s fail : %s\n", QueryCmd, mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    return 0;
}

int32_t CreateCameraInfoTable(void)
{
    char QueryCmd[512];

    snprintf(QueryCmd, 512, "CREATE TABLE IF NOT EXISTS `camera_info` ("
                            "`%s` VARCHAR(42) NOT NULL, "
                            "`%s` VARCHAR(128) NOT NULL, "
                            "`%s` VARCHAR(2048) NOT NULL, "
                            "`%s` VARCHAR(2048) NOT NULL"
                            ")CHARSET=utf8",
             CAMERA_ID_MYSQL_NAME, CAMERA_NAME_MYSQL_NAME, REC_SCHE_JSON_NAME, BAK_SCHE_JSON_NAME);

    CheckConnState();

    if (mysql_real_query(ptrConnect, static_cast<const char *>(QueryCmd), strlen(QueryCmd)))
    {
        myprintf("%s fail : %s\n", QueryCmd, mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    return 0;
}

int32_t InsertCameraInfoToTable(std::string &cameraId, std::string &name, std::string &rec_schedule, std::string &bak_schedule)
{
    char QueryCmd[2048];

    snprintf(QueryCmd, 2048, "INSERT INTO `camera_info` "
                             "(`%s`, `%s`, `%s`, `%s`) "
                             "VALUES (\'%s\', \'%s\', \'%s\', \'%s\')",
             CAMERA_ID_MYSQL_NAME, CAMERA_NAME_MYSQL_NAME, REC_SCHE_JSON_NAME, BAK_SCHE_JSON_NAME,
             cameraId.c_str(), name.c_str(), rec_schedule.c_str(), bak_schedule.c_str());

    CheckConnState();

    if (mysql_real_query(ptrConnect, static_cast<const char *>(QueryCmd), strlen(QueryCmd)))
    {
        myprintf("%s fail : %s\n", QueryCmd, mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    return 0;
}

int32_t InsertInfoToTable(std::string &cameraId, time_t start_time_ms, std::string &type, std::string &name, std::string &status)
{
    char QueryCmd[256];

    snprintf(QueryCmd, 256, "INSERT INTO `%s` (`%s`, `%s`, `%s`, `%s`, `%s`) VALUES (%ld, \'%s\', \'%s\', \'%s\', \'no\')",
             cameraId.c_str(),
             CAMERA_TM_MYSQL_NAME, CAMERA_NAME_MYSQL_NAME, CAMERA_TYPE_MYSQL_NAME, CAMERA_STATUS_MYSQL_NAME, CAMERA_BACKUP_MYSQL_NAME,
             start_time_ms, name.c_str(), type.c_str(), status.c_str());

    CheckConnState();

    if (mysql_real_query(ptrConnect, static_cast<const char *>(QueryCmd), strlen(QueryCmd)))
    {
        myprintf("%s fail : %s\n", QueryCmd, mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    return 0;
}

int32_t UpdateStatusToTable(std::string &cameraId, std::string &name, std::string &status)
{
    char QueryCmd[256];

    snprintf(QueryCmd, sizeof(QueryCmd), "UPDATE `%s` SET `%s`=\'%s\' WHERE `%s`=\'%s\' LIMIT 1",
             cameraId.c_str(), CAMERA_STATUS_MYSQL_NAME, status.c_str(), CAMERA_NAME_MYSQL_NAME, name.c_str());
    myprintf("%s\n", QueryCmd);

    CheckConnState();

    if (mysql_real_query(ptrConnect, static_cast<const char *>(QueryCmd), strlen(QueryCmd)))
    {
        myprintf("%s fail : %s\n", QueryCmd, mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    return 0;
}

int32_t UpdateBackupToTable(std::string &cameraId, std::string &name, std::string &backup)
{
    char QueryCmd[256];

    snprintf(QueryCmd, sizeof(QueryCmd), "UPDATE `%s` SET `%s`=\'%s\' WHERE `%s`=\'%s\' LIMIT 1",
             cameraId.c_str(), CAMERA_BACKUP_MYSQL_NAME, backup.c_str(), CAMERA_NAME_MYSQL_NAME, name.c_str());
    myprintf("%s\n", QueryCmd);

    CheckConnState();

    if (mysql_real_query(ptrConnect, static_cast<const char *>(QueryCmd), strlen(QueryCmd)))
    {
        myprintf("%s fail : %s\n", QueryCmd, mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    return 0;
}

int32_t UpdateRecScheduleToCameraTable(std::string &cameraId, std::string &rec_schedule)
{
    char QueryCmd[2048];

    snprintf(QueryCmd, 2048, "UPDATE `camera_info` SET `%s`=\'%s\' WHERE `%s`=\'%s\'",
             REC_SCHE_JSON_NAME, rec_schedule.c_str(), CAMERA_ID_MYSQL_NAME, cameraId.c_str());

    CheckConnState();

    if (mysql_real_query(ptrConnect, static_cast<const char *>(QueryCmd), strlen(QueryCmd)))
    {
        myprintf("%s fail : %s\n", QueryCmd, mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    return 0;
}

int32_t UpdateBakScheduleToCameraTable(std::string &cameraId, std::string &bak_schedule)
{
    char QueryCmd[2048];

    snprintf(QueryCmd, 2048, "UPDATE `camera_info` SET `%s`=\'%s\' WHERE `%s`=\'%s\'",
             BAK_SCHE_JSON_NAME, bak_schedule.c_str(), CAMERA_ID_MYSQL_NAME, cameraId.c_str());

    CheckConnState();

    if (mysql_real_query(ptrConnect, static_cast<const char *>(QueryCmd), strlen(QueryCmd)))
    {
        myprintf("%s fail : %s\n", QueryCmd, mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    return 0;
}

int32_t DeleteCameraTable(std::string &cameraId)
{
    char QueryCmd[256];

    snprintf(QueryCmd, 256, "DROP TABLE `%s`", cameraId.c_str());

    CheckConnState();

    if (mysql_real_query(ptrConnect, static_cast<const char *>(QueryCmd), strlen(QueryCmd)))
    {
        myprintf("%s fail : %s\n", QueryCmd, mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    return 0;
}

int32_t DeleteInfoInTable(std::string &cameraId, std::string &name)
{
    char QueryCmd[256];

    snprintf(QueryCmd, 256, "DELETE FROM `%s` WHERE `%s`=\"%s\" LIMIT 1", cameraId.c_str(), CAMERA_NAME_MYSQL_NAME, name.c_str());

    CheckConnState();

    if (mysql_real_query(ptrConnect, static_cast<const char *>(QueryCmd), strlen(QueryCmd)))
    {
        myprintf("%s fail : %s\n", QueryCmd, mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    return 0;
}

int32_t DeleteCameraInfoInTable(std::string &cameraId)
{
    char QueryCmd[256];

    snprintf(QueryCmd, 256, "DELETE FROM `camera_info` WHERE `%s`=\'%s\' LIMIT 1", CAMERA_ID_MYSQL_NAME, cameraId.c_str());

    CheckConnState();

    if (mysql_real_query(ptrConnect, static_cast<const char *>(QueryCmd), strlen(QueryCmd)))
    {
        myprintf("%s fail : %s\n", QueryCmd, mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    return 0;
}

int32_t TranslateToStructTM(std::string &date_time, struct tm *result)
{
    if (!result)
        return -1;

    std::vector<char> separat;
    separat.push_back('-');
    separat.push_back(' ');
    separat.push_back(':');
    std::vector<std::string> extra;
    ExtraStr(date_time, separat, extra);
    if (extra.size() < 6)
        return -1;

    result->tm_year = std::stoi(extra[0].c_str()) - 1900;
    result->tm_mon = std::stoi(extra[1].c_str()) - 1;
    result->tm_mday = std::stoi(extra[2].c_str());
    result->tm_hour = std::stoi(extra[3].c_str());
    result->tm_min = std::stoi(extra[4].c_str());
    result->tm_sec = std::stoi(extra[5].c_str());

    if (result->tm_year < 0)
        return -1;
    if (result->tm_mon < 0 || result->tm_mon > 11)
        return -1;
    if (result->tm_mday < 0 || result->tm_mday > 31)
        return -1;
    if (result->tm_hour < 0 || result->tm_hour > 24)
        return -1;
    if (result->tm_min < 0 || result->tm_min > 60)
        return -1;
    if (result->tm_sec < 0 || result->tm_sec > 60)
        return -1;

    return 0;
}

int64_t SearchClosestTimeFromDB(const char *tableName, time_t searchTime, int8_t *flag)
{
    if (!flag)
        return -1;

    char QueryCmd[64];
    snprintf(QueryCmd, 64, "select * from `%s`", tableName);

    CheckConnState();

    if (mysql_real_query(ptrConnect, "set names utf8", 15))
    {
        myprintf("mysql set names utf8 fail : %s\n", mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    if (mysql_real_query(ptrConnect, static_cast<const char *>(QueryCmd), strlen(QueryCmd)))
    {
        myprintf("%s fail : %s\n", QueryCmd, mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    MYSQL_RES *ptrTables = mysql_store_result(ptrConnect);
    if (ptrTables == nullptr)
    {
        myprintf("mysql_store_result fail : %s\n", mysql_error(ptrConnect));
        SetOtherError(mysql_error(ptrConnect));
        return -1;
    }

    MYSQL_FIELD *ptrFields;
    int start_time_ms_idx = -1;
    while ((ptrFields = mysql_fetch_field(ptrTables)))
    {
        if (!strcmp(ptrFields->name, "start_time_ms"))
        {
            start_time_ms_idx = mysql_field_tell(ptrTables) - 1;
        }
    }
    if (start_time_ms_idx < 0)
    {
        myprintf("%s : did not fine start_time_ms fields\n", __FUNCTION__);
        SetOtherError(mysql_error(ptrConnect));
        mysql_free_result(ptrTables);
        return -1;
    }

    uint64_t row_nums = mysql_num_rows(ptrTables);
    if (!row_nums)
        return -1;

    int64_t row_start = 0, row_end = row_nums - 1;
    int64_t average = 0;
    int64_t search_ret = -1;
    MYSQL_ROW Row, rowStart, rowEnd;

    mysql_data_seek(ptrTables, row_start);
    rowStart = mysql_fetch_row(ptrTables);

    mysql_data_seek(ptrTables, row_end);
    rowEnd = mysql_fetch_row(ptrTables);

    if (searchTime < std::stol(rowStart[start_time_ms_idx]))
    {
        *flag = -1;
        search_ret = row_start;
        goto quit;
    }
    else if (searchTime > std::stol(rowEnd[start_time_ms_idx]))
    {
        *flag = 1;
        search_ret = row_end;
        goto quit;
    }
    else
    {
        flag = 0;
    }

    while (1)
    {
        average = (row_start + row_end) / 2;
        mysql_data_seek(ptrTables, average);
        Row = mysql_fetch_row(ptrTables);
        if (!Row)
        {
            myprintf("mysql_fetch_row null\n");
            SetOtherError(mysql_error(ptrConnect));
            return -1;
        }

        if (row_end - row_start > 1)
        {
            if (searchTime < std::stol(Row[start_time_ms_idx]))
            {
                row_end = average;
            }
            else if (searchTime > std::stol(Row[start_time_ms_idx]))
            {
                row_start = average;
            }
            else
            {
                search_ret = average;
                break;
            }
        }
        else
        {
            if (searchTime <= std::stol(Row[start_time_ms_idx]))
            {
                search_ret = row_start;
            }
            else
            {
                search_ret = row_end;
            }
            break;
        }
    }

quit:
    mysql_free_result(ptrTables);
    return search_ret;
}

int32_t QueryInfoByDate(std::string &cameraId, std::string &start, std::string &end, std::vector<struct mysqlstoreinfo> &result)
{
    struct tm date_start, date_end;

    if (TranslateToStructTM(start, &date_start) < 0)
        return -1;
    if (TranslateToStructTM(end, &date_end) < 0)
        return -1;

    time_t start_sec, end_sec;
    start_sec = mktime(&date_start);
    end_sec = mktime(&date_end);
    myprintf("start_sec = %ld, end_sec = %ld\n", start_sec, end_sec);

    int64_t start_idx_db, end_idx_db;
    int8_t start_flag, end_flag;
    start_idx_db = SearchClosestTimeFromDB(cameraId.c_str(), start_sec, &start_flag);
    end_idx_db = SearchClosestTimeFromDB(cameraId.c_str(), end_sec, &end_flag);
    if (start_idx_db < 0 || end_idx_db < 0)
    {
        return 1;
    }
    if (start_flag < 0 && end_flag < 0)
    {
        SetError(do_not_find_rec_files);
        return -1;
    }
    else if (start_flag > 0 && end_flag > 0)
    {
        SetError(do_not_find_rec_files);
        return -1;
    }

    myprintf("start_idx_db = %ld, end_idx_db = %ld\n", start_idx_db, end_idx_db);
    int32_t nRet = GetStoreInfoFromDB(cameraId.c_str(), start_idx_db, end_idx_db, result);
    for (int i = 0; i < result.size(); i++)
    {
        myprintf("%s\n", result[i].file_name.c_str());
    }
    return nRet;
}

int32_t ConnectDataBase(const char *ptrIp, uint32_t port, const char *ptrUserName, const char *ptrPassword, const char *ptrDBName)
{
    if (ptrIp == nullptr || ptrUserName == nullptr || ptrPassword == nullptr || ptrDBName == nullptr)
    {
        return -1;
    }

    ptrMysql = mysql_init(nullptr);

    unsigned int timeout = 7;
    if (mysql_options(ptrMysql, MYSQL_OPT_CONNECT_TIMEOUT, (const char *)&timeout))
    {
        myprintf("set mysql timeout fail : %s\n", mysql_error(ptrMysql));
        SetOtherError(mysql_error(ptrMysql));
        return -1;
    }

    my_bool reconn = 1;
    if (mysql_options(ptrMysql, MYSQL_OPT_RECONNECT, &reconn))
    {
        myprintf("set mysql timeout fail : %s\n", mysql_error(ptrMysql));
        SetOtherError(mysql_error(ptrMysql));
        return -1;
    }

    ptrConnect = mysql_real_connect(ptrMysql, ptrIp, ptrUserName, ptrPassword, ptrDBName, port, nullptr, 0);
    if (ptrConnect == nullptr)
    {
        myprintf("mysql connect fail : %s\n", mysql_error(ptrMysql));
        SetOtherError(mysql_error(ptrMysql));
        return -1;
    }

    return 0;
}

int32_t DatabaseInit(std::string &ip, std::string &user, std::string &passwd, std::string &name)
{
    if (ConnectDataBase(ip.c_str(), 3306, user.c_str(), passwd.c_str(), name.c_str()) < 0)
        return -1;

    if (CreateCameraInfoTable() < 0)
        return -1;

    return 0;
}
