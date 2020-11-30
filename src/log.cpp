#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include "log.h"

static FILE *f_log = nullptr;
char file_log_path[256];
int is_f_log_in_used = 0;

void *MoniterLogFile(void *arg)
{
    uint64_t max_log_size = 10*1024*1024;
    struct stat stat_buf;

    while(true)
    {
        is_f_log_in_used = 1;
        if(stat(file_log_path, &stat_buf) < 0)
        {
            sleep(1);
            continue;
        }

        if(stat_buf.st_size >= max_log_size)
        {
            truncate64(file_log_path, 0);
            fseek(f_log, 0, SEEK_SET);
        }
        is_f_log_in_used = 0;

        sleep(60);
    }

    return 0;
}

int PrintfRefef(const char *log_path)
{
    pthread_t pth;
    pthread_attr_t attr;

    strcpy(file_log_path, log_path);
    file_log_path[strlen(log_path)] = 0;

    f_log = fopen64(file_log_path, "a+");
    if(!f_log)
    {
        printf("open log file %s error\n", file_log_path);
        perror("");
        return -1;
    }

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if (pthread_create(&pth, nullptr, MoniterLogFile, nullptr) < 0)
    {
        perror("creat printf thread error");
        return -1;
    }

    return 0;
}

int myprintf(const char *fmt, ...)
{
    va_list ap;

    if(!f_log)
        return -1;

    while(is_f_log_in_used)
        usleep(10000);

    va_start(ap, fmt);
    vfprintf(f_log, fmt, ap);
    va_end(ap);
    fflush(f_log);

    return 0;
}

int PrintToLog(const char *fmt, ...)
{
    va_list ap;
    char buf[1024];

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    return 0;
}
