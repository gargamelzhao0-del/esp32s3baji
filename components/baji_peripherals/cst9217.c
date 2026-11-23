/* SPDX-License-Identifier: Apache-2.0
 * CST9217 wire protocol and command-mode sequence follow Waveshare's Apache-2.0
 * esp_lcd_touch_cst9217 driver (Espressif Systems 2015-2024, Waveshare 2025).
 */
#include "peripherals_private.h"
#include "peripheral_decode.h"
#include "esp_attr.h"
#include "freertos/task.h"

#define CST_ADDRESS 0x5a
#define CST_REPORT 0xd000
#define CST_COMMAND_MODE 0xd101
#define CST_PROJECT_ID 0xd204
#define CST_RESOLUTION 0xd1f8
#define CST_STARTUP_ATTEMPTS 3

/* This board has one touch IRQ. The critical section also synchronizes teardown
 * with an ISR already running on the other core. */
static portMUX_TYPE irq_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t irq_target;

static void IRAM_ATTR touch_irq(void *unused)
{
    (void)unused;
    BaseType_t woke = pdFALSE;
    portENTER_CRITICAL_ISR(&irq_lock);
    if (irq_target) xSemaphoreGiveFromISR(irq_target, &woke);
    portEXIT_CRITICAL_ISR(&irq_lock);
    if (woke) portYIELD_FROM_ISR();
}

static void delay_ms(unsigned ms)
{
    /* The next tick can arrive immediately. Round up and allow that first tick. */
    vTaskDelay((ms * configTICK_RATE_HZ + 999) / 1000 + 1);
}

static esp_err_t select_register(baji_peripherals_t *p, uint16_t reg)
{
    const uint8_t address[] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return i2c_master_transmit(p->touch, address, sizeof(address), BAJI_I2C_TIMEOUT_MS);
}

static esp_err_t read_register(baji_peripherals_t *p, uint16_t reg, uint8_t *out, size_t size)
{
    esp_err_t err = select_register(p, reg);
    if (err != ESP_OK) return err;
    delay_ms(2); /* The controller requires STOP and a delay between TX and RX. */
    return i2c_master_receive(p->touch, out, size, BAJI_I2C_TIMEOUT_MS);
}

esp_err_t baji_touch_release_pins(baji_peripherals_t *p)
{
    if (p->irq_handler_added) {
        esp_err_t err = gpio_isr_handler_remove(BAJI_TOUCH_INT);
        if (err != ESP_OK) return err;
        portENTER_CRITICAL(&irq_lock);
        irq_target = NULL;
        portEXIT_CRITICAL(&irq_lock);
        p->irq_handler_added = false;
    }
    if (p->irq_pin_owned) {
        esp_err_t err = gpio_reset_pin(BAJI_TOUCH_INT);
        if (err != ESP_OK) return err;
        p->irq_pin_owned = false;
    }
    if (p->reset_owned) {
        esp_err_t err = gpio_reset_pin(BAJI_TOUCH_RST);
        if (err != ESP_OK) return err;
        p->reset_owned = false;
    }
    return ESP_OK;
}

static esp_err_t identify_controller(baji_peripherals_t *p)
{
    esp_err_t err = select_register(p, CST_COMMAND_MODE);
    if (err != ESP_OK) return err;
    delay_ms(2);
    err = select_register(p, CST_COMMAND_MODE);
    if (err != ESP_OK) return err;
    delay_ms(10);
    uint8_t id[4];
    err = read_register(p, CST_PROJECT_ID, id, sizeof(id));
    if (err != ESP_OK) return err;
    /* The donor bench boot capture reports chip type 0x9217 and 466 x 466. */
    if (id[2] != 0x17 || id[3] != 0x92) return ESP_ERR_INVALID_RESPONSE;
    uint8_t resolution[4];
    err = read_register(p, CST_RESOLUTION, resolution, sizeof(resolution));
    if (err != ESP_OK) return err;
    if ((resolution[0] | (resolution[1] << 8)) != BAJI_LCD_WIDTH ||
        (resolution[2] | (resolution[3] << 8)) != BAJI_LCD_HEIGHT) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t start_touch(baji_peripherals_t *p)
{
    esp_err_t err = baji_attach_i2c(p, CST_ADDRESS, &p->touch);
    if (err != ESP_OK) return err;
    const gpio_config_t reset_cfg = {
        .pin_bit_mask = 1ULL << BAJI_TOUCH_RST, .mode = GPIO_MODE_OUTPUT,
    };
    err = gpio_config(&reset_cfg);
    if (err != ESP_OK) return err;
    p->reset_owned = true;
    err = gpio_set_level(BAJI_TOUCH_RST, 0);
    if (err != ESP_OK) return err;
    delay_ms(10);
    err = gpio_set_level(BAJI_TOUCH_RST, 1);
    if (err != ESP_OK) return err;
    delay_ms(50);
    /* I2C may NACK while the controller is coming out of reset.
     * Replay command mode and identification together, never the reset or IRQ setup. */
    for (unsigned attempt = 0; attempt < CST_STARTUP_ATTEMPTS; ++attempt) {
        if (attempt) delay_ms(5);
        err = identify_controller(p);
        if (err == ESP_OK) break;
    }
    if (err != ESP_OK) return err;
    const gpio_config_t irq_cfg = {
        .pin_bit_mask = 1ULL << BAJI_TOUCH_INT,
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    err = gpio_config(&irq_cfg);
    if (err != ESP_OK) return err;
    p->irq_pin_owned = true;
    /* IDF's ISR service is process-wide. Leave it installed for other clients. */
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = gpio_isr_handler_add(BAJI_TOUCH_INT, touch_irq, NULL);
    if (err != ESP_OK) return err;
    p->irq_handler_added = true;
    portENTER_CRITICAL(&irq_lock);
    irq_target = p->irq;
    portEXIT_CRITICAL(&irq_lock);
    /* A report already asserted during IRQ setup has no new falling edge. */
    if (gpio_get_level(BAJI_TOUCH_INT) == 0) xSemaphoreGive(p->irq);
    return ESP_OK;
}

esp_err_t baji_peripherals_touch_init(baji_peripherals_t *p)
{
    esp_err_t err = baji_peripheral_lock(p);
    if (err != ESP_OK) return err;
    if (!p->touch_ready) {
        err = start_touch(p);
        p->touch_ready = err == ESP_OK;
        if (err != ESP_OK) {
            esp_err_t cleanup = baji_touch_release_pins(p);
            if (cleanup != ESP_OK) err = cleanup;
        }
    }
    xSemaphoreGive(p->mutex);
    return err;
}

esp_err_t baji_peripherals_wait_touch(baji_peripherals_t *p, uint32_t timeout_ms)
{
    esp_err_t err = baji_peripheral_lock(p);
    if (err != ESP_OK) return err;
    bool ready = p->touch_ready;
    xSemaphoreGive(p->mutex);
    if (!ready) return ESP_ERR_INVALID_STATE;
    uint64_t ticks = ((uint64_t)timeout_ms * configTICK_RATE_HZ + 999) / 1000;
    if (ticks >= portMAX_DELAY) ticks = portMAX_DELAY - 1;
    return xSemaphoreTake(p->irq, (TickType_t)ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t read_touch(baji_peripherals_t *p, baji_touch_sample_t *out, bool polling)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_err_t err = baji_peripheral_lock(p);
    if (err != ESP_OK) return err;
    uint8_t report[10];
    err = p->touch_ready ? read_register(p, CST_REPORT, report, sizeof(report)) : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        /* With no pending IRQ the chip can return register contents instead of
         * a report. Only a successful bus read can mean "no report yet". */
        if (polling && report[6] != BAJI_CST_REPORT_MARKER) err = ESP_ERR_NOT_FINISHED;
        else err = baji_decode_touch(report, BAJI_LCD_WIDTH, BAJI_LCD_HEIGHT, out);
    }
    xSemaphoreGive(p->mutex);
    return err;
}

esp_err_t baji_peripherals_read_touch(baji_peripherals_t *p, baji_touch_sample_t *out)
{
    return read_touch(p, out, false);
}

esp_err_t baji_peripherals_poll_touch(baji_peripherals_t *p, baji_touch_sample_t *out)
{
    return read_touch(p, out, true);
}
