#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    BAJI_DISPLAY_WIDTH = 466,
    BAJI_DISPLAY_HEIGHT = 466,
    BAJI_DISPLAY_STRIP_ROWS = 32,
    BAJI_DISPLAY_MAX_TRANSFER_BYTES = BAJI_DISPLAY_WIDTH * BAJI_DISPLAY_STRIP_ROWS * 2
};

/* One task owns the display from init through deinit. Calls from another task,
 * including a second init, return ESP_ERR_INVALID_STATE. Task context only. */
esp_err_t baji_display_init(uint8_t brightness_percent);
esp_err_t baji_display_set_brightness(uint8_t percent);

/* The rectangle covers [x, x+width) x [y, y+height). CO5300 needs even x, y,
 * width and height, all in bounds, with exactly width*height*2 bytes. Each
 * call accepts at most BAJI_DISPLAY_MAX_TRANSFER_BYTES (29,824). Narrower
 * rectangles can be taller than BAJI_DISPLAY_STRIP_ROWS.
 *
 * Pixels are packed RGB565, high byte first (red: F8 00), with no row padding.
 * Supply 4-byte aligned internal DMA memory, e.g. heap_caps_malloc(bytes,
 * MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA). The driver never copies pixel data.
 *
 * A successful submission borrows the buffer until wait() succeeds, or the
 * next successful write/brightness/deinit drains it. A failed submission does
 * not borrow the proposed buffer. If draining an earlier write times out,
 * keep its buffer and call wait() again. To pipeline, prepare the next strip
 * in a second buffer while the first one transfers. Call wait() at batch
 * end, before going idle, to release the SPI bus and its frequency lock. */
esp_err_t baji_display_write_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                  const void *rgb565_be, size_t bytes);
esp_err_t baji_display_wait(void);
esp_err_t baji_display_deinit(void);

#ifdef __cplusplus
}
#endif
