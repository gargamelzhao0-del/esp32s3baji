#include "baji_board.h"
#include "baji_display.h"
#include "esp_heap_caps.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdlib.h>

struct baji_board {
    baji_peripherals_t *io;
    uint8_t *buffer[2];
    unsigned next;
    bool panel_open, closing, painting;
    TaskHandle_t owner;
};

static void free_board(baji_board_t *board)
{
    heap_caps_free(board->buffer[0]);
    heap_caps_free(board->buffer[1]);
    free(board);
}

esp_err_t baji_board_open(baji_board_t **out, baji_board_info_t *info)
{
    if (!out || !info) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    *info = (baji_board_info_t){ .touch_status = ESP_ERR_INVALID_STATE,
                                 .power_status = ESP_ERR_INVALID_STATE };
    baji_board_t *board = calloc(1, sizeof(*board));
    if (!board) return ESP_ERR_NO_MEM;
    for (unsigned i = 0; i < 2; ++i) {
        board->buffer[i] = heap_caps_malloc(BAJI_DISPLAY_MAX_TRANSFER_BYTES,
                                            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!board->buffer[i]) {
            free_board(board);
            return ESP_ERR_NO_MEM;
        }
    }
    esp_err_t err = baji_display_init(0);
    if (err != ESP_OK) {
        free_board(board);
        return err;
    }
    board->panel_open = true;
    board->owner = xTaskGetCurrentTaskHandle();
    info->width = BAJI_DISPLAY_WIDTH;
    info->height = BAJI_DISPLAY_HEIGHT;
    info->psram_bytes = esp_psram_get_size();
    info->flash_status = esp_flash_get_physical_size(NULL, &info->flash_bytes);
    err = baji_peripherals_open(&board->io);
    info->touch_status = err == ESP_OK ? baji_peripherals_touch_init(board->io) : err;
    info->power_status = err == ESP_OK ? baji_peripherals_power_init(board->io) : err;
    *out = board;
    return ESP_OK;
}

baji_peripherals_t *baji_board_peripherals(baji_board_t *board)
{
    return board && !board->closing ? board->io : NULL;
}

esp_err_t baji_board_set_brightness(baji_board_t *board, uint8_t percent)
{
    if (!board) return ESP_ERR_INVALID_ARG;
    if (board->owner != xTaskGetCurrentTaskHandle() || board->closing || board->painting)
        return ESP_ERR_INVALID_STATE;
    return baji_display_set_brightness(percent);
}

/* Submit windows back to back. Only the caller drains the last transfer;
 * the second buffer can be painted while the previous window is on the wire. */
static esp_err_t submit_window(baji_board_t *board, baji_rect_t band, baji_paint_fn paint, void *scene)
{
    const unsigned end = band.y + band.height;
    const unsigned max_rows = (BAJI_DISPLAY_MAX_TRANSFER_BYTES / (2U * band.width)) & ~1U;
    esp_err_t err = ESP_OK;
    for (unsigned y = band.y; y < end && err == ESP_OK; y += band.height) {
        band.y = y;
        band.height = end-y < max_rows ? end-y : max_rows;
        uint8_t *pixels = board->buffer[board->next];
        paint(&band, pixels, scene);
        err = baji_display_write_rect(band.x, band.y, band.width, band.height,
                                      pixels, (size_t)band.width * band.height * 2);
        // Only the accepted buffer becomes DMA-owned. After an error the next
        // paint still uses the free buffer, even if the previous DMA is pending.
        if (err == ESP_OK) board->next ^= 1U;
    }
    return err;
}

/* Measured from the top of this 466px panel. The lower cap is its mirror.
 * Each inset is rounded down to an even x at the widest row of the band,
 * using (2*x-465)^2 + (2*y-465)^2 <= 471^2. The radius includes the
 * same three-pixel guard used on the MotoNav 1.75C. */
static const struct { uint8_t bottom, inset; } cap[] = {
    {14, 148}, {34, 108}, {60, 74}, {88, 48}, {122, 26}, {154, 10}
};
_Static_assert(BAJI_DISPLAY_WIDTH == 466 && BAJI_DISPLAY_HEIGHT == 466,
               "round aperture is specific to the Waveshare 1.75C");

static int outer_distance(unsigned first, unsigned past_last)
{
    int low = 465 - 2 * (int)first, high = 2 * ((int)past_last - 1) - 465;
    return low > high ? low : high;
}

static esp_err_t submit_round(baji_board_t *board, baji_rect_t area, baji_paint_fn paint, void *scene)
{
    const unsigned right = area.x + area.width, bottom = area.y + area.height;
    int dx = outer_distance(area.x, right), dy = outer_distance(area.y, bottom);
    if (dx*dx + dy*dy <= 471*471)
        return submit_window(board, area, paint, scene);

    enum { N = sizeof(cap) / sizeof(cap[0]) };
    unsigned top = 0;
    baji_rect_t window = {0};
    for (unsigned i = 0; i <= 2 * N && top < bottom; ++i) {
        unsigned end, inset;
        if (i < N) {
            end = cap[i].bottom;
            inset = cap[i].inset;
        } else if (i == N) {
            end = BAJI_DISPLAY_HEIGHT - cap[N-1].bottom;
            inset = 0;
        } else {
            unsigned j = 2 * N - i;
            end = BAJI_DISPLAY_HEIGHT - (j ? cap[j-1].bottom : 0);
            inset = cap[j].inset;
        }
        unsigned y0 = area.y > top ? area.y : top, y1 = bottom < end ? bottom : end;
        unsigned x0 = area.x > inset ? area.x : inset;
        unsigned limit = BAJI_DISPLAY_WIDTH - inset, x1 = right < limit ? right : limit;
        if (y0 < y1 && x0 < x1) {
            /* A narrow dirty area may intersect several aperture bands at
             * identical columns. Let the DMA byte limit alone split that run. */
            if (window.x == x0 && window.width == x1-x0 && window.y+window.height == y0) {
                window.height += y1-y0;
            } else {
                if (window.height) {
                    esp_err_t err = submit_window(board, window, paint, scene);
                    if (err != ESP_OK) return err;
                }
                window = (baji_rect_t){x0, y0, x1-x0, y1-y0};
            }
        }
        top = end;
    }
    return window.height ? submit_window(board, window, paint, scene) : ESP_OK;
}

static esp_err_t paint_areas(baji_board_t *board, const baji_rect_t *areas, size_t count,
                             baji_paint_fn paint, void *scene, bool round)
{
    if (!board || !paint || !areas || !count) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < count; ++i) {
        baji_rect_t r = areas[i];
        if (!r.width || !r.height || r.x + r.width > BAJI_DISPLAY_WIDTH ||
            r.y + r.height > BAJI_DISPLAY_HEIGHT) return ESP_ERR_INVALID_ARG;
    }
    if (board->owner != xTaskGetCurrentTaskHandle() || board->closing || board->painting)
        return ESP_ERR_INVALID_STATE;
    board->painting = true;
    esp_err_t err = ESP_OK;
    for (size_t i = 0; i < count && err == ESP_OK; ++i) {
        baji_rect_t area = areas[i];
        const unsigned right = area.x + area.width, bottom = area.y + area.height;
        area.x &= ~1U;
        area.y &= ~1U;
        area.width = ((right + 1U) & ~1U) - area.x;
        area.height = ((bottom + 1U) & ~1U) - area.y;
        err = round ? submit_round(board, area, paint, scene) : submit_window(board, area, paint, scene);
    }
    if (err == ESP_OK) err = baji_display_wait();
    board->painting = false;
    return err;
}

esp_err_t baji_board_paint(baji_board_t *board, baji_rect_t area, baji_paint_fn paint, void *scene)
{
    return paint_areas(board, &area, 1, paint, scene, false);
}

esp_err_t baji_board_paint_round(baji_board_t *board, baji_rect_t area, baji_paint_fn paint, void *scene)
{
    return paint_areas(board, &area, 1, paint, scene, true);
}

esp_err_t baji_board_paint_round_regions(baji_board_t *board, const baji_rect_t *areas, size_t count,
                                         baji_paint_fn paint, void *scene)
{
    return paint_areas(board, areas, count, paint, scene, true);
}

esp_err_t baji_board_close(baji_board_t *board)
{
    if (!board) return ESP_ERR_INVALID_ARG;
    if (board->owner != xTaskGetCurrentTaskHandle() || board->painting) return ESP_ERR_INVALID_STATE;
    board->closing = true;
    if (board->panel_open) {
        esp_err_t err = baji_display_deinit();
        if (err != ESP_OK) return err;
        board->panel_open = false;
    }
    if (board->io) {
        esp_err_t err = baji_peripherals_close(board->io);
        if (err != ESP_OK) return err;
    }
    free_board(board);
    return ESP_OK;
}
