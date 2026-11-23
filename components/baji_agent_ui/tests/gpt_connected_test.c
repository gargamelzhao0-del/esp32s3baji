#include "pet_app.h"
#include "baji_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct baji_pet { baji_pet_state_t pose; };
struct baji_board { unsigned batches; bool fail; };
static struct baji_pet art;
static int signal;
static baji_agent_display_t agent = {.has_session = true, .view = {.connection = BAJI_ONLINE}};
static bool read_feed(void *ctx, uint64_t now, baji_agent_display_t *out)
{
    (void)now;
    *out = *(const baji_agent_display_t *)ctx;
    return true;
}
int64_t esp_timer_get_time(void) { return 0; }
esp_err_t baji_pet_open(baji_pet_t **p) { *p = &art; return ESP_OK; }
void baji_pet_close(baji_pet_t *p) { assert(p == &art); }
unsigned baji_pet_substeps(const baji_pet_t *p, baji_pet_clip_t clip) { (void)p; (void)clip; return 4; }
unsigned baji_pet_fps(baji_pet_clip_t clip) { return clip == BAJI_PET_PAT ? 16 : 12; }
unsigned baji_pet_frame_count(baji_pet_clip_t clip) { (void)clip; return 8; }
bool baji_pet_forehead(const baji_pet_t *p, baji_pet_state_t pose, uint16_t x, uint16_t y)
{
    (void)p; (void)pose;
    return x == 230 && y == 150;
}
void baji_pet_select(baji_pet_t *p, baji_pet_state_t pose) { p->pose = pose; }
size_t baji_pet_damage(baji_pet_t *p, const baji_pet_state_t *old, const baji_pet_state_t *next,
                       baji_rect_t *areas, size_t cap)
{
    (void)p; (void)old; (void)next;
    assert(cap > 0);
    areas[0] = (baji_rect_t){160,160,100,10};
    return 1;
}
void baji_pet_paint(const baji_rect_t *r, uint8_t *pixels, void *ctx)
{
    assert(ctx == &art);
    memset(pixels, 0x17, r->width * r->height * 2);
}

esp_err_t baji_board_paint_round_regions(baji_board_t *board, const baji_rect_t *regions,
                                         size_t count, baji_paint_fn paint, void *ctx)
{
    assert(count && regions);
    uint8_t pixels[2];
    paint(&(baji_rect_t){140,385,1,1}, pixels, ctx);
    assert(pixels[0] != 0x17);
    ++board->batches;
    if (board->fail) { board->fail = false; return ESP_FAIL; }
    return ESP_OK;
}

esp_err_t baji_runtime_run(baji_board_t *board, const baji_board_info_t *info,
                           const baji_runtime_config_t *cfg, baji_frame_fn cb, void *ctx)
{
    assert(info->width == 466 && cfg->changed == &signal);
    int64_t next = INT64_MAX;
    baji_input_t input = {0};
    agent.view.pose = "thinking";
    assert(cb(ctx, &input, 0, &next) == ESP_OK && art.pose.gaze_x == -1 && art.pose.gaze_y == -1);
    unsigned batches = board->batches;
    assert(cb(ctx, &input, 0, &next) == ESP_OK && board->batches == batches);
    assert(next == 20834); /* 48 Hz authored motion deadline survives the 250 ms lease tick */
    agent.view.pose = "writing";
    board->fail = true;
    assert(cb(ctx, &input, 0, &next) == ESP_FAIL);
    assert(cb(ctx, &input, 0, &next) == ESP_OK && board->batches == batches + 2);
    assert(cb(ctx, &input, 1000, &next) == ESP_OK && art.pose.gaze_x == 1 && art.pose.gaze_y == 1);
    assert(art.pose.clip == BAJI_PET_IDLE);
    input.presses = 1;
    input.press_x = 230; input.press_y = 150;
    input.touch = (baji_touch_sample_t){true,0,0};
    assert(cb(ctx, &input, 1000, &next) == ESP_OK && art.pose.clip == BAJI_PET_PAT);
    assert(art.pose.gaze_x == 0 && art.pose.gaze_y == 0);
    input.touch.pressed = false;
    agent.view.connection = BAJI_OFFLINE;
    assert(cb(ctx, &input, 1010000, &next) == ESP_OK && art.pose.clip == BAJI_PET_IDLE);
    assert(art.pose.gaze_x == 0 && art.pose.gaze_y == 0);
    agent.view.connection = BAJI_ONLINE;
    agent.view.pose = "running";
    assert(cb(ctx, &input, 1010000, &next) == ESP_OK && art.pose.gaze_x == 1);
    return ESP_OK;
}

int main(void)
{
    struct baji_board board = {0};
    baji_board_info_t info = {.width = 466, .height = 466};
    baji_agent_feed_t feed = {.read = read_feed, .context = &agent, .changed = &signal};
    assert(pet_app_run_connected(&board, &info, &feed) == ESP_OK);
    puts("Connected GPT gaze, 48 Hz deadline, local touch, lost DMA, unchanged snapshots and reconnect passed");
    return 0;
}
