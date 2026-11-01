#include "logger.h"
#include "main.h"

static UART_HandleTypeDef* log_huart = NULL;

void LOG_Init(void* huart) {
    log_huart = (UART_HandleTypeDef*)huart;
}


void LOG_Printf(const char* fmt, ...) {
    if (log_huart == NULL) {
        return;
    }

    static char buff[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buff, sizeof(buff), fmt, args);
    HAL_UART_Transmit(log_huart, (uint8_t*)buff, strlen(buff), HAL_MAX_DELAY);
    va_end(args);
}