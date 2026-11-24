#pragma once

#include "baji_peripherals.h"

/* A quick tap can begin and end between two renders. presses identifies the
 * latest press even after release. touch.x/y retain the last contact point
 * on release. All coordinates are in display space. */
typedef struct {
    baji_touch_sample_t touch;
    uint32_t presses;
    uint16_t press_x, press_y;
    /* Sensor observation times on esp_timer's monotonic clock. A replay may
     * leave these zero. All fields remain latched after release. */
    int64_t press_us, touch_us;
    uint16_t travel; /* greatest sampled displacement on either axis since press */
} baji_input_t;
