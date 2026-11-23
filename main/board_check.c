#include "board_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "board_check";
enum { MARK_RADIUS = 9, POWER_PERIOD_MS = 5000 };

typedef struct {
    int cx, cy;
    baji_touch_sample_t touch;
} scene_t;

/* Repeat the wire-order pixel in disjoint chunks. This also works on odd
 * clipped widths and on a byte-aligned caller buffer. */
static void fill_solid(uint8_t *pixels, unsigned count, uint16_t color)
{
    if (!count) return;
    pixels[0] = (uint8_t)(color >> 8);
    pixels[1] = (uint8_t)color;
    for (unsigned filled = 1; filled < count; ) {
        unsigned next = count - filled < filled ? count - filled : filled;
        memcpy(pixels + 2 * filled, pixels, 2 * next);
        filled += next;
    }
}

/* x0 and x1 are inclusive display coordinates. */
static void span(const baji_rect_t *band, uint8_t *row, int x0, int x1, uint16_t color)
{
    int last = band->x + band->width - 1;
    if (x0 < band->x) x0 = band->x;
    if (x1 > last) x1 = last;
    if (x0 <= x1) fill_solid(row + 2 * (x0 - band->x), x1 - x0 + 1, color);
}

/* Floor(sqrt(squared)) for the two exact circular boundaries. */
static int circle_extent(int squared)
{
    int lo = 0, hi = 176;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (mid * mid <= squared) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

static void paint(const baji_rect_t *band, uint8_t *pixels, void *context)
{
    const scene_t *scene = context;
    const int cx = scene->cx;
    int x0 = (int)band->x - cx, x1 = x0 + band->width - 1;
    int near_x = x0 > 0 ? x0 : x1 < 0 ? -x1 : 0;
    int far_x = abs(x0) > abs(x1) ? abs(x0) : abs(x1);
    /* A small dirty window usually misses the ring. Reject those rows before
     * computing either circle extent, including tall narrow cursor windows. */
    int min_yy = 174 * 174 - far_x * far_x;
    int max_yy = 176 * 176 - near_x * near_x;
    fill_solid(pixels, (unsigned)band->width * band->height, 0x1083);
    for (int y = band->y; y < band->y + band->height; ++y, pixels += band->width * 2) {
        int dy = y - scene->cy, yy = dy * dy;
        if (yy >= min_yy && yy <= max_yy) {
            int outer = circle_extent(176 * 176 - yy);
            if (yy >= 174 * 174) {
                span(band, pixels, cx - outer, cx + outer, 0x526b);
            } else {
                /* Strictly inside the inner circle is excluded from the ring. */
                int hole = circle_extent(174 * 174 - 1 - yy);
                span(band, pixels, cx - outer, cx - hole - 1, 0x526b);
                span(band, pixels, cx + hole + 1, cx + outer, 0x526b);
            }
        }
        if (dy >= -177 && dy < -155)
            span(band, pixels, cx - 2, cx + 2, 0xff2e);
        if (dy >= -15 && dy < 15) {
            span(band, pixels, cx - 90, cx - 31, 0xf800);
            span(band, pixels, cx - 30, cx + 29, 0x07e0);
            span(band, pixels, cx + 30, cx + 89, 0x001f);
        }
        if (scene->touch.pressed) {
            int offset = abs(y - scene->touch.y);
            if (offset <= MARK_RADIUS) {
                int arm = offset <= 1 ? MARK_RADIUS : 1;
                span(band, pixels, scene->touch.x - arm, scene->touch.x + arm, 0xffff);
            }
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
    if (!old.pressed) return baji_board_paint_round(board, b, paint, scene);
    if (!scene->touch.pressed) return baji_board_paint_round(board, a, paint, scene);
    int x = a.x < b.x ? a.x : b.x, y = a.y < b.y ? a.y : b.y;
    int right = a.x+a.width > b.x+b.width ? a.x+a.width : b.x+b.width;
    int bottom = a.y+a.height > b.y+b.height ? a.y+a.height : b.y+b.height;
    // Nearby samples share one transfer. A long drag must not dirty the entire diagonal.
    if ((right-x)*(bottom-y) <= a.width*a.height + b.width*b.height)
        return baji_board_paint_round(board, (baji_rect_t){x, y, right-x, bottom-y}, paint, scene);
    const baji_rect_t dirty[] = {a, b};
    return baji_board_paint_round_regions(board, dirty, 2, paint, scene);
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
    esp_err_t err = baji_board_paint_round(board, (baji_rect_t){0, 0, info->width, info->height}, paint, &scene);
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
        if (err == ESP_OK) {
            err = baji_peripherals_read_touch(io, &sample);
        } else if (err == ESP_ERR_TIMEOUT) {
            err = baji_peripherals_poll_touch(io, &sample);
            if (err == ESP_ERR_NOT_FINISHED) continue;
        }
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
