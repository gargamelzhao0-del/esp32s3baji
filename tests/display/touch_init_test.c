#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "peripherals_private.h"

enum { COMMAND=0xd101, PROJECT=0xd204, RESOLUTION=0xd1f8, REPORT=0xd000 };
static int reset_low, commands, id_reads, report_reads, selected, failed_register, failures;
static unsigned tick, selected_at;
static bool fail_tx;

static bool nack(int reg, bool tx)
{
    if (reg != failed_register || fail_tx != tx || failures == 0) return false;
    --failures;
    return true;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *buf, size_t len, int timeout)
{
    assert(dev && len == 2 && timeout == 40);
    selected = (buf[0] << 8) | buf[1];
    selected_at = tick;
    if (selected == COMMAND) ++commands;
    return nack(selected, true) ? ESP_ERR_INVALID_RESPONSE : ESP_OK;
}

esp_err_t i2c_master_receive(i2c_master_dev_handle_t dev, uint8_t *out, size_t size, int timeout)
{
    assert(dev && timeout == 40 && tick - selected_at >= 2);
    if (selected == PROJECT) ++id_reads;
    if (selected == REPORT) ++report_reads;
    if (nack(selected, false)) return ESP_ERR_INVALID_RESPONSE;
    if (selected == PROJECT && size == 4) {
        memcpy(out, (uint8_t[]){0x34, 0x57, 0x17, 0x92}, 4);
    } else if (selected == RESOLUTION && size == 4) {
        memcpy(out, (uint8_t[]){0xd2, 1, 0xd2, 1}, 4);
    } else {
        assert(!"unexpected read");
    }
    return ESP_OK;
}

void vTaskDelay(int ticks) { tick += ticks; }
esp_err_t gpio_set_level(int pin, int level) { assert(pin == BAJI_TOUCH_RST); reset_low += !level; return ESP_OK; }
esp_err_t gpio_config(const gpio_config_t *cfg) { assert(cfg); return ESP_OK; }
esp_err_t gpio_reset_pin(int pin) { (void)pin; return ESP_OK; }
esp_err_t gpio_install_isr_service(int flags) { (void)flags; return ESP_OK; }
esp_err_t gpio_isr_handler_remove(int pin) { assert(pin == BAJI_TOUCH_INT); return ESP_OK; }
esp_err_t gpio_isr_handler_add(int pin, void (*handler)(void *), void *arg)
{
    assert(pin == BAJI_TOUCH_INT && handler && !arg);
    return ESP_OK;
}
int xSemaphoreGiveFromISR(SemaphoreHandle_t sem, BaseType_t *woke) { (void)sem; (void)woke; return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t sem) { (void)sem; return pdTRUE; }
int xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks) { (void)sem; (void)ticks; return pdTRUE; }
esp_err_t baji_peripheral_lock(baji_peripherals_t *p) { assert(p); return ESP_OK; }
esp_err_t baji_attach_i2c(baji_peripherals_t *p, uint8_t addr, i2c_master_dev_handle_t *dev)
{
    assert(addr == 0x5a);
    *dev = p;
    return ESP_OK;
}

static void check_start(int reg, bool tx, int nacks, bool recovers)
{
    baji_peripherals_t driver = {0};
    reset_low = commands = id_reads = report_reads = 0;
    failed_register = reg;
    failures = nacks;
    fail_tx = tx;
    esp_err_t err = baji_peripherals_touch_init(&driver);
    assert(err == (recovers ? ESP_OK : ESP_ERR_INVALID_RESPONSE));
    assert(driver.touch_ready == recovers && reset_low == 1);
    if (reg == COMMAND) {
        assert(commands == (recovers ? nacks + 2 : 3));
    } else {
        assert(id_reads == (recovers ? nacks + 1 : 3));
    }
    if (recovers) {
        failed_register = REPORT;
        fail_tx = false;
        failures = 2;
        baji_touch_sample_t sample = {.pressed = true, .x = 9, .y = 10};
        assert(baji_peripherals_read_touch(&driver, &sample) == ESP_ERR_INVALID_RESPONSE);
        assert(report_reads == 1 && reset_low == 1 && sample.pressed && sample.x == 9 && sample.y == 10);
    }
    assert(baji_touch_release_pins(&driver) == ESP_OK);
}

int main(void)
{
    check_start(COMMAND, true, 1, true);
    check_start(PROJECT, false, 2, true);
    check_start(RESOLUTION, true, 1, true);
    check_start(COMMAND, true, 4, false);
    check_start(PROJECT, false, 4, false);
    puts("CST9217 startup NACK recovery, retry limit and single-attempt runtime reads passed.");
}
