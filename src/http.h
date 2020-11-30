#ifndef HTTP_H
#define HTTP_H

#include <string.h>
#include <vector>
#include "common.h"
#include "cJSON.h"
#include "mysql.h"
#include "store.h"
#include "stream.h"

void ResponseClient(int sockfd, char *content, uint32_t content_len);

#endif // HTTP_H
