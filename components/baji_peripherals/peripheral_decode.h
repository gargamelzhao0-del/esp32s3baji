#pragma once

#include "baji_peripherals.h"

#define BAJI_CST_REPORT_MARKER 0xab

/* Waveshare CST9217 single-point packet, first contact in a multi-contact report. */
static inline esp_err_t baji_decode_touch(const uint8_t data[10], uint16_t width, uint16_t height,
                                         baji_touch_sample_t *out)
{
    if (data[6] != BAJI_CST_REPORT_MARKER) return ESP_ERR_INVALID_RESPONSE;
    baji_touch_sample_t sample = {0};
    if ((data[5] & 0x7f) && (data[0] & 0x0f) == 6) {
        uint16_t x = ((uint16_t)data[1] << 4) | (data[3] >> 4);
        uint16_t y = ((uint16_t)data[2] << 4) | (data[3] & 0x0f);
        if (x >= width || y >= height) return ESP_ERR_INVALID_RESPONSE;
        sample.pressed = true;
        sample.x = width - 1 - x;
        sample.y = height - 1 - y;
    }
    *out = sample;
    return ESP_OK;
}

/* AXP2101 STATUS1/2 and BAT_PERCENT_DATA, consistent with XPowersAXP2101. */
static inline baji_power_status_t baji_decode_power(const uint8_t status[2], uint8_t percent)
{
    bool battery_present = (status[0] & (1U << 3)) != 0;
    bool valid = battery_present && percent <= 100;
    baji_power_status_t sample = {
        .battery_present = battery_present,
        .usb_present = (status[0] & (1U << 5)) && !(status[1] & (1U << 3)),
        .charging = battery_present && (status[1] >> 5) == 1,
        .percent_valid = valid,
        .percent = valid ? percent : 0,
    };
    return sample;
}
