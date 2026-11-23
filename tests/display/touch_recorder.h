#pragma once
#include "spi_recorder.h"

typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef void *SemaphoreHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define IRAM_ATTR
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define configTICK_RATE_HZ 1000
#define portENTER_CRITICAL_ISR portENTER_CRITICAL
#define portEXIT_CRITICAL_ISR portEXIT_CRITICAL
#define portYIELD_FROM_ISR() ((void)0)

int xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t *woke);
int xSemaphoreGive(SemaphoreHandle_t semaphore);
int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks);
esp_err_t gpio_isr_handler_remove(int pin);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(int pin, void (*handler)(void *), void *arg);
int gpio_get_level(int pin);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data, size_t size, int timeout);
esp_err_t i2c_master_receive(i2c_master_dev_handle_t dev, uint8_t *data, size_t size, int timeout);
