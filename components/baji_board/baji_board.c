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

esp_err_t baji_board_paint(baji_board_t *board, baji_rect_t area, baji_paint_fn paint, void *scene)
{
    const unsigned right = (unsigned)area.x + area.width, bottom = (unsigned)area.y + area.height;
    if (!board || !paint || !area.width || !area.height ||
        right > BAJI_DISPLAY_WIDTH || bottom > BAJI_DISPLAY_HEIGHT) return ESP_ERR_INVALID_ARG;
    if (board->owner != xTaskGetCurrentTaskHandle() || board->closing || board->painting)
        return ESP_ERR_INVALID_STATE;
    // CO5300 windows are even on both axes. Let the scene reproduce the real
    // neighboring pixels, including at the right/bottom edge of the panel.
    const unsigned left = area.x & ~1U, top = area.y & ~1U;
    const unsigned end = (bottom + 1U) & ~1U;
    baji_rect_t band = {.x = left, .width = ((right + 1U) & ~1U) - left};
    // Spend the DMA budget on rows for narrow windows (e.g. a tall cursor).
    const unsigned max_rows = (BAJI_DISPLAY_MAX_TRANSFER_BYTES / (2U * band.width)) & ~1U;
    board->painting = true;
    esp_err_t err = ESP_OK;
    for (unsigned y = top; y < end && err == ESP_OK; y += band.height) {
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
    if (err == ESP_OK) err = baji_display_wait();
    board->painting = false;
    return err;
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
