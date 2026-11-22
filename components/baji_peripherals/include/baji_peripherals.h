#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct baji_peripherals baji_peripherals_t;

typedef struct {
    bool pressed;
    uint16_t x; /* Display pixel coordinates; meaningful while pressed. */
    uint16_t y;
} baji_touch_sample_t;

typedef struct {
    bool battery_present;
    bool usb_present; /* PMIC reports usable VBUS. */
    bool charging;
    bool percent_valid;
    uint8_t percent; /* 0..100 only when percent_valid. */
} baji_power_status_t;

/* One owner for the board's I2C bus. A failed open leaves *out == NULL. */
esp_err_t baji_peripherals_open(baji_peripherals_t **out);
/* Each device initializes independently. Calls are idempotent and retryable. */
esp_err_t baji_peripherals_touch_init(baji_peripherals_t *p);
esp_err_t baji_peripherals_power_init(baji_peripherals_t *p);

/* Single consumer, task context. Starts with one pending sample, then coalesces
 * falling edges. ESP_ERR_TIMEOUT means no pending edge. A held contact should
 * also be sampled periodically to catch release even without a final IRQ. */
esp_err_t baji_peripherals_wait_touch(baji_peripherals_t *p, uint32_t timeout_ms);
/* Task context. Both reads publish a new sample only on ESP_OK. */
esp_err_t baji_peripherals_read_touch(baji_peripherals_t *p, baji_touch_sample_t *out);
esp_err_t baji_peripherals_read_power(baji_peripherals_t *p, baji_power_status_t *out);
/* Stop/join all callers first. ESP_OK consumes the handle; an error allows retry. */
esp_err_t baji_peripherals_close(baji_peripherals_t *p);

#ifdef __cplusplus
}
#endif
