#include "peripherals_private.h"
#include "esp_log.h"

/* Fixed 1.75C playback configuration. Clock dividers correspond to the
 * ES8311 4.096 MHz MCLK / 16 kHz table in Espressif esp_codec_dev. The
 * amplifier stays off while the register sequence is applied. */
esp_err_t baji_peripherals_speaker_init(baji_peripherals_t *p)
{
    esp_err_t err = baji_peripheral_lock(p);
    if (err != ESP_OK) return err;
    if (!p->speaker_ready) {
        err = baji_attach_i2c(p, BAJI_ES8311_ADDRESS, &p->speaker);
        static const uint8_t setup[][2] = {
            {0x0d,0xfa}, {0x44,0x08}, {0x44,0x08}, /* analog bias and I2C noise immunity */
            {0x01,0x30}, {0x02,0x00}, {0x03,0x10}, {0x16,0x24}, {0x04,0x10},
            {0x05,0x00}, {0x0b,0x00}, {0x0c,0x00}, {0x10,0x1f}, {0x11,0x7f},
            {0x00,0x80}, {0x01,0x3f}, {0x06,0x03}, {0x07,0x00}, {0x08,0xff},
            {0x03,0x10}, {0x04,0x20}, {0x13,0x10}, {0x44,0x58},
            {0x09,0x0c}, {0x0a,0x4c}, /* 16-bit I2S input, ADC serial output disabled */
            {0x0e,0x02}, {0x12,0x00}, {0x14,0x1a}, {0x0d,0x01}, {0x37,0x08},
            {0x45,0x00}, {0x32,0xa8}, {0x31,0x00},
        };
        for (size_t i = 0; err == ESP_OK && i < sizeof(setup)/sizeof(setup[0]); i++)
            err = i2c_master_transmit(p->speaker, setup[i], sizeof(setup[i]), BAJI_I2C_TIMEOUT_MS);
        /* The verified diagnostic addresses each identification register
         * separately after bringing up the DAC. Do not rely on burst reads. */
        static const uint8_t regs[] = {0xfd, 0xfe, 0x32};
        uint8_t values[sizeof(regs)] = {0};
        for (size_t i = 0; err == ESP_OK && i < sizeof(regs); i++)
            err = i2c_master_transmit_receive(p->speaker, regs+i, 1, values+i, 1, BAJI_I2C_TIMEOUT_MS);
        if (err == ESP_OK && (values[0] != 0x83 || values[1] != 0x11 || values[2] != 0xa8)) {
            ESP_LOGW("baji_speaker", "unexpected DAC readback: addr=18 id=%02x%02x volume=%02x",
                     values[0], values[1], values[2]);
            err = ESP_ERR_INVALID_RESPONSE;
        }
        p->speaker_ready = err == ESP_OK;
        if (p->speaker_ready) ESP_LOGI("baji_speaker", "ES8311 id=8311 volume=a8 I2C=18");
    }
    xSemaphoreGive(p->mutex);
    return err;
}
