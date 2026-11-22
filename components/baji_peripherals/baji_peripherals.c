#include "peripherals_private.h"
#include "esp_heap_caps.h"

esp_err_t baji_attach_i2c(baji_peripherals_t *p, uint8_t address, i2c_master_dev_handle_t *dev)
{
    if (*dev) return ESP_OK;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(p->bus, &cfg, dev);
}

esp_err_t baji_peripheral_lock(baji_peripherals_t *p)
{
    if (!p) return ESP_ERR_INVALID_ARG;
    return xSemaphoreTake(p->mutex, pdMS_TO_TICKS(250)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t baji_peripherals_open(baji_peripherals_t **out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    baji_peripherals_t *p = heap_caps_calloc(1, sizeof(*p), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) return ESP_ERR_NO_MEM;
    p->mutex = xSemaphoreCreateMutex();
    p->irq = xSemaphoreCreateBinary();
    esp_err_t err = ESP_ERR_NO_MEM;
    if (p->mutex && p->irq) {
        const i2c_master_bus_config_t cfg = {
            .i2c_port = BAJI_I2C_PORT,
            .sda_io_num = BAJI_I2C_SDA,
            .scl_io_num = BAJI_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        err = i2c_new_master_bus(&cfg, &p->bus);
        if (err == ESP_OK) {
            *out = p;
            return ESP_OK;
        }
    }
    if (p->irq) vSemaphoreDelete(p->irq);
    if (p->mutex) vSemaphoreDelete(p->mutex);
    heap_caps_free(p);
    return err;
}

static esp_err_t detach(i2c_master_dev_handle_t *dev)
{
    if (!*dev) return ESP_OK;
    esp_err_t err = i2c_master_bus_rm_device(*dev);
    if (err == ESP_OK) *dev = NULL;
    return err;
}

esp_err_t baji_peripherals_close(baji_peripherals_t *p)
{
    if (!p) return ESP_ERR_INVALID_ARG;
    p->touch_ready = false;
    p->power_ready = false;
    esp_err_t err = baji_touch_release_pins(p);
    if (err == ESP_OK) err = detach(&p->touch);
    if (err == ESP_OK) err = detach(&p->power);
    if (err == ESP_OK) err = i2c_del_master_bus(p->bus);
    if (err != ESP_OK) return err; /* The owner can retry close. */
    vSemaphoreDelete(p->irq);
    vSemaphoreDelete(p->mutex);
    heap_caps_free(p);
    return ESP_OK;
}
