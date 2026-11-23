/* Host-only surface used by baji_display.c. The target build uses IDF headers. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_ERR_NOT_FINISHED 0x10c
enum { ESP_OK, ESP_FAIL, ESP_ERR_TIMEOUT, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_ERR_NO_MEM, ESP_ERR_INVALID_RESPONSE };
enum {
    GPIO_NUM_1=1, GPIO_NUM_2=2, GPIO_NUM_4=4, GPIO_NUM_5=5, GPIO_NUM_6=6, GPIO_NUM_7=7,
    GPIO_NUM_11=11, GPIO_NUM_12=12, GPIO_NUM_13=13, GPIO_NUM_38=38, SPI2_HOST=2, GPIO_MODE_OUTPUT=1,
    GPIO_MODE_INPUT=2, GPIO_INTR_POSEDGE=1, GPIO_INTR_NEGEDGE=2,
    SPI_TRANS_USE_TXDATA=1, SPI_TRANS_MODE_QIO=2, SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL=4,
    SPI_DEVICE_HALFDUPLEX=1, SPI_DMA_CH_AUTO=1
};
typedef struct { uint64_t pin_bit_mask; int mode, intr_type; } gpio_config_t;
typedef struct {
    int sclk_io_num, data0_io_num, data1_io_num, data2_io_num, data3_io_num;
    int data4_io_num, data5_io_num, data6_io_num, data7_io_num, max_transfer_sz;
} spi_bus_config_t;
typedef struct {
    int command_bits, address_bits, clock_speed_hz, spics_io_num, flags, queue_size;
} spi_device_interface_config_t;
typedef struct {
    uint32_t cmd, addr, flags;
    size_t length;
    uint8_t tx_data[4];
    const void *tx_buffer;
} spi_transaction_t;

typedef void *TaskHandle_t;
typedef void *spi_device_handle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY UINT32_MAX

TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskDelay(int ticks);
enum { MALLOC_CAP_INTERNAL=1, MALLOC_CAP_DMA=2, MALLOC_CAP_8BIT=4 };
void *heap_caps_malloc(size_t bytes, unsigned caps);
void heap_caps_free(void *address);
esp_err_t gpio_set_level(int pin, int level);
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_reset_pin(int pin);
bool esp_ptr_dma_capable(const void *address);
esp_err_t spi_bus_initialize(int host, const spi_bus_config_t *config, int dma);
esp_err_t spi_bus_free(int host);
esp_err_t spi_bus_add_device(int host, const spi_device_interface_config_t *config, spi_device_handle_t *device);
esp_err_t spi_bus_remove_device(spi_device_handle_t device);
esp_err_t spi_device_polling_transmit(spi_device_handle_t device, spi_transaction_t *transaction);
esp_err_t spi_device_polling_start(spi_device_handle_t device, spi_transaction_t *transaction, uint32_t ticks);
esp_err_t spi_device_polling_end(spi_device_handle_t device, uint32_t ticks);
esp_err_t spi_device_acquire_bus(spi_device_handle_t device, uint32_t ticks);
void spi_device_release_bus(spi_device_handle_t device);
esp_err_t spi_device_queue_trans(spi_device_handle_t device, spi_transaction_t *transaction, int ticks);
esp_err_t spi_device_get_trans_result(spi_device_handle_t device, spi_transaction_t **done, int ticks);
size_t esp_psram_get_size(void);
esp_err_t esp_flash_get_physical_size(void *chip, uint32_t *size);
