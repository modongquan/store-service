#include "common.h"
#include "rtmpsrv.h"

int main(int argc, char *argv[])
{
    setbuf(stdout, nullptr);

#ifdef REDEF_PRINTF
    if(PrintfRefef("./rtmp_server.log") < 0)
        return -1;
#endif

    if(argc < 2) return -1;
    myprintf("argv[0] = %s, argv[1] = %s\n", argv[0], argv[1]);

    int client_sock = atoi(argv[1]);

    if (LoadInfoFile() < 0)
        return -1;

    if(0 == fork())
    {
        RtmpServerWork(client_sock);
    }

    return 0;
}
