#ifndef RTMPSRV_H
#define RTMPSRV_H

#include <stdint.h>
#include <stdlib.h>

int RtmpServerStart(void);
int RtmpServerWork(int sockfd);

#endif // RTMPSRV_H
