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

/* Single-consumer input API. Wait for an IRQ, then read one report. After a
 * quiet interval, poll once to recover a missed change. ESP_ERR_NOT_FINISHED
 * means no report was available. Other errors are propagated. Only ESP_OK
 * updates out. The wait duration excludes the subsequent I2C transaction. */
esp_err_t baji_peripherals_next_touch(baji_peripherals_t *p, uint32_t wait_ms,
                                      baji_touch_sample_t *out);

/* Single consumer, task context. Coalesces falling edges and includes a report
 * already asserted at startup. ESP_ERR_TIMEOUT means no pending edge. */
esp_err_t baji_peripherals_wait_touch(baji_peripherals_t *p, uint32_t timeout_ms);
/* Task context. Both reads publish a new sample only on ESP_OK. */
esp_err_t baji_peripherals_read_touch(baji_peripherals_t *p, baji_touch_sample_t *out);
/* Poll after an IRQ timeout to recover a missed edge or release. A successful
 * transfer without a report marker returns ESP_ERR_NOT_FINISHED, leaving out
 * untouched. After a signalled IRQ, use read_touch for strict validation. */
esp_err_t baji_peripherals_poll_touch(baji_peripherals_t *p, baji_touch_sample_t *out);
esp_err_t baji_peripherals_read_power(baji_peripherals_t *p, baji_power_status_t *out);
/* Configure the playback DAC for I2S 16 kHz / stereo S16 with a 256× MCLK.
 * The caller supplies the clock before configuring the DAC. */
esp_err_t baji_peripherals_speaker_init(baji_peripherals_t *p);
/* Stop/join all callers first. ESP_OK consumes the handle. On failure the
 * partially closed handle accepts only another close attempt. */
esp_err_t baji_peripherals_close(baji_peripherals_t *p);

#ifdef __cplusplus
}
#endif
