#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>


void LOG_Init(void* huart);
void LOG_Printf(const char* fmt, ...);

#endif // LOGGER_H