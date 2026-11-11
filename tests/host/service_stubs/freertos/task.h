#pragma once
int xTaskCreate(void (*fn)(void *), const char *, unsigned, void *, unsigned, void *);
void vTaskDelay(unsigned);
