#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "baji_display.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_clk.h"

/* Exercise the exact production G1 paint callback, without changing its linkage
 * or exporting test-only symbols from the badge firmware. */
#include "board_check.c"

enum { WARMUP = 6, SAMPLES = 48 };

typedef enum { DRAW_RECT, DRAW_ROUND, DRAW_ROUND_BATCH, RAW_SYNC, RAW_STREAM } draw_t;
typedef struct {
    const char *name;
    baji_rect_t area;
    draw_t method;
    bool moves;
    bool erase_old;
} workload_t;

typedef struct {
    baji_board_t *board;
    scene_t scene;
    uint8_t *wire;
} fixture_t;

static const workload_t workloads[] = {
    {"raw_full_sync",   {0, 0, 466, 466}, .method = RAW_SYNC},
    {"raw_full_stream", {0, 0, 466, 466}, .method = RAW_STREAM},
    {"g1_full",         {0, 0, 466, 466}, .method = DRAW_RECT},
    {"center_138x132",  {164, 166, 138, 132}, .method = DRAW_RECT},
    {"corner_112x112",  {8, 8, 112, 112}, .method = DRAW_RECT},
    {"still_24x24",     {210, 210, 24, 24}, .method = DRAW_RECT},
    {"still_20x20",     {210, 210, 20, 20}, .method = DRAW_RECT},
    {"move_20x20",      {90, 98, 20, 20}, .method = DRAW_RECT, .moves = true},
    {"two_dirty_20x20", {90, 98, 20, 20}, .method = DRAW_RECT, .moves = true, .erase_old = true},
    {"two_dirty_large_sync", {90, 125, 100, 80}, .method = DRAW_RECT, .moves = true, .erase_old = true},
#ifdef BAJI_BENCH_BATCH
    {"two_dirty_round_batch", {90, 98, 20, 20}, .method = DRAW_ROUND_BATCH, .moves = true, .erase_old = true},
    {"two_dirty_large_batch", {90, 125, 100, 80}, .method = DRAW_ROUND_BATCH, .moves = true, .erase_old = true},
#endif
    {"still_2x2",       {232, 232, 2, 2}, .method = DRAW_RECT},
    {"move_2x2",        {90, 98, 2, 2}, .method = DRAW_RECT, .moves = true},
    {"column_2x466",    {232, 0, 2, 466}, .method = DRAW_RECT},
#ifdef BAJI_BENCH_ROUND
    {"round_full",          {0, 0, 466, 466}, .method = DRAW_ROUND},
    {"round_center_138x132",{164, 166, 138, 132}, .method = DRAW_ROUND},
    {"round_corner_112x112",{8, 8, 112, 112}, .method = DRAW_ROUND},
#endif
};

static esp_err_t raw_frame(fixture_t *f, bool sync)
{
    /* Immutable internal DMA strip. The no-fill comparison measures transport
     * overhead, including the last completion. */
    for (unsigned y = 0; y < BAJI_DISPLAY_HEIGHT; y += BAJI_DISPLAY_STRIP_ROWS) {
        unsigned lines = BAJI_DISPLAY_HEIGHT - y;
        if (lines > BAJI_DISPLAY_STRIP_ROWS) lines = BAJI_DISPLAY_STRIP_ROWS;
        esp_err_t e = baji_display_write_rect(0, y, BAJI_DISPLAY_WIDTH, lines, f->wire,
                                               BAJI_DISPLAY_WIDTH * lines * 2);
        if (e != ESP_OK) return e;
        if (sync) {
            e = baji_display_wait();
            if (e != ESP_OK) return e;
        }
    }
    return baji_display_wait();
}

static esp_err_t draw_once(fixture_t *f, const workload_t *w, unsigned sample)
{
    if (w->method == RAW_SYNC || w->method == RAW_STREAM)
        return raw_frame(f, w->method == RAW_SYNC);
#ifdef BAJI_BENCH_ROUND
    if (w->method == DRAW_ROUND)
        return baji_board_paint_round(f->board, w->area, paint, &f->scene);
#endif
    baji_rect_t area = w->area;
    if (w->moves) {
        baji_rect_t far = {area.x + 194, area.y + 182, area.width, area.height};
        if (w->erase_old) {
            baji_rect_t old = (sample & 1) ? w->area : far;
#ifdef BAJI_BENCH_BATCH
            if (w->method == DRAW_ROUND_BATCH) {
                const baji_rect_t pair[] = {old, (sample & 1) ? far : w->area};
                return baji_board_paint_round_regions(f->board, pair, 2, paint, &f->scene);
            }
#endif
            esp_err_t e = baji_board_paint(f->board, old, paint, &f->scene);
            if (e != ESP_OK) return e;
        }
        if (sample & 1) area = far;
    }
    return baji_board_paint(f->board, area, paint, &f->scene);
}

static int compare(const void *a, const void *b)
{
    const unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;
    return (x > y) - (x < y);
}

static esp_err_t measure(fixture_t *f, const workload_t *w)
{
    unsigned values[SAMPLES], total = 0;
    for (unsigned i = 0; i < WARMUP + SAMPLES; ++i) {
        int64_t start = esp_timer_get_time();
        esp_err_t e = draw_once(f, w, i);
        unsigned elapsed = (unsigned)(esp_timer_get_time() - start);
        if (e != ESP_OK) {
            printf("DISPLAY_BENCH_FAIL case=%s iteration=%u err=%d\n", w->name, i, e);
            return e;
        }
        if (i >= WARMUP) {
            values[i-WARMUP] = elapsed;
            total += elapsed;
        }
        if ((i & 7U) == 7U) vTaskDelay(1);
    }
    qsort(values, SAMPLES, sizeof(values[0]), compare);
    printf("DISPLAY_BENCH case=%s n=%d min_us=%u p50_us=%u p95_us=%u max_us=%u mean_us=%u\n",
           w->name, SAMPLES, values[0], (values[SAMPLES/2-1]+values[SAMPLES/2])/2,
           values[(SAMPLES*95+99)/100-1], values[SAMPLES-1], total/SAMPLES);
    return ESP_OK;
}

/* The 1.75C schematic connects LCD_TE to GPIO13. Measure scan cadence in
 * isolation, then remove the handler before timing any draws. */
static portMUX_TYPE te_lock = portMUX_INITIALIZER_UNLOCKED;
static unsigned te_count;
static uint32_t first_edge_us, last_edge_us;

static void IRAM_ATTR te_edge(void *arg)
{
    (void)arg;
    uint32_t now = (uint32_t)esp_timer_get_time();
    portENTER_CRITICAL_ISR(&te_lock);
    if (te_count++ == 0) first_edge_us = now;
    last_edge_us = now;
    portEXIT_CRITICAL_ISR(&te_lock);
}

static void measure_te(void)
{
    const gpio_num_t pin = GPIO_NUM_13;
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_INPUT, .intr_type = GPIO_INTR_POSEDGE,
    };
    esp_err_t e = gpio_config(&cfg);
    if (e == ESP_OK) e = gpio_isr_handler_add(pin, te_edge, NULL);
    if (e != ESP_OK) {
        printf("DISPLAY_TE setup_err=%d\n", e);
        gpio_reset_pin(pin);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    gpio_isr_handler_remove(pin);
    portENTER_CRITICAL(&te_lock);
    unsigned count = te_count;
    uint32_t span_us = last_edge_us - first_edge_us;
    portEXIT_CRITICAL(&te_lock);
    gpio_reset_pin(pin);
    printf("DISPLAY_TE pin=%d edges=%u span_us=%" PRIu32 " period_us=%" PRIu32 "\n",
           pin, count, span_us, count > 1 ? span_us / (count - 1) : 0);
}

void app_main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    vTaskDelay(pdMS_TO_TICKS(2500));
    fixture_t f = {0};
    baji_board_info_t info;
    esp_err_t e = baji_board_open(&f.board, &info);
    if (e != ESP_OK) {
        printf("DISPLAY_BENCH_FAIL open=%d\n", e);
        return;
    }
    f.scene.cx = info.width/2;
    f.scene.cy = info.height/2;
    f.wire = heap_caps_calloc(1, BAJI_DISPLAY_MAX_TRANSFER_BYTES, MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
    if (!f.wire) {
        puts("DISPLAY_BENCH_FAIL out_of_dma_memory");
        while (baji_board_close(f.board) != ESP_OK) vTaskDelay(pdMS_TO_TICKS(100));
        return;
    }
    printf("DISPLAY_BENCH_BEGIN cpu_mhz=%d strip_rows=%d dma_free=%u\n",
           esp_clk_cpu_freq()/1000000, BAJI_DISPLAY_STRIP_ROWS,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    measure_te();
    for (size_t i = 0; i < sizeof(workloads)/sizeof(workloads[0]); ++i) {
        e = measure(&f, &workloads[i]);
        if (e != ESP_OK) break;
    }
    /* If a transfer timed out the driver could still own f.wire. Keep it
     * live until successful release, including when reporting a failure. */
    esp_err_t drained = baji_display_wait();
    if (drained == ESP_OK) {
        heap_caps_free(f.wire);
        f.wire = NULL;
    }
    if (e == ESP_OK) e = drained;
    printf("DISPLAY_BENCH_DONE err=%d\n", e);
    if (e == ESP_OK) baji_board_check_run(f.board, &info);
    while (baji_board_close(f.board) != ESP_OK) vTaskDelay(pdMS_TO_TICKS(100));
    if (f.wire) heap_caps_free(f.wire);
}
