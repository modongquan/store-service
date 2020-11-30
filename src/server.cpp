#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include "common.h"

#define READ_BUF_MAX_SIZE (32768 * 128)
#define WRITE_BUF_MAX_SIZE 1024

struct client_buf_info
{
    uint8_t *pReadBuf;
    uint32_t readBufSize;
    uint32_t ReadIndex = 0;
};

struct server_thread
{
    int sockfd;
    struct sockaddr_in addr;
    client_cb clientCb;
    int is_pthread_created;
    uint32_t rd_buf_size;
};

void *ClientResponseProc(void *arg)
{
    struct server_thread *pClientThread = (struct server_thread *)arg;
    struct server_thread clientThread = *pClientThread;
    struct client_buf_info clientBufInfo;
    struct timeval timeout;
    int sock_flag, rd_size, mss;
    socklen_t len = sizeof(mss);
    fd_set rd_fds;

    pClientThread->is_pthread_created = 1;

    if (!clientThread.rd_buf_size)
    {
        if (getsockopt(clientThread.sockfd, IPPROTO_TCP, TCP_MAXSEG, &mss, &len) < 0)
        {
            myprintf("[%s][%s] get tcp mss fail\n", __FILE__, __FUNCTION__);
            perror("");
            close(clientThread.sockfd);
            return nullptr;
        }
        clientThread.rd_buf_size = mss * 128;
    }
    if (!(clientBufInfo.pReadBuf = (uint8_t *)malloc(clientThread.rd_buf_size)))
    {
        myprintf("client %s:%d malloc read buf fail\n", inet_ntoa(clientThread.addr.sin_addr), ntohs(clientThread.addr.sin_port));
        close(clientThread.sockfd);
        return nullptr;
    }
    clientBufInfo.readBufSize = clientThread.rd_buf_size;
    clientBufInfo.ReadIndex = 0;

    sock_flag = fcntl(clientThread.sockfd, F_GETFL, 0);
    fcntl(clientThread.sockfd, F_SETFL, sock_flag | O_NONBLOCK);

    FD_ZERO(&rd_fds);
    FD_SET(clientThread.sockfd, &rd_fds);

    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    int ret = select(clientThread.sockfd + 1, &rd_fds, nullptr, nullptr, &timeout);
    if (ret > 0)
    {
        while ((rd_size = read(clientThread.sockfd, clientBufInfo.pReadBuf + clientBufInfo.ReadIndex, READ_BUF_MAX_SIZE - clientBufInfo.ReadIndex)) > 0)
        {
            clientBufInfo.ReadIndex += rd_size;
            if (clientBufInfo.ReadIndex >= READ_BUF_MAX_SIZE)
            {
                myprintf("client %s:%d read buf is full\n", inet_ntoa(clientThread.addr.sin_addr), ntohs(clientThread.addr.sin_port));
                clientBufInfo.ReadIndex = READ_BUF_MAX_SIZE - 1;
                break;
            }
            // usleep(10000);
        }
        if (!clientBufInfo.ReadIndex)
        {
            myprintf("read from client %s:%d error\n", inet_ntoa(clientThread.addr.sin_addr), ntohs(clientThread.addr.sin_port));
            goto quit;
        }

        clientBufInfo.pReadBuf[clientBufInfo.ReadIndex] = '\0';
        myprintf("recv content from client %s:%d : \n%s\n", inet_ntoa(clientThread.addr.sin_addr), ntohs(clientThread.addr.sin_port), clientBufInfo.pReadBuf);

        if (clientThread.clientCb)
            clientThread.clientCb(clientThread.sockfd, clientBufInfo.pReadBuf, clientBufInfo.ReadIndex);

        clientBufInfo.ReadIndex = 0;
    }
    else if (!ret)
    {
        myprintf("client %s:%d connect timeout\n", inet_ntoa(clientThread.addr.sin_addr), ntohs(clientThread.addr.sin_port));
    }
    else
    {
        myprintf("client %s:%d connect error\n", inet_ntoa(clientThread.addr.sin_addr), ntohs(clientThread.addr.sin_port));
    }

quit:
    free(clientBufInfo.pReadBuf);
    close(clientThread.sockfd);

    return nullptr;
}

void *ClientConnectProc(void *arg)
{
    struct server_thread *pServerThread = (struct server_thread *)arg;
    struct server_thread serverThread = *pServerThread, clientThread;
    socklen_t addr_len = sizeof(clientThread.addr);

    pthread_t pth;
    pthread_attr_t pth_attr;

    pServerThread->is_pthread_created = 1;

    while (sys_flag)
    {
        clientThread.sockfd = accept(serverThread.sockfd, (struct sockaddr *)&clientThread.addr, &addr_len);
        if (clientThread.sockfd < 0)
        {
            perror("ServerProc accept fail");
            break;
        }
        myprintf("client %s:%d connect server %s:%d successful\n", inet_ntoa(clientThread.addr.sin_addr), ntohs(clientThread.addr.sin_port),
               inet_ntoa(serverThread.addr.sin_addr), ntohs(serverThread.addr.sin_port));

        pthread_attr_init(&pth_attr);
        pthread_attr_setdetachstate(&pth_attr, PTHREAD_CREATE_DETACHED);

        clientThread.is_pthread_created = 0;
        clientThread.clientCb = serverThread.clientCb;
        clientThread.rd_buf_size = serverThread.rd_buf_size;

        if (!pthread_create(&pth, &pth_attr, ClientResponseProc, (void *)&clientThread))
        {
            while (!clientThread.is_pthread_created)
                usleep(100000);
        }
        else
        {
            myprintf("server %s:%d create client %s:%d pthread fail\n", inet_ntoa(serverThread.addr.sin_addr), ntohs(serverThread.addr.sin_port),
                   inet_ntoa(clientThread.addr.sin_addr), ntohs(clientThread.addr.sin_port));
        }
    }

    close(serverThread.sockfd);
    return nullptr;
}

int32_t StartServer(std::string &ip, uint32_t port, uint32_t rd_buf_size, client_cb clientCB)
{
    struct server_thread serverThread;
    int tmp = 1;

    pthread_t pth;
    pthread_attr_t attr;

    serverThread.sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverThread.sockfd <= 0)
    {
        myprintf("server(%s:%d) socket fail", ip.c_str(), port);
        perror("");
        return -1;
    }

    setsockopt(serverThread.sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&tmp, sizeof(tmp));

    serverThread.addr.sin_family = AF_INET;
    serverThread.addr.sin_addr.s_addr = inet_addr(ip.c_str()); //htonl(INADDR_ANY);
    serverThread.addr.sin_port = htons(port);

    if (bind(serverThread.sockfd, (struct sockaddr *)&serverThread.addr, sizeof(struct sockaddr_in)) == -1)
    {
        myprintf("%s, TCP bind failed for port number: %d\n", __FUNCTION__, port);
        perror("");
        close(serverThread.sockfd);
        return -1;
    }

    if (listen(serverThread.sockfd, 10) == -1)
    {
        myprintf("%s, listen failed\n", __FUNCTION__);
        close(serverThread.sockfd);
        return -1;
    }

    serverThread.clientCb = clientCB;
    serverThread.rd_buf_size = rd_buf_size;
    serverThread.is_pthread_created = 0;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if (pthread_create(&pth, nullptr, ClientConnectProc, (void *)&serverThread) < 0)
    {
        close(serverThread.sockfd);
        return -1;
    }
        
    while (!serverThread.is_pthread_created)
        usleep(100000);

    return 0;
}
