#include "pet_player.h"
#include <stdlib.h>

/* The web gesture holds zero-based frame three at the first compressed pose. */
enum { CHEEK_HOLD_US = 3*1000000/16 };

void pet_player_init(pet_player_t *player, baji_pet_t *art,
                     const baji_board_info_t *info, int64_t now_us)
{
    *player = (pet_player_t){.art = art, .idle_since = now_us,
                             .paint = baji_pet_paint, .paint_context = art};
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

static int toward(int gaze, int target)
{
    return gaze - (gaze > target) + (gaze < target);
}

static void update_gaze(const pet_player_t *p, baji_pet_state_t *next, const baji_input_t *input)
{
    if (next->clip>=BAJI_PET_CHEEK_LEFT) {
        next->gaze_x=0;
        next->gaze_y=0;
        return;
    }
    if (input->touch.pressed) {
        next->gaze_x = gaze_axis(input->touch.x, 233, 75, 2, next->gaze_x);
        next->gaze_y = gaze_axis(input->touch.y, 233, 102, 1, next->gaze_y);
    } else {
        next->gaze_x = toward(next->gaze_x, p->rest_gaze_x);
        next->gaze_y = toward(next->gaze_y, p->rest_gaze_y);
    }
}

int64_t pet_player_next_update(const pet_player_t *p, const baji_input_t *input, int64_t now)
{
    baji_pet_state_t gaze = p->shown;
    update_gaze(p, &gaze, input);
    if (gaze.gaze_x != p->shown.gaze_x || gaze.gaze_y != p->shown.gaze_y)
        return now;
    if (p->cheek_held && now-p->action_since>=CHEEK_HOLD_US) return INT64_MAX;
    int64_t origin = p->action == BAJI_PET_IDLE ? p->idle_since : p->action_since;
    unsigned fps = baji_pet_fps(p->shown.clip) * baji_pet_substeps(p->art,p->shown.clip);
    int64_t next_frame = (now - origin) * fps / 1000000 + 1;
    return origin + (next_frame * 1000000 + fps - 1) / fps;
}

static bool third_tap(pet_player_t *p,int64_t release)
{
    if (p->tap_count==2 && release-p->tap_times[0]<1100000) {
        p->tap_count=0;
        return true;
    }
    if (p->tap_count==2) {
        p->tap_times[0]=p->tap_times[1];
        p->tap_count=1;
    }
    p->tap_times[p->tap_count++]=release;
    return false;
}

static void release_cheek(pet_player_t *p,int64_t when)
{
    if (when-p->action_since>CHEEK_HOLD_US) p->action_since=when-CHEEK_HOLD_US;
    p->cheek_held=false;
}

static void handle_contact(pet_player_t *p,const baji_input_t *input,int64_t now)
{
    int64_t pressed=input->press_us>0 && input->press_us<=now ? input->press_us : now;
    if (input->presses!=p->last_press) {
        if (p->cheek_held) release_cheek(p,pressed);
        p->last_press=input->presses;
        int side=p->cheek_at ? p->cheek_at(p->art,p->shown,input->press_x,input->press_y) : 0;
        if (side) {
            p->action=side<0 ? BAJI_PET_CHEEK_LEFT : BAJI_PET_CHEEK_RIGHT;
            p->action_since=pressed;
            p->cheek_held=true;
            p->cheek_moved=false;
            p->turns=0;
            p->direction=0;
            p->extreme_x=input->press_x;
        } else if (baji_pet_forehead(p->art,p->shown,input->press_x,input->press_y)) {
            p->action=BAJI_PET_PAT;
            p->action_since=now;
        }
    }
    if (!p->cheek_held) return;
    int dx=(int)input->touch.x-input->press_x, dy=(int)input->touch.y-input->press_y;
    /* Preserve an exit that happened between display callbacks, even if the
     * latest point has returned to the cheek. Horizontal rubbing has more room. */
    if (abs(dx)>40 || input->travel>40 || abs(dy)>26 || input->travel_y>26) {
        release_cheek(p,now);
        return;
    }
    if (dx*dx+dy*dy>54 || input->travel>7) p->cheek_moved=true;
    if (input->touch.pressed) {
        int distance=(int)input->touch.x-p->extreme_x;
        int sign=(distance>0)-(distance<0);
        if (!sign || (sign!=p->direction && abs(distance)<16)) return;
        if (p->direction && sign!=p->direction) p->turns++;
        p->direction=sign;
        p->extreme_x=input->touch.x;
        if (p->turns<2) return;
    } else {
        int64_t release=input->touch_us>=p->action_since && input->touch_us>0 && input->touch_us<=now ?
                        input->touch_us : now;
        bool tap=!p->cheek_moved && release-p->action_since<420000;
        release_cheek(p,release);
        if (!tap || !third_tap(p,release)) return;
    }
    p->cheek_held=false;
    p->tap_count=0;
    p->action=BAJI_PET_TICKLE;
    p->action_since=now;
}

esp_err_t pet_player_tick(pet_player_t *p, baji_board_t *board,
                          const baji_input_t *input, int64_t now, bool *submitted)
{
    *submitted = false;
    handle_contact(p,input,now);
    baji_pet_state_t next = {.clip=p->action};
    unsigned steps=baji_pet_substeps(p->art,next.clip);
    unsigned fps=baji_pet_fps(next.clip)*steps;
    int64_t age=now-(next.clip==BAJI_PET_IDLE ? p->idle_since : p->action_since);
    if (p->cheek_held && age>CHEEK_HOLD_US) age=CHEEK_HOLD_US;
    uint64_t sample=(uint64_t)age*fps/1000000;
    unsigned frames=baji_pet_frame_count(next.clip);
    if (next.clip!=BAJI_PET_IDLE && sample/steps>=frames) {
        p->action=BAJI_PET_IDLE;
        next.clip=BAJI_PET_IDLE;
        p->idle_since=now;
    } else {
        next.frame=sample/steps%frames;
        next.phase=sample%steps;
    }
    next.gaze_x = p->shown.gaze_x;
    next.gaze_y = p->shown.gaze_y;
    update_gaze(p, &next, input);

    /* The display retains its last frame. Keep even failed/partial submissions
     * dirty until a later complete redraw from the latest scene succeeds. */
    if (p->have_frame && (p->shown.clip != next.clip || p->shown.frame != next.frame || p->shown.phase != next.phase ||
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
                                                        p->damage.count, p->paint, p->paint_context);
        if (err != ESP_OK) return err;
        baji_damage_clear(&p->damage);
        *submitted = true;
    }
    p->shown = next;
    p->have_frame = true;
    return ESP_OK;
}
