#pragma once
#include "touch_recorder.h"
#include <stdio.h>

#define ESP_LOGW(tag, ...) ((void)(tag), (void)sizeof(printf(__VA_ARGS__)))
#define esp_err_to_name(e) "test error"
#define pdPASS 1

int64_t esp_timer_get_time(void);
int xTaskCreate(void (*entry)(void *), const char *name, unsigned stack, void *arg,
                unsigned priority, TaskHandle_t *handle);
void vTaskDelete(TaskHandle_t task);
