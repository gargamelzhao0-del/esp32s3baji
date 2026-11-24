#include "deepseek_controls.h"
#include "motion_curve.h"
#include <stdlib.h>

enum { HOLD_US = 650000, HOLD_SLOP = 16, SWIPE = 38, BREATH_HALF_US = 1400000 };

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
    dsk_advance(&ui->pixel, now);
    if (g->presses != input->presses) {
        *g = (dsk_gesture_t){.presses = input->presses, .start_x = input->press_x, .start_y = input->press_y,
                             .start_item = ui->visible_item, .active = true};
        ui->press_time = input->press_us ? input->press_us : now;
        ui->hold_cancelled = false;
        ui->start_pan_x = ui->hd.pan_x;
        ui->start_pan_y = ui->hd.pan_y;
    }
    if (g->active) {
        int dx = (int)input->touch.x - g->start_x, dy = (int)input->touch.y - g->start_y;
        if (input->travel >= HOLD_SLOP || abs(dx) >= HOLD_SLOP || abs(dy) >= HOLD_SLOP)
            ui->hold_cancelled = true;
        int64_t held_until = !down && input->touch_us ? input->touch_us : now;
        if (ui->hd_available && !ui->hold_cancelled && held_until - ui->press_time >= HOLD_US) {
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
    bool show_agent = agent_item && now >= ui->local_until;
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
    if (g->active && ui->hd_available && !ui->hold_cancelled && ui->press_time + HOLD_US < next)
        next = ui->press_time + HOLD_US;
    if (agent_item && !show_agent && ui->local_until < next) next = ui->local_until;
    return next;
}
