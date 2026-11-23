#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>
#include "baji_display.h"
#include "baji_board.h"
#include "spi_recorder.h"

static alignas(4) uint8_t strips[2][BAJI_DISPLAY_MAX_TRANSFER_BYTES];
static int caller, actors[2];
static bool bus, device, reset, stalled, reject_queue, polling;
static bool bus_locked, reject_acquire;
static unsigned lock_count, unlock_count;
static uint8_t fail_register;
static spi_transaction_t *borrowed;
static struct { uint8_t reg, data[4]; } commands[64];
static size_t command_count;
static int waits[4];
static size_t wait_count, allocations, releases;

/* CO5300 GRAM model. CASET/RASET persist, RAMWR uses their start address,
 * and RAMWRC continues on the next pixel. Record bytes of the real transfers. */
static struct {
    unsigned x0, x1, y0, y1, x, y;
    bool enabled;
    uint16_t pixels[466][466];
} gram;

static void record_color(const spi_transaction_t *t)
{
    if (t->addr == 0x2c00) { gram.x = gram.x0; gram.y = gram.y0; }
    else assert(t->addr == 0x3c00);
    const uint8_t *data = t->tx_buffer;
    for (size_t i = 0; i < t->length / 8; i += 2) {
        assert(gram.x >= 6 && gram.x < 472 && gram.y < 466);
        gram.pixels[gram.y][gram.x-6] = (uint16_t)((data[i] << 8) | data[i+1]);
        if (++gram.x > gram.x1) {
            gram.x = gram.x0;
            if (++gram.y > gram.y1) gram.y = gram.y0;
        }
    }
}

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
    assert(handle == &device && device && !borrowed && !bus_locked);
    device = false;
    return ESP_OK;
}
esp_err_t spi_device_acquire_bus(spi_device_handle_t handle, uint32_t ticks)
{
    assert(handle == &device && device && !borrowed && !bus_locked && ticks == portMAX_DELAY);
    if (reject_acquire) return ESP_FAIL;
    bus_locked = true;
    ++lock_count;
    return ESP_OK;
}
void spi_device_release_bus(spi_device_handle_t handle)
{
    assert(handle == &device && device && !borrowed && bus_locked);
    bus_locked = false;
    ++unlock_count;
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
    if (gram.enabled && (reg == 0x2a || reg == 0x2b)) {
        assert(transaction->length == 32);
        const uint8_t *p = transaction->tx_data;
        unsigned start = (p[0] << 8) | p[1], end = (p[2] << 8) | p[3];
        if (reg == 0x2a) { gram.x0 = start; gram.x1 = end; }
        else { gram.y0 = start; gram.y1 = end; }
    }
    return reg == fail_register ? ESP_FAIL : ESP_OK;
}
static esp_err_t start_color(spi_device_handle_t handle, spi_transaction_t *transaction, bool poll)
{
    assert(handle == &device && device && !borrowed);
    assert(bus_locked || transaction->addr == 0x3c00);
    assert(transaction->cmd == 0x32);
    assert(transaction->flags == (SPI_TRANS_MODE_QIO | SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL));
    if (reject_queue) return ESP_ERR_TIMEOUT;
    if (gram.enabled) record_color(transaction);
    borrowed = transaction;
    polling = poll;
    return ESP_OK;
}
esp_err_t spi_device_polling_start(spi_device_handle_t handle, spi_transaction_t *transaction, uint32_t ticks)
{
    assert(ticks == portMAX_DELAY);
    return start_color(handle, transaction, true);
}
esp_err_t spi_device_polling_end(spi_device_handle_t handle, uint32_t ticks)
{
    assert(handle == &device && borrowed && polling && ticks > 0);
    if (stalled) return ESP_ERR_TIMEOUT;
    borrowed = NULL;
    return ESP_OK;
}
esp_err_t spi_device_queue_trans(spi_device_handle_t handle, spi_transaction_t *transaction, int ticks)
{
    assert(ticks > 0);
    return start_color(handle, transaction, false);
}
esp_err_t spi_device_get_trans_result(spi_device_handle_t handle, spi_transaction_t **done, int ticks)
{
    assert(handle == &device && borrowed && !polling && ticks > 0);
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
    assert(bus_locked && lock_count == 1 && unlock_count == 0);
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
    assert(!bus_locked && lock_count == 1 && unlock_count == 1);
    assert(commands[2].reg == 0x51 && commands[2].data[0] == 255);
    assert(baji_display_write_rect(4, 26, 4, 8, strips[0], 64) == ESP_OK);
    assert(borrowed->addr == 0x2c00 && command_count == 5);
    assert(baji_display_wait() == ESP_OK);
    assert(!bus_locked && lock_count == 2 && unlock_count == 2);
}

static void check_rectangles_and_retry(void)
{
    reject_queue = true;
    assert(baji_display_write_rect(4, 34, 4, 8, strips[0], 64) == ESP_ERR_TIMEOUT && !borrowed);
    assert(!bus_locked);
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

static void check_window_pixels(void)
{
    static uint16_t expected[466][466];
    const baji_rect_t rectangles[] = {
        {0,0,466,2}, {0,2,466,4}, {0,0,466,2},   /* continue then restart */
        {40,0,16,8}, {40,0,16,8}, {44,0,16,8},  /* repeated / changed columns */
        {44,8,16,8}, {46,0,16,8}, {46,30,16,8}, /* original row after continue */
        {462,462,4,4}, {464,462,2,4}, {0,0,2,2}, {0,18,2,446}, {0,464,2,2},
    };
    /* Force a fresh address window at the start of the emulation. */
    assert(baji_display_set_brightness(50) == ESP_OK);
    gram.enabled = true;
    command_count = 0;
    for (size_t i = 0; i < sizeof(rectangles)/sizeof(rectangles[0]); ++i) {
        baji_rect_t r = rectangles[i];
        uint8_t *data = strips[i % 2];
        for (unsigned dy = 0; dy < r.height; ++dy) {
            for (unsigned dx = 0; dx < r.width; ++dx) {
                uint16_t value = (uint16_t)(1 + i*997 + dx*13 + dy*173);
                size_t offset = 2u * (dy*r.width + dx);
                data[offset] = value >> 8;
                data[offset+1] = (uint8_t)value;
                expected[r.y+dy][r.x+dx] = value;
            }
        }
        assert(baji_display_write_rect(r.x,r.y,r.width,r.height,data,2u*r.width*r.height) == ESP_OK);
        assert(baji_display_wait() == ESP_OK);
        assert(memcmp(gram.pixels, expected, sizeof(expected)) == 0);
    }
    gram.enabled = false;
}

static void check_window_restart(void)
{
    size_t before = command_count;
    assert(baji_display_write_rect(64, 20, 32, 16, strips[0], 1024) == ESP_OK && polling);
    assert(command_count == before + 2 && borrowed->addr == 0x2c00);
    /* Same columns, non-contiguous y: reset the write pointer with RASET. */
    assert(baji_display_write_rect(64, 20, 32, 18, strips[1], 1152) == ESP_OK && !polling);
    assert(command_count == before + 3 && commands[before + 2].reg == 0x2b);
    assert(borrowed->addr == 0x2c00);
    assert(baji_display_write_rect(64, 38, 32, 2, strips[0], 128) == ESP_OK && polling);
    assert(command_count == before + 3 && borrowed->addr == 0x3c00);
    /* The last continuation advanced the write cursor, but kept the original
     * RASET. Horizontal motion at that row needs only the new columns. */
    assert(baji_display_write_rect(68, 20, 32, 2, strips[0], 128) == ESP_OK);
    assert(command_count == before + 4 && commands[before + 3].reg == 0x2a);
    assert(borrowed->addr == 0x2c00);
    /* A failed page command can leave the hardware with the *new* columns.
     * Redrawing the old columns must restore both ranges on the wire. */
    fail_register = 0x2b;
    assert(baji_display_write_rect(66, 22, 32, 2, strips[0], 128) == ESP_FAIL && !borrowed);
    assert(!bus_locked);
    before = command_count;
    fail_register = 0;
    reject_acquire = true;
    assert(baji_display_write_rect(68, 20, 32, 2, strips[0], 128) == ESP_FAIL && !bus_locked);
    assert(command_count == before);
    reject_acquire = false;
    assert(baji_display_write_rect(68, 20, 32, 2, strips[0], 128) == ESP_OK);
    assert(command_count == before + 2 && commands[before].reg == 0x2a && commands[before+1].reg == 0x2b);
}

static void check_sync_strips(void)
{
    unsigned initial = lock_count;
    assert(baji_display_write_rect(0, 0, 466, 32, strips[0], sizeof(strips[0])) == ESP_OK);
    assert(bus_locked && baji_display_wait() == ESP_OK && !bus_locked);
    for (unsigned y = 32; y <= 64; y += 32) {
        assert(baji_display_write_rect(0, y, 466, 32, strips[0], sizeof(strips[0])) == ESP_OK);
        assert(borrowed->addr == 0x3c00 && !bus_locked);
        assert(baji_display_wait() == ESP_OK);
    }
    assert(lock_count == initial + 1 && unlock_count == lock_count);
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

unsigned check_round_region(baji_board_t *board, baji_rect_t area);

static const baji_rect_t pair[] = {{220,210,20,20}, {228,218,20,20}};
static unsigned painted;

static void paint_pair(const baji_rect_t *area, uint8_t *pixels, void *context)
{
    (void)context;
    assert(painted < 2 && area->x == pair[painted].x && area->y == pair[painted].y);
    assert(area->width == 20 && area->height == 20);
    if (painted == 1) assert(borrowed && borrowed->tx_buffer != pixels);
    memset(pixels, (int)++painted, 800);
}

static void check_round_windows(void)
{
    baji_board_t *board;
    baji_board_info_t info;
    command_count = wait_count = allocations = releases = 0;
    assert(baji_board_open(&board, &info) == ESP_OK);
    command_count = 0;
    assert(check_round_region(board, (baji_rect_t){0,0,466,466}) == 182340);
    assert(check_round_region(board, (baji_rect_t){0,0,4,4}) == 0);
    assert(check_round_region(board, (baji_rect_t){165,167,137,131}) == 138*132);
    const baji_rect_t edges[] = {
        {0,0,466,1}, {1,465,464,1}, {465,1,1,464}, {0,0,1,466},
        {0,0,233,233}, {233,233,233,233}, {29,28,179,231}, {209,141,41,263}
    };
    for (unsigned i = 0; i < sizeof(edges)/sizeof(edges[0]); ++i) {
        command_count = 0;
        assert(check_round_region(board, edges[i]) > 0);
    }
    const baji_rect_t invalid[] = {pair[0], {465,10,2,8}};
    assert(baji_board_paint_round_regions(board, invalid, 2, paint_pair, NULL) == ESP_ERR_INVALID_ARG);
    assert(baji_board_paint_round_regions(board, pair, 0, paint_pair, NULL) == ESP_ERR_INVALID_ARG);
    assert(painted == 0);
    assert(baji_board_paint_round_regions(board, pair, 2, paint_pair, NULL) == ESP_OK);
    assert(painted == 2 && !borrowed);
    painted = 0;
    stalled = true;
    assert(baji_board_paint_round_regions(board, pair, 2, paint_pair, NULL) == ESP_ERR_TIMEOUT);
    assert(painted == 2 && borrowed && ((const uint8_t *)borrowed->tx_buffer)[0] == 1);
    stalled = false;
    painted = 0;
    assert(baji_board_paint_round_regions(board, pair, 2, paint_pair, NULL) == ESP_OK && !borrowed);
    assert(baji_board_close(board) == ESP_OK);
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
    check_window_restart();
    check_window_pixels();
    check_sync_strips();
    assert(baji_display_deinit() == ESP_OK && !borrowed && !bus && !device && !reset);
    open_display();
    assert(baji_display_deinit() == ESP_OK);
    check_board_paint();
    check_round_windows();
    puts("Display protocol, DMA ownership, odd dirty rectangles, double-buffer retries and teardown passed.");
}
