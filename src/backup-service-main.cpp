#include <signal.h>
#include "common.h"
#include "backup.h"

extern int32_t StartServer(std::string &ip, uint32_t port, uint32_t rd_buf_size, client_cb clientCB);

char pid_path[128] = {"/var/run/backup-service.pid"};

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
    int ret = -1;

    signal(SIGINT, SignalHdl);
    signal(SIGSEGV, SignalHdl);
    signal(SIGTERM, SignalHdl);
    signal(SIGQUIT, SignalHdl);

    setbuf(stdout, nullptr);

#ifdef REDEF_PRINTF
    if(PrintfRefef("./bak_service.log") < 0)
        return -1;
#endif

    if (UpdataPidFile() < 0)
        goto quit;
    
    if(LoadInfoFile() < 0)
        goto quit;

    BackupServerInit();

    if (StartServer(backupIp, backup_port, 0, (client_cb)BackupRespond) < 0)
        goto quit;

    while(sys_flag)
    {
        sleep(1);
    }
    ret = 0;

quit:
    RemovePidFile();
    return ret;
}