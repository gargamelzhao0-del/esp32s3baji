#pragma once
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
enum { ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 0x101,
       ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_SIZE,
       ESP_ERR_INVALID_CRC = 0x109 };
typedef struct host_semaphore *SemaphoreHandle_t;
#define portMAX_DELAY UINT32_MAX
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreTake(SemaphoreHandle_t semaphore, uint32_t wait);
int xSemaphoreGive(SemaphoreHandle_t semaphore);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);
void esp_fill_random(void *out, size_t size);
int64_t esp_timer_get_time(void);

typedef struct { int type; } mbedtls_md_info_t;
#define MBEDTLS_MD_SHA256 6
const mbedtls_md_info_t *mbedtls_md_info_from_type(int type);
int mbedtls_md_hmac(const mbedtls_md_info_t *info, const uint8_t *key, size_t key_size,
                    const uint8_t *data, size_t data_size, uint8_t *digest);
int mbedtls_ct_memcmp(const void *left, const void *right, size_t size);
