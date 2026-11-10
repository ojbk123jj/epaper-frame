#pragma once

#include "mock.h"

typedef struct display_test_mutex *SemaphoreHandle_t;
#define pdTRUE 1
#define pdFALSE 0

SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned ticks);
int xSemaphoreGive(SemaphoreHandle_t mutex);
void vSemaphoreDelete(SemaphoreHandle_t mutex);
