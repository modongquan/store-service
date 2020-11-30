#include "http.h"
#include "media.h"
#include "backup.h"

#define HTTP_REQ_STRING_LEN 256

struct http_req_ctx
{
    char req[HTTP_REQ_STRING_LEN];
    char *(*req_handle)(char *);
};

const char http_ver[] = {"HTTP/1.1"};
const char res_head[] = {
    "Content-Type:application/json\r\n\r\n"};

extern std::vector<std::map<std::string, std::string>> cameraInfoList;
extern void DeleteCameraInfoInList(std::string &camera_ip);
extern void StorePlannedStream(const char *streamIp, uint8_t *data, uint32_t size, uint32_t flag);
extern void StoreManualStream(const char *streamIp, uint8_t *data, uint32_t size, uint32_t flag);
extern void StoreAlarmStream(const char *streamIp, uint8_t *data, uint32_t size, uint32_t flag);

char *CreateResponse(const char *line, const char *head, const char *body)
{
    if (!line || !head || !body)
        return nullptr;

    uint32_t resSize = strlen(line) + strlen(head) + strlen(body) + 1;
    char *pRes = (char *)malloc(resSize);
    snprintf(pRes, resSize, "%s%s%s", line, head, body);
    myprintf("%s\n", pRes);

    free((void *)body);

    return pRes;
}

char *ParsePostData(char *info)
{
    const char start_str[] = {"\r\n\r\n"};

    char *start = strstr(info, start_str);
    if (!start)
        return nullptr;

    start += strlen(start_str);
    return start;
}

int ParseContentLen(char *info)
{
    const char content_len[] = {"Content-Length: "};
    char len[128];
    int idx = 0;

    if (!info)
        return -1;

    char *start = strstr(info, content_len);
    if (!start)
        return -1;

    start += strlen(content_len);
    while (*start != '\r')
    {
        len[idx++] = *start++;
        if (idx >= sizeof(len))
            return -1;
    }

    return atoi(len);
}

cJSON *CreateDefaultSchedule(void)
{
    cJSON *scheArray = cJSON_CreateArray();

    for (int i = 0; i < 7; i++)
    {
        cJSON *dayArray = cJSON_CreateArray();
        cJSON_AddItemToArray(dayArray, cJSON_CreateString("00:00:00-00:00:00"));
        cJSON_AddItemToArray(scheArray, dayArray);
    }

    return scheArray;
}

char *CreateRespondString(int code, const char *message)
{
    cJSON *resBody = nullptr;
    char *res = nullptr;

    if (!message)
        return nullptr;

    resBody = cJSON_CreateObject();
    if (!resBody)
        return nullptr;

    cJSON_AddItemToObject(resBody, "code", cJSON_CreateBool(code));
    cJSON_AddItemToObject(resBody, "message", cJSON_CreateString(message));

    res = cJSON_Print(resBody);
    cJSON_Delete(resBody);

    return res;
}
char *AddRecodeDevice(char *info)
{
    std::string devId;
    std::string storeType(RECODE_TYPE_PLANNED);
    std::string name;
    std::string rec_schedule_cjson, bak_schedule_cjson;
    std::map<std::string, std::string> camera_info;
    std::vector<std::vector<std::vector<struct tm>>> rec_schedule;
    std::vector<std::vector<std::vector<struct tm>>> bak_schedule;

    cJSON *devInfoObj = nullptr;
    cJSON *devMainIdObj = nullptr;
    cJSON *devSubIdObj = nullptr;
    cJSON *devNameObj = nullptr;
    cJSON *devRecScheduleObj = nullptr;
    cJSON *devBakScheduleObj = nullptr;

    char *res = nullptr, *data = nullptr, *rec_schedule_start = nullptr, *bak_schedule_start = nullptr;
    const char *err = nullptr;
    char str[125];
    int code = true;
    uint8_t isRecScheCreated = 0, isBakScheCreated = 0;

    data = ParsePostData(info);
    if (!data)
    {
        myprintf("do not find post data\n");
        SetError(no_post_data);
        err = GetError;
        goto quit;
    }

    devInfoObj = cJSON_Parse(data);
    if (!devInfoObj)
    {
        myprintf("post data is invalid json\n");
        SetError(invalid_json_data);
        err = GetError;
        goto quit;
    }

    devMainIdObj = cJSON_GetObjectItem(devInfoObj, "mainId");
    if (!devMainIdObj)
    {
        myprintf("post json do not find \"mainId\" info\n");
        SetError(no_main_id_in_json);
        err = GetError;
        goto quit;
    }

    devSubIdObj = cJSON_GetObjectItem(devInfoObj, "subId");
    if (!devSubIdObj)
    {
        myprintf("post json do not find \"subId\" info\n");
        SetError(no_sub_id_in_json);
        err = GetError;
        goto quit;
    }

    devNameObj = cJSON_GetObjectItem(devInfoObj, "name");
    if (!devNameObj)
    {
        myprintf("post json do not find \"name\" info\n");
        SetError(no_dev_name_in_json);
        err = GetError;
        goto quit;
    }

    devRecScheduleObj = cJSON_GetObjectItem(devInfoObj, REC_SCHE_JSON_NAME);
    if (!devRecScheduleObj)
    {
        devRecScheduleObj = CreateDefaultSchedule();
        if (!devRecScheduleObj)
        {
            myprintf("create default \"%s\" fail\n", REC_SCHE_JSON_NAME);
            SetError(create_def_sche_fail);
            err = GetError;
            goto quit;
        }
        isRecScheCreated = 1;
    }

    devBakScheduleObj = cJSON_GetObjectItem(devInfoObj, BAK_SCHE_JSON_NAME);
    if (!devBakScheduleObj)
    {
        devBakScheduleObj = CreateDefaultSchedule();
        if (!devBakScheduleObj)
        {
            myprintf("create default \"%s\" fail\n", BAK_SCHE_JSON_NAME);
            SetError(create_def_sche_fail);
            err = GetError;
            goto quit;
        }
        isBakScheCreated = 1;
    }

    devId = devMainIdObj->valuestring;
    devId += "-";
    devId += devSubIdObj->valuestring;
    name = devNameObj->valuestring;

    rec_schedule_start = cJSON_Print(devRecScheduleObj);
    rec_schedule_cjson.assign(rec_schedule_start);

    bak_schedule_start = cJSON_Print(devBakScheduleObj);
    bak_schedule_cjson.assign(bak_schedule_start);

    if (ParseSchedule(rec_schedule_cjson.c_str(), rec_schedule) < 0)
    {
        err = GetError;
        goto quit;
    }
    if (ParseSchedule(bak_schedule_cjson.c_str(), bak_schedule) < 0)
    {
        err = GetError;
        goto quit;
    }

    if (CreateCameraTable(devId) < 0)
    {
        err = GetOtherError;
        goto quit;
    }
    if (InsertCameraInfoToTable(devId, name, rec_schedule_cjson, bak_schedule_cjson) < 0)
    {
        err = GetOtherError;
        goto quit;
    }
    if (AddBakSchedule(devId, bak_schedule) < 0)
    {
        err = GetError;
        goto delete_camera_info;
    }
    if (AddStoreDevice(devId, storeType) < 0)
    {
        err = GetError;
        goto delete_bak_sche;
    }
    if (AddStream(devId.c_str(), rec_schedule, StorePlannedStream) < 0)
    {
        err = GetError;
        goto remove_store_dev;
    }

    camera_info.insert(camera_info.end(), std::pair<std::string, std::string>(std::string(CAMERA_ID_MYSQL_NAME), devId));
    camera_info.insert(camera_info.end(), std::pair<std::string, std::string>(std::string(CAMERA_NAME_MYSQL_NAME), name));
    camera_info.insert(camera_info.end(), std::pair<std::string, std::string>(std::string(REC_SCHE_JSON_NAME), rec_schedule_cjson));
    camera_info.insert(camera_info.end(), std::pair<std::string, std::string>(std::string(BAK_SCHE_JSON_NAME), bak_schedule_cjson));
    cameraInfoList.push_back(camera_info);

    myprintf("add device %s successful\n", devId.c_str());
    goto quit;

remove_store_dev:
    RemoveStoreDevice(devId);

delete_bak_sche:
    RemoveBakSchedule(devId);

delete_camera_info:
    DeleteCameraInfoInTable(devId);

quit:
    snprintf(str, sizeof(str), "添加摄像机(%s)%s%s", (!devId.empty()) ? devId.c_str() : "Id未知道", (!err) ? "成功!" : "失败:", (!err) ? " " : err);
    if (err)
        code = false;
    res = CreateRespondString(code, str);

    if (devRecScheduleObj && isRecScheCreated)
        cJSON_Delete(devRecScheduleObj);
    if (devBakScheduleObj && isBakScheCreated)
        cJSON_Delete(devBakScheduleObj);
    if (devInfoObj)
        cJSON_Delete(devInfoObj);
    if (rec_schedule_start)
        free(rec_schedule_start);
    if (bak_schedule_start)
        free(bak_schedule_start);

    return res;
}

char *DeleteRecodeDevice(char *info)
{
    std::string cameraId;
    const char *err = nullptr;
    char *data = nullptr, *res = nullptr;
    char str[125];
    int code = true;

    cJSON *devInfoObj = nullptr;
    cJSON *devMainIdObj = nullptr;
    cJSON *devSubIdObj = nullptr;

    if (!info)
        return nullptr;

    data = ParsePostData(info);
    if (!data)
    {
        myprintf("do not find post data\n");
        SetError(no_post_data);
        err = GetError;
        goto quit;
    }

    devInfoObj = cJSON_Parse(data);
    if (!devInfoObj)
    {
        myprintf("post data is invalid json\n");
        SetError(invalid_json_data);
        err = GetError;
        goto quit;
    }

    devMainIdObj = cJSON_GetObjectItem(devInfoObj, "mainId");
    if (!devMainIdObj)
    {
        myprintf("post json do not find \"mainId\" info\n");
        SetError(no_main_id_in_json);
        err = GetError;
        goto quit;
    }

    devSubIdObj = cJSON_GetObjectItem(devInfoObj, "subId");
    if (!devSubIdObj)
    {
        myprintf("post json do not find \"subId\" info\n");
        SetError(no_sub_id_in_json);
        err = GetError;
        goto quit;
    }

    cameraId = cameraId + devMainIdObj->valuestring + "-" + devSubIdObj->valuestring;

    if (DeleteCameraInfoInTable(cameraId) < 0)
    {
        err = GetOtherError;
        goto quit;
    }
    if (DeleteStream(cameraId.c_str()) < 0)
    {
        err = GetError;
    }
    if (RemoveStoreDevice(cameraId) < 0)
    {
        err = GetError;
    }
    RemoveBakSchedule(cameraId);
    DeleteCameraInfoInList(cameraId);

quit:
    snprintf(str, sizeof(str), "删除摄像机(%s)%s%s", cameraId.c_str(), (!err) ? "成功!" : "失败:", (!err) ? " " : err);
    if (err)
        code = false;
    res = CreateRespondString(code, str);

    return res;
}

char *UpdateSchedule(char *info)
{
    std::string devId, cameraIdInInfo;
    std::string rec_schedule_cjson, bak_schedule_cjson;
    std::map<std::string, std::string> camera_info;
    std::vector<std::vector<std::vector<struct tm>>> rec_schedule;
    std::vector<std::vector<std::vector<struct tm>>> bak_schedule;

    cJSON *devInfoObj = nullptr;
    cJSON *devMainIdObj = nullptr;
    cJSON *devSubIdObj = nullptr;
    cJSON *devRecScheduleObj = nullptr;
    cJSON *devBakScheduleObj = nullptr;

    char *res = nullptr, *data = nullptr, *rec_sche_start = nullptr, *bak_sche_start = nullptr;
    const char *err = nullptr;
    char str[125];
    int code = true;
    int cameraIdx = -1;

    if (!info)
        return nullptr;

    data = ParsePostData(info);
    if (!data)
    {
        myprintf("do not find post data\n");
        SetError(no_post_data);
        err = GetError;
        goto quit;
    }

    devInfoObj = cJSON_Parse(data);
    if (!devInfoObj)
    {
        myprintf("post data is invalid json\n");
        SetError(invalid_json_data);
        err = GetError;
        goto quit;
    }

    devMainIdObj = cJSON_GetObjectItem(devInfoObj, "mainId");
    if (!devMainIdObj)
    {
        myprintf("post json do not find \"mainId\" info\n");
        SetError(no_main_id_in_json);
        err = GetError;
        goto quit;
    }

    devSubIdObj = cJSON_GetObjectItem(devInfoObj, "subId");
    if (!devSubIdObj)
    {
        myprintf("post json do not find \"subId\" info\n");
        SetError(no_sub_id_in_json);
        err = GetError;
        goto quit;
    }

    devId = devId + devMainIdObj->valuestring + "-" + devSubIdObj->valuestring;

    for (int i = 0; i < cameraInfoList.size(); i++)
    {
        cameraIdInInfo = cameraInfoList[i][std::string(CAMERA_ID_MYSQL_NAME)];
        if (cameraIdInInfo != devId)
            continue;
        cameraIdx = i;
        break;
    }
    if (cameraIdx < 0)
    {
        myprintf("do not find camera(%s)\n", devId.c_str());
        SetError(do_not_find_camera);
        err = GetError;
        goto quit;
    }

    devRecScheduleObj = cJSON_GetObjectItem(devInfoObj, REC_SCHE_JSON_NAME);
    devBakScheduleObj = cJSON_GetObjectItem(devInfoObj, BAK_SCHE_JSON_NAME);
    if (!devRecScheduleObj && !devBakScheduleObj)
    {
        myprintf("post json do not find schedule info\n");
        SetError(no_schedule_in_json);
        err = GetError;
        goto quit;
    }

    if (devRecScheduleObj)
    {
        rec_sche_start = cJSON_Print(devRecScheduleObj);
        rec_schedule_cjson.assign(rec_sche_start);

        if (ParseSchedule(rec_schedule_cjson.c_str(), rec_schedule) < 0)
        {
            err = GetError;
            goto quit;
        }

        if (UpdateRecScheduleToCameraTable(devId, rec_schedule_cjson) < 0)
        {
            err = GetOtherError;
            goto quit;
        }

        if (UpdateRecodeSchedule(devId, rec_schedule) < 0)
        {
            err = GetError;
        }

        cameraInfoList[cameraIdx][std::string(REC_SCHE_JSON_NAME)] = rec_schedule_cjson;
    }

    if (devBakScheduleObj)
    {
        bak_sche_start = cJSON_Print(devBakScheduleObj);
        bak_schedule_cjson.assign(bak_sche_start);

        if (ParseSchedule(bak_schedule_cjson.c_str(), bak_schedule) < 0)
        {
            err = GetError;
            goto quit;
        }

        if (UpdateBakScheduleToCameraTable(devId, bak_schedule_cjson) < 0)
        {
            err = GetOtherError;
            goto quit;
        }

        if (UpdateBakSchedule(devId, bak_schedule) < 0)
        {
            err = GetError;
        }

        cameraInfoList[cameraIdx][std::string(BAK_SCHE_JSON_NAME)] = bak_schedule_cjson;
    }

quit:
    snprintf(str, sizeof(str), "更新摄像机(%s)计划%s%s", devId.c_str(), (!err) ? "成功!" : "失败:", (!err) ? " " : err);
    if (err)
        code = false;
    res = CreateRespondString(code, str);

    if (devInfoObj)
        cJSON_Delete(devInfoObj);
    if (rec_sche_start)
        free(rec_sche_start);
    if (bak_sche_start)
        free(bak_sche_start);

    return res;
}

char *GetSchedule(char *info)
{
    std::string cameraId, scheduleType, cameraIdInInfo, scheduleJson;
    std::vector<std::vector<std::vector<struct tm>>> schedule;
    const char *err = nullptr;
    char *data = nullptr, *res = nullptr;
    char message[128];
    int scheDayTimeNum = 0, code = true;
    int typeId;

    cJSON *devInfoObj = nullptr;
    cJSON *devMainIdObj = nullptr;
    cJSON *devSubIdObj = nullptr;
    cJSON *scheTypeObj = nullptr;
    cJSON *scheTypeIdObj = nullptr;

    cJSON *scheParsObj = nullptr;
    cJSON *scheParsDayObj = nullptr;
    cJSON *scheDayTimeObj = nullptr;

    cJSON *scheduleObj = nullptr;
    cJSON *mondayObj = nullptr;
    cJSON *tuesdayObj = nullptr;
    cJSON *wednesdayObj = nullptr;
    cJSON *thursdayObj = nullptr;
    cJSON *fridayObj = nullptr;
    cJSON *saturdayObj = nullptr;
    cJSON *sundayObj = nullptr;

    cJSON *resObj = nullptr;

    if (!info)
        return nullptr;

    data = ParsePostData(info);
    if (!data)
    {
        myprintf("do not find post data\n");
        SetError(no_post_data);
        err = GetError;
        goto quit;
    }

    devInfoObj = cJSON_Parse(data);
    if (!devInfoObj)
    {
        myprintf("post data is invalid json\n");
        SetError(invalid_json_data);
        err = GetError;
        goto quit;
    }

    devMainIdObj = cJSON_GetObjectItem(devInfoObj, MAIN_ID_JSON_NAME);
    if (!devMainIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", MAIN_ID_JSON_NAME);
        SetError(no_main_id_in_json);
        err = GetError;
        goto quit;
    }

    devSubIdObj = cJSON_GetObjectItem(devInfoObj, SUB_ID_JSON_NAME);
    if (!devSubIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", SUB_ID_JSON_NAME);
        SetError(no_sub_id_in_json);
        err = GetError;
        goto quit;
    }

    scheTypeObj = cJSON_GetObjectItem(devInfoObj, TYPE_JSON_NAME);
    scheTypeIdObj = cJSON_GetObjectItem(devInfoObj, TYPE_ID_JSON_NAME);
    if (!scheTypeObj && !scheTypeIdObj)
    {
        myprintf("post json do not find \"%s\" or \"%s\" info\n", TYPE_ID_JSON_NAME, TYPE_JSON_NAME);
        SetError(no_type_id_in_json);
        err = GetError;
        goto quit;
    }
    if (scheTypeIdObj)
    {
        typeId = scheTypeIdObj->valueint;
    }
    else if (scheTypeObj)
    {
        if (!strcmp(scheTypeObj->valuestring, "recode"))
            typeId = 0;
        else if (!strcmp(scheTypeObj->valuestring, "backup"))
            typeId = 1;
        else
        {
            myprintf("invalid schedule type in post json\n");
            SetError(invalid_schedule_type_in_json);
            err = GetError;
            goto quit;
        }
    }

    cameraId = cameraId + devMainIdObj->valuestring + "-" + devSubIdObj->valuestring;

    if (0 == typeId)
        scheduleType = REC_SCHE_JSON_NAME;
    else if (1 == typeId)
        scheduleType = BAK_SCHE_JSON_NAME;
    else
    {
        myprintf("invalid schedule type in post json\n");
        SetError(invalid_schedule_type_in_json);
        err = GetError;
        goto quit;
    }

    for (int i = 0; i < cameraInfoList.size(); i++)
    {
        cameraIdInInfo = cameraInfoList[i][std::string(CAMERA_ID_MYSQL_NAME)];
        if (cameraIdInInfo != cameraId)
            continue;
        scheduleJson = cameraInfoList[i][scheduleType];
        break;
    }
    if (scheduleJson.empty())
    {
        myprintf("do not find camera(%s)\n", cameraId.c_str());
        SetError(do_not_find_camera);
        err = GetError;
        goto quit;
    }

    scheParsObj = cJSON_Parse(scheduleJson.c_str());
    if (!scheParsObj)
    {
        myprintf("cJSON_Parse error\n");
        SetError(cjson_called_error);
        err = GetError;
        goto quit;
    }

    scheduleObj = cJSON_CreateObject();
    if (!scheduleObj)
    {
        myprintf("cJSON_CreateObject error\n");
        SetError(cjson_called_error);
        err = GetError;
        goto quit;
    }

    cJSON_AddItemToObject(scheduleObj, MAIN_ID_JSON_NAME, cJSON_CreateString(devMainIdObj->valuestring));
    cJSON_AddItemToObject(scheduleObj, SUB_ID_JSON_NAME, cJSON_CreateString(devSubIdObj->valuestring));

    scheParsDayObj = cJSON_GetArrayItem(scheParsObj, 0);
    scheDayTimeNum = cJSON_GetArraySize(scheParsDayObj);
    mondayObj = cJSON_CreateArray();
    for (int i = 0; i < scheDayTimeNum; i++)
    {
        scheDayTimeObj = cJSON_GetArrayItem(scheParsDayObj, i);
        cJSON_AddItemToArray(mondayObj, cJSON_CreateString(scheDayTimeObj->valuestring));
    }
    cJSON_AddItemToObject(scheduleObj, "monday", mondayObj);

    scheParsDayObj = cJSON_GetArrayItem(scheParsObj, 1);
    scheDayTimeNum = cJSON_GetArraySize(scheParsDayObj);
    tuesdayObj = cJSON_CreateArray();
    for (int i = 0; i < scheDayTimeNum; i++)
    {
        scheDayTimeObj = cJSON_GetArrayItem(scheParsDayObj, i);
        cJSON_AddItemToArray(tuesdayObj, cJSON_CreateString(scheDayTimeObj->valuestring));
    }
    cJSON_AddItemToObject(scheduleObj, "tuesday", tuesdayObj);

    scheParsDayObj = cJSON_GetArrayItem(scheParsObj, 2);
    scheDayTimeNum = cJSON_GetArraySize(scheParsDayObj);
    wednesdayObj = cJSON_CreateArray();
    for (int i = 0; i < scheDayTimeNum; i++)
    {
        scheDayTimeObj = cJSON_GetArrayItem(scheParsDayObj, i);
        cJSON_AddItemToArray(wednesdayObj, cJSON_CreateString(scheDayTimeObj->valuestring));
    }
    cJSON_AddItemToObject(scheduleObj, "wednesday", wednesdayObj);

    scheParsDayObj = cJSON_GetArrayItem(scheParsObj, 3);
    scheDayTimeNum = cJSON_GetArraySize(scheParsDayObj);
    thursdayObj = cJSON_CreateArray();
    for (int i = 0; i < scheDayTimeNum; i++)
    {
        scheDayTimeObj = cJSON_GetArrayItem(scheParsDayObj, i);
        cJSON_AddItemToArray(thursdayObj, cJSON_CreateString(scheDayTimeObj->valuestring));
    }
    cJSON_AddItemToObject(scheduleObj, "thursday", thursdayObj);

    scheParsDayObj = cJSON_GetArrayItem(scheParsObj, 4);
    scheDayTimeNum = cJSON_GetArraySize(scheParsDayObj);
    fridayObj = cJSON_CreateArray();
    for (int i = 0; i < scheDayTimeNum; i++)
    {
        scheDayTimeObj = cJSON_GetArrayItem(scheParsDayObj, i);
        cJSON_AddItemToArray(fridayObj, cJSON_CreateString(scheDayTimeObj->valuestring));
    }
    cJSON_AddItemToObject(scheduleObj, "friday", fridayObj);

    scheParsDayObj = cJSON_GetArrayItem(scheParsObj, 5);
    scheDayTimeNum = cJSON_GetArraySize(scheParsDayObj);
    saturdayObj = cJSON_CreateArray();
    for (int i = 0; i < scheDayTimeNum; i++)
    {
        scheDayTimeObj = cJSON_GetArrayItem(scheParsDayObj, i);
        cJSON_AddItemToArray(saturdayObj, cJSON_CreateString(scheDayTimeObj->valuestring));
    }
    cJSON_AddItemToObject(scheduleObj, "saturday", saturdayObj);

    scheParsDayObj = cJSON_GetArrayItem(scheParsObj, 6);
    scheDayTimeNum = cJSON_GetArraySize(scheParsDayObj);
    sundayObj = cJSON_CreateArray();
    for (int i = 0; i < scheDayTimeNum; i++)
    {
        scheDayTimeObj = cJSON_GetArrayItem(scheParsDayObj, i);
        cJSON_AddItemToArray(sundayObj, cJSON_CreateString(scheDayTimeObj->valuestring));
    }
    cJSON_AddItemToObject(scheduleObj, "sunday", sundayObj);

    resObj = cJSON_CreateObject();
    cJSON_AddItemToObject(resObj, "code", cJSON_CreateBool(code));
    cJSON_AddItemToObject(resObj, "schedule", scheduleObj);

quit:
    if (!err)
    {
        res = cJSON_Print(resObj);
    }
    else
    {
        snprintf(message, sizeof(message), "获取摄像机(%s)计划失败:%s", cameraId.c_str(), err);
        if (err)
            code = false;
        res = CreateRespondString(code, message);
    }

    if (devInfoObj)
        cJSON_Delete(devInfoObj);
    if (scheParsObj)
        cJSON_Delete(scheParsObj);
    if (scheduleObj)
    {
        if (resObj)
            cJSON_Delete(resObj);
        else
            cJSON_Delete(scheduleObj);
    }

    return res;
}

char *EventRecode(char *info)
{
    std::string cameraId, startTime, endTime, storeType;
    char message[128];
    char *data = nullptr, *res = nullptr;
    const char *err = nullptr;
    int code = true;
    struct tm startTm, endTm;

    cJSON *postDataObj = nullptr;
    cJSON *mainIdObj = nullptr;
    cJSON *subIdObj = nullptr;
    cJSON *startTimeObj = nullptr;
    cJSON *endTimeObj = nullptr;
    cJSON *typeIdObj = nullptr;

    std::string deviceId;

    if (!info)
        return nullptr;

    data = ParsePostData(info);
    if (!data)
    {
        myprintf("do not find post data\n");
        SetError(no_post_data);
        err = GetError;
        goto quit;
    }

    postDataObj = cJSON_Parse(data);
    if (!postDataObj)
    {
        myprintf("post data is invalid json\n");
        SetError(invalid_json_data);
        err = GetError;
        goto quit;
    }

    mainIdObj = cJSON_GetObjectItem(postDataObj, MAIN_ID_JSON_NAME);
    if (!mainIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", MAIN_ID_JSON_NAME);
        SetError(no_main_id_in_json);
        err = GetError;
        goto quit;
    }

    subIdObj = cJSON_GetObjectItem(postDataObj, SUB_ID_JSON_NAME);
    if (!subIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", SUB_ID_JSON_NAME);
        SetError(no_sub_id_in_json);
        err = GetError;
        goto quit;
    }

    startTimeObj = cJSON_GetObjectItem(postDataObj, START_TIME_JSON_NAME);
    if (!startTimeObj)
    {
        myprintf("post json do not find \"%s\" info\n", START_TIME_JSON_NAME);
        SetError(no_start_time_in_json);
        err = GetError;
        goto quit;
    }

    endTimeObj = cJSON_GetObjectItem(postDataObj, END_TIME_JSON_NAME);
    if (!startTimeObj)
    {
        myprintf("post json do not find \"%s\" info\n", END_TIME_JSON_NAME);
        SetError(no_end_time_in_json);
        err = GetError;
        goto quit;
    }

    typeIdObj = cJSON_GetObjectItem(postDataObj, EVENT_ID_JSON_NAME);
    if (!typeIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", EVENT_ID_JSON_NAME);
        SetError(no_type_id_in_json);
        err = GetError;
        goto quit;
    }

    cameraId = cameraId + mainIdObj->valuestring + "-" + subIdObj->valuestring;
    startTime = startTimeObj->valuestring;
    endTime = endTimeObj->valuestring;

    if (TransDateToTm(startTime, startTm) < 0 || TransDateToTm(endTime, endTm) < 0)
    {
        myprintf("invalid date format in json\n");
        SetError(invalid_date_format_in_json);
        err = GetError;
        goto quit;
    }

    if (1 == typeIdObj->valueint)
    {
        storeType = RECODE_TYPE_MANUAL;
        if (AddStoreEvent(cameraId, storeType) < 0)
        {
            err = GetError;
            goto quit;
        }

        if (AddStreamEvent(cameraId, startTm, endTm, StoreManualStream) < 0)
        {
            err = GetError;
            goto remove_store_dev;
        }
    }
    else if (2 == typeIdObj->valueint)
    {
        storeType = RECODE_TYPE_ALARM;
        if (AddStoreEvent(cameraId, storeType) < 0)
        {
            err = GetError;
            goto quit;
        }

        if (AddStreamEvent(cameraId, startTm, endTm, StoreAlarmStream) < 0)
        {
            err = GetError;
            goto remove_store_dev;
        }
    }
    else
    {
        printf("invalid event type in json\n");
        SetError(invalid_event_type_in_json);
        err = GetError;
        goto quit;
    }
    goto quit;

remove_store_dev:
    RemoveStoreEvent(cameraId, storeType);

quit:
    snprintf(message, sizeof(message), "%s%s", (!err) ? "成功!" : "失败:", (!err) ? " " : err);
    if (err)
        code = false;
    res = CreateRespondString(code, message);

    cJSON_Delete(postDataObj);

    return res;
}

char *OperateFile(char *info)
{
    char message[128];
    char *data = nullptr, *res = nullptr;
    const char *err = nullptr;
    int code = true;

    cJSON *postDataObj = nullptr;
    cJSON *mainIdObj = nullptr;
    cJSON *subIdObj = nullptr;
    cJSON *operateFileObj = nullptr;
    cJSON *operateIdObj = nullptr;

    std::string deviceId;
    std::string operateFile;
    std::string status;

    if (!info)
        return nullptr;

    data = ParsePostData(info);
    if (!data)
    {
        myprintf("do not find post data\n");
        SetError(no_post_data);
        err = GetError;
        goto quit;
    }

    postDataObj = cJSON_Parse(data);
    if (!postDataObj)
    {
        myprintf("post data is invalid json\n");
        SetError(invalid_json_data);
        err = GetError;
        goto quit;
    }

    mainIdObj = cJSON_GetObjectItem(postDataObj, MAIN_ID_JSON_NAME);
    if (!mainIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", MAIN_ID_JSON_NAME);
        SetError(no_main_id_in_json);
        err = GetError;
        goto quit;
    }

    subIdObj = cJSON_GetObjectItem(postDataObj, SUB_ID_JSON_NAME);
    if (!subIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", SUB_ID_JSON_NAME);
        SetError(no_sub_id_in_json);
        err = GetError;
        goto quit;
    }

    operateFileObj = cJSON_GetObjectItem(postDataObj, FILE_NAME_JSON_NAME);
    if (!operateFileObj)
    {
        myprintf("post json do not find \"%s\" info\n", FILE_NAME_JSON_NAME);
        SetError(no_file_name_in_json);
        err = GetError;
        goto quit;
    }

    operateIdObj = cJSON_GetObjectItem(postDataObj, OPERATE_TYPE_JSON_NAME);
    if (!operateIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", OPERATE_TYPE_JSON_NAME);
        SetError(no_operate_type_in_json);
        err = GetError;
        goto quit;
    }

    deviceId = mainIdObj->valuestring;
    deviceId += "-";
    deviceId += subIdObj->valuestring;
    operateFile = operateFileObj->valuestring;

    if (operateIdObj->valueint == 2)
    {
        if (BackupClientStart(deviceId, operateFile) < 0)
        {
            err = "手动备份失败";
            goto quit;
        }
    }
    else if (operateIdObj->valueint == 0)
    {
        status = RECODE_FILE_STATUS_LOCK;
        if (UpdateStatusToTable(deviceId, operateFile, status) < 0)
        {
            err = GetOtherError;
            goto quit;
        }
    }
    else if (operateIdObj->valueint == 1)
    {
        status = RECODE_FILE_STATUS_UNLOCK;
        if (UpdateStatusToTable(deviceId, operateFile, status) < 0)
        {
            err = GetOtherError;
            goto quit;
        }
    }
    else
    {
        myprintf("invalid file operate type in json\n");
        SetError(invalid_file_operate_type);
        err = GetError;
        goto quit;
    }

quit:
    snprintf(message, sizeof(message), "%s%s", (!err) ? "成功!" : "失败:", (!err) ? " " : err);
    if (err)
        code = false;
    res = CreateRespondString(code, message);

    cJSON_Delete(postDataObj);

    return res;
}

char *GetRecodeList(char *info)
{
    std::string cameraId, startTime, endTime, recodeType;
    std::vector<struct mysqlstoreinfo> recordeList;
    const char *err = nullptr;
    char *data = nullptr;
    char str[128];
    int ret = 0;

    cJSON *postDataObj = nullptr;
    cJSON *mainIdObj = nullptr;
    cJSON *subIdObj = nullptr;
    cJSON *startTimeObj = nullptr;
    cJSON *endTimeObj = nullptr;
    cJSON *typeIdObj = nullptr;
    cJSON *resBody = nullptr;
    cJSON *recListJson = nullptr;
    cJSON *recFileObj = nullptr;
    cJSON *recFileNameObj = nullptr;

    if (!info)
        return nullptr;

    data = ParsePostData(info);
    if (!data)
    {
        myprintf("do not find post data\n");
        SetError(no_post_data);
        err = GetError;
        goto quit;
    }

    postDataObj = cJSON_Parse(data);
    if (!postDataObj)
    {
        myprintf("post data is invalid json\n");
        SetError(invalid_json_data);
        err = GetError;
        goto quit;
    }

    mainIdObj = cJSON_GetObjectItem(postDataObj, MAIN_ID_JSON_NAME);
    if (!mainIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", MAIN_ID_JSON_NAME);
        SetError(no_main_id_in_json);
        err = GetError;
        goto quit;
    }

    subIdObj = cJSON_GetObjectItem(postDataObj, SUB_ID_JSON_NAME);
    if (!subIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", SUB_ID_JSON_NAME);
        SetError(no_sub_id_in_json);
        err = GetError;
        goto quit;
    }

    startTimeObj = cJSON_GetObjectItem(postDataObj, START_TIME_JSON_NAME);
    if (!startTimeObj)
    {
        myprintf("post json do not find \"%s\" info\n", START_TIME_JSON_NAME);
        SetError(no_start_time_in_json);
        err = GetError;
        goto quit;
    }

    endTimeObj = cJSON_GetObjectItem(postDataObj, END_TIME_JSON_NAME);
    if (!startTimeObj)
    {
        myprintf("post json do not find \"%s\" info\n", END_TIME_JSON_NAME);
        SetError(no_end_time_in_json);
        err = GetError;
        goto quit;
    }

    typeIdObj = cJSON_GetObjectItem(postDataObj, TYPE_ID_JSON_NAME);
    if (!typeIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", TYPE_ID_JSON_NAME);
        SetError(no_type_id_in_json);
        err = GetError;
        goto quit;
    }
    if (0 == typeIdObj->valueint)
    {
        recodeType = RECODE_TYPE_PLANNED;
    }
    else if (1 == typeIdObj->valueint)
    {
        recodeType = RECODE_TYPE_MANUAL;
    }
    else if (2 == typeIdObj->valueint)
    {
        recodeType = RECODE_TYPE_ALARM;
    }
    else
    {
        myprintf("invalid %s in json\n", TYPE_ID_JSON_NAME);
        SetError(invalid_type_id_in_json);
        err = GetError;
        goto quit;
    }

    cameraId = cameraId + mainIdObj->valuestring + "-" + subIdObj->valuestring;
    startTime = startTimeObj->valuestring;
    endTime = endTimeObj->valuestring;

    ret = QueryInfoByDate(cameraId, startTime, endTime, recordeList);
    if (ret < 0)
    {
        err = GetError;
        goto quit;
    }
    else if (ret > 0)
    {
        err = GetOtherError;
        goto quit;
    }

quit:
    resBody = cJSON_CreateObject();
    if (!resBody)
        return nullptr;

    if (!err)
    {
        recListJson = cJSON_CreateArray();
        if (!recListJson)
            return nullptr;

        for (int i = 0; i < recordeList.size(); i++)
        {
            recFileObj = cJSON_CreateObject();
            if (recFileObj)
            {
                if (recodeType != recordeList[i].file_type)
                    continue;
                cJSON_AddItemToObject(recFileObj, CAMERA_NAME_MYSQL_NAME, cJSON_CreateString(recordeList[i].file_name.c_str()));
                cJSON_AddItemToObject(recFileObj, CAMERA_TYPE_MYSQL_NAME, cJSON_CreateString(recordeList[i].file_type.c_str()));
                cJSON_AddItemToObject(recFileObj, CAMERA_STATUS_MYSQL_NAME, cJSON_CreateString(recordeList[i].file_status.c_str()));
                cJSON_AddItemToObject(recFileObj, CAMERA_BACKUP_MYSQL_NAME, cJSON_CreateString(recordeList[i].file_backup.c_str()));
                cJSON_AddItemToArray(recListJson, recFileObj);
            }
        }

        cJSON_AddItemToObject(resBody, "code", cJSON_CreateBool(true));
        cJSON_AddItemToObject(resBody, "message", recListJson);
    }
    else
    {
        cJSON_AddItemToObject(resBody, "code", cJSON_CreateBool(false));
        snprintf(str, sizeof(str), "(%s)查询失败:%s", cameraId.c_str(), err);
        cJSON_AddItemToObject(resBody, "message", cJSON_CreateString(str));
    }

    char *res = cJSON_Print(resBody);
    cJSON_Delete(resBody);
    if (postDataObj)
        cJSON_Delete(postDataObj);

    return res;
}

char *GetBackupList(char *info)
{
    std::string cameraId, startTime, endTime;
    std::vector<struct mysqlstoreinfo> recordeList;
    const char *err = nullptr;
    char *data = nullptr;
    char str[128];
    int ret = 0;

    cJSON *postDataObj = nullptr;
    cJSON *mainIdObj = nullptr;
    cJSON *subIdObj = nullptr;
    cJSON *startTimeObj = nullptr;
    cJSON *endTimeObj = nullptr;
    cJSON *typeIdObj = nullptr;
    cJSON *resBody = nullptr;
    cJSON *recListJson = nullptr;
    cJSON *recFileObj = nullptr;
    cJSON *recFileNameObj = nullptr;

    if (!info)
        return nullptr;

    data = ParsePostData(info);
    if (!data)
    {
        myprintf("do not find post data\n");
        SetError(no_post_data);
        err = GetError;
        goto quit;
    }

    postDataObj = cJSON_Parse(data);
    if (!postDataObj)
    {
        myprintf("post data is invalid json\n");
        SetError(invalid_json_data);
        err = GetError;
        goto quit;
    }

    mainIdObj = cJSON_GetObjectItem(postDataObj, MAIN_ID_JSON_NAME);
    if (!mainIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", MAIN_ID_JSON_NAME);
        SetError(no_main_id_in_json);
        err = GetError;
        goto quit;
    }

    subIdObj = cJSON_GetObjectItem(postDataObj, SUB_ID_JSON_NAME);
    if (!subIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", SUB_ID_JSON_NAME);
        SetError(no_sub_id_in_json);
        err = GetError;
        goto quit;
    }

    startTimeObj = cJSON_GetObjectItem(postDataObj, START_TIME_JSON_NAME);
    if (!startTimeObj)
    {
        myprintf("post json do not find \"%s\" info\n", START_TIME_JSON_NAME);
        SetError(no_start_time_in_json);
        err = GetError;
        goto quit;
    }

    endTimeObj = cJSON_GetObjectItem(postDataObj, END_TIME_JSON_NAME);
    if (!startTimeObj)
    {
        myprintf("post json do not find \"%s\" info\n", END_TIME_JSON_NAME);
        SetError(no_end_time_in_json);
        err = GetError;
        goto quit;
    }

    // typeIdObj = cJSON_GetObjectItem(postDataObj, TYPE_ID_JSON_NAME);
    // if (!typeIdObj)
    // {
    //     myprintf("post json do not find \"%s\" info\n", TYPE_ID_JSON_NAME);
    //     SetError(no_type_id_in_json);
    //     err = GetError;
    //     goto quit;
    // }

    cameraId = cameraId + mainIdObj->valuestring + "-" + subIdObj->valuestring;
    startTime = startTimeObj->valuestring;
    endTime = endTimeObj->valuestring;

    ret = QueryInfoByDate(cameraId, startTime, endTime, recordeList);
    if (ret < 0)
    {
        err = GetError;
        goto quit;
    }
    else if (ret > 0)
    {
        err = GetOtherError;
        goto quit;
    }

quit:
    resBody = cJSON_CreateObject();
    if (!resBody)
        return nullptr;

    if (!err)
    {
        recListJson = cJSON_CreateArray();
        if (!recListJson)
            return nullptr;

        for (int i = 0; i < recordeList.size(); i++)
        {
            if (recordeList[i].file_backup == std::string("yes"))
            {
                recFileObj = cJSON_CreateObject();
                if (recFileObj)
                {
                    cJSON_AddItemToObject(recFileObj, CAMERA_NAME_MYSQL_NAME, cJSON_CreateString(recordeList[i].file_name.c_str()));
                    cJSON_AddItemToObject(recFileObj, CAMERA_TYPE_MYSQL_NAME, cJSON_CreateString(recordeList[i].file_type.c_str()));
                    cJSON_AddItemToObject(recFileObj, CAMERA_STATUS_MYSQL_NAME, cJSON_CreateString(recordeList[i].file_status.c_str()));
                    cJSON_AddItemToObject(recFileObj, CAMERA_BACKUP_MYSQL_NAME, cJSON_CreateString(recordeList[i].file_backup.c_str()));
                    cJSON_AddItemToArray(recListJson, recFileObj);
                }
            }
        }

        cJSON_AddItemToObject(resBody, "code", cJSON_CreateBool(true));
        cJSON_AddItemToObject(resBody, "message", recListJson);
    }
    else
    {
        cJSON_AddItemToObject(resBody, "code", cJSON_CreateBool(false));
        snprintf(str, sizeof(str), "(%s)查询失败:%s", cameraId.c_str(), err);
        cJSON_AddItemToObject(resBody, "message", cJSON_CreateString(str));
    }

    char *res = cJSON_Print(resBody);
    cJSON_Delete(resBody);
    if (postDataObj)
        cJSON_Delete(postDataObj);

    return res;
}

char *AddPlaybackDev(char *info)
{
    std::string pb_file = storePath, url, cameraId;
    char *data = nullptr, *res = nullptr;
    const char *err = nullptr;
    char message[128];
    int code = true;

    cJSON *postDataObj = nullptr;
    cJSON *mainIdObj = nullptr;
    cJSON *subIdObj = nullptr;
    cJSON *fileNameObj = nullptr;
    cJSON *resObj = nullptr;

    if (!info)
        return nullptr;

    data = ParsePostData(info);
    if (!data)
    {
        myprintf("do not find post data\n");
        SetError(no_post_data);
        err = GetError;
        goto quit;
    }

    postDataObj = cJSON_Parse(data);
    if (!postDataObj)
    {
        myprintf("post data is invalid json\n");
        SetError(invalid_json_data);
        err = GetError;
        goto quit;
    }

    mainIdObj = cJSON_GetObjectItem(postDataObj, MAIN_ID_JSON_NAME);
    if (!mainIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", MAIN_ID_JSON_NAME);
        SetError(no_main_id_in_json);
        err = GetError;
        goto quit;
    }

    subIdObj = cJSON_GetObjectItem(postDataObj, SUB_ID_JSON_NAME);
    if (!subIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", SUB_ID_JSON_NAME);
        SetError(no_sub_id_in_json);
        err = GetError;
        goto quit;
    }

    fileNameObj = cJSON_GetObjectItem(postDataObj, FILE_NAME_JSON_NAME);
    if (!fileNameObj)
    {
        myprintf("post json do not find \"%s\" info\n", FILE_NAME_JSON_NAME);
        SetError(no_url_in_json);
        err = GetError;
        goto quit;
    }

    cameraId = cameraId + mainIdObj->valuestring + "-" + subIdObj->valuestring;
    pb_file = pb_file + cameraId + "/" + fileNameObj->valuestring;
    myprintf("playback file path = %s\n", pb_file.c_str());

    if (TranslateToMp4(pb_file, url) < 0)
    {
        SetError(trans_to_mp4_fail);
        err = GetError;
        goto quit;
    }       

    resObj = cJSON_CreateObject();
    if (!resObj)
    {
        SetError(cjson_called_error);
        err = GetError;
        goto quit;
    }

    cJSON_AddItemToObject(resObj, URL_JSON_NAME, cJSON_CreateString(url.c_str()));
    cJSON_AddItemToObject(resObj, "code", cJSON_CreateBool(code));
    res = cJSON_Print(resObj);
    cJSON_Delete(resObj);

quit:
    if (err)
    {
        code = false;
        res = CreateRespondString(code, err);
    }

    cJSON_Delete(postDataObj);

    return res;
}

char *DelPlaybackDev(char *info)
{
    std::string pb_url, pb_file, cameraId;
    std::size_t pos1, pos2;
    char *data = nullptr, *res = nullptr;
    const char *err = nullptr;
    char message[128];
    int code = true;

    cJSON *postDataObj = nullptr;
    cJSON *mainIdObj = nullptr;
    cJSON *subIdObj = nullptr;
    cJSON *pbUrlObj = nullptr;

    if (!info)
        return nullptr;

    data = ParsePostData(info);
    if (!data)
    {
        myprintf("do not find post data\n");
        SetError(no_post_data);
        err = GetError;
        goto quit;
    }

    postDataObj = cJSON_Parse(data);
    if (!postDataObj)
    {
        myprintf("post data is invalid json\n");
        SetError(invalid_json_data);
        err = GetError;
        goto quit;
    }

    mainIdObj = cJSON_GetObjectItem(postDataObj, MAIN_ID_JSON_NAME);
    if (!mainIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", MAIN_ID_JSON_NAME);
        SetError(no_main_id_in_json);
        err = GetError;
        goto quit;
    }

    subIdObj = cJSON_GetObjectItem(postDataObj, SUB_ID_JSON_NAME);
    if (!subIdObj)
    {
        myprintf("post json do not find \"%s\" info\n", SUB_ID_JSON_NAME);
        SetError(no_sub_id_in_json);
        err = GetError;
        goto quit;
    }

    pbUrlObj = cJSON_GetObjectItem(postDataObj, URL_JSON_NAME);
    if (!pbUrlObj)
    {
        myprintf("post json do not find \"%s\" info\n", URL_JSON_NAME);
        SetError(no_url_in_json);
        err = GetError;
        goto quit;
    }

    cameraId = cameraId + mainIdObj->valuestring + "-" + subIdObj->valuestring;
    pb_url = pbUrlObj->valuestring;

    pb_file = pb_url.substr(pb_url.find_last_of('/') + 1);
    pos1 = pb_file.find("%20");
    pos2 = pb_file.find(' ');
    if (pos1 != std::string::npos)
    {
        pb_file.erase(pos1, 3);
        pb_file.insert(pos1, " ");
    }
    else if (std::string::npos == pos1 && std::string::npos == pos2)
    {
        SetError(invalid_url_in_json);
        err = GetError;
        goto quit;
    }

    pb_file = transMediaPath + pb_file;

    if (unlink(pb_file.c_str()) < 0)
    {
        myprintf("unlink file(%s) fail\n", pb_file.c_str());
        SetError(delete_file_fail);
        err = GetError;
        goto quit;
    }

    res = CreateRespondString(code, "成功");

quit:
    if (err)
    {
        code = false;
        res = CreateRespondString(code, err);
    }

    cJSON_Delete(postDataObj);

    return res;
}

struct http_req_ctx httpReqList[] = {
    {"addrecodedevice", AddRecodeDevice},
    {"deleterecodedevice", DeleteRecodeDevice},
    {"updateschedule", UpdateSchedule},
    {"getschedule", GetSchedule},
    {"eventrecode", EventRecode},
    {"operatefile", OperateFile},
    {"getrecodelist", GetRecodeList},
    {"getbackuplist", GetBackupList},
    {"playback", AddPlaybackDev},
    {"deleteplayback", DelPlaybackDev},
};

char *ParseReq(char *content, char *req)
{
    if (!content || !req)
        return nullptr;

    const char *method[] = {
        "GET /",
        "POST /",
    };
    char *start = nullptr;

    for (int i = 0; i < sizeof(method) / sizeof(const char *); i++)
    {
        start = strstr(content, method[i]);
        if (start)
        {
            start += strlen(method[i]);
            break;
        }
    }
    if (!start)
        return nullptr;

    char *end = strstr(content, http_ver);
    if (!end)
        return nullptr;

    while (start < end)
    {
        *req++ = *start++;
        if (*start == '/' || *start == ' ')
            break;
    }

    return start;
}

char *ResponseReq(char *req, char *info)
{
    if (!req)
        return nullptr;

    uint32_t req_total_num = sizeof(httpReqList) / sizeof(struct http_req_ctx);

    for (uint32_t i = 0; i < req_total_num; i++)
    {
        if (!strcmp(httpReqList[i].req, req))
        {
            return httpReqList[i].req_handle(info);
        }
    }

    return nullptr;
}

void ResponseClient(int sockfd, char *content, uint32_t content_len)
{
    if (!content)
        return;

    char req[HTTP_REQ_STRING_LEN];
    memset(req, 0, HTTP_REQ_STRING_LEN);

    char *info;
    char *res;
    int parse_content_len, real_content_len;
    cJSON *body;

    parse_content_len = ParseContentLen(content);
    if (parse_content_len > 0)
    {
        info = ParsePostData(content);
        if (info)
        {
            real_content_len = strlen(info);
            if (real_content_len < parse_content_len)
            {
                read(sockfd, content + content_len, parse_content_len - real_content_len);
            }
        }
    }

    info = ParseReq(content, req);
    if (!info)
        goto error;

    res = ResponseReq(req, info);
    if (res)
    {
        res = CreateResponse("HTTP/1.1 200 OK\r\n", res_head, res);
        goto quit;
    }

error:
    body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "message", cJSON_CreateString("解析请求错误"));

    res = CreateResponse("HTTP/1.1 400 Bad Request\r\n", res_head, cJSON_Print(body));

quit:
    write(sockfd, (const void *)res, strlen(res));
    free(res);
}
