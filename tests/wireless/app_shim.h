#pragma once
#include "lifecycle_shim.h"
#include "baji_agent_feed.h"
#define ESP_LOGE(...) discard_log(__VA_ARGS__)
#define ESP_LOGI(...) discard_log(__VA_ARGS__)
typedef struct { unsigned value; } baji_board_t;
typedef struct {
    unsigned width, height, psram_bytes, flash_bytes;
    esp_err_t flash_status, touch_status, power_status;
} baji_board_info_t;
esp_err_t baji_board_open(baji_board_t **,baji_board_info_t *);
esp_err_t baji_board_close(baji_board_t *);
void vTaskDelay(unsigned);
#ifdef BAJI_GPT_PET
#define LOCAL_RUN pet_app_run
#define CONNECTED_RUN pet_app_run_connected
#else
#define LOCAL_RUN deepseek_app_run
#define CONNECTED_RUN deepseek_app_run_connected
#endif
esp_err_t LOCAL_RUN(baji_board_t *, const baji_board_info_t *);
esp_err_t CONNECTED_RUN(baji_board_t *, const baji_board_info_t *, const baji_agent_feed_t *);
