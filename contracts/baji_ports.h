#pragma once
/* Draft application/transport contracts. The live hardware API is baji_board.h. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "baji_state.h"

typedef enum {
    BAJI_OK, BAJI_NOT_READY, BAJI_NOT_SUPPORTED, BAJI_IO_ERROR,
    BAJI_BAD_ASSET, BAJI_NO_MEMORY
} baji_result_t;
typedef struct { uint16_t x, y; uint64_t at_ms; bool pressed; } baji_touch_t;

baji_result_t baji_transport_start(void);
baji_result_t baji_transport_send(const uint8_t *frame, size_t bytes);
baji_result_t baji_assets_open(const char *character, const char *variant,
                               const char *pose);
baji_result_t baji_assets_prefetch(void);
void baji_ui_apply(const baji_view_t *view); /* LVGL owner task only */
void baji_input_on_touch(const baji_touch_t *sample);
