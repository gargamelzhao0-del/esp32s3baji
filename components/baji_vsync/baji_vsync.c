#include "baji_vsync.h"
#include "baji_board_profile.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdbool.h>

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t event_storage;
static SemaphoreHandle_t event;
static TaskHandle_t owner;
static uintptr_t generation;
static bool enabled, pin_owned, handler_added;
static uint32_t sequence;
static int64_t edge_time;

static void IRAM_ATTR vsync_irq(void *argument)
{
    BaseType_t woke = pdFALSE;
    portENTER_CRITICAL_ISR(&lock);
    /* A late dispatch on the other core must not enter a reopened session. */
    if (enabled && (uintptr_t)argument == generation) {
        ++sequence;
        edge_time = esp_timer_get_time();
        xSemaphoreGiveFromISR(event, &woke);
    }
    portEXIT_CRITICAL_ISR(&lock);
    if (woke) portYIELD_FROM_ISR();
}

esp_err_t baji_vsync_open(void)
{
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&lock);
    if (owner) {
        portEXIT_CRITICAL(&lock);
        return ESP_ERR_INVALID_STATE;
    }
    owner = self;
    ++generation;
    if (!generation) ++generation;
    uintptr_t ticket = generation;
    portEXIT_CRITICAL(&lock);

    /* Static storage lives beyond the last interrupt dispatch. A previous
     * session may have left one token, but never a pending waiter. */
    if (!event) event = xSemaphoreCreateBinaryStatic(&event_storage);
    (void)xSemaphoreTake(event, 0);
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BAJI_LCD_TE,
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    esp_err_t err = gpio_config(&config);
    pin_owned = err == ESP_OK;
    if (err == ESP_OK) {
        portENTER_CRITICAL(&lock);
        sequence = 0;
        edge_time = 0;
        enabled = true;
        portEXIT_CRITICAL(&lock);
        /* Touch normally owns the existing service. If touch is unavailable,
         * TE can start it independently. Leave it installed on close. */
        err = gpio_isr_handler_add(BAJI_LCD_TE, vsync_irq, (void *)ticket);
        if (err == ESP_ERR_INVALID_STATE) {
            err = gpio_install_isr_service(0);
            if (err == ESP_OK || err == ESP_ERR_INVALID_STATE)
                err = gpio_isr_handler_add(BAJI_LCD_TE, vsync_irq, (void *)ticket);
        }
        handler_added = err == ESP_OK;
    }
    if (err != ESP_OK) {
        esp_err_t cleanup = baji_vsync_close();
        if (cleanup != ESP_OK) return cleanup;
    }
    return err;
}

baji_vsync_edge_t baji_vsync_snapshot(void)
{
    portENTER_CRITICAL(&lock);
    baji_vsync_edge_t edge = {
        .seq = enabled ? sequence : 0,
        .edge_us = enabled ? edge_time : 0,
        .observed_us = esp_timer_get_time(),
    };
    portEXIT_CRITICAL(&lock);
    return edge;
}

esp_err_t baji_vsync_wait(uint32_t after_seq, uint32_t timeout_ms, baji_vsync_edge_t *edge)
{
    if (!edge) return ESP_ERR_INVALID_ARG;
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&lock);
    bool ready = enabled && self == owner;
    portEXIT_CRITICAL(&lock);
    if (!ready) return ESP_ERR_INVALID_STATE;

    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    for (;;) {
        *edge = baji_vsync_snapshot();
        if (edge->edge_us && edge->seq != after_seq) return ESP_OK;
        int64_t remaining = deadline - edge->observed_us;
        if (remaining <= 0) return ESP_ERR_TIMEOUT;
        uint64_t ticks = ((uint64_t)remaining * configTICK_RATE_HZ + 999999) / 1000000;
        if (ticks >= portMAX_DELAY) ticks = portMAX_DELAY - 1;
        xSemaphoreTake(event, (TickType_t)ticks);
    }
}

esp_err_t baji_vsync_close(void)
{
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&lock);
    if (self != owner) {
        portEXIT_CRITICAL(&lock);
        return ESP_ERR_INVALID_STATE;
    }
    enabled = false;
    portEXIT_CRITICAL(&lock);

    if (handler_added) {
        esp_err_t err = gpio_isr_handler_remove(BAJI_LCD_TE);
        if (err != ESP_OK) return err;
        handler_added = false;
    }
    if (pin_owned) {
        esp_err_t err = gpio_reset_pin(BAJI_LCD_TE);
        if (err != ESP_OK) return err;
        pin_owned = false;
    }
    portENTER_CRITICAL(&lock);
    owner = NULL;
    portEXIT_CRITICAL(&lock);
    return ESP_OK;
}
