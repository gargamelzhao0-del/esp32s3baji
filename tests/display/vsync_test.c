#include <assert.h>
#include <stdio.h>
#include "touch_recorder.h"
#include "baji_vsync.h"

static bool installed, fail_install, fail_config, fail_add, fail_remove, fail_reset;
static bool signal_during_wait;
static int resets, adds, removes;
static int64_t now = 7000;
static int tasks[2];
static TaskHandle_t task = tasks;
static void (*callback)(void *);
static void *argument;

int64_t esp_timer_get_time(void) { return now; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return task; }

esp_err_t gpio_install_isr_service(int flags)
{
    assert(flags == 0);
    if (fail_install) return ESP_ERR_NO_MEM;
    if (installed) return ESP_ERR_INVALID_STATE;
    installed = true;
    return ESP_OK;
}

esp_err_t gpio_config(const gpio_config_t *cfg)
{
    assert(cfg->pin_bit_mask == (1ULL << GPIO_NUM_13));
    assert(cfg->mode == GPIO_MODE_INPUT && cfg->intr_type == GPIO_INTR_POSEDGE);
    return fail_config ? ESP_FAIL : ESP_OK;
}

esp_err_t gpio_isr_handler_add(int pin, void (*fn)(void *), void *arg)
{
    assert(pin == GPIO_NUM_13 && !callback);
    if (!installed) return ESP_ERR_INVALID_STATE;
    if (fail_add) return ESP_FAIL;
    callback = fn;
    argument = arg;
    ++adds;
    return ESP_OK;
}

esp_err_t gpio_isr_handler_remove(int pin)
{
    assert(pin == GPIO_NUM_13 && callback);
    if (fail_remove) return ESP_FAIL;
    callback = NULL;
    ++removes;
    return ESP_OK;
}

esp_err_t gpio_reset_pin(int pin)
{
    assert(pin == GPIO_NUM_13);
    if (fail_reset) return ESP_FAIL;
    ++resets;
    return ESP_OK;
}

SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *storage)
{
    storage->token = 0;
    return storage;
}

int xSemaphoreGiveFromISR(SemaphoreHandle_t event, BaseType_t *woke)
{
    ((StaticSemaphore_t *)event)->token = 1;
    *woke = pdTRUE;
    return pdTRUE;
}

int xSemaphoreTake(SemaphoreHandle_t event, TickType_t ticks)
{
    StaticSemaphore_t *s = event;
    if (!s->token && ticks && signal_during_wait) {
        signal_during_wait = false;
        now += 37;
        callback(argument);
    }
    if (s->token) { s->token = 0; return pdTRUE; }
    now += (int64_t)ticks * 1000;
    return pdFALSE;
}

int main(void)
{
    baji_vsync_edge_t e;
    assert(baji_vsync_wait(0, 0, NULL) == ESP_ERR_INVALID_ARG);
    assert(baji_vsync_wait(0, 0, &e) == ESP_ERR_INVALID_STATE);
    fail_install = true;
    assert(baji_vsync_open() == ESP_ERR_NO_MEM && !callback);
    fail_install = false;
    fail_config = true;
    assert(baji_vsync_open() == ESP_FAIL && !callback);
    fail_config = false;
    fail_add = true;
    assert(baji_vsync_open() == ESP_FAIL && !callback && resets == 2);
    /* Registration and its cleanup both fail. The caller never received a
     * session; another task must be able to open without closing it. */
    fail_reset = true;
    assert(baji_vsync_open() == ESP_FAIL && !callback);
    assert(baji_vsync_close() == ESP_ERR_INVALID_STATE);
    task = tasks + 1;
    fail_add = false;
    fail_reset = false;
    assert(baji_vsync_open() == ESP_OK && adds == 1);
    assert(baji_vsync_open() == ESP_ERR_INVALID_STATE);
    assert(baji_vsync_wait(0, 0, &e) == ESP_ERR_TIMEOUT && !e.seq && !e.edge_us);
    callback(argument);
    now += 17000;
    callback(argument);
    assert(baji_vsync_wait(0, 0, &e) == ESP_OK && e.seq == 2 && e.edge_us == now);
    int64_t start = now;
    assert(baji_vsync_wait(e.seq, 3, &e) == ESP_ERR_TIMEOUT && now == start + 3000);
    assert(e.edge_us == start && e.observed_us == now);
    signal_during_wait = true;
    assert(baji_vsync_wait(e.seq, 30, &e) == ESP_OK && e.seq == 3 && e.edge_us == now);

    task = tasks;
    assert(baji_vsync_close() == ESP_ERR_INVALID_STATE);
    assert(baji_vsync_wait(e.seq, 0, &e) == ESP_ERR_INVALID_STATE);
    task = tasks + 1;
    void (*old_callback)(void *) = callback;
    void *old_argument = argument;
    fail_remove = true;
    assert(baji_vsync_close() == ESP_FAIL);
    old_callback(old_argument);
    assert(!baji_vsync_snapshot().edge_us);
    fail_remove = false;
    fail_reset = true;
    assert(baji_vsync_close() == ESP_FAIL && removes == 1);
    fail_reset = false;
    assert(baji_vsync_close() == ESP_OK && resets == 3 && removes == 1);
    assert(baji_vsync_open() == ESP_OK && adds == 2);
    old_callback(old_argument);
    assert(baji_vsync_wait(0, 1, &e) == ESP_ERR_TIMEOUT && !e.seq);
    callback(argument);
    assert(baji_vsync_wait(0, 0, &e) == ESP_OK && e.seq == 1);
    assert(baji_vsync_close() == ESP_OK && !callback);
    puts("TE edge coalescing, timing, missed IRQ, owner and stale IRQ after reopen passed.");
}
