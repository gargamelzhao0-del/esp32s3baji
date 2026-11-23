#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t seq;
    int64_t edge_us;
    int64_t observed_us;
} baji_vsync_edge_t;

/* One display task owns the TE input. Start after panel initialization; TE
 * and touch share IDF's process-wide GPIO interrupt service. */
esp_err_t baji_vsync_open(void);

/* Monotonic microseconds, sampled together with the latest rising-edge count.
 * Before the first edge or while closed, seq and edge_us are zero. */
baji_vsync_edge_t baji_vsync_snapshot(void);

/* Return the newest edge after after_seq. Multiple pending edges coalesce.
 * On timeout the snapshot is still filled; zero timeout is a poll. */
esp_err_t baji_vsync_wait(uint32_t after_seq, uint32_t timeout_ms, baji_vsync_edge_t *edge);

/* The opening task releases the pin and handler. Retry on cleanup failure. */
esp_err_t baji_vsync_close(void);

#ifdef __cplusplus
}
#endif
