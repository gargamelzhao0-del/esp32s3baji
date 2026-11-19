#pragma once
/* DESIGN CONTRACTS ONLY. No hardware implementation is linked by this header.
 * Ownership and error semantics are specified in docs/02-architecture.md.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "baji_state.h"

typedef enum {
    BAJI_OK, BAJI_NOT_READY, BAJI_NOT_SUPPORTED, BAJI_IO_ERROR,
    BAJI_BAD_ASSET, BAJI_NO_MEMORY
} baji_result_t;
typedef struct {
    bool battery_present;
    bool battery_valid;
    uint8_t battery_percent;
    bool charging;
} baji_power_sample_t;
typedef struct { uint16_t x, y; uint64_t at_ms; bool pressed; } baji_touch_t;

baji_result_t baji_board_init(void);
baji_result_t baji_board_read_power(baji_power_sample_t *out);
baji_result_t baji_board_set_brightness(uint8_t percent);
baji_result_t baji_transport_start(void);
baji_result_t baji_transport_send(const uint8_t *frame, size_t bytes);
baji_result_t baji_assets_open(const char *character, const char *variant,
                               const char *pose);
baji_result_t baji_assets_prefetch(void);
void baji_ui_apply(const baji_view_t *view); /* LVGL owner task only */
void baji_input_on_touch(const baji_touch_t *sample);
