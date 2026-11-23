#pragma once

#include "baji_peripherals.h"

/* A quick tap can begin and end between two renders. presses identifies the
 * latest press even after release. touch.x/y retain the last contact point
 * on release. All coordinates are in display space. */
typedef struct {
    baji_touch_sample_t touch;
    uint32_t presses;
    uint16_t press_x, press_y;
} baji_input_t;
