#pragma once

#include "baji_peripherals.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "baji_board_profile.h"

#define BAJI_I2C_TIMEOUT_MS 40

struct baji_peripherals {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t touch;
    i2c_master_dev_handle_t power;
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t irq;
    bool touch_ready;
    bool power_ready;
    bool reset_owned;
    bool irq_pin_owned;
    bool irq_handler_added;
};

/* init/read callers hold mutex. Each sensor can retry initialization independently. */
esp_err_t baji_attach_i2c(baji_peripherals_t *p, uint8_t address, i2c_master_dev_handle_t *dev);
esp_err_t baji_peripheral_lock(baji_peripherals_t *p);
esp_err_t baji_touch_release_pins(baji_peripherals_t *p);
