#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "peripherals_private.h"

static int bus_id, touch_id, power_id;
static unsigned allocations, frees, semaphores, buses, devices, percentage_reads;
static bool fail_alloc, fail_bus_open, fail_power_detach, fail_bus_close, fail_percentage;
static uint8_t model = 0x4a, status1, status2, percentage;

void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{
    assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (fail_alloc) return NULL;
    ++allocations;
    return calloc(count, size);
}
void heap_caps_free(void *address) { ++frees; free(address); }
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage)
{
    ++semaphores;
    storage->token = 1;
    return storage;
}
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *storage)
{
    ++semaphores;
    storage->token = 0;
    return storage;
}
void vSemaphoreDelete(SemaphoreHandle_t s) { assert(s && semaphores); --semaphores; }
int xSemaphoreTake(SemaphoreHandle_t s, TickType_t timeout)
{
    StaticSemaphore_t *mutex = s;
    assert(timeout == 250 && mutex->token == 1);
    mutex->token = 0;
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t s)
{
    StaticSemaphore_t *mutex = s;
    assert(!mutex->token);
    mutex->token = 1;
    return pdTRUE;
}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *cfg, i2c_master_bus_handle_t *bus)
{
    assert(cfg->i2c_port == BAJI_I2C_PORT && cfg->sda_io_num == BAJI_I2C_SDA &&
           cfg->scl_io_num == BAJI_I2C_SCL && !buses);
    if (fail_bus_open) return ESP_FAIL;
    *bus = &bus_id;
    ++buses;
    return ESP_OK;
}
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus)
{
    assert(!devices && bus == &bus_id && buses == 1);
    if (fail_bus_close) return ESP_FAIL;
    --buses;
    return ESP_OK;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg,
                                    i2c_master_dev_handle_t *dev)
{
    assert(bus == &bus_id && buses && cfg->scl_speed_hz == 400000 && !*dev);
    assert(cfg->device_address == 0x34 || cfg->device_address == 0x5a);
    *dev = cfg->device_address == 0x34 ? &power_id : &touch_id;
    ++devices;
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev)
{
    assert((dev == &power_id || dev == &touch_id) && devices);
    if (dev == &power_id && fail_power_detach) return ESP_FAIL;
    --devices;
    return ESP_OK;
}
esp_err_t baji_touch_release_pins(baji_peripherals_t *p) { assert(p->closing); return ESP_OK; }
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *tx, size_t tx_len,
                                      uint8_t *rx, size_t rx_len, int timeout)
{
    assert(dev == &power_id && tx_len == 1 && timeout == BAJI_I2C_TIMEOUT_MS);
    switch (*tx) {
    case 3: assert(rx_len == 1); *rx = model; break;
    case 0: assert(rx_len == 2); rx[0] = status1; rx[1] = status2; break;
    case 0xa4:
        assert(rx_len == 1);
        ++percentage_reads;
        if (fail_percentage) return ESP_ERR_TIMEOUT;
        *rx = percentage;
        break;
    default: assert(!"unexpected PMIC register");
    }
    return ESP_OK;
}

int main(void)
{
    baji_peripherals_t *p = NULL;
    fail_alloc = true;
    assert(baji_peripherals_open(&p) == ESP_ERR_NO_MEM && !p && !semaphores);
    fail_alloc = false;
    fail_bus_open = true;
    assert(baji_peripherals_open(&p) == ESP_FAIL && !p && allocations == frees && !semaphores);
    fail_bus_open = false;
    assert(baji_peripherals_open(&p) == ESP_OK && p && allocations == frees+1 && semaphores == 2);

    baji_power_status_t sample = {.percent = 66};
    assert(baji_peripherals_read_power(p, &sample) == ESP_ERR_INVALID_STATE && sample.percent == 66);
    model = 0x33;
    assert(baji_peripherals_power_init(p) == ESP_ERR_INVALID_RESPONSE && devices == 1);
    model = 0x4a;
    assert(baji_peripherals_power_init(p) == ESP_OK && devices == 1);
    status1 = 1 << 5;
    assert(baji_peripherals_read_power(p, &sample) == ESP_OK && sample.usb_present);
    assert(!sample.percent_valid && !percentage_reads);
    status1 |= 1 << 3;
    fail_percentage = true;
    assert(baji_peripherals_read_power(p, &sample) == ESP_ERR_TIMEOUT && !sample.battery_present);
    fail_percentage = false;
    percentage = 0;
    assert(baji_peripherals_read_power(p, &sample) == ESP_OK && sample.percent_valid && !sample.percent);
    assert(percentage_reads == 2);

    assert(baji_attach_i2c(p, 0x5a, &p->touch) == ESP_OK && devices == 2);
    fail_power_detach = true;
    assert(baji_peripherals_close(p) == ESP_FAIL && devices == 1 && !p->touch && p->power);
    assert(baji_peripherals_power_init(p) == ESP_ERR_INVALID_STATE);
    fail_power_detach = false;
    fail_bus_close = true;
    assert(baji_peripherals_close(p) == ESP_FAIL && !devices && !p->power && semaphores == 2);
    fail_bus_close = false;
    assert(baji_peripherals_close(p) == ESP_OK && !buses && !semaphores && allocations == frees);
    puts("I2C ownership, partial close retries, PMIC sampling and failure isolation passed.");
}
