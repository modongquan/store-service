#ifndef STREAM_H
#define STREAM_H

#include "common.h"

typedef void (*StreamCB)(const char *, uint8_t *, uint32_t, uint32_t);

int32_t StreamInit(void);
int32_t AddStreamEvent(std::string &streamId, struct tm &startTime, struct tm &endTime, StreamCB stream_cb);
int32_t AddStream(const char *streamId, std::vector<std::vector<std::vector<struct tm>>> &schedule, StreamCB stream_cb);
int32_t DeleteStream(const char *streamId);
int32_t UpdateRecodeSchedule(std::string &streamId, std::vector<std::vector<std::vector<struct tm>>> &schedule);

#endif
