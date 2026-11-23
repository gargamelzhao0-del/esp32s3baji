#include "pet_player.h"

void pet_player_init(pet_player_t *player, baji_pet_t *art,
                     const baji_board_info_t *info, int64_t now_us)
{
    *player = (pet_player_t){.art = art, .idle_since = now_us};
    baji_damage_init(&player->damage, info->width, info->height);
    baji_damage_mark(&player->damage, 0, 0, info->width, info->height);
}

/* Quantise around the face, with hysteresis to keep a stationary finger
 * near a boundary from making the eyes toggle at the scan frequency. */
static int gaze_axis(int position, int center, int spacing, int limit, int previous)
{
    int offset = position - center;
    if (previous < limit && 2 * offset > (2 * previous + 1) * spacing + 8)
        return previous + 1;
    if (previous > -limit && 2 * offset < (2 * previous - 1) * spacing - 8)
        return previous - 1;
    return previous;
}

static int toward_center(int gaze)
{
    return gaze - (gaze > 0) + (gaze < 0);
}

static void update_gaze(baji_pet_state_t *next, const baji_input_t *input)
{
    if (input->touch.pressed) {
        next->gaze_x = gaze_axis(input->touch.x, 233, 75, 2, next->gaze_x);
        next->gaze_y = gaze_axis(input->touch.y, 233, 102, 1, next->gaze_y);
    } else {
        next->gaze_x = toward_center(next->gaze_x);
        next->gaze_y = toward_center(next->gaze_y);
    }
}

int64_t pet_player_next_update(const pet_player_t *p, const baji_input_t *input, int64_t now)
{
    baji_pet_state_t gaze = p->shown;
    update_gaze(&gaze, input);
    if (gaze.gaze_x != p->shown.gaze_x || gaze.gaze_y != p->shown.gaze_y)
        return now;
    int64_t origin = p->patting ? p->pat_since : p->idle_since;
    unsigned fps = baji_pet_fps(p->shown.clip);
    int64_t next_frame = (now - origin) * fps / 1000000 + 1;
    return origin + (next_frame * 1000000 + fps - 1) / fps;
}

esp_err_t pet_player_tick(pet_player_t *p, baji_board_t *board,
                          const baji_input_t *input, int64_t now, bool *submitted)
{
    *submitted = false;
    if (input->presses != p->last_press) {
        p->last_press = input->presses;
        if (baji_pet_forehead(p->art, p->shown, input->press_x, input->press_y)) {
            p->patting = true;
            p->pat_since = now;
        }
    }
    baji_pet_state_t next = {.clip = BAJI_PET_IDLE};
    if (p->patting) {
        unsigned frame = (uint64_t)(now - p->pat_since) * baji_pet_fps(BAJI_PET_PAT) / 1000000;
        if (frame < baji_pet_frame_count(BAJI_PET_PAT)) {
            next.clip = BAJI_PET_PAT;
            next.frame = frame;
        } else {
            p->patting = false;
            p->idle_since = now;
        }
    }
    if (!p->patting)
        next.frame = ((uint64_t)(now - p->idle_since) * baji_pet_fps(BAJI_PET_IDLE) / 1000000)
                     % baji_pet_frame_count(BAJI_PET_IDLE);
    next.gaze_x = p->shown.gaze_x;
    next.gaze_y = p->shown.gaze_y;
    update_gaze(&next, input);

    /* The display retains its last frame. Keep even failed/partial submissions
     * dirty until a later complete redraw from the latest scene succeeds. */
    if (p->have_frame && (p->shown.clip != next.clip || p->shown.frame != next.frame ||
                          p->shown.gaze_x != next.gaze_x || p->shown.gaze_y != next.gaze_y)) {
        /* Preserve separate authored windows and both eyes until the
         * transfer planner has seen them all and can price the merges. */
        baji_rect_t changed[2 * BAJI_DAMAGE_CAPACITY];
        size_t n = baji_pet_damage(p->art, &p->shown, &next, changed,
                                   sizeof(changed) / sizeof(changed[0]));
        for (size_t i = 0; i < n; ++i) baji_damage_add(&p->damage, changed[i]);
    }
    if (p->damage.count) {
        baji_pet_select(p->art, next);
        esp_err_t err = baji_board_paint_round_regions(board, p->damage.regions,
                                                        p->damage.count, baji_pet_paint, p->art);
        if (err != ESP_OK) return err;
        baji_damage_clear(&p->damage);
        *submitted = true;
    }
    p->shown = next;
    p->have_frame = true;
    return ESP_OK;
}
