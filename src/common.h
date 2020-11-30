#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <vector>
#include <map>

#define REDEF_PRINTF
#ifdef REDEF_PRINTF
#include "log.h"
#endif

#define STORE_FILE_MAX_SIZE (64 * 1024 * 1024)
#define FILE_IO_BLOCK_SIZE (4 * 1024)

#define CAMERA_ID_MYSQL_NAME "id"
#define CAMERA_TM_MYSQL_NAME "start_time_ms"
#define CAMERA_NAME_MYSQL_NAME "name"
#define CAMERA_TYPE_MYSQL_NAME "type"
#define CAMERA_STATUS_MYSQL_NAME "status"
#define CAMERA_BACKUP_MYSQL_NAME "backup"

#define MAIN_ID_JSON_NAME "mainId"
#define SUB_ID_JSON_NAME "subId"
#define START_TIME_JSON_NAME "startTime"
#define END_TIME_JSON_NAME "endTime"
#define TYPE_ID_JSON_NAME "typeId"
#define TYPE_JSON_NAME CAMERA_TYPE_MYSQL_NAME
#define EVENT_ID_JSON_NAME "eventId"
#define FILE_NAME_JSON_NAME "file"
#define OPERATE_TYPE_JSON_NAME "operateId"
#define REC_SCHE_JSON_NAME "recSchedule"
#define BAK_SCHE_JSON_NAME "bakSchedule"
#define MESSAGE_JSON_NAME "message"
#define URL_JSON_NAME "url"

#define RECODE_TYPE_PLANNED "planned"
#define RECODE_TYPE_MANUAL "manual"
#define RECODE_TYPE_ALARM "alarming"

#define RECODE_FILE_STATUS_LOCK "lock"
#define RECODE_FILE_STATUS_UNLOCK "unlock"

typedef void (*client_cb)(int, uint8_t *, uint32_t);

enum
{
    create_thread_fail = 0,
    create_store_directory_fail,
    malloc_memory_fail,
    device_already_exist,
    schedule_days_not_enough,
    schedule_format_error,
    no_post_data,
    invalid_json_data,
    no_main_id_in_json,
    no_sub_id_in_json,
    no_start_time_in_json,
    no_end_time_in_json,
    no_type_id_in_json,
    no_event_id_in_json,
    no_file_name_in_json,
    no_operate_type_in_json,
    no_dev_name_in_json,
    no_schedule_in_json,
    no_url_in_json,
    invalid_schedule_type_in_json,
    invalid_date_format_in_json,
    invalid_event_type_in_json,
    invalid_file_operate_type,
    invalid_type_id_in_json,
    invalid_url_in_json,
    trans_to_mp4_fail,
    create_def_sche_fail,
    func_input_null_addr,
    do_not_find_camera,
    do_not_find_rec_files,
    cjson_called_error,
    delete_file_fail,
    store_api_ret_num
};

enum
{
    load_file_server_ip = 0,
    load_file_backup_ip,
    load_file_stream_media_ip,
    load_file_dowload_media_ip,
    load_file_server_port,
    load_file_playback_port,
    load_file_backup_port,
    load_file_stream_media_port,
    load_file_db_user,
    load_file_db_passwd,
    load_file_db_name,
    load_file_store_path,
    load_file_bak_path,
    load_file_trans_media_path,
    load_file_num
};

extern const char *store_api_ret[store_api_ret_num];
extern volatile int sys_flag;
extern volatile uint32_t error_no;
extern const char *other_error;

#ifdef __cplusplus
extern "C"
{
#endif
    extern std::string serverIp;
    extern std::string backupIp;
    extern std::string streamMediaIp;
    extern std::string downloadMediaIp;
    extern std::string databaseUser;
    extern std::string databasePasswd;
    extern std::string databaseName;
    extern std::string storePath;
    extern std::string backupPath;
    extern std::string transMediaPath;
    extern int32_t server_port, playback_port, backup_port, stream_media_port;

    extern std::string transMediaPath;
#ifdef __cplusplus
}
#endif

#define SetError(x) error_no = x
#define GetError store_api_ret[error_no]
#define SetOtherError(x) other_error = x
#define GetOtherError other_error

int32_t LoadInfoFile(void);
int ScheduleFormatCheck(const char *schedule);
int32_t TransDateToTm(std::string &date, struct tm &struTm);
int32_t ParseSchedule(const char *json, std::vector<std::vector<std::vector<struct tm>>> &schedule);
int32_t ParseKeyFrame(uint8_t *data, uint32_t size);

#endif
