#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

int ReadPid(const char *pid_path)
{
    if (!pid_path)
        return -1;

    int fd = 0;
    char rd_buf[256];
    size_t rd_idx = 0;

    fd = open(pid_path, O_RDONLY, 0777);
    if (fd < 0)
    {
        printf("open %s fail\n", pid_path);
        return -1;
    }

    while (read(fd, rd_buf + rd_idx, 1) == 1)
    {
        rd_idx++;
    }
    if (!rd_idx)
    {
        printf("%s is null\n", pid_path);
        close(fd);
        return -1;
    }
    rd_buf[rd_idx] = 0;
    close(fd);

    return atoi(rd_buf);
}

int ExtraPid(char *pids_str, int *pids, int pid_size)
{
    if (!pids_str || !pids)
        return -1;

    char pid_buf[256];
    char pid_ch;
    int i = 0, pid_idx = 0;

    while ((pid_ch = *pids_str++) != 0)
    {
        if (pid_ch != 0x0a)
        {
            pid_buf[i++] = pid_ch;
        }
        else
        {
            pid_buf[i] = 0;
            pids[pid_idx++] = atoi(pid_buf);
            i = 0;
            if (pid_idx >= pid_size)
            {
                printf("pid size not enough\n");
                return -1;
            }
        }
    }

    return pid_idx;
}

int main(int argc, char *argv[])
{
    const char *pid_path;
    const char *process_path;
    const char *process_name;

    char run_cmd[256];
    char detectCmd[256];
    char result_buf[256];
    int pid_buf[128];
    FILE *result;
    int pid;

    if (argc < 4)
    {
        printf("daemon input error.  ./daemon /var/run/store-service.pid ./main\n");
        return -1;
    }

    pid_path = argv[1];
    process_path = argv[2];
    process_name = argv[3];
    printf("pid path = %s, process path = %s, process name = %s\n", pid_path, process_path, process_name);

    snprintf(run_cmd, 256, "%s/%s&", process_path, process_name);

    if (daemon(0, 1) < 0)
    {
        printf("create daemon error\n");
        return -1;
    }
    chdir(process_path);
    printf("create daemon successful\n");

    while (true)
    {
        sleep(10);

        if (access(pid_path, F_OK))
        {
            printf("do not find %s, start to run \"%s\"\n", pid_path, run_cmd);
            system(run_cmd);
        }
        else
        {
            printf("%s exist\n", pid_path);
            pid = ReadPid(pid_path);
            if (pid < 0)
            {
                system(run_cmd);
                continue;
            }
            printf("read store service pid = %d\n", pid);

            snprintf(detectCmd, 256, "ps -ef | awk '{print $2}' | grep %d", pid);
            memset(result_buf, 256, 0);

            result = popen(detectCmd, "r");
            int rd_size = fread(result_buf, 1, sizeof(result_buf), result);
            if (!rd_size)
            {
                printf("1 do not find process(pid=%d),  restart \"%s\"\n", pid, run_cmd);
                system(run_cmd);
            }
            else if (rd_size > 0)
            {
                result_buf[rd_size] = 0;
                printf("detect resutl = %s\n", result_buf);

                int extra_size = ExtraPid(result_buf, pid_buf, 128);
                int i;
                printf("extra pid size = %d\n", extra_size);
                for (i = 0; i < extra_size; i++)
                {
                    printf("extra pid[%d] = %d\n", i, pid_buf[i]);
                    if (pid_buf[i] == pid)
                    {
                        break;
                    }
                }
                if (i == extra_size)
                {
                    printf("2 do not find process(pid=%d),  restart \"%s\"\n", pid, run_cmd);
                    system(run_cmd);
                }
            }
            else
            {
                printf("popen \"%s\" fail\n", detectCmd);
            }
            pclose(result);
        }
    }

    return 0;
}