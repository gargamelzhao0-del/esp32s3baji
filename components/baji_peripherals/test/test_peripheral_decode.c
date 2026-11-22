#include <assert.h>
#include <stdio.h>
#include "peripheral_decode.h"

static void check_pressed(uint16_t raw_x, uint16_t raw_y, uint16_t expected_x, uint16_t expected_y)
{
    uint8_t frame[10] = {0x16, raw_x >> 4, raw_y >> 4, (raw_x << 4) | (raw_y & 15), 0, 0x81, 0xab};
    baji_touch_sample_t sample = {0};
    assert(baji_decode_touch(frame, 466, 466, &sample) == ESP_OK);
    assert(sample.pressed && sample.x == expected_x && sample.y == expected_y);
}

int main(void)
{
    for (uint16_t x = 0; x < 466; ++x) {
        for (uint16_t y = 0; y < 466; ++y) check_pressed(x, y, 465 - x, 465 - y);
    }
    uint8_t frame[10] = {6, 0x1d, 0, 0x20, 0, 1, 0xab}; /* x=466 */
    baji_touch_sample_t sample = {.pressed = true, .x = 2, .y = 7};
    assert(baji_decode_touch(frame, 466, 466, &sample) == ESP_ERR_INVALID_RESPONSE);
    assert(sample.pressed && sample.x == 2 && sample.y == 7);
    frame[5] = 0;
    assert(baji_decode_touch(frame, 466, 466, &sample) == ESP_OK && !sample.pressed);
    frame[5] = 1;
    frame[0] = 4;
    assert(baji_decode_touch(frame, 466, 466, &sample) == ESP_OK && !sample.pressed);
    frame[6] = 0;
    assert(baji_decode_touch(frame, 466, 466, &sample) == ESP_ERR_INVALID_RESPONSE);

    const uint8_t charge[] = {0x28, 0x20}, discharge[] = {0x08, 0x40};
    const uint8_t unplugged[] = {0x20, 0}, bad_vbus[] = {0x28, 0x08};
    baji_power_status_t power = baji_decode_power(charge, 100);
    assert(power.usb_present && power.charging && power.battery_present && power.percent_valid && power.percent == 100);
    power = baji_decode_power(discharge, 0);
    assert(!power.usb_present && !power.charging && power.battery_present && power.percent_valid && power.percent == 0);
    power = baji_decode_power(unplugged, 0);
    assert(power.usb_present && !power.battery_present && !power.percent_valid);
    power = baji_decode_power(bad_vbus, 101);
    assert(!power.usb_present && power.battery_present && !power.percent_valid);
    printf("%u mirrored touch positions and touch/power edge cases passed\n", 466U * 466U);
}
