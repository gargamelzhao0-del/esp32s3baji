#pragma once

#include <stddef.h>
#include <stdint.h>
#include "baji_graphics.h"
#include "baji_peripherals.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct baji_board baji_board_t;
typedef struct {
    uint16_t width, height;
    uint32_t psram_bytes, flash_bytes;
    esp_err_t flash_status;
    esp_err_t touch_status, power_status;
} baji_board_info_t;
/* Starts the panel dark. Optional touch and fuel gauge report independent
 * results; set brightness after the initial frame has reached the panel. */
esp_err_t baji_board_open(baji_board_t **out, baji_board_info_t *info);
esp_err_t baji_board_paint(baji_board_t *board, baji_rect_t area, baji_paint_fn paint, void *scene);
/* Same callback and completion contract, limited to the round display with
 * conservative overscan. Hidden corners may be skipped entirely. Use paint()
 * when the contents of the complete rectangular GRAM matter. */
esp_err_t baji_board_paint_round(baji_board_t *board, baji_rect_t area, baji_paint_fn paint, void *scene);
/* Render disjoint or overlapping dirty regions in caller order, then wait
 * for the final DMA. Keep the list valid and unchanged throughout the call.
 * All bounds are checked before the first pixel is sent. */
esp_err_t baji_board_paint_round_regions(baji_board_t *board, const baji_rect_t *areas, size_t count,
                                         baji_paint_fn paint, void *scene);
esp_err_t baji_board_set_brightness(baji_board_t *board, uint8_t percent);
/* Borrowed for the lifetime of the board. May be NULL if I2C failed to start. */
baji_peripherals_t *baji_board_peripherals(baji_board_t *board);
/* On failure keep the handle and retry close on the owner task. */
esp_err_t baji_board_close(baji_board_t *board);

#ifdef __cplusplus
}
#endif
