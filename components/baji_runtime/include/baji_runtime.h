#pragma once

#include "baji_board.h"
#include "baji_input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Render on the board owner task. On success return the absolute monotonic
 * microsecond deadline for the next change in next_us. A deadline at or
 * before now requests the next scan. New touch input preempts any deadline.
 * INT64_MAX waits for input only; BAJI_FRAME_DONE ends the loop normally. */
#define BAJI_FRAME_DONE INT64_C(-1)
typedef esp_err_t (*baji_frame_fn)(void *context, const baji_input_t *input,
                                   int64_t now_us, int64_t *next_us);

typedef struct {
    uint8_t brightness;
    /* Optional caller-owned binary semaphore, dedicated to this loop.
     * A producer publishes its latest state under its own lock, then gives
     * the semaphore. Keep it alive until run returns. Repeated wakes coalesce. */
    SemaphoreHandle_t changed;
} baji_runtime_config_t;

/* The first frame completes before the panel lights. Later failed frames are
 * retried on the next scan; three consecutive errors terminate the loop.
 * Owns the touch reader and TE subscription for the duration of this call. */
esp_err_t baji_runtime_run(baji_board_t *board, const baji_board_info_t *info,
                           const baji_runtime_config_t *config, baji_frame_fn render, void *context);

#ifdef __cplusplus
}
#endif
