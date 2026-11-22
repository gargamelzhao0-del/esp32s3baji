#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>
#include "baji_display.h"
#include "baji_board.h"
#include "spi_recorder.h"

static alignas(4) uint8_t strips[2][BAJI_DISPLAY_MAX_TRANSFER_BYTES];
static int caller, actors[2];
static bool bus, device, reset, stalled, reject_queue;
static uint8_t fail_register;
static spi_transaction_t *borrowed;
static struct { uint8_t reg, data[4]; } commands[64];
static size_t command_count;
static int waits[4];
static size_t wait_count, allocations, releases;

size_t esp_psram_get_size(void) { return 8U << 20; }
esp_err_t esp_flash_get_physical_size(void *chip, uint32_t *size)
{
    assert(!chip);
    *size = 32U << 20;
    return ESP_OK;
}

/* Exercise the real board's display-only startup when optional I2C is absent. */
esp_err_t baji_peripherals_open(baji_peripherals_t **out) { *out = NULL; return ESP_FAIL; }
esp_err_t baji_peripherals_touch_init(baji_peripherals_t *p) { (void)p; assert(0); return ESP_FAIL; }
esp_err_t baji_peripherals_power_init(baji_peripherals_t *p) { (void)p; assert(0); return ESP_FAIL; }
esp_err_t baji_peripherals_close(baji_peripherals_t *p) { (void)p; assert(0); return ESP_FAIL; }
void *heap_caps_malloc(size_t bytes, unsigned caps)
{
    assert(allocations < 2 && bytes == sizeof(strips[0]));
    assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    return strips[allocations++];
}
void heap_caps_free(void *address)
{
    assert(!borrowed && (address == strips[0] || address == strips[1]));
    ++releases;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) { return &actors[caller]; }
void vTaskDelay(int ticks)
{
    assert(wait_count < 4);
    waits[wait_count++] = ticks;
}
esp_err_t gpio_config(const gpio_config_t *config)
{
    assert(config->pin_bit_mask == (1u << 1));
    reset = true;
    return ESP_OK;
}
esp_err_t gpio_set_level(int pin, int level)
{
    assert(reset && pin == 1 && level == (wait_count != 0));
    return ESP_OK;
}
esp_err_t gpio_reset_pin(int pin)
{
    assert(reset && pin == 1);
    reset = false;
    return ESP_OK;
}
bool esp_ptr_dma_capable(const void *address)
{
    uintptr_t p = (uintptr_t)address, start = (uintptr_t)strips;
    return p >= start && p - start < sizeof(strips);
}
esp_err_t spi_bus_initialize(int host, const spi_bus_config_t *config, int dma)
{
    assert(!bus && host == 2 && dma == SPI_DMA_CH_AUTO);
    assert(config->sclk_io_num == 38 && config->data0_io_num == 4 && config->data3_io_num == 7);
    assert(config->max_transfer_sz == BAJI_DISPLAY_MAX_TRANSFER_BYTES);
    bus = true;
    return ESP_OK;
}
esp_err_t spi_bus_free(int host)
{
    assert(host == 2 && bus && !device);
    bus = false;
    return ESP_OK;
}
esp_err_t spi_bus_add_device(int host, const spi_device_interface_config_t *config, spi_device_handle_t *out)
{
    assert(host == 2 && bus && !device && config->clock_speed_hz == 40000000);
    assert(config->spics_io_num == 12 && config->command_bits == 8 && config->address_bits == 24);
    device = true;
    *out = &device;
    return ESP_OK;
}
esp_err_t spi_bus_remove_device(spi_device_handle_t handle)
{
    assert(handle == &device && device && !borrowed);
    device = false;
    return ESP_OK;
}
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle, spi_transaction_t *transaction)
{
    assert(handle == &device && device && !borrowed && transaction->cmd == 0x02);
    assert(transaction->length || !(transaction->flags & SPI_TRANS_USE_TXDATA));
    assert(transaction->length <= 32 && command_count < 64);
    uint8_t reg = transaction->addr >> 8;
    commands[command_count].reg = reg;
    memcpy(commands[command_count].data, transaction->tx_data, transaction->length / 8);
    ++command_count;
    return reg == fail_register ? ESP_FAIL : ESP_OK;
}
esp_err_t spi_device_queue_trans(spi_device_handle_t handle, spi_transaction_t *transaction, int ticks)
{
    assert(handle == &device && device && !borrowed && ticks > 0);
    assert(transaction->cmd == 0x32);
    assert(transaction->flags == (SPI_TRANS_MODE_QIO | SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL));
    if (reject_queue) return ESP_ERR_TIMEOUT;
    borrowed = transaction;
    return ESP_OK;
}
esp_err_t spi_device_get_trans_result(spi_device_handle_t handle, spi_transaction_t **done, int ticks)
{
    assert(handle == &device && borrowed && ticks > 0);
    if (stalled) return ESP_ERR_TIMEOUT;
    *done = borrowed;
    borrowed = NULL;
    return ESP_OK;
}

static void open_display(void)
{
    wait_count = command_count = 0;
    assert(baji_display_init(45) == ESP_OK);
    const int expected_waits[] = {10, 150, 600, 600};
    assert(wait_count == 4 && memcmp(waits, expected_waits, sizeof(waits)) == 0);
    assert(commands[command_count-1].reg == 0x29);
    bool saw_brightness = false;
    for (size_t i = 0; i < command_count; ++i) {
        if (commands[i].reg == 0x51) saw_brightness = commands[i].data[0] == 114;
    }
    assert(saw_brightness);
    command_count = 0;
}

static void check_dma_handoff(void)
{
    assert(baji_display_write_rect(4, 10, 4, 8, strips[0], 64) == ESP_OK);
    const uint8_t columns[] = {0, 10, 0, 13}, rows[] = {0, 10, 1, 0xd1};
    assert(command_count == 2);
    assert(commands[0].reg == 0x2a && memcmp(commands[0].data, columns, 4) == 0);
    assert(commands[1].reg == 0x2b && memcmp(commands[1].data, rows, 4) == 0);
    assert(borrowed->addr == 0x2c00 && borrowed->tx_buffer == strips[0] && borrowed->length == 512);

    stalled = true;
    assert(baji_display_wait() == ESP_ERR_TIMEOUT);
    assert(baji_display_write_rect(4, 18, 4, 8, strips[1], 64) == ESP_ERR_TIMEOUT);
    assert(baji_display_deinit() == ESP_ERR_TIMEOUT);
    assert(borrowed->tx_buffer == strips[0] && device && command_count == 2);
    stalled = false;
    assert(baji_display_write_rect(4, 18, 4, 8, strips[1], 64) == ESP_OK);
    assert(borrowed->addr == 0x3c00 && borrowed->tx_buffer == strips[1] && command_count == 2);

    caller = 1;
    assert(baji_display_deinit() == ESP_ERR_INVALID_STATE);
    assert(baji_display_set_brightness(0) == ESP_ERR_INVALID_STATE);
    caller = 0;
    assert(baji_display_set_brightness(100) == ESP_OK && borrowed == NULL);
    assert(commands[2].reg == 0x51 && commands[2].data[0] == 255);
    assert(baji_display_write_rect(4, 26, 4, 8, strips[0], 64) == ESP_OK);
    assert(borrowed->addr == 0x2c00 && command_count == 5);
    assert(baji_display_wait() == ESP_OK);
}

static void check_rectangles_and_retry(void)
{
    reject_queue = true;
    assert(baji_display_write_rect(4, 34, 4, 8, strips[0], 64) == ESP_ERR_TIMEOUT && !borrowed);
    reject_queue = false;
    assert(baji_display_write_rect(4, 34, 4, 8, strips[0], 64) == ESP_OK && borrowed->addr == 0x2c00);
    assert(baji_display_wait() == ESP_OK);

    assert(baji_display_write_rect(0, 0, 466, 34, strips[0], 31688) == ESP_ERR_INVALID_ARG);
    assert(baji_display_write_rect(0, 1, 4, 4, strips[0], 32) == ESP_ERR_INVALID_ARG);
    assert(baji_display_write_rect(0, 0, 4, 4, strips[0]+1, 32) == ESP_ERR_INVALID_ARG);
    assert(baji_display_write_rect(0, 0, 4, 4, strips[0], 4) == ESP_ERR_INVALID_ARG);
    assert(baji_display_write_rect(0, 434, 466, 32, strips[0], sizeof(strips[0])) == ESP_OK);
    assert(baji_display_write_rect(0, 0, 466, 32, strips[1], sizeof(strips[1])) == ESP_OK);
    assert(borrowed->addr == 0x2c00);
}

typedef struct {
    uint8_t *expected_pixels;
    uint16_t y, remaining, x, width, max_rows;
} paint_cursor_t;

static void paint_band(const baji_rect_t *band, uint8_t *pixels, void *context)
{
    paint_cursor_t *cursor = context;
    /* Painting the second strip starts while the first DMA may still run. */
    assert(!borrowed || borrowed->tx_buffer != pixels);
    assert(pixels == cursor->expected_pixels && band->x == cursor->x && band->width == cursor->width);
    uint16_t expected = cursor->remaining > cursor->max_rows ? cursor->max_rows : cursor->remaining;
    assert(band->y == cursor->y && band->height == expected);
    memset(pixels, 0x55, (size_t)band->width * band->height * 2);
    cursor->expected_pixels = pixels == strips[0] ? strips[1] : strips[0];
    cursor->y += band->height;
    cursor->remaining -= band->height;
}

static void check_board_paint(void)
{
    wait_count = command_count = 0;
    baji_board_t *board;
    baji_board_info_t info;
    assert(baji_board_open(&board, &info) == ESP_OK && info.touch_status == ESP_FAIL);
    assert(info.flash_status == ESP_OK && info.flash_bytes == (32U << 20) && info.psram_bytes == (8U << 20));
    const baji_rect_t edge = {.x=1, .y=419, .width=465, .height=47};
    paint_cursor_t cursor = {strips[0], 418, 48, 0, 466, 32};
    assert(baji_board_paint(board, edge, paint_band, &cursor) == ESP_OK && cursor.remaining == 0);

    const baji_rect_t narrow = {.x=465, .y=1, .width=1, .height=465};
    cursor = (paint_cursor_t){strips[0], 0, 466, 464, 2, 466};
    assert(baji_board_paint(board, narrow, paint_band, &cursor) == ESP_OK && cursor.remaining == 0);
    stalled = true;
    cursor = (paint_cursor_t){strips[1], 418, 48, 0, 466, 32};
    assert(baji_board_paint(board, edge, paint_band, &cursor) == ESP_ERR_TIMEOUT);
    assert(borrowed->tx_buffer == strips[1]);
    stalled = false;
    /* The next paint must start with strip 0. Strip 1 is still DMA-owned. */
    cursor = (paint_cursor_t){strips[0], 464, 2, 464, 2, 466};
    const baji_rect_t corner = {.x=465, .y=465, .width=1, .height=1};
    assert(baji_board_paint(board, corner, paint_band, &cursor) == ESP_OK && cursor.remaining == 0);

    stalled = true;
    cursor = (paint_cursor_t){strips[1], 464, 2, 464, 2, 466};
    assert(baji_board_paint(board, corner, paint_band, &cursor) == ESP_ERR_TIMEOUT);
    assert(baji_board_close(board) == ESP_ERR_TIMEOUT && releases == 0);
    stalled = false;
    assert(baji_board_close(board) == ESP_OK && releases == 2);
}

int main(void)
{
    assert(baji_display_init(101) == ESP_ERR_INVALID_ARG);
    fail_register = 0xc4;
    assert(baji_display_init(0) == ESP_FAIL && !bus && !device && !reset);
    fail_register = 0;
    open_display();
    check_dma_handoff();
    check_rectangles_and_retry();
    assert(baji_display_deinit() == ESP_OK && !borrowed && !bus && !device && !reset);
    open_display();
    assert(baji_display_deinit() == ESP_OK);
    check_board_paint();
    puts("Display protocol, DMA ownership, odd dirty rectangles, double-buffer retries and teardown passed.");
}
