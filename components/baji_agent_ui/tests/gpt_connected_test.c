#include "pet_app.h"
#include "baji_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct baji_pet { baji_pet_state_t pose; };
struct baji_board { unsigned batches; bool fail; };
static struct baji_pet art;
static int signal;
static bool offline;
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
unsigned baji_pet_substeps(const baji_pet_t *p, baji_pet_clip_t clip) { (void)p; return clip==BAJI_PET_IDLE ? 4 : 3; }
unsigned baji_pet_fps(baji_pet_clip_t clip) { return clip==BAJI_PET_IDLE ? 12 : 16; }
unsigned baji_pet_frame_count(baji_pet_clip_t clip) { return clip==BAJI_PET_TICKLE ? 30 : clip>=BAJI_PET_CHEEK_LEFT ? 22 : 8; }
int baji_pet_cheek(const baji_pet_t *p, baji_pet_state_t pose, uint16_t x, uint16_t y)
{
    (void)p; (void)pose;
    return y==255 && x==180 ? -1 : y==255 && x==280 ? 1 : 0;
}
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
    assert((pixels[0] == 0x17) == offline);
    paint(&(baji_rect_t){232,8,1,1}, pixels, ctx);
    assert(pixels[0] != 0x17); /* power always remains visible */
    ++board->batches;
    if (board->fail) { board->fail = false; return ESP_FAIL; }
    return ESP_OK;
}

static void exercise_cheeks(baji_frame_fn cb,void *ctx,baji_input_t *input)
{
    int64_t next;
    int64_t down=3000000;
    input->presses++;
    input->press_x=180; input->press_y=255;
    input->press_us=down;
    input->touch=(baji_touch_sample_t){true,180,255};
    assert(cb(ctx,input,down,&next)==ESP_OK && art.pose.clip==BAJI_PET_CHEEK_LEFT);
    assert(cb(ctx,input,down+500000,&next)==ESP_OK && art.pose.frame==3 && art.pose.phase==0);
    if (offline) assert(next==INT64_MAX);
    input->touch.pressed=false;
    input->touch_us=down+500000;
    assert(cb(ctx,input,input->touch_us,&next)==ESP_OK && art.pose.frame==3);
    assert(cb(ctx,input,down+562500,&next)==ESP_OK && art.pose.frame==4);
    // A right cheek short tap may be fully observed between two displayed frames.
    for (int i=0;i<3;i++) {
        input->presses++;
        input->press_x=input->touch.x=(i==1 ? 180 : 280);
        input->press_us=down+700000+i*270000;
        input->touch_us=input->press_us+65000;
        assert(cb(ctx,input,input->touch_us+1000,&next)==ESP_OK);
        baji_pet_clip_t expected=i==2 ? BAJI_PET_TICKLE :
                                 i==1 ? BAJI_PET_CHEEK_LEFT : BAJI_PET_CHEEK_RIGHT;
        assert(art.pose.clip==expected);
    }
    assert(cb(ctx,input,input->touch_us+2000000,&next)==ESP_OK && art.pose.clip==BAJI_PET_IDLE);
    // Two real reversals cause laughter even without lifting the finger.
    down+=4000000;
    input->presses++;
    input->press_x=input->touch.x=280;
    input->press_us=down;
    input->touch.pressed=true;
    assert(cb(ctx,input,down,&next)==ESP_OK && art.pose.clip==BAJI_PET_CHEEK_RIGHT);
    for (int move=0;move<3;move++) {
        input->touch.x=move==1 ? 244 : 316;
        input->travel=36;
        assert(cb(ctx,input,down+(move+1)*42000,&next)==ESP_OK);
    }
    assert(art.pose.clip==BAJI_PET_TICKLE);
    // A broad swipe that leaves the cheek releases the pose without counting a tap.
    input->presses++;
    input->press_us=down+300000;
    input->touch.x=280;
    input->travel=0;
    assert(cb(ctx,input,input->press_us,&next)==ESP_OK);
    input->touch.x=325;
    assert(cb(ctx,input,input->press_us+250000,&next)==ESP_OK);
    assert(cb(ctx,input,input->press_us+1600000,&next)==ESP_OK && art.pose.clip==BAJI_PET_IDLE);
    // Both the outward motion and the return can be sampled before the
    // renderer runs. The sticky maximum still ends the contact at that point.
    for (unsigned axis=0;axis<2;axis++) {
        input->presses++;
        input->press_us+=2000000;
        input->touch.x=280;
        input->travel=0;
        input->travel_y=0;
        assert(cb(ctx,input,input->press_us,&next)==ESP_OK && art.pose.clip==BAJI_PET_CHEEK_RIGHT);
        input->travel=axis==0 ? 45 : 27;
        input->travel_y=axis==0 ? 0 : 27;
        assert(cb(ctx,input,input->press_us+250000,&next)==ESP_OK);
        assert(cb(ctx,input,input->press_us+1600000,&next)==ESP_OK && art.pose.clip==BAJI_PET_IDLE);
    }
}

esp_err_t baji_runtime_run(baji_board_t *board, const baji_board_info_t *info,
                           const baji_runtime_config_t *cfg, baji_frame_fn cb, void *ctx)
{
    assert(info->width == 466 && cfg->changed == (offline ? NULL : &signal) && cfg->watch_power);
    int64_t next = INT64_MAX;
    baji_input_t input = {.power = {BAJI_POWER_UNKNOWN,-1}};
    if (offline) {
        assert(cb(ctx, &input, 0, &next) == ESP_OK);
        unsigned previous = board->batches;
        input.power = (baji_power_reading_t){BAJI_POWER_BATTERY,47};
        assert(cb(ctx, &input, 0, &next) == ESP_OK && board->batches == previous + 1);
        assert(cb(ctx, &input, 0, &next) == ESP_OK && board->batches == previous + 1);
        exercise_cheeks(cb,ctx,&input);
        return ESP_OK;
    }
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
    exercise_cheeks(cb,ctx,&input);
    return ESP_OK;
}

int main(void)
{
    struct baji_board board = {0};
    baji_board_info_t info = {.width = 466, .height = 466};
    baji_agent_feed_t feed = {.read = read_feed, .context = &agent, .changed = &signal};
    assert(pet_app_run_connected(&board, &info, &feed) == ESP_OK);
    offline = true;
    assert(pet_app_run(&board, &info) == ESP_OK);
    puts("Connected GPT gaze, 48 Hz deadline, local touch, lost DMA, unchanged snapshots and reconnect passed");
    return 0;
}
