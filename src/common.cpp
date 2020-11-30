#include "common.h"
#include "cJSON.h"

const char *store_api_ret[store_api_ret_num] = {
    "创建线程失败",
    "创建存储文件夹失败",
    "申请内存失败",
    "设备已存在",
    "录像计划天数小于7天",
    "录像计划格式错误",
    "没有POST数据",
    "无效的json数据",
    "json数据没有设备主ID",
    "json数据没有设备子ID",
    "json数据没有开始时间",
    "json数据没有结束时间",
    "json数据没有typeId",
    "json数据没有eventId",
    "json数据没有文件名称",
    "json数据没有操作类型",
    "json数据没有设备名称",
    "json数据没有录像或者备份计划",
    "json数据没有url",
    "无效的计划类型",
    "无效的日期格式",
    "无效的事件类型",
    "无效的文件操作类型",
    "无效的typeId",
    "无效的url",
    "转换MP4文件失败",
    "创建自定义计划json失败",
    "函数输入空地址",
    "没有找到摄像机",
    "没有找到对应文件",
    "cjson调用失败",
    "删除文件失败"};

volatile int sys_flag = 1;
volatile uint32_t error_no;
const char *other_error;

std::string serverIp;
std::string backupIp;
std::string streamMediaIp;
std::string downloadMediaIp;
std::string databaseUser;
std::string databasePasswd;
std::string databaseName;
std::string storePath;
std::string backupPath;
std::string transMediaPath;
int32_t server_port, playback_port, backup_port, stream_media_port;

int32_t LoadInfoFile(void)
{
    int fd = open("./store_service.conf", O_RDONLY);
    if (fd < 0)
    {
        perror("open ./store_service.conf fail");
        return -1;
    }

    uint8_t rdBuf[512];
    size_t rd_size = read(fd, rdBuf, 512);
    if (rd_size < 0)
        return -1;
    rdBuf[rd_size] = 0;
    myprintf("Read Info Frome DataBase.conf : %s", rdBuf);
    close(fd);

    int32_t idx = 0;
    std::vector<std::string> infoFromFile;
    std::string info;
    while (rdBuf[idx] != 0x0d && rdBuf[idx] != 0x0a)
    {
        if (rdBuf[idx] != ' ')
        {
            info.insert(info.end(), 1, rdBuf[idx]);
        }
        else
        {
            if (!info.empty())
            {
                infoFromFile.push_back(info);
                info.clear();
            }
        }
        idx++;
    }
    if (!info.empty())
    {
        infoFromFile.push_back(info);
        info.clear();
    }

    myprintf("info from file : %ld\n", infoFromFile.size());
    for (int i = 0; i < infoFromFile.size(); i++)
    {
        myprintf("%s\n", infoFromFile[i].c_str());
    }

    if (infoFromFile.size() < load_file_num)
    {
        myprintf("Info From File Size Not Enough\n");
        return -1;
    }

    serverIp = infoFromFile[load_file_server_ip];
    backupIp = infoFromFile[load_file_backup_ip];
    streamMediaIp = infoFromFile[load_file_stream_media_ip];
    downloadMediaIp = infoFromFile[load_file_dowload_media_ip];
    server_port = atoi(infoFromFile[load_file_server_port].c_str());
    playback_port = atoi(infoFromFile[load_file_playback_port].c_str());
    backup_port = atoi(infoFromFile[load_file_backup_port].c_str());
    stream_media_port = atoi(infoFromFile[load_file_stream_media_port].c_str());
    databaseUser = infoFromFile[load_file_db_user];
    databasePasswd = infoFromFile[load_file_db_passwd];
    databaseName = infoFromFile[load_file_db_name];
    storePath = infoFromFile[load_file_store_path];
    backupPath = infoFromFile[load_file_bak_path];
    transMediaPath = infoFromFile[load_file_trans_media_path];

    return 0;
}

void ExtraStr(std::string &in, std::vector<char> &ch, std::vector<std::string> &out)
{
    int size = in.size();
    int idx = 0;
    std::string str_cut;

    while (idx < size)
    {
        char ch_pop = in.at(idx);
        int i;
        for (i = 0; i < ch.size(); i++)
        {
            if (ch_pop != ch[i])
            {
                continue;
            }
            else
            {
                break;
            }
        }
        if (i == ch.size())
        {
            str_cut.insert(str_cut.end(), ch_pop);
        }
        else
        {
            if (!str_cut.empty())
            {
                out.push_back(str_cut);
                str_cut.clear();
            }
        }
        idx++;
    }
    if (!str_cut.empty())
    {
        out.push_back(str_cut);
        str_cut.clear();
    }
}

int ScheduleFormatCheck(const char *schedule)
{
    if (!schedule)
        return -1;

    const char format[] = {"00:00:00-00:00:00"};
    int format_len = strlen(format);
    int hour, min, sec;

    if (schedule[format_len] != 0)
        return -1;

    if (schedule[2] != ':' || schedule[5] != ':' || schedule[11] != ':' || schedule[14] != ':')
        return -1;

    if (schedule[8] != '-')
        return -1;

    hour = (schedule[0] - 0x30) * 10 + (schedule[1] - 0x30);
    if (hour < 0 || hour > 24)
        return -1;

    min = (schedule[3] - 0x30) * 10 + (schedule[4] - 0x30);
    if (min < 0 || min > 59)
        return -1;

    sec = (schedule[6] - 0x30) * 10 + (schedule[7] - 0x30);
    if (sec < 0 || sec > 59)
        return -1;

    hour = (schedule[9] - 0x30) * 10 + (schedule[10] - 0x30);
    if (hour < 0 || hour > 24)
        return -1;

    min = (schedule[12] - 0x30) * 10 + (schedule[13] - 0x30);
    if (min < 0 || min > 59)
        return -1;

    sec = (schedule[15] - 0x30) * 10 + (schedule[16] - 0x30);
    if (sec < 0 || sec > 59)
        return -1;

    return 0;
}

void TranslateScheduleToTm(std::vector<std::vector<std::string>> &schedule,
                           std::vector<std::vector<std::vector<struct tm>>> &schedule_tm)
{
    schedule_tm.resize(schedule.size());

    for (int i = 0; i < schedule.size(); i++)
    {
        schedule_tm[i].resize(schedule[i].size());

        for (int j = 0; j < schedule[i].size(); j++)
        {
            schedule_tm[i][j].resize(2);

            const char *time_format = schedule[i][j].c_str();
            struct tm *start_tm = &schedule_tm[i][j][0];
            struct tm *end_tm = &schedule_tm[i][j][1];

            start_tm->tm_hour = (time_format[0] - 0x30) * 10 + (time_format[1] - 0x30);
            start_tm->tm_min = (time_format[3] - 0x30) * 10 + (time_format[4] - 0x30);
            start_tm->tm_sec = (time_format[6] - 0x30) * 10 + (time_format[7] - 0x30);

            end_tm->tm_hour = (time_format[9] - 0x30) * 10 + (time_format[10] - 0x30);
            end_tm->tm_min = (time_format[12] - 0x30) * 10 + (time_format[13] - 0x30);
            end_tm->tm_sec = (time_format[15] - 0x30) * 10 + (time_format[16] - 0x30);
        }
    }

    schedule_tm.insert(schedule_tm.begin(), schedule_tm.back());
    schedule_tm.pop_back();
}

int32_t TransDateToTm(std::string &date, struct tm &struTm)
{
    const char *charDate = date.c_str();
    char year[5], mon[3], day[3], hour[3], min[3], sec[3];
    int idx = 0;

    while (*charDate != '-')
    {
        year[idx++] = *charDate++;
        if (idx >= sizeof(year))
            return -1;
    }
    charDate++;
    year[idx] = 0;
    idx = 0;

    while (*charDate != '-')
    {
        mon[idx++] = *charDate++;
        if (idx >= sizeof(mon))
            return -1;
    }
    charDate++;
    mon[idx] = 0;
    idx = 0;

    while (*charDate != ' ')
    {
        day[idx++] = *charDate++;
        if (idx >= sizeof(day))
            return -1;
    }
    charDate++;
    day[idx] = 0;
    idx = 0;

    while (*charDate != ':')
    {
        hour[idx++] = *charDate++;
        if (idx >= sizeof(hour))
            return -1;
    }
    charDate++;
    hour[idx] = 0;
    idx = 0;

    while (*charDate != ':')
    {
        min[idx++] = *charDate++;
        if (idx >= sizeof(min))
            return -1;
    }
    charDate++;
    min[idx] = 0;
    idx = 0;

    while (*charDate != 0)
    {
        sec[idx++] = *charDate++;
        if (idx >= sizeof(sec))
            return -1;
    }
    charDate++;
    sec[idx] = 0;
    idx = 0;

    struTm.tm_year = atoi(year) - 1900;
    struTm.tm_mon = atoi(mon) - 1;
    struTm.tm_mday = atoi(day);
    struTm.tm_hour = atoi(hour);
    struTm.tm_min = atoi(min);
    struTm.tm_sec = atoi(sec);
    if (struTm.tm_year < 0)
        return -1;
    if (struTm.tm_mon < 0 || struTm.tm_mon > 11)
        return -1;
    if (struTm.tm_mday < 1 || struTm.tm_mon > 31)
        return -1;
    if (struTm.tm_hour < 0 || struTm.tm_hour > 23)
        return -1;
    if (struTm.tm_min < 0 || struTm.tm_min > 59)
        return -1;
    if (struTm.tm_sec < 0 || struTm.tm_min > 59)
        return -1;

    return 0;
}

int32_t ParseSchedule(const char *json, std::vector<std::vector<std::vector<struct tm>>> &schedule)
{
    int schedule_num, schedule_day_num;
    std::vector<std::string> daySchedule;
    std::vector<std::vector<std::string>> weekSchedule;

    cJSON *scheduleObj = cJSON_Parse(json);
    if (!scheduleObj)
    {
        myprintf("parse schedule fail\n");
        SetError(schedule_format_error);
        return -1;
    }

    schedule_num = cJSON_GetArraySize(scheduleObj);
    if (schedule_num != 7)
    {
        myprintf("schedule days is not equal 7\n");
        SetError(schedule_days_not_enough);
        return -1;
    }

    for (int i = 0; i < schedule_num; i++)
    {
        cJSON *scheduleDay = cJSON_GetArrayItem(scheduleObj, i);
        schedule_day_num = cJSON_GetArraySize(scheduleDay);
        if (!schedule_day_num)
        {
            myprintf("schedule day item is 0\n");
            SetError(schedule_format_error);
            return -1;
        }

        daySchedule.clear();
        for (int j = 0; j < schedule_day_num; j++)
        {
            cJSON *scheduleDayItem = cJSON_GetArrayItem(scheduleDay, j);
            if (ScheduleFormatCheck(scheduleDayItem->valuestring) < 0)
            {
                myprintf("schedule format error\n");
                SetError(schedule_format_error);
                return -1;
            }
            daySchedule.push_back(scheduleDayItem->valuestring);
        }

        weekSchedule.push_back(daySchedule);
    }

    TranslateScheduleToTm(weekSchedule, schedule);

    return 0;
}

int32_t ParseKeyFrame(uint8_t *data, uint32_t size)
{
    uint32_t total = 0;

    if (!data)
        return -1;

    while (total < size)
    {
        uint32_t seperator1 = *(uint32_t *)data;
        uint32_t seperator2 = (seperator1 & 0x00ffffff);
        if (seperator1 != 0x01000000 && seperator2 != 0x00010000)
        {
            data++;
            total++;
            continue;
        }

        uint32_t off = sizeof(seperator1);
        if (seperator2 == 0x00010000)
            off--;

        uint8_t flag = (data[off] & 0x1f);
        if (flag == 0x05)
            return 0;
        else
        {
            total += (off + 1);
            data += (off + 1);
        }          
    }
}