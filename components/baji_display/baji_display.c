/* SPDX-License-Identifier: Apache-2.0
 * CO5300 source: Copyright 2023 Espressif Systems (Shanghai) CO LTD.
 * Panel settings from Waveshare ESP32-S3-Touch-AMOLED-1.75C BSP v3.0.0
 * and esp_lcd_co5300 v2.2.0 (Copyright 2023 Espressif Systems (Shanghai) CO LTD).
 * Adapted from MotoNav's qspi_panel and StripStream for a single bounded
 * RGB565 DMA job. Upstream license: LICENSES/Apache-2.0.txt. */
#include "baji_display.h"
#include "baji_board_profile.h"

#include <stdbool.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

_Static_assert(BAJI_DISPLAY_WIDTH == BAJI_LCD_WIDTH && BAJI_DISPLAY_HEIGHT == BAJI_LCD_HEIGHT,
               "display geometry must match the board profile");
_Static_assert(BAJI_DISPLAY_MAX_TRANSFER_BYTES <= 32768, "one ESP32-S3 DMA transaction per strip");

enum { CMD_CASET = 0x2a, CMD_RASET = 0x2b, CMD_RAMWR = 0x2c, CMD_RAMWRC = 0x3c,
       CMD_BRIGHTNESS = 0x51, IO_TIMEOUT_MS = 250 };

static struct {
    spi_device_handle_t device;
    spi_transaction_t color;
    bool bus_owned, reset_owned, ready, pending;
    /* next_y == 0 means there is no open column window to continue. */
    uint16_t previous_x, previous_width, next_y;
} panel;

static TaskHandle_t owner;
static portMUX_TYPE owner_lock = portMUX_INITIALIZER_UNLOCKED;

/* Only lifecycle ownership is shared. The designated task owns all other
 * fields, including the persistent descriptor passed to the SPI driver. */
static bool owns_display(void)
{
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&owner_lock);
    bool matches = owner == caller;
    portEXIT_CRITICAL(&owner_lock);
    return matches;
}

static esp_err_t control(uint8_t reg, const uint8_t *data, size_t count)
{
    /* All commands for this board carry 0..4 inline data bytes. Control is
     * polled between colors; register/address stay single-lane on CO5300. */
    spi_transaction_t transaction = {
        .cmd = 0x02,
        .addr = (uint32_t)reg << 8,
        .flags = count ? SPI_TRANS_USE_TXDATA : 0,
        .length = count * 8,
    };
    if (count) memcpy(transaction.tx_data, data, count);
    return spi_device_polling_transmit(panel.device, &transaction);
}

static esp_err_t configure_panel(uint8_t brightness)
{
    const gpio_config_t reset = {
        .pin_bit_mask = 1ULL << BAJI_LCD_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&reset);
    if (err != ESP_OK) return err;
    panel.reset_owned = true;
    err = gpio_set_level(BAJI_LCD_RST, 0);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));
    err = gpio_set_level(BAJI_LCD_RST, 1);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(150));

    /* Values and delays used on the reference board. Set the requested
     * brightness before DISPON so startup never flashes at full brightness. */
    static const struct {
        uint8_t reg, count, data[4];
        uint16_t delay_ms;
    } init[] = {
        {0x36, 1, {0x00}, 0}, {0x3a, 1, {0x55}, 0},
        {0xfe, 1, {0x20}, 0}, {0x19, 1, {0x10}, 0}, {0x1c, 1, {0xa0}, 0},
        {0xfe, 1, {0x00}, 0}, {0xc4, 1, {0x80}, 0}, {0x3a, 1, {0x55}, 0},
        {0x35, 1, {0x00}, 0}, {0x53, 1, {0x20}, 0}, {CMD_BRIGHTNESS, 1, {0}, 0},
        {0x63, 1, {0xff}, 0},
        {CMD_CASET, 4, {0x00, 0x06, 0x01, 0xd7}, 0},
        {CMD_RASET, 4, {0x00, 0x00, 0x01, 0xd1}, 600},
        {0x11, 0, {0}, 600}, {0x29, 0, {0}, 0},
    };
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
        const uint8_t *value = init[i].reg == CMD_BRIGHTNESS ? &brightness : init[i].data;
        err = control(init[i].reg, value, init[i].count);
        if (err != ESP_OK) return err;
        if (init[i].delay_ms) vTaskDelay(pdMS_TO_TICKS(init[i].delay_ms));
    }
    return ESP_OK;
}

static esp_err_t release_hardware(void)
{
    esp_err_t err;
    if (panel.device) {
        err = spi_bus_remove_device(panel.device);
        if (err != ESP_OK) return err;
        panel.device = NULL;
    }
    if (panel.bus_owned) {
        err = spi_bus_free(BAJI_LCD_HOST);
        if (err != ESP_OK) return err;
        panel.bus_owned = false;
    }
    if (panel.reset_owned) {
        err = gpio_reset_pin(BAJI_LCD_RST);
        if (err != ESP_OK) return err;
        panel.reset_owned = false;
    }
    panel.next_y = 0;
    portENTER_CRITICAL(&owner_lock);
    owner = NULL;
    portEXIT_CRITICAL(&owner_lock);
    return ESP_OK;
}

esp_err_t baji_display_init(uint8_t brightness_percent)
{
    if (brightness_percent > 100) return ESP_ERR_INVALID_ARG;
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&owner_lock);
    bool acquired = owner == NULL;
    if (acquired) owner = caller;
    portEXIT_CRITICAL(&owner_lock);
    if (!acquired) return ESP_ERR_INVALID_STATE;

    const spi_bus_config_t bus = {
        .sclk_io_num = BAJI_LCD_SCLK,
        .data0_io_num = BAJI_LCD_D0, .data1_io_num = BAJI_LCD_D1,
        .data2_io_num = BAJI_LCD_D2, .data3_io_num = BAJI_LCD_D3,
        .data4_io_num = -1, .data5_io_num = -1, .data6_io_num = -1, .data7_io_num = -1,
        .max_transfer_sz = BAJI_DISPLAY_MAX_TRANSFER_BYTES,
    };
    const spi_device_interface_config_t device = {
        .command_bits = 8, .address_bits = 24,
        .clock_speed_hz = 40 * 1000 * 1000,
        .spics_io_num = BAJI_LCD_CS,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 1,
    };
    esp_err_t err = spi_bus_initialize(BAJI_LCD_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err == ESP_OK) {
        panel.bus_owned = true;
        err = spi_bus_add_device(BAJI_LCD_HOST, &device, &panel.device);
    }
    if (err == ESP_OK) err = configure_panel(brightness_percent * 255 / 100);
    if (err != ESP_OK) {
        esp_err_t cleanup = release_hardware();
        return cleanup == ESP_OK ? err : cleanup;
    }
    panel.ready = true;
    return ESP_OK;
}

static esp_err_t wait_pending(void)
{
    if (!panel.pending) return ESP_OK;
    spi_transaction_t *done = NULL;
    esp_err_t err = spi_device_get_trans_result(panel.device, &done, pdMS_TO_TICKS(IO_TIMEOUT_MS));
    if (done != &panel.color) {
        /* A timeout returns no descriptor. Do not overwrite the descriptor
         * or return the old buffer to the caller until SPI hands it back. */
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }
    panel.pending = false;
    if (err != ESP_OK) panel.next_y = 0;
    return err;
}

esp_err_t baji_display_wait(void)
{
    return owns_display() && panel.ready ? wait_pending() : ESP_ERR_INVALID_STATE;
}

static esp_err_t address_range(uint8_t reg, uint16_t first, uint16_t last)
{
    const uint8_t data[] = {first >> 8, first & 0xff, last >> 8, last & 0xff};
    return control(reg, data, sizeof(data));
}

esp_err_t baji_display_write_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                  const void *rgb565_be, size_t bytes)
{
    if (!owns_display() || !panel.ready) return ESP_ERR_INVALID_STATE;
    if (!width || !height || ((x | y | width | height) & 1) ||
        x + width > BAJI_LCD_WIDTH || y + height > BAJI_LCD_HEIGHT ||
        bytes != (size_t)width * height * 2 || bytes > BAJI_DISPLAY_MAX_TRANSFER_BYTES ||
        ((uintptr_t)rgb565_be & 3) || !esp_ptr_dma_capable(rgb565_be) ||
        !esp_ptr_dma_capable((const uint8_t *)rgb565_be + bytes - 1)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = wait_pending();
    if (err != ESP_OK) return err;

    bool continuation = panel.next_y && panel.next_y == y &&
                        panel.previous_x == x && panel.previous_width == width;
    panel.next_y = 0;
    if (!continuation) {
        err = address_range(CMD_CASET, x + BAJI_LCD_X_GAP, x + width - 1 + BAJI_LCD_X_GAP);
        if (err == ESP_OK)
            err = address_range(CMD_RASET, y + BAJI_LCD_Y_GAP, BAJI_LCD_HEIGHT - 1 + BAJI_LCD_Y_GAP);
        if (err != ESP_OK) return err;
    }

    panel.color = (spi_transaction_t) {
        .cmd = 0x32,
        .addr = (uint32_t)(continuation ? CMD_RAMWRC : CMD_RAMWR) << 8,
        .flags = SPI_TRANS_MODE_QIO | SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL,
        .length = bytes * 8,
        .tx_buffer = rgb565_be,
    };
    err = spi_device_queue_trans(panel.device, &panel.color, pdMS_TO_TICKS(IO_TIMEOUT_MS));
    if (err == ESP_OK) {
        panel.pending = true;
        panel.previous_x = x;
        panel.previous_width = width;
        panel.next_y = y + height < BAJI_LCD_HEIGHT ? y + height : 0;
    }
    return err;
}

esp_err_t baji_display_set_brightness(uint8_t percent)
{
    if (percent > 100) return ESP_ERR_INVALID_ARG;
    esp_err_t err = baji_display_wait();
    if (err != ESP_OK) return err;
    panel.next_y = 0;
    uint8_t value = percent * 255 / 100;
    return control(CMD_BRIGHTNESS, &value, 1);
}

esp_err_t baji_display_deinit(void)
{
    if (!owns_display()) return ESP_ERR_INVALID_STATE;
    if (panel.ready) {
        esp_err_t err = wait_pending();
        if (err != ESP_OK) return err;
        err = control(0x28, NULL, 0);
        if (err != ESP_OK) return err;
        panel.ready = false;
    }
    return release_hardware();
}
