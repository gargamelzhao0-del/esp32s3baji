#pragma once

#include "baji_peripherals.h"

typedef enum {
    BAJI_POWER_HIDDEN, BAJI_POWER_UNKNOWN, BAJI_POWER_NO_BATTERY, BAJI_POWER_USB_ONLY,
    BAJI_POWER_BATTERY, BAJI_POWER_USB_BATTERY, BAJI_POWER_CHARGING,
} baji_power_source_t;

typedef struct {
    baji_power_source_t source;
    int8_t percent; /* -1 for unavailable; otherwise the PMIC's reported 0..100. */
} baji_power_reading_t;

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
    uint16_t travel;   /* greatest sampled displacement on either axis since press */
    uint16_t travel_y; /* greatest vertical displacement for anisotropic gesture zones */
    baji_power_reading_t power;
} baji_input_t;
