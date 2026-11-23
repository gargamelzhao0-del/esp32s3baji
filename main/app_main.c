#include "sdkconfig.h"
#if defined(BAJI_G1_ONLY) || defined(CONFIG_BAJI_BOARD_CHECK)
#include "board_check.h"
#elif defined(BAJI_GPT_PET)
#include "pet_app.h"
#else
#include "baji_deepseek_app.h"
#endif
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if defined(BAJI_WIRELESS) && !defined(BAJI_G1_ONLY) && !defined(CONFIG_BAJI_BOARD_CHECK)
#include "baji_connectivity.h"
#define BAJI_CONNECTED_APP 1
#endif

void app_main(void)
{
    baji_board_t *board;
    baji_board_info_t info;
    esp_err_t err = baji_board_open(&board, &info);
    if (err != ESP_OK) {
        ESP_LOGE("baji", "board startup: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI("baji", "1.75C %ux%u, PSRAM=%u bytes, touch=%s, power=%s",
             (unsigned)info.width, (unsigned)info.height, (unsigned)info.psram_bytes,
             esp_err_to_name(info.touch_status), esp_err_to_name(info.power_status));
    if (info.flash_status == ESP_OK)
        ESP_LOGI("baji", "physical flash=%u bytes", (unsigned)info.flash_bytes);
    else
        ESP_LOGW("baji", "flash size unknown: %s", esp_err_to_name(info.flash_status));
#ifdef BAJI_CONNECTED_APP
    err = baji_connectivity_start();
    if (err != ESP_OK) ESP_LOGW("baji", "wireless startup: %s", esp_err_to_name(err));
    const baji_agent_feed_t *feed = baji_connectivity_feed();
#ifdef BAJI_GPT_PET
    err = pet_app_run_connected(board, &info, feed);
#else
    err = deepseek_app_run_connected(board, &info, feed);
#endif
    esp_err_t stop = baji_connectivity_stop();
    if (err == ESP_ERR_NO_MEM) {
        /* Asset and UI allocations happen after radio bring-up. If internal
         * RAM is under pressure, release the radios and reopen local play. */
        ESP_LOGW("baji", "display memory pressure, restarting local pet");
#ifdef BAJI_GPT_PET
        err = pet_app_run(board, &info);
#else
        err = deepseek_app_run(board, &info);
#endif
    }
#elif defined(BAJI_G1_ONLY) || defined(CONFIG_BAJI_BOARD_CHECK)
    err = baji_board_check_run(board, &info);
#elif defined(BAJI_GPT_PET)
    err = pet_app_run(board, &info);
#else
    err = deepseek_app_run(board, &info);
#endif
    ESP_LOGE("baji", "display application stopped: %s", esp_err_to_name(err));
    // app_main's task is deleted when it returns. Keep the owner alive until
    // SPI has actually returned its last descriptor and teardown succeeds.
    esp_err_t last_cleanup = ESP_OK;
    while ((err = baji_board_close(board)) != ESP_OK) {
        if (err != last_cleanup) ESP_LOGE("baji", "hardware cleanup: %s", esp_err_to_name(err));
        last_cleanup = err;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#ifdef BAJI_CONNECTED_APP
    while (stop != ESP_OK) {
        ESP_LOGW("baji","wireless cleanup: %s",esp_err_to_name(stop));
        vTaskDelay(pdMS_TO_TICKS(1000));
        stop = baji_connectivity_stop();
    }
#endif
}
