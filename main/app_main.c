#include "baji_state.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Preparation build: deliberately does not initialize the board or serial
 * business protocol. Replace composition here after the G1 board gate passes.
 */
void app_main(void) {
    baji_state_t state;
    baji_state_init(&state);
    ESP_LOGI("baji", "Preparation build: core ready; board/UI/transport not integrated.");
    for (;;) {
        baji_view_t view = baji_state_view(&state, esp_timer_get_time() / 1000);
        ESP_LOGI("baji", "Awaiting integration; connection=%d", (int)view.connection);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
