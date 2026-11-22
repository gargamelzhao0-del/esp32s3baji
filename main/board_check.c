#include "board_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static const char *TAG = "board_check";
enum { MARK_RADIUS = 9, POWER_PERIOD_MS = 5000 };

typedef struct {
    int cx, cy;
    baji_touch_sample_t touch;
} scene_t;

static uint16_t color_at(const scene_t *scene, int x, int y)
{
    const baji_touch_sample_t *p = &scene->touch;
    if (p->pressed) {
        int dx = abs(x - p->x), dy = abs(y - p->y);
        if ((dx <= 1 && dy <= MARK_RADIUS) || (dy <= 1 && dx <= MARK_RADIUS)) return 0xffff;
    }
    int dx = x - scene->cx, dy = y - scene->cy;
    // Left to right RGB verifies the wire byte order. A single gold tick marks twelve o'clock.
    if (dy >= -15 && dy < 15 && dx >= -90 && dx < 90)
        return dx < -30 ? 0xf800 : dx < 30 ? 0x07e0 : 0x001f;
    if (abs(dx) <= 2 && dy >= -177 && dy < -155) return 0xff2e;
    int r2 = dx*dx + dy*dy;
    if (r2 >= 174*174 && r2 <= 176*176) return 0x526b;
    return 0x1083;
}

static void paint(const baji_rect_t *band, uint8_t *pixels, void *context)
{
    const scene_t *scene = context;
    for (unsigned y = band->y; y < band->y + band->height; ++y) {
        for (unsigned x = band->x; x < band->x + band->width; ++x) {
            uint16_t pixel = color_at(scene, x, y);
            *pixels++ = (uint8_t)(pixel >> 8);
            *pixels++ = (uint8_t)pixel;
        }
    }
}

static baji_rect_t mark_bounds(const baji_board_info_t *info, baji_touch_sample_t point)
{
    int x0 = point.x > MARK_RADIUS ? point.x - MARK_RADIUS : 0;
    int y0 = point.y > MARK_RADIUS ? point.y - MARK_RADIUS : 0;
    int x1 = point.x + MARK_RADIUS + 1, y1 = point.y + MARK_RADIUS + 1;
    if (x1 > info->width) x1 = info->width;
    if (y1 > info->height) y1 = info->height;
    return (baji_rect_t){x0, y0, x1-x0, y1-y0};
}

static esp_err_t repaint_touch(baji_board_t *board, const baji_board_info_t *info,
                               scene_t *scene, baji_touch_sample_t old)
{
    baji_rect_t a = mark_bounds(info, old), b = mark_bounds(info, scene->touch);
    if (!old.pressed) return baji_board_paint(board, b, paint, scene);
    if (!scene->touch.pressed) return baji_board_paint(board, a, paint, scene);
    int x = a.x < b.x ? a.x : b.x, y = a.y < b.y ? a.y : b.y;
    int right = a.x+a.width > b.x+b.width ? a.x+a.width : b.x+b.width;
    int bottom = a.y+a.height > b.y+b.height ? a.y+a.height : b.y+b.height;
    // Nearby samples share one transfer. A long drag must not dirty the entire diagonal.
    if ((right-x)*(bottom-y) <= a.width*a.height + b.width*b.height)
        return baji_board_paint(board, (baji_rect_t){x, y, right-x, bottom-y}, paint, scene);
    esp_err_t err = baji_board_paint(board, a, paint, scene);
    return err == ESP_OK ? baji_board_paint(board, b, paint, scene) : err;
}

static void report_power(baji_peripherals_t *io)
{
    baji_power_status_t power;
    esp_err_t err = baji_peripherals_read_power(io, &power);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "power sample: %s", esp_err_to_name(err));
        return;
    }
    if (power.percent_valid)
        ESP_LOGI(TAG, "battery=%u%% charging=%d usb=%d", (unsigned)power.percent, power.charging, power.usb_present);
    else
        ESP_LOGI(TAG, "battery_present=%d percent=unknown charging=%d usb=%d",
                 power.battery_present, power.charging, power.usb_present);
}

esp_err_t baji_board_check_run(baji_board_t *board, const baji_board_info_t *info)
{
    scene_t scene = {.cx = info->width/2, .cy = info->height/2};
    int64_t start = esp_timer_get_time();
    esp_err_t err = baji_board_paint(board, (baji_rect_t){0, 0, info->width, info->height}, paint, &scene);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "first RGB565 frame incl. last DMA: %lld us", (long long)(esp_timer_get_time()-start));
    err = baji_board_set_brightness(board, 45);
    if (err != ESP_OK) return err;

    baji_peripherals_t *io = baji_board_peripherals(board);
    int64_t next_power = 0;
    esp_err_t last_touch_error = ESP_OK;
    for (;;) {
        int64_t now = esp_timer_get_time()/1000;
        if (info->power_status == ESP_OK && now >= next_power) {
            report_power(io);
            next_power = now + POWER_PERIOD_MS;
        }
        if (info->touch_status != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(POWER_PERIOD_MS));
            continue;
        }
        // IRQ wakes immediately on a new contact. Poll while held to catch the
        // release; a slow idle fallback also recovers a missed interrupt edge.
        err = baji_peripherals_wait_touch(io, scene.touch.pressed ? 16 : 250);
        baji_touch_sample_t sample;
        if (err == ESP_OK || err == ESP_ERR_TIMEOUT) err = baji_peripherals_read_touch(io, &sample);
        if (err != ESP_OK) {
            if (err != last_touch_error) ESP_LOGW(TAG, "touch sample: %s", esp_err_to_name(err));
            last_touch_error = err;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        last_touch_error = ESP_OK;
        baji_touch_sample_t old = scene.touch;
        if (sample.pressed == old.pressed && (!sample.pressed || (sample.x == old.x && sample.y == old.y)))
            continue;
        scene.touch = sample;
        err = repaint_touch(board, info, &scene, old);
        if (err != ESP_OK) return err;
    }
}
