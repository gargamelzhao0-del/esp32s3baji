/* Regression for partial DMA, failed decoding that discards the old image,
 * and switching renderers while the display contains the other mode. */
#include "baji_deepseek_app.h"
#include "baji_deepseek_hd.h"
#include "baji_runtime.h"
#include "deepseek_review.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail_dma, fail_decode, last_full;
static unsigned transfers, opened_hd, closed_hd, hd_item;
static int64_t next_update;
static baji_agent_display_t agent = {
    .has_session = true, .view = {.connection = BAJI_ONLINE, .phase = BAJI_TOOL, .pose = "reading"}
};
static bool read_feed(void *context, uint64_t now_ms, baji_agent_display_t *out)
{
    assert(context == &agent && now_ms < UINT64_MAX);
    *out = agent;
    return true;
}
static uint16_t token;
static int64_t now;
static baji_input_t input;
static deepseek_app_t *app;
static bool started_without_partition;
static uint8_t *capture;

int64_t esp_timer_get_time(void) { return now; }
const void *dsh_builtin(size_t *size) { *size = 1; return "x"; }
bool dsh_partition_motion_open(dsh_motion_t *out) { assert(!out->count); return false; }
void dsh_partition_motion_close(dsh_motion_t *out) { assert(!out->count); }
esp_err_t baji_runtime_run(baji_board_t *board, const baji_board_info_t *info,
                           const baji_runtime_config_t *cfg, baji_frame_fn cb, void *ctx)
{
    assert(board && info && cfg && cfg->watch_power && cb);
    baji_input_t sample = {.power = {BAJI_POWER_UNKNOWN,-1}};
    int64_t deadline;
    assert(cb(ctx, &sample, now, &deadline) == ESP_OK && !deepseek_app_is_hd(ctx));
    unsigned previous = transfers;
    sample.power = (baji_power_reading_t){BAJI_POWER_USB_ONLY,-1};
    assert(cb(ctx, &sample, now, &deadline) == ESP_OK && transfers == previous + 1 && !last_full);
    assert(cb(ctx, &sample, now, &deadline) == ESP_OK && transfers == previous + 1);
    sample.presses = 1;
    sample.press_x = sample.press_y = 220;
    sample.touch = (baji_touch_sample_t){true,220,220};
    assert(cb(ctx, &sample, now, &deadline) == ESP_OK);
    assert(cb(ctx, &sample, now + 650000, &deadline) == ESP_OK && deepseek_app_is_hd(ctx));
    previous = transfers;
    sample.power = (baji_power_reading_t){BAJI_POWER_CHARGING,42};
    assert(cb(ctx, &sample, now + 650000, &deadline) == ESP_OK && transfers == previous + 1 && !last_full);
    assert(cb(ctx, &sample, now + 650000, &deadline) == ESP_OK && transfers == previous + 1);
    started_without_partition = true;
    return ESP_OK;
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
bool dsh_canvas_select_blend(dsh_canvas_t *c, dsh_view_t view, unsigned mix)
{
    (void)mix;
    c->image = NULL;
    if (fail_decode) { fail_decode = false; return false; }
    hd_item = view.item;
    c->view = view;
    c->image = &token;
    return true;
}
unsigned dsh_canvas_motion_damage(dsh_canvas_t *c, unsigned from, baji_rect_t areas[DSH_DAMAGE_STRIPS])
{
    (void)c;
    (void)from;
    areas[0] = (baji_rect_t){DSH_INSET, DSH_INSET, DSH_STAGE, DSH_STAGE};
    return 1;
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
    if (capture) {
        static uint8_t contiguous[DSK_SIDE*DSK_SIDE*2];
        uint8_t line[DSK_SIDE*2];
        for (size_t i = 0; i < count; i++) {
            baji_rect_t row = regions[i];
            paint(&row, contiguous, ctx);
            row.height = 1;
            for (unsigned j = 0; j < regions[i].height; j++, row.y++) {
                paint(&row, line, ctx);
                assert(!memcmp(contiguous+2*j*row.width, line, 2*row.width));
                memcpy(capture + 2*(DSK_SIDE*row.y + row.x), line, 2*row.width);
            }
        }
    }
    uint8_t pixel[2];
    baji_rect_t dot = {regions[0].x, regions[0].y, 1, 1};
    paint(&dot, pixel, ctx); /* Hardware may already have consumed part of a failed batch. */
    if (fail_dma) { fail_dma = false; return ESP_FAIL; }
    return ESP_OK;
}
static esp_err_t tick(int delta)
{
    now += delta;
    return deepseek_app_tick(app, &input, now, &next_update);
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

static void nap_pixels(const baji_board_info_t *info, const void *pack, size_t length, bool close)
{
    static uint8_t screens[2][DSK_SIDE*DSK_SIDE*2];
    deepseek_app_t *pair[2];
    baji_input_t events[2] = {0};
    int64_t t = 0, due;
    memset(screens, 0, sizeof(screens));
    unsigned steps = 0;
    for (int i = 0; i < 2; i++) {
        capture = screens[i];
        assert(deepseek_app_open(&pair[i], (baji_board_t *)&token, info, pack, length, NULL, 0, 0) == ESP_OK);
        assert(deepseek_app_tick(pair[i], &events[i], 0, &due) == ESP_OK);
        /* Normal tap navigation from feed to nap, plus the user-facing close-up swipe. */
        events[i] = (baji_input_t){.press_x=230,.press_y=220,.touch={false,230,220}};
        for (int j = 0; j < 4; j++) {
            events[i].presses++;
            assert(deepseek_app_tick(pair[i], &events[i], 0, &due) == ESP_OK);
        }
        if (close) {
            events[i].presses++;
            events[i].touch.y = 129;
            assert(deepseek_app_tick(pair[i], &events[i], 0, &due) == ESP_OK);
        }
    }
    assert(!memcmp(screens[0], screens[1], sizeof(screens[0])));
    events[1].presses++;
    events[1].press_x = events[1].touch.x = close ? 156 : 176;
    events[1].press_y = events[1].touch.y = close ? 367 : 299;
    events[1].touch.pressed = true;
    /* One native nap cycle at all eight in-between samples per key, then the rebound. */
    for (int frame = 0; frame <= 71; frame++) {
        if (frame == 65) events[1].touch.pressed = false;
        for (int i = 0; i < 2; i++) {
            capture = screens[i];
            assert(deepseek_app_tick(pair[i], &events[i], t, &due) == ESP_OK);
        }
        unsigned changed = 0;
        for (int y = 0; y < DSK_SIDE; y++) {
            for (int x = 0; x < DSK_SIDE; x++) {
                int p = 2*(y*DSK_SIDE+x);
                if (screens[0][p] == screens[1][p] && screens[0][p+1] == screens[1][p+1]) continue;
                assert(close ? (x >= 135 && x <= 177 && y >= 350 && y < 385)
                             : (x >= 155 && x <= 197 && y >= 284 && y <= 314));
                changed++;
            }
        }
        if (frame < 65) { assert(changed > 10); steps++; }
        if (frame == 71) assert(changed == 0);
        t += 31250;
    }
    assert(steps == 65);
    for (int i = 0; i < 2; i++) deepseek_app_close(pair[i]);
    capture = NULL;
    transfers = 0;
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
    nap_pixels(&info, pack, length, false);
    nap_pixels(&info, pack, length, true);
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
    baji_agent_feed_t feed = {.context = &agent, .read = read_feed, .changed = &token};
    assert(deepseek_app_attach_feed(app, &feed) == ESP_OK);
    assert(tick(0) == ESP_OK); /* pixel stage can select the real reading state */
    hold();
    assert(hd_item == DSK_CLIPS + 2);
    unsigned prev = transfers;
    assert(tick(1000) == ESP_OK && transfers == prev && next_update == INT64_MAX);
    agent.view.pose = "writing";
    assert(tick(0) == ESP_OK && hd_item == DSK_CLIPS + 3);
    tap(230, 230, true);
    assert(hd_item < DSK_CLIPS);
    unsigned local_item = hd_item;
    assert(tick(10000) == ESP_OK && hd_item == local_item);
    assert(tick(2100000) == ESP_OK && hd_item == DSK_CLIPS + 3);
    agent.view.connection = BAJI_OFFLINE;
    assert(tick(0) == ESP_OK && hd_item == local_item);
    assert(next_update == INT64_MAX); /* paused HD now waits on touch or a fresh snapshot */
    agent.view.connection = BAJI_ONLINE;
    agent.view.pose = "asking";
    assert(tick(0) == ESP_OK && hd_item == DSK_CLIPS + 6);
    agent.has_session = false;
    assert(tick(0) == ESP_OK && hd_item == local_item);
    /* A gesture that starts between remote jobs still owns its clip. */
    tap(230, 230, true);
    assert(hd_item == (local_item + 1) % DSK_CLIPS);
    local_item = hd_item;
    agent.has_session = true;
    agent.view.pose = "reading";
    assert(tick(80000) == ESP_OK && hd_item == local_item);
    agent.view.connection = BAJI_OFFLINE;
    assert(tick(5000) == ESP_OK && hd_item == local_item);
    agent.view.connection = BAJI_ONLINE;
    assert(tick(5000) == ESP_OK && hd_item == local_item);
    assert(tick(2100000) == ESP_OK && hd_item == DSK_CLIPS + 2);
    /* Start from the reading pose seen at touch down. Even a mid-gesture
     * Agent update cannot change the base for a left swipe. */
    input.presses++;
    input.press_x = input.touch.x = 280;
    input.press_y = input.touch.y = 220;
    input.touch.pressed = true;
    assert(tick(1000) == ESP_OK);
    agent.view.pose = "searching";
    input.touch.x = 170;
    input.touch.pressed = false;
    assert(tick(10000) == ESP_OK && hd_item == DSK_CLIPS + 3);
    assert(tick(1600000) == ESP_OK && hd_item == DSK_CLIPS + 5);
    input.presses++;
    input.press_x = input.touch.x = 230;
    input.press_y = 280;
    input.touch.y = 160;
    assert(tick(1000) == ESP_OK && last_full); /* enter the overview */
    prev = transfers;
    agent.view.pose = "reading";
    assert(tick(0) == ESP_OK && transfers == prev);
    deepseek_app_close(app);
    assert(deepseek_app_run((baji_board_t *)&token, &info) == ESP_OK && started_without_partition);
    free(pack);
    puts("Presenter recovers partial transfers and HD decode; gestures finish across remote changes and navigate the visible pose");
    return 0;
}
