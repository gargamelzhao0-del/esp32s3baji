#include "board_check.h"
#include "pet_app.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
#ifdef CONFIG_BAJI_BOARD_CHECK
    err = baji_board_check_run(board, &info);
#else
    err = pet_app_run(board, &info);
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
}
