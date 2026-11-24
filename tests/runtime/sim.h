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
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait);
void xTaskNotifyGive(TaskHandle_t task);
void test_info_log(const char *tag, const char *format, const char *source, int percent);
