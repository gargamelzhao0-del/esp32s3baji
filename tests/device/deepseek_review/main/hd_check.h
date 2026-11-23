#pragma once
#include "baji_board.h"
#include <stdint.h>

typedef struct {
    uint8_t kind, item, flag, breath;
    int16_t pan_x, pan_y;
    uint32_t crc;
} hd_sample_t;
_Static_assert(sizeof(hd_sample_t) == 12, "USB HD record layout");
bool review_usb_read_exact(void *data, size_t size);
esp_err_t hd_check_pixels(baji_board_t *gallery_board, const void *pack, size_t bytes,
                          const hd_sample_t *samples, size_t count);
esp_err_t hd_check_motion(baji_board_t *board, const void *pack, size_t bytes, bool trace);
esp_err_t hd_check_switch(baji_board_t *board, const baji_board_info_t *info, const void *pixels,
                          size_t pixel_bytes, const void *hd, size_t hd_bytes);
