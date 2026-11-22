#include "peripherals_private.h"
#include "peripheral_decode.h"

#define AXP_ADDRESS 0x34
#define AXP_IC_TYPE 0x03
#define AXP_CHIP_ID 0x4a
#define AXP_STATUS1 0x00
#define AXP_PERCENT 0xa4

static esp_err_t read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, buf, len, BAJI_I2C_TIMEOUT_MS);
}

esp_err_t baji_peripherals_power_init(baji_peripherals_t *p)
{
    esp_err_t err = baji_peripheral_lock(p);
    if (err != ESP_OK) return err;
    if (!p->power_ready) {
        err = baji_attach_i2c(p, AXP_ADDRESS, &p->power);
        uint8_t id;
        if (err == ESP_OK) err = read_reg(p->power, AXP_IC_TYPE, &id, 1);
        if (err == ESP_OK && id != AXP_CHIP_ID) err = ESP_ERR_INVALID_RESPONSE;
        p->power_ready = err == ESP_OK;
    }
    xSemaphoreGive(p->mutex);
    return err;
}

esp_err_t baji_peripherals_read_power(baji_peripherals_t *p, baji_power_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_err_t err = baji_peripheral_lock(p);
    if (err != ESP_OK) return err;
    uint8_t status[2], percent = UINT8_MAX;
    err = p->power_ready ? read_reg(p->power, AXP_STATUS1, status, sizeof(status)) : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK && (status[0] & (1U << 3))) {
        err = read_reg(p->power, AXP_PERCENT, &percent, 1);
    }
    if (err == ESP_OK) *out = baji_decode_power(status, percent);
    xSemaphoreGive(p->mutex);
    return err;
}
