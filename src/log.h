#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdarg.h>

int PrintfRefef(const char *log_path);
int myprintf(const char *fmt, ...);
int PrintToLog(const char *fmt, ...);

#endif // LOG_H
