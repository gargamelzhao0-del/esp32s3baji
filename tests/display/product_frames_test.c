#include "baji_board.h"
#include "spi_recorder.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int64_t now;
static esp_err_t result;
static unsigned reports;
static uint16_t color;
static baji_rect_t point = {106,166,1,1};

int64_t esp_timer_get_time(void) { return now; }
size_t heap_caps_get_free_size(unsigned caps) { assert(caps == (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)); return 1024; }

static void draw(const baji_rect_t *rect, uint8_t *pixels, void *context)
{
    assert(rect == &point && !context);
    pixels[0] = color >> 8;
    pixels[1] = (uint8_t)color;
}

esp_err_t __real_baji_board_paint_round_regions(baji_board_t *board, const baji_rect_t *regions,
                                                size_t count, baji_paint_fn paint, void *context)
{
    assert(!board && count == 1);
    uint8_t pixels[2];
    paint(regions, pixels, context);
    return result;
}

void frame_log(const char *tag, const char *fmt, unsigned ms, unsigned submitted, unsigned changed,
               unsigned errors, unsigned dma)
{
    assert(!strcmp(tag, "baji_frames") && strstr(fmt, "character=") && dma == 1024);
    const unsigned expected[][4] = {{5000,4,2,1}, {6000,1,0,0}, {5000,2,0,1}, {5000,1,0,0}};
    assert(reports < 4 && ms == expected[reports][0] && submitted == expected[reports][1] &&
           changed == expected[reports][2] && errors == expected[reports][3]);
    ++reports;
}

esp_err_t __wrap_baji_board_paint_round_regions(baji_board_t *, const baji_rect_t *, size_t, baji_paint_fn, void *);
static void submit(int ms, uint16_t rgb, esp_err_t outcome)
{
    now = (int64_t)ms * 1000;
    color = rgb;
    result = outcome;
    assert(__wrap_baji_board_paint_round_regions(NULL, &point, 1, draw, NULL) == result);
}

int main(void)
{
    submit(0, 1, ESP_OK);
    submit(1000, 2, ESP_FAIL);
    submit(2000, 2, ESP_OK); /* the retry counts against the last delivered pixel */
    submit(5000, 2, ESP_OK);
    submit(11000, 2, ESP_OK); /* the whole idle gap belongs to this window */
    submit(11500, 3, ESP_FAIL);
    submit(16000, 2, ESP_OK); /* a dropped change that reverted adds no animation frame */
    point = (baji_rect_t){202,374,1,1};
    submit(21000, 4, ESP_OK); /* the first row of the footer does not animate the character */
    assert(reports == 4);
    puts("Product frame windows include idle time and commit only delivered character changes");
}
