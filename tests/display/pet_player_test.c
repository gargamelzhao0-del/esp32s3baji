#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pet_player.h"

enum { SIDE = 466 };
struct baji_pet { baji_pet_state_t selected; };
struct baji_board {
    uint8_t screen[SIDE][SIDE];
    unsigned submissions;
    bool fail_after_row;
};

static const baji_rect_t pose = {160, 84, 26, 18}, eyes = {200, 204, 32, 8};
unsigned baji_pet_fps(baji_pet_clip_t clip) { return clip == BAJI_PET_PAT ? 16 : 10; }
unsigned baji_pet_frame_count(baji_pet_clip_t clip) { return clip == BAJI_PET_PAT ? 8 : 6; }

void baji_pet_select(baji_pet_t *art, baji_pet_state_t state) { art->selected = state; }

bool baji_pet_forehead(const baji_pet_t *art, baji_pet_state_t state, uint16_t x, uint16_t y)
{
    (void)art;
    (void)state;
    return x == 210 && y == 180;
}

size_t baji_pet_damage(baji_pet_t *art, const baji_pet_state_t *old,
                       const baji_pet_state_t *next, baji_rect_t *regions, size_t capacity)
{
    (void)art;
    assert(old && capacity >= 2);
    size_t n = 0;
    if (old->clip != next->clip || old->frame != next->frame) regions[n++] = pose;
    if (old->gaze_x != next->gaze_x || old->gaze_y != next->gaze_y) regions[n++] = eyes;
    return n;
}

static bool inside(unsigned x, unsigned y, baji_rect_t r)
{
    return x >= r.x && x < (unsigned)r.x+r.width && y >= r.y && y < (unsigned)r.y+r.height;
}

static uint8_t color(baji_pet_state_t state, unsigned x, unsigned y)
{
    if (inside(x, y, pose)) return 1 + 20*state.clip + state.frame;
    if (inside(x, y, eyes)) return 80 + 3*state.gaze_x + state.gaze_y;
    return 24;
}

void baji_pet_paint(const baji_rect_t *band, uint8_t *pixels, void *context)
{
    baji_pet_t *art = context;
    for (unsigned y = 0; y < band->height; ++y) {
        for (unsigned x = 0; x < band->width; ++x) {
            *pixels++ = 0;
            *pixels++ = color(art->selected, band->x+x, band->y+y);
        }
    }
}

esp_err_t baji_board_paint_round_regions(baji_board_t *board, const baji_rect_t *regions,
                                         size_t count, baji_paint_fn paint, void *scene)
{
    uint8_t row[2*SIDE];
    ++board->submissions;
    for (size_t i = 0; i < count; ++i) {
        baji_rect_t r = regions[i];
        assert(r.x+r.width <= SIDE && r.y+r.height <= SIDE);
        unsigned bottom = r.y+r.height;
        r.height = 1;
        for (; r.y < bottom; ++r.y) {
            paint(&r, row, scene);
            for (unsigned x = 0; x < r.width; ++x) board->screen[r.y][r.x+x] = row[2*x+1];
            if (board->fail_after_row) { board->fail_after_row = false; return ESP_FAIL; }
        }
    }
    return ESP_OK;
}

static void verify_screen(const baji_board_t *board, baji_pet_state_t shown)
{
    for (unsigned y = 0; y < SIDE; ++y)
        for (unsigned x = 0; x < SIDE; ++x) assert(board->screen[y][x] == color(shown, x, y));
}

int main(void)
{
    static baji_board_t board;
    baji_pet_t art = {0};
    baji_board_info_t info = {.width=SIDE, .height=SIDE};
    pet_player_t player;
    baji_input_t in = {0};
    bool sent;
    pet_player_init(&player, &art, &info, 0);
    assert(pet_player_tick(&player, &board, &in, 0, &sent) == ESP_OK && sent);
    verify_screen(&board, player.shown);
    assert(pet_player_tick(&player, &board, &in, 17000, &sent) == ESP_OK && !sent);
    assert(board.submissions == 1);

    /* A gaze-only write fails after changing pixels. The next input returns
     * to the previously committed pose, so there is no fresh frame difference. */
    in.touch = (baji_touch_sample_t){.pressed=true, .x=465, .y=233};
    board.fail_after_row = true;
    assert(pet_player_tick(&player, &board, &in, 34000, &sent) == ESP_FAIL && !sent);
    assert(player.damage.count && player.shown.gaze_x == 0);
    assert(board.screen[eyes.y][eyes.x] != color(player.shown, eyes.x, eyes.y));
    in.touch.pressed = false;
    assert(pet_player_tick(&player, &board, &in, 51000, &sent) == ESP_OK && sent);
    assert(!player.damage.count);
    verify_screen(&board, player.shown);
    assert(pet_player_tick(&player, &board, &in, 68000, &sent) == ESP_OK && !sent);

    /* The reader saw a down and an up between scans. Coordinates are retained
     * separately from the released sample, and the counter triggers one pat. */
    in.presses = 1;
    in.press_x = 210;
    in.press_y = 180;
    assert(pet_player_tick(&player, &board, &in, 85000, &sent) == ESP_OK && sent);
    assert(player.shown.clip == BAJI_PET_PAT && player.shown.frame == 0);
    assert(pet_player_tick(&player, &board, &in, 102000, &sent) == ESP_OK && !sent);
    assert(pet_player_tick(&player, &board, &in, 275000, &sent) == ESP_OK && sent);
    assert(player.shown.frame == 3); /* missed scans do not slow the gesture */
    assert(pet_player_tick(&player, &board, &in, 585000, &sent) == ESP_OK && sent);
    assert(player.shown.clip == BAJI_PET_IDLE && player.shown.frame == 0);

    /* Drag the eyes while holding the same idle body frame, then release.
     * The last contact is away from the forehead, so no new pat starts. */
    in.presses++;
    in.press_x = 455;
    in.press_y = 16;
    in.touch = (baji_touch_sample_t){.pressed=true, .x=455, .y=16};
    assert(pet_player_tick(&player, &board, &in, 602000, &sent) == ESP_OK && sent);
    assert(player.shown.gaze_x == 1 && player.shown.gaze_y == -1);
    assert(pet_player_tick(&player, &board, &in, 619000, &sent) == ESP_OK && sent);
    assert(player.shown.gaze_x == 2 && player.shown.frame == 0);
    assert(pet_player_tick(&player, &board, &in, 636000, &sent) == ESP_OK && !sent);
    in.touch.pressed = false;
    assert(pet_player_tick(&player, &board, &in, 653000, &sent) == ESP_OK && sent);
    assert(pet_player_tick(&player, &board, &in, 670000, &sent) == ESP_OK && sent);
    assert(player.shown.gaze_x == 0 && player.shown.gaze_y == 0);
    assert(pet_player_tick(&player, &board, &in, 685000, &sent) == ESP_OK && sent);
    assert(player.shown.clip == BAJI_PET_IDLE && player.shown.frame == 1);
    verify_screen(&board, player.shown);
    puts("Pet short tap, idle cadence, gaze-only updates and interrupted DMA recovery passed.");
}
