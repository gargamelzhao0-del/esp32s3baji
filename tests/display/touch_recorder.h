#pragma once
#include "spi_recorder.h"

typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
enum { I2C_NUM_1=1, GPIO_NUM_14=14, GPIO_NUM_15=15, I2C_CLK_SRC_DEFAULT=0, I2C_ADDR_BIT_LEN_7=7 };
typedef struct {
    int i2c_port, sda_io_num, scl_io_num, clk_source, glitch_ignore_cnt;
    struct { bool enable_internal_pullup; } flags;
} i2c_master_bus_config_t;
typedef struct { int dev_addr_length, device_address, scl_speed_hz; } i2c_device_config_t;
typedef void *SemaphoreHandle_t;
typedef struct { int token; } StaticSemaphore_t;
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
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *storage);
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);
void *heap_caps_calloc(size_t count, size_t size, unsigned caps);
int xSemaphoreGive(SemaphoreHandle_t semaphore);
int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks);
esp_err_t gpio_isr_handler_remove(int pin);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(int pin, void (*handler)(void *), void *arg);
int gpio_get_level(int pin);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data, size_t size, int timeout);
esp_err_t i2c_master_receive(i2c_master_dev_handle_t dev, uint8_t *data, size_t size, int timeout);
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *cfg, i2c_master_bus_handle_t *bus);
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg,
                                    i2c_master_dev_handle_t *dev);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *tx, size_t tx_len,
                                      uint8_t *rx, size_t rx_len, int timeout);
