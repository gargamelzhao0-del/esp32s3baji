#include "deepseek_controls.h"
#include "motion_curve.h"
#include <stdlib.h>

enum { HOLD_US = 650000, HOLD_SLOP = 16, SWIPE = 38, BREATH_HALF_US = 1400000 };
enum { NAP_CLIP = 5, PILLOW_STEP_US = 26000 };
static const int8_t rebound[] = {8,6,3,1,-2,-1};

static dsk_cloth_t exposed_pillow(const dsk_controls_t *ui)
{
    if (ui->visible_item != NAP_CLIP || (ui->hd_mode && ui->hd.gallery)) return (dsk_cloth_t){0};
    int bottom = ui->agent_footer ? 374 : 390;
    if (ui->hd_mode) {
        int breath = ui->hd.breath * 8 + ui->breath_mix;
        return (dsk_cloth_t){174, 299 - (3*breath + 64)/128, 29, 26, bottom};
    }
    /* Centres measured at the eight nap keys of the two review crops.
     * Interpolate only the centre; the fingers and cheek remain outside. */
    static const int8_t offsets[2][8] = {{0,-2,-3,-2,0,2,3,2}, {0,-3,-5,-3,0,3,5,3}};
    bool close = ui->pixel.shown.close;
    int key = ui->pixel.shown.frame % 8;
    const int8_t *dy = offsets[close];
    int steps = ui->pixel.motion_steps ? ui->pixel.motion_steps : 1;
    int offset = dy[key] + (dy[(key+1)%8]-dy[key]) * ui->pixel.subframe / steps;
    return close ? (dsk_cloth_t){156, 369+offset, 29, 13, bottom} : (dsk_cloth_t){178, 298+offset, 32, 26, bottom};
}

static int press_depth(int64_t elapsed)
{
    static const uint8_t depth[] = {2,4,6,7,8};
    if (elapsed <= 0) return depth[0];
    if (elapsed >= 4*PILLOW_STEP_US) return 8;
    return depth[elapsed/PILLOW_STEP_US];
}

static int64_t advance_pillow(dsk_controls_t *ui, int64_t now)
{
    int depth = 0;
    int64_t next = INT64_MAX;
    if (ui->pillow_held) {
        depth = press_depth(now-ui->press_time);
        if (depth < 8) next = ui->press_time + ((now-ui->press_time)/PILLOW_STEP_US+1)*PILLOW_STEP_US;
    } else if (ui->release_depth) {
        int64_t elapsed = now-ui->pillow_release;
        int step = elapsed / PILLOW_STEP_US;
        depth = rebound[step] * ui->release_depth / 8;
        next = ui->pillow_release + (step+1)*PILLOW_STEP_US;
    }
    ui->pillow = (dsk_pillow_t){0};
    if (depth) {
        dsk_cloth_t c = exposed_pillow(ui);
        ui->pillow = (dsk_pillow_t){c, c.x+ui->pillow_dx, c.y+ui->pillow_dy, depth};
    }
    return next;
}

void dsk_controls_init(dsk_controls_t *ui, unsigned steps, bool hd_available, int64_t now)
{
    *ui = (dsk_controls_t){.hd_available = hd_available, .breathing = true, .breath_origin = now};
    ui->hd.guides = true;
    dsk_curve_edges(ui->breath_edges, 129, BREATH_HALF_US);
    dsk_model_init(&ui->pixel, steps, now);
    ui->visible_item = ui->pixel.shown.item;
    dsk_model_smooth_bubble(&ui->pixel);
}

static int clamp_pan(int position)
{
    if (position < -100) return -100;
    if (position > 100) return 100;
    return position;
}

static void next_sheet(dsh_view_t *hd, bool forward)
{
    hd->sheet = (hd->sheet + (forward ? 1 : 3)) % 4;
    hd->zoom = 0;
    hd->pan_x = hd->pan_y = 0;
}

static void gallery_release(dsk_controls_t *ui, int dx, int dy)
{
    dsh_view_t *v = &ui->hd;
    if (v->zoom && ui->hold_cancelled) return;
    if (abs(dy) >= SWIPE && abs(dy) > abs(dx)) {
        if (dy > 0) v->gallery = false;
    } else if (abs(dx) >= SWIPE) {
        next_sheet(v, dx < 0);
    } else if (ui->touch.start_x >= 147 && ui->touch.start_x <= 319) {
        v->zoom = !v->zoom;
        v->pan_x = v->pan_y = 0;
    } else {
        next_sheet(v, ui->touch.start_x > 233);
    }
}

static bool hd_release(dsk_controls_t *ui, int dx, int dy, int64_t now)
{
    if (ui->hd.gallery) { gallery_release(ui, dx, dy); return false; }
    unsigned x = ui->touch.start_x, y = ui->touch.start_y;
    dsk_model_t *m = &ui->pixel;
    if (abs(dy) >= SWIPE && abs(dy) > abs(dx)) {
        if (dy < 0) ui->hd.gallery = true;
    } else if (abs(dx) >= SWIPE) {
        /* The HD panel follows the currently selected clip/pose on the page.
         * Each horizontal step selects its adjacent review button. */
        unsigned item = (ui->touch.start_item + (dx < 0 ? 1 : DSK_ITEMS - 1)) % DSK_ITEMS;
        dsk_command(m, item < DSK_CLIPS ? DSK_SELECT_CLIP : DSK_SELECT_STATE,
                    item < DSK_CLIPS ? item : item - DSK_CLIPS);
        return true;
    } else if (y < 104) {
        ui->hd.guides = !ui->hd.guides;
    } else if (y > 362) {
        if (ui->breathing) ui->freeze_time = now;
        else ui->breath_origin += now - ui->freeze_time;
        ui->breathing = !ui->breathing;
    } else if (x < 122) {
        unsigned item = ui->touch.start_item;
        unsigned pose = item >= DSK_CLIPS ? item - DSK_CLIPS + 1 : m->last_pose;
        dsk_command(m, DSK_SELECT_STATE, pose % (DSK_ITEMS - DSK_CLIPS));
        return true;
    } else {
        unsigned item = ui->touch.start_item;
        dsk_command(m, DSK_SELECT_CLIP, item < DSK_CLIPS ? (item + 1) % DSK_CLIPS : m->last_clip);
        return true;
    }
    return false;
}

int64_t dsk_controls_tick(dsk_controls_t *ui, const baji_input_t *input, unsigned agent_item, int64_t now)
{
    dsk_gesture_t *g = &ui->touch;
    bool down = input->touch.pressed;
    if (g->presses != input->presses) {
        dsk_cloth_t cloth = exposed_pillow(ui);
        ui->pillow_held = dsk_pillow_contains(cloth, input->press_x, input->press_y);
        ui->release_depth = 0;
        if (ui->pillow_held) {
            ui->pillow_dx = (int)input->press_x - cloth.x;
            ui->pillow_dy = (int)input->press_y - cloth.y;
        }
        *g = (dsk_gesture_t){.presses = input->presses, .start_x = input->press_x, .start_y = input->press_y,
                             .start_item = ui->visible_item, .active = true};
        ui->press_time = input->press_us ? input->press_us : now;
        ui->hold_cancelled = false;
        ui->start_pan_x = ui->hd.pan_x;
        ui->start_pan_y = ui->hd.pan_y;
    }
    dsk_advance(&ui->pixel, now);
    if (g->active) {
        int dx = (int)input->touch.x - g->start_x, dy = (int)input->touch.y - g->start_y;
        if (input->travel >= HOLD_SLOP || abs(dx) >= HOLD_SLOP || abs(dy) >= HOLD_SLOP)
            ui->hold_cancelled = true;
        int64_t held_until = !down && input->touch_us ? input->touch_us : now;
        if (ui->pillow_held) {
            if (!down || ui->hold_cancelled) {
                ui->pillow_held = false;
                ui->release_depth = press_depth(held_until-ui->press_time);
                if (ui->release_depth < 4) ui->release_depth = 4;
                ui->pillow_release = now;
                g->active = false; /* A drag that started on the pillow cannot page on release. */
            }
        } else if (ui->hd_available && !ui->hold_cancelled && held_until - ui->press_time >= HOLD_US) {
            ui->hd_mode = !ui->hd_mode;
            g->active = false; /* Swallow the release of the mode-switching press. */
        } else {
            if (ui->hd_mode && ui->hd.gallery && ui->hd.zoom && ui->hold_cancelled) {
                ui->hd.pan_x = clamp_pan(ui->start_pan_x - dx * 100 / 200);
                ui->hd.pan_y = clamp_pan(ui->start_pan_y - dy * 100 / 200);
            }
            if (!down) {
                bool tap_or_swipe = !ui->hold_cancelled || abs(dx) >= SWIPE || abs(dy) >= SWIPE;
                if (tap_or_swipe && (ui->hd_mode ? hd_release(ui, dx, dy, now)
                                                  : dsk_pixel_release(&ui->pixel, g, dx, dy))) {
                    unsigned item = ui->pixel.shown.item;
                    ui->local_until = now + (item < DSK_CLIPS
                        ? 1000000LL * dsk_clip_frames(item) / dsk_clip_fps(item) : 1600000);
                }
                g->active = false;
            }
        }
    }
    if (ui->release_depth && now-ui->pillow_release >= (int)sizeof(rebound)*PILLOW_STEP_US) ui->release_depth = 0;
    bool pillow_active = ui->pillow_held || ui->release_depth;
    bool show_agent = agent_item && now >= ui->local_until && !pillow_active;
    ui->visible_item = show_agent ? agent_item : ui->pixel.shown.item;
    int64_t next = INT64_MAX;
    if (!ui->hd_mode) next = show_agent ? dsk_next_bubble_update(&ui->pixel, now) : dsk_next_update(&ui->pixel, now);
    else if (!ui->hd.gallery) {
        uint32_t delta = 0;
        int64_t time = ui->breathing ? now : ui->freeze_time;
        unsigned phase = dsk_curve_at(ui->breath_edges, 129, BREATH_HALF_US,
                                      time - ui->breath_origin, &delta);
        ui->hd.breath = phase / 8;
        ui->breath_mix = phase % 8;
        if (ui->breathing) next = now + delta;
    }
    int64_t pillow_next = advance_pillow(ui, now);
    if (pillow_next < next) next = pillow_next;
    if (g->active && !ui->pillow_held && ui->hd_available && !ui->hold_cancelled && ui->press_time + HOLD_US < next)
        next = ui->press_time + HOLD_US;
    if (agent_item && !show_agent && ui->local_until > now && ui->local_until < next) next = ui->local_until;
    return next;
}
