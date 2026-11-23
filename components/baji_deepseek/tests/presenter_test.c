/* Regression for partial DMA, failed decoding that discards the old image,
 * and switching renderers while the display contains the other mode. */
#include "baji_deepseek_app.h"
#include "baji_deepseek_hd.h"
#include "baji_runtime.h"
#include "deepseek_review.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static bool fail_dma, fail_decode, last_full;
static unsigned transfers, opened_hd, closed_hd;
static uint16_t token;
static int64_t now;
static baji_input_t input;
static deepseek_app_t *app;

int64_t esp_timer_get_time(void) { return now; }
esp_err_t baji_runtime_run(baji_board_t *board, const baji_board_info_t *info,
                           const baji_runtime_config_t *cfg, baji_frame_fn cb, void *ctx)
{
    (void)board; (void)info; (void)cfg; (void)cb; (void)ctx;
    abort(); /* Test drives the public presenter at deterministic timestamps. */
}
bool dsh_art_open(dsh_art_t *art, const void *bytes, size_t size)
{
    art->bytes = bytes;
    return size == 1;
}
bool dsh_canvas_init(dsh_canvas_t *c, const dsh_art_t *art)
{
    *c = (dsh_canvas_t){.art = art};
    opened_hd++;
    return true;
}
bool dsh_motion_open(dsh_motion_t *m, const void *bytes, size_t size)
{
    m->bytes = bytes;
    return size == 1;
}
void dsh_canvas_set_motion(dsh_canvas_t *c, const dsh_motion_t *m)
{
    c->motion = m;
}
bool dsh_canvas_select(dsh_canvas_t *c, dsh_view_t view)
{
    c->image = NULL;
    if (fail_decode) { fail_decode = false; return false; }
    c->view = view;
    c->image = &token;
    return true;
}
void dsh_canvas_close(dsh_canvas_t *c)
{
    c->image = NULL;
    closed_hd++;
}
void dsh_paint(const baji_rect_t *r, uint8_t *out, void *ctx)
{
    (void)r; (void)out;
    assert(((const dsh_canvas_t *)ctx)->image);
}
esp_err_t baji_board_paint_round_regions(baji_board_t *board, const baji_rect_t *regions,
                                         size_t count, baji_paint_fn paint, void *ctx)
{
    assert(board && count && ctx);
    last_full = count == 1 && regions[0].width == DSK_SIDE && regions[0].height == DSK_SIDE;
    transfers++;
    uint8_t pixel[2];
    baji_rect_t dot = {regions[0].x, regions[0].y, 1, 1};
    paint(&dot, pixel, ctx); /* Hardware may already have consumed part of a failed batch. */
    if (fail_dma) { fail_dma = false; return ESP_FAIL; }
    return ESP_OK;
}
static esp_err_t tick(int delta)
{
    now += delta;
    int64_t next = now;
    return deepseek_app_tick(app, &input, now, &next);
}
static void hold(void)
{
    input.presses++;
    input.press_x = input.press_y = 230;
    input.touch = (baji_touch_sample_t){true,230,230};
    assert(tick(1000) == ESP_OK && tick(650000) == ESP_OK && last_full);
    input.touch.pressed = false;
    assert(tick(1000) == ESP_OK);
}
static void tap(unsigned x, unsigned y, bool ok)
{
    input.presses++;
    input.press_x = input.touch.x = x;
    input.press_y = input.touch.y = y;
    input.touch.pressed = false;
    assert((tick(1000) == ESP_OK) == ok);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *in = fopen(argv[1], "rb");
    assert(in && !fseek(in, 0, SEEK_END));
    long length = ftell(in);
    assert(length > 40 && !fseek(in, 0, SEEK_SET));
    void *pack = malloc(length);
    assert(pack && fread(pack, 1, length, in) == (size_t)length);
    fclose(in);
    baji_board_info_t info = {.width = DSK_SIDE, .height = DSK_SIDE};
    assert(deepseek_app_open(&app, (baji_board_t *)&token, &info, pack, length, "x", 1, 0) == ESP_OK);
    assert(tick(0) == ESP_OK && transfers == 1 && last_full);
    fail_dma = true;
    assert(tick(150000) == ESP_FAIL);
    unsigned before = transfers;
    assert(tick(1) == ESP_OK && transfers == before + 1);
    hold();
    assert(deepseek_app_is_hd(app) && opened_hd == 1);
    tap(230, 400, true); /* freeze CSS time so the next two commands restore the identical view */
    fail_decode = true;
    tap(230, 65, false); /* lose the old image while toggling its guides */
    tap(230, 65, true);  /* return to old view; it still needs to be decoded again */
    assert(last_full);
    hold();
    assert(!deepseek_app_is_hd(app) && closed_hd == 1);
    deepseek_app_close(app);
    free(pack);
    puts("Presenter recovers partial transfer and discarded HD decode before returning to pixel mode");
    return 0;
}
