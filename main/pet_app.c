#include "pet_app.h"
#include "pet_player.h"
#include "baji_vsync.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* I2C register reads have a mandatory delay. The display task only copies
 * the newest complete sample under the lock and never waits on that delay. */
typedef struct {
    portMUX_TYPE lock;
    pet_input_t latest;
    baji_peripherals_t *io;
    SemaphoreHandle_t done;
    bool stopping;
} touch_reader_t;

static void read_touch(void *context)
{
    touch_reader_t *reader = context;
    bool pressed = false;
    int64_t next_log = 0;
    for (;;) {
        portENTER_CRITICAL(&reader->lock);
        bool stopping = reader->stopping;
        portEXIT_CRITICAL(&reader->lock);
        if (stopping) break;
        baji_touch_sample_t point;
        esp_err_t err = baji_peripherals_wait_touch(reader->io, pressed ? 24 : 120);
        if (err == ESP_OK)
            err = baji_peripherals_read_touch(reader->io, &point);
        else if (err == ESP_ERR_TIMEOUT)
            err = baji_peripherals_poll_touch(reader->io, &point);
        if (err == ESP_ERR_NOT_FINISHED) continue;
        if (err != ESP_OK) {
            int64_t now = esp_timer_get_time();
            if (now >= next_log) {
                ESP_LOGW("pet", "touch read: %s", esp_err_to_name(err));
                next_log = now + 5000000;
            }
            vTaskDelay(pdMS_TO_TICKS(12));
            continue;
        }
        portENTER_CRITICAL(&reader->lock);
        if (point.pressed && !pressed) {
            reader->latest.presses++;
            reader->latest.press_x = point.x;
            reader->latest.press_y = point.y;
        }
        reader->latest.touch = point;
        portEXIT_CRITICAL(&reader->lock);
        pressed = point.pressed;
    }
    xSemaphoreGive(reader->done);
    vTaskDelete(NULL);
}

esp_err_t pet_app_run_loaded(baji_board_t *board, const baji_board_info_t *info, baji_pet_t *art)
{
    pet_player_t player;
    pet_input_t input = {0};
    bool submitted;
    pet_player_init(&player, art, info, esp_timer_get_time());
    esp_err_t err = pet_player_tick(&player, board, &input, esp_timer_get_time(), &submitted);
    if (err == ESP_OK) err = baji_board_set_brightness(board, 45);
    if (err != ESP_OK) return err;

    touch_reader_t reader = {.lock = portMUX_INITIALIZER_UNLOCKED, .io = baji_board_peripherals(board)};
    bool reading = false;
    if (info->touch_status == ESP_OK) {
        reader.done = xSemaphoreCreateBinary();
        reading = reader.done && xTaskCreate(read_touch, "pet_touch", 3072, &reader, 4, NULL) == pdPASS;
        if (!reading) ESP_LOGW("pet", "touch worker unavailable");
    }
    bool hardware_clock = baji_vsync_open() == ESP_OK;
    uint32_t seq = hardware_clock ? baji_vsync_snapshot().seq : 0;
    ESP_LOGI("pet", "pixel pet ready, touch=%d TE=%d", reading, hardware_clock);
    unsigned failed = 0;
    const int64_t period = 17000;
    int64_t deadline = esp_timer_get_time() + period;
    while (failed < 3) {
        int64_t left = deadline - esp_timer_get_time();
        unsigned wait_ms = left > 0 ? (unsigned)((left + 999) / 1000) : 0;
        if (hardware_clock) {
            baji_vsync_edge_t edge;
            err = baji_vsync_wait(seq, wait_ms, &edge);
            if (err == ESP_OK) {
                seq = edge.seq;
                /* Real edges take priority. Allow for jitter before falling
                 * back to the software clock if this pulse fails to arrive. */
                deadline = edge.edge_us + period + 1500;
            } else if (err != ESP_ERR_TIMEOUT) break;
        } else if (wait_ms) {
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
        }
        int64_t now = esp_timer_get_time();
        if (!hardware_clock && now < deadline) continue;
        if (now >= deadline) deadline += ((now - deadline) / period + 1) * period;
        portENTER_CRITICAL(&reader.lock);
        input = reader.latest;
        portEXIT_CRITICAL(&reader.lock);
        err = pet_player_tick(&player, board, &input, now, &submitted);
        failed = err == ESP_OK ? 0 : failed + 1;
        if (err != ESP_OK) ESP_LOGW("pet", "display update: %s", esp_err_to_name(err));
    }
    if (hardware_clock) {
        while (baji_vsync_close() != ESP_OK) vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (reading) {
        portENTER_CRITICAL(&reader.lock);
        reader.stopping = true;
        portEXIT_CRITICAL(&reader.lock);
        xSemaphoreTake(reader.done, portMAX_DELAY);
    }
    if (reader.done) vSemaphoreDelete(reader.done);
    return err;
}

esp_err_t pet_app_run(baji_board_t *board, const baji_board_info_t *info)
{
    baji_pet_t *art = NULL;
    esp_err_t err = baji_pet_open(&art);
    if (err == ESP_OK) err = pet_app_run_loaded(board, info, art);
    baji_pet_close(art);
    return err;
}
