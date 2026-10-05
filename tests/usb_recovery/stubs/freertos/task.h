#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(x) (x)
TickType_t xTaskGetTickCount(void);
