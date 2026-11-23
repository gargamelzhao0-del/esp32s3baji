#include "baji_runtime.h"
#include "baji_vsync.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

enum { SCAN_US = 17000, EDGE_GUARD_US = 1500 };

typedef struct {
    portMUX_TYPE lock;
    baji_input_t latest;
    baji_peripherals_t *io;
    StaticSemaphore_t wake_storage, done_storage;
    SemaphoreHandle_t wake, done;
    bool stopping;
} input_reader_t;

static void read_input(void *context)
{
    input_reader_t *r = context;
    baji_touch_sample_t previous = {0};
    int64_t next_log = 0;
    for (;;) {
        portENTER_CRITICAL(&r->lock);
        bool stop = r->stopping;
        portEXIT_CRITICAL(&r->lock);
        if (stop) break;
        baji_touch_sample_t touch;
        esp_err_t err = baji_peripherals_next_touch(r->io, previous.pressed ? 24 : 120, &touch);
        if (err == ESP_ERR_NOT_FINISHED) continue;
        if (err != ESP_OK) {
            int64_t now = esp_timer_get_time();
            if (now >= next_log) {
                ESP_LOGW("baji_runtime", "touch read: %s", esp_err_to_name(err));
                next_log = now + 5000000;
            }
            vTaskDelay(pdMS_TO_TICKS(12));
            continue;
        }
        if (touch.pressed == previous.pressed &&
            (!touch.pressed || (touch.x == previous.x && touch.y == previous.y))) continue;
        /* The CST9217's release packet contains no coordinates. Preserve
         * the last drag point even if a whole swipe fits between callbacks. */
        if (!touch.pressed) {
            touch.x = previous.x;
            touch.y = previous.y;
        }
        portENTER_CRITICAL(&r->lock);
        if (touch.pressed && !previous.pressed) {
            r->latest.presses++;
            r->latest.press_x = touch.x;
            r->latest.press_y = touch.y;
        }
        r->latest.touch = touch;
        portEXIT_CRITICAL(&r->lock);
        xSemaphoreGive(r->wake);
        previous = touch;
    }
    xSemaphoreGive(r->done);
    vTaskDelete(NULL);
}

/* Round down when arming early for the next TE, up when waiting for a
 * software deadline. A finite delay never becomes portMAX_DELAY. */
static TickType_t ticks_left(int64_t us, bool ceil)
{
    if (us <= 0) return 0;
    if (us / 1000000 >= portMAX_DELAY / configTICK_RATE_HZ) return portMAX_DELAY - 1;
    return ((uint64_t)us * configTICK_RATE_HZ + (ceil ? 999999 : 0)) / 1000000;
}

typedef struct {
    bool open, missing;
    uint32_t last_seq;
} frame_clock_t;

static esp_err_t wait_frame(input_reader_t *r, frame_clock_t *clock, int64_t due, int64_t last_frame)
{
    /* Sleep across scans the application does not need. An input change is
     * latched even if its IRQ fires during painting or this transition. */
    int64_t now = esp_timer_get_time();
    TickType_t idle = due == INT64_MAX ? portMAX_DELAY :
                      due <= now ? 0 : ticks_left(due - now - EDGE_GUARD_US, false);
    bool input = xSemaphoreTake(r->wake, idle) == pdTRUE;
    now = esp_timer_get_time();
    int64_t earliest = input ? now : due;
    if (clock->open) {
        baji_vsync_edge_t edge = baji_vsync_snapshot();
        if (edge.seq != clock->last_seq) clock->missing = false;
        /* If the timer woke just after an eligible edge, use it while the
         * panel is still near that boundary. Never start on a stale edge. */
        while (!clock->missing) {
            if (edge.edge_us > last_frame && edge.edge_us >= earliest &&
                edge.observed_us - edge.edge_us <= EDGE_GUARD_US) return ESP_OK;
            esp_err_t err = baji_vsync_wait(edge.seq, (SCAN_US + EDGE_GUARD_US + 999) / 1000, &edge);
            if (err == ESP_ERR_TIMEOUT) {
                /* A stopped panel should cost at most one timeout. Check
                 * the counter on later frames to pick up resumed edges. */
                clock->missing = true;
                clock->last_seq = edge.seq;
                break;
            }
            if (err != ESP_OK) return err;
            if (xSemaphoreTake(r->wake, 0) == pdTRUE) earliest = edge.edge_us;
        }
    }
    /* With a disconnected TE, pace from the previous frame start. Time
     * spent rendering counts toward the period. Input can shorten a wait. */
    int64_t target = last_frame + SCAN_US;
    if (!input && earliest > target) target = earliest;
    for (;;) {
        TickType_t left = ticks_left(target - esp_timer_get_time(), true);
        if (!left) return ESP_OK;
        if (xSemaphoreTake(r->wake, left) == pdTRUE) target = last_frame + SCAN_US;
    }
}

esp_err_t baji_runtime_run(baji_board_t *board, const baji_board_info_t *info,
                           const baji_runtime_config_t *config, baji_frame_fn render, void *context)
{
    if (!board || !info || !config || !render || config->brightness > 100) return ESP_ERR_INVALID_ARG;
    input_reader_t reader = {.lock = portMUX_INITIALIZER_UNLOCKED, .io = baji_board_peripherals(board)};
    baji_input_t input = {0};
    int64_t now = esp_timer_get_time(), due = now;
    esp_err_t err = render(context, &input, now, &due);
    if (err == ESP_OK) err = baji_board_set_brightness(board, config->brightness);
    if (err != ESP_OK || due == BAJI_FRAME_DONE) return err;

    reader.wake = config->changed ? config->changed : xSemaphoreCreateBinaryStatic(&reader.wake_storage);
    reader.done = xSemaphoreCreateBinaryStatic(&reader.done_storage);
    bool reading = info->touch_status == ESP_OK &&
                   xTaskCreate(read_input, "baji_touch", 3072, &reader, 4, NULL) == pdPASS;
    if (info->touch_status == ESP_OK && !reading) ESP_LOGW("baji_runtime", "touch worker unavailable");
    frame_clock_t clock = {.open = baji_vsync_open() == ESP_OK};
    if (!clock.open) ESP_LOGW("baji_runtime", "TE unavailable, using software pacing");
    unsigned failures = 0;
    while (due != BAJI_FRAME_DONE) {
        err = wait_frame(&reader, &clock, due, now);
        if (err != ESP_OK) break;
        /* Drain before copying, so a later update always leaves a token. */
        xSemaphoreTake(reader.wake, 0);
        portENTER_CRITICAL(&reader.lock);
        input = reader.latest;
        portEXIT_CRITICAL(&reader.lock);
        now = esp_timer_get_time();
        due = now;
        err = render(context, &input, now, &due);
        if (err == ESP_OK) failures = 0;
        else {
            if (++failures == 3) break;
            due = now;
            ESP_LOGW("baji_runtime", "frame retry: %s", esp_err_to_name(err));
        }
    }
    if (clock.open) {
        esp_err_t reported = ESP_OK, close_err;
        while ((close_err = baji_vsync_close()) != ESP_OK) {
            if (close_err != reported) ESP_LOGW("baji_runtime", "TE cleanup: %s", esp_err_to_name(close_err));
            reported = close_err;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (reading) {
        portENTER_CRITICAL(&reader.lock);
        reader.stopping = true;
        portEXIT_CRITICAL(&reader.lock);
        xSemaphoreTake(reader.done, portMAX_DELAY);
    }
    vSemaphoreDelete(reader.done);
    if (!config->changed) vSemaphoreDelete(reader.wake);
    return err;
}
