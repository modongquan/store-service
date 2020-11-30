#ifndef STORE_H
#define STORE_H

#include "common.h"

typedef void (*store_cb)(const char *, time_t, const char *, const char *);

int32_t StoreInit(store_cb storeCB);
int32_t AddStoreDevice(std::string &devId, std::string &type);
int32_t AddStoreEvent(std::string &devId, std::string &type);
int32_t RemoveStoreDevice(std::string &devId);
int32_t RemoveStoreEvent(std::string &devId, std::string &type);
int32_t WriteFrameToBuf(std::string &devId, std::string &type, uint8_t *frameData, uint32_t frameSize, uint32_t flag);
int32_t ReadStoreFileFrame(std::string &file_path, uint8_t *frameBuf, uint32_t buf_size);
int32_t GetOneFrameByIndex(uint32_t idx, uint8_t *frames, uint32_t frames_size, uint8_t *out, uint32_t out_size, uint64_t *sys_time);

#endif