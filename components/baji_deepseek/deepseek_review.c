#include "deepseek_review.h"
#include "motion_curve.h"
#include <stdlib.h>

static const uint8_t counts[DSK_CLIPS] = {8, 12, 10, 8, 8, 8};
static const uint8_t rates[DSK_CLIPS] = {6, 7, 7, 8, 7, 4};
enum { HALF_FLOAT_US = 1200000 };

unsigned dsk_clip_frames(unsigned clip) { return clip < DSK_CLIPS ? counts[clip] : 0; }
unsigned dsk_clip_fps(unsigned clip) { return clip < DSK_CLIPS ? rates[clip] : 0; }

/* CSS ease-in-out cubic-bezier(.42,0,.58,1). Invert y to obtain the time
 * at which Chrome reaches a half-step between two captured bubble poses. */
static uint32_t float_edge(double target, unsigned half_us)
{
    double lo = 0, hi = 1;
    for (unsigned i = 0; i < 32; i++) {
        double u = (lo + hi) * 0.5;
        if (u * u * (3 - 2 * u) < target) lo = u;
        else hi = u;
    }
    double u = hi, v = 1 - u;
    double t = 3 * v * v * u * .42 + 3 * v * u * u * .58 + u * u * u;
    return (uint32_t)(t * half_us + .5);
}

void dsk_curve_edges(uint32_t *edges, unsigned steps, unsigned half_us)
{
    edges[0] = 0;
    for (unsigned i = 1; i < steps; i++)
        edges[i] = float_edge(((double)i - .5) / (steps - 1), half_us);
}

unsigned dsk_curve_at(const uint32_t *edges, unsigned steps, unsigned half_us,
                      int64_t elapsed, uint32_t *next_delta)
{
    unsigned pos = elapsed < 0 ? 0 : (unsigned)(elapsed % (2 * half_us));
    unsigned distance = pos > half_us ? 2 * half_us - pos : pos;
    unsigned sample = 0;
    while (sample + 1 < steps && distance >= edges[sample + 1]) sample++;
    if (next_delta && steps > 1) {
        unsigned edge;
        if (pos < half_us && sample + 1 < steps) edge = edges[sample + 1];
        else if (sample) edge = 2 * half_us - edges[sample] + 1;
        else edge = 2 * half_us + edges[1];
        *next_delta = edge - pos;
    }
    return sample;
}

void dsk_model_init(dsk_model_t *m, unsigned steps, int64_t now)
{
    *m = (dsk_model_t){.shown = {.item = 1, .phase = 1}, .last_clip = 1,
                       .playing = true, .motion_steps = 1, .float_origin = now,
                       .last_tick = now, .clock_us = now};
    if (steps < 1 || steps > DSK_MAX_BUBBLE_STEPS) steps = 1;
    m->bubble_steps = steps;
    dsk_curve_edges(m->float_edges, steps, HALF_FLOAT_US);
}

void dsk_model_smooth_bubble(dsk_model_t *m)
{
    if (m->smooth_bubble || m->bubble_steps < 2) return;
    unsigned samples = (m->bubble_steps - 1) * 8 + 1;
    if (samples > DSK_MAX_BUBBLE_STEPS) return;
    m->smooth_bubble = true;
    m->bubble_steps = samples;
    dsk_curve_edges(m->float_edges, samples, HALF_FLOAT_US);
}

bool dsk_command(dsk_model_t *m, dsk_command_t command, unsigned value)
{
    dsk_frame_t *s = &m->shown;
    switch (command) {
    case DSK_SELECT_CLIP:
        if (value >= DSK_CLIPS) return false;
        s->item = m->last_clip = value;
        s->frame = 0;
        m->subframe = m->start_sample = 0;
        m->last_tick = m->clock_us;
        m->playing = true;
        break;
    case DSK_SELECT_STATE:
        if (value >= DSK_ITEMS - DSK_CLIPS) return false;
        m->last_pose = value;
        s->item = DSK_CLIPS + value;
        s->frame = 0;
        m->subframe = 0;
        break;
    case DSK_SET_PHASE:
        if (value >= DSK_PHASES) return false;
        s->phase = value;
        break;
    case DSK_SET_VIEW:
        if (value > 1) return false;
        s->close = value;
        break;
    case DSK_TOGGLE_PLAY:
        m->start_sample = s->frame * m->motion_steps + m->subframe;
        m->last_tick = m->clock_us;
        m->playing = !m->playing;
        break;
    case DSK_STEP_BACK:
    case DSK_STEP_FORWARD: {
        bool clip = s->item < DSK_CLIPS;
        unsigned n = clip ? counts[s->item] : DSK_ITEMS - DSK_CLIPS;
        unsigned prev = clip ? s->frame : m->last_pose;
        unsigned next = (prev + (command == DSK_STEP_BACK ? n - 1 : 1)) % n;
        if (clip) s->frame = next;
        else {
            m->last_pose = next;
            s->item = DSK_CLIPS + next;
        }
        m->playing = false;
        m->subframe = 0;
        m->start_sample = s->frame * m->motion_steps;
        m->last_tick = m->clock_us;
        break;
    }
    default:
        return false;
    }
    return true;
}

void dsk_advance(dsk_model_t *m, int64_t now)
{
    m->clock_us = now;
    if (m->shown.item < DSK_CLIPS && m->playing && now >= m->last_tick) {
        unsigned rate = rates[m->shown.item] * m->motion_steps;
        uint64_t tick = (uint64_t)(now - m->last_tick) * rate / 1000000;
        unsigned sample = (unsigned)((m->start_sample + tick) % (counts[m->shown.item] * m->motion_steps));
        m->shown.frame = sample / m->motion_steps;
        m->subframe = sample % m->motion_steps;
    }
    unsigned offset = dsk_curve_at(m->float_edges, m->bubble_steps, HALF_FLOAT_US,
                                   now - m->float_origin, NULL);
    m->shown.bubble = m->smooth_bubble ? offset / 8 : offset;
    m->bubble_mix = m->smooth_bubble ? offset % 8 : 0;
}

int64_t dsk_next_update(const dsk_model_t *m, int64_t now)
{
    int64_t next = INT64_MAX;
    if (m->shown.item < DSK_CLIPS && m->playing) {
        unsigned rate = rates[m->shown.item] * m->motion_steps;
        int64_t elapsed = now >= m->last_tick ? now - m->last_tick : 0;
        int64_t tick = elapsed * rate / 1000000 + 1;
        next = m->last_tick + (tick * 1000000 + rate - 1) / rate;
    }
    if (m->bubble_steps > 1) {
        uint32_t delta;
        dsk_curve_at(m->float_edges, m->bubble_steps, HALF_FLOAT_US, now - m->float_origin, &delta);
        int64_t bubble_due = now + delta;
        if (bubble_due < next) next = bubble_due;
    }
    return next;
}

static void tap(dsk_model_t *m, unsigned x, unsigned y)
{
    /* Buttons sit on the matching on-screen objects; the larger hit areas
     * leave room for a fingertip without changing the source artwork. */
    if (y <= 76 && x >= 119 && x <= 347)
        dsk_command(m, DSK_SET_PHASE, (m->shown.phase + 1) % DSK_PHASES);
    else if (y >= 369 && x >= 105 && x <= 361)
        dsk_command(m, DSK_TOGGLE_PLAY, 0);
    else if (x < 109 && y >= 172 && y <= 271) {
        unsigned pose = m->last_pose + (m->shown.item >= DSK_CLIPS);
        dsk_command(m, DSK_SELECT_STATE, pose % (DSK_ITEMS - DSK_CLIPS));
    } else {
        unsigned clip = m->last_clip + (m->shown.item < DSK_CLIPS);
        dsk_command(m, DSK_SELECT_CLIP, clip % DSK_CLIPS);
    }
}

void dsk_touch(dsk_gesture_t *g, dsk_model_t *m, uint32_t presses,
               uint16_t px, uint16_t py, bool down, uint16_t x, uint16_t y)
{
    if (presses != g->presses) {
        g->presses = presses;
        g->start_x = px;
        g->start_y = py;
        g->active = true;
    }
    if (!g->active || down) return;
    g->active = false;
    int dx = (int)x - g->start_x, dy = (int)y - g->start_y;
    if (abs(dx) >= 38 && abs(dx) >= abs(dy))
        dsk_command(m, dx < 0 ? DSK_STEP_FORWARD : DSK_STEP_BACK, 0);
    else if (abs(dy) >= 38)
        dsk_command(m, DSK_SET_VIEW, dy < 0);
    else
        tap(m, g->start_x, g->start_y);
}
