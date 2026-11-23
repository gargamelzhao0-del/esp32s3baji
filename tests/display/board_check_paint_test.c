#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "board_check.h"

enum { WIDTH = 466, HEIGHT = 466, STRIP = 32 };
static const baji_touch_sample_t touches[] = {
    {false, 0, 0}, {true, 0, 0}, {true, 465, 465}, {true, 0, 465}, {true, 465, 0},
    {true, 233, 57}, {true, 57, 233}, {true, 202, 218}, {true, 203, 219},
    {true, 263, 247}, {false, 0, 0}
};
static unsigned step;
static int checked = -1;

/* Independent per-pixel reference from the original G1 scene. */
static uint16_t reference(int x, int y)
{
    int dx = x - 233, dy = y - 233;
    int mx = abs(x - touches[step].x), my = abs(y - touches[step].y);
    if (touches[step].pressed && ((mx <= 1 && my <= 9) || (my <= 1 && mx <= 9))) return 0xffff;
    if (dy >= -15 && dy < 15 && dx >= -90 && dx < 90)
        return dx < -30 ? 0xf800 : dx < 30 ? 0x07e0 : 0x001f;
    if (abs(dx) <= 2 && dy >= -177 && dy < -155) return 0xff2e;
    int squared = dx * dx + dy * dy;
    return squared >= 174 * 174 && squared <= 176 * 176 ? 0x526b : 0x1083;
}

static void check_rect(baji_rect_t area, baji_paint_fn paint, void *scene)
{
    static uint8_t buf[WIDTH * STRIP * 2 + 2];
    unsigned end = area.y + area.height;
    for (unsigned y = area.y; y < end; y += STRIP) {
        area.y = y;
        area.height = end - y < STRIP ? end - y : STRIP;
        size_t bytes = 2u * area.width * area.height;
        memset(buf, 0xa5, bytes+2);
        paint(&area, buf+1, scene);
        assert(buf[0] == 0xa5 && buf[bytes+1] == 0xa5);
        size_t pos = 1;
        for (int row = area.y; row < area.y + area.height; ++row) {
            for (int x = area.x; x < area.x + area.width; ++x) {
                uint16_t pixel = reference(x, row);
                assert(buf[pos++] == pixel >> 8);
                assert(buf[pos++] == (pixel & 0xff));
            }
        }
    }
}

esp_err_t baji_board_paint_round(baji_board_t *board, baji_rect_t dirty, baji_paint_fn paint, void *scene)
{
    (void)board;
    check_rect(dirty, paint, scene);
    if ((int)step != checked) {
        checked = step;
        check_rect((baji_rect_t){0, 0, WIDTH, HEIGHT}, paint, scene);
        check_rect((baji_rect_t){57, 55, 201, 347}, paint, scene);
        check_rect((baji_rect_t){465, 3, 1, 463}, paint, scene);
    }
    /* End the board's continuous loop after validating the release. */
    return step + 1 == sizeof(touches)/sizeof(touches[0]) ? ESP_FAIL : ESP_OK;
}

esp_err_t baji_board_paint_round_regions(baji_board_t *board, const baji_rect_t *areas, size_t count,
                                         baji_paint_fn paint, void *scene)
{
    assert(count == 2);
    for (size_t i = 0; i < count; ++i) {
        esp_err_t err = baji_board_paint_round(board, areas[i], paint, scene);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t baji_board_set_brightness(baji_board_t *b, uint8_t pct)
{ (void)b; assert(pct == 45); return ESP_OK; }
baji_peripherals_t *baji_board_peripherals(baji_board_t *b) { (void)b; return NULL; }
esp_err_t baji_peripherals_wait_touch(baji_peripherals_t *p, uint32_t ms)
{ (void)p; (void)ms; return ESP_OK; }
esp_err_t baji_peripherals_read_touch(baji_peripherals_t *p, baji_touch_sample_t *s)
{ (void)p; assert(step + 1 < sizeof(touches)/sizeof(touches[0])); *s = touches[++step]; return ESP_OK; }
esp_err_t baji_peripherals_poll_touch(baji_peripherals_t *p, baji_touch_sample_t *s)
{ (void)p; (void)s; assert(0); return ESP_FAIL; }
esp_err_t baji_peripherals_read_power(baji_peripherals_t *p, baji_power_status_t *s)
{ (void)p; (void)s; assert(0); return ESP_FAIL; }
void vTaskDelay(int ticks) { (void)ticks; assert(0); }

int main(void)
{
    baji_board_info_t info = {.width=WIDTH, .height=HEIGHT, .touch_status=ESP_OK, .power_status=ESP_FAIL};
    assert(baji_board_check_run(NULL,&info) == ESP_FAIL);
    assert(step + 1 == sizeof(touches)/sizeof(touches[0]));
    puts("G1 217156 pixels per scene: raw RGB565, ring, ticks, touch overlays, release and odd crops passed.");
}
