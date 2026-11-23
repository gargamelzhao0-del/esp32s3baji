#include "breath_trace.h"
#include "baji_runtime.h"
#include "baji_scene_damage.h"
#include "baji_vsync.h"
#include "body_probe.h"
#include "esp_timer.h"
#include "motion_curve.h"
#include <stdio.h>

enum { HALF_US = 1400000, CYCLES = 12, STEPS = 129 };
typedef struct {
    unsigned draws, changes, missed, crossed, max_us, max_select, total_select, total_dma;
    baji_vsync_edge_t start;
} trace_stats_t;
typedef struct {
    dsh_canvas_t *canvas;
    baji_board_t *board;
    uint32_t edges[STEPS];
    unsigned shown, seed_us;
    int64_t origin;
    baji_vsync_edge_t previous, end;
    trace_stats_t cold, hot;
    esp_err_t error;
} trace_t;

static esp_err_t tick(void *context, const baji_input_t *input, int64_t now, int64_t *due)
{
    (void)input;
    trace_t *t = context;
    baji_vsync_edge_t edge = baji_vsync_snapshot();
    if (!t->origin) t->origin = now;
    int64_t elapsed = now - t->origin;
    if (elapsed >= (CYCLES + 1) * 2LL * HALF_US) {
        *due = BAJI_FRAME_DONE;
        t->end = edge;
        return ESP_OK;
    }
    trace_stats_t *s = elapsed < 2LL * HALF_US ? &t->cold : &t->hot;
    if (!s->start.observed_us) s->start = edge;
    uint32_t delta;
    unsigned phase = dsk_curve_at(t->edges, STEPS, HALF_US, elapsed, &delta);
    *due = now + delta;
    if (edge.seq && t->previous.seq) {
        /* Omitted TE edges count as missed opportunities only when that scan
         * was due to show a different phase. Breathing holds at both extrema. */
        unsigned steps = edge.seq - t->previous.seq;
        for (unsigned i = 1; i < steps; i++) {
            int64_t expected = t->previous.edge_us + (edge.edge_us-t->previous.edge_us)*i/steps;
            if (dsk_curve_at(t->edges, STEPS, HALF_US, expected-t->origin, NULL) != t->shown)
                s->missed++;
        }
    }
    t->previous = edge;
    if (phase == t->shown) return ESP_OK;
    dsh_view_t v = {.item=1, .guides=true, .breath=phase/8};
    observed_paint_t observer = {.original=dsh_paint, .scene=t->canvas};
    int64_t begin = esp_timer_get_time();
    if (!dsh_canvas_select_blend(t->canvas, v, phase % 8)) t->error = ESP_ERR_INVALID_STATE;
    baji_damage_t damage;
    baji_damage_init(&damage, DSH_PANEL, DSH_PANEL);
    if (t->shown == STEPS) baji_damage_mark(&damage, 0, 0, DSH_PANEL, DSH_PANEL);
    else if (t->error == ESP_OK) {
        baji_rect_t areas[DSH_DAMAGE_STRIPS];
        unsigned n = dsh_canvas_motion_damage(t->canvas, t->shown, areas);
        for (unsigned i = 0; i < n; ++i) baji_damage_add(&damage, areas[i]);
    }
    int64_t selected = esp_timer_get_time();
    if (t->error == ESP_OK && damage.count)
        t->error = baji_board_paint_round_regions(t->board, damage.regions, damage.count, observe_band, &observer);
    if (t->error != ESP_OK) { *due = BAJI_FRAME_DONE; return t->error; }
    baji_vsync_edge_t complete = baji_vsync_snapshot();
    unsigned select_us = (unsigned)(selected-begin);
    unsigned total_us = (unsigned)(complete.observed_us-begin);
    if (select_us > s->max_select) s->max_select = select_us;
    if (total_us > s->max_us) s->max_us = total_us;
    s->total_select += select_us;
    s->total_dma += total_us - select_us;
    s->crossed += complete.seq != edge.seq;
    if (t->shown == STEPS) t->seed_us = total_us;
    else s->changes += observer.changed;
    s->draws += damage.count != 0;
    t->shown = phase;
    return ESP_OK;
}

esp_err_t breath_trace(baji_board_t *board, const baji_board_info_t *info,
                       dsh_canvas_t *canvas, const dsh_motion_t pages[16])
{
    trace_t t = {.board=board, .canvas=canvas, .shown=STEPS};
    dsk_curve_edges(t.edges, STEPS, HALF_US);
    if (!dsh_canvas_seed_motion(canvas, 1, pages)) return ESP_ERR_NO_MEM;
    const baji_runtime_config_t config = {.brightness=45};
    esp_err_t err = baji_runtime_run(board, info, &config, tick, &t);
    if (err == ESP_OK) err = t.error;
    trace_stats_t h = t.hot, c = t.cold;
    printf("DSK_BREATH cycles=%u steps=%u body_changes=%u span_us=%u scans=%u "
           "missed_due=%u crossed=%u max_us=%u seed_us=%u cold_span_us=%u cold_draws=%u "
           "cold_changes=%u cold_max_us=%u select_avg_us=%u select_max_us=%u dma_avg_us=%u err=%d\n",
           CYCLES, h.draws, h.changes,
           h.start.observed_us ? (unsigned)(t.end.observed_us-h.start.observed_us) : 0,
           h.start.seq ? (unsigned)(t.end.seq-h.start.seq) : 0, h.missed, h.crossed,
           h.max_us, t.seed_us, h.start.observed_us ? (unsigned)(h.start.observed_us-t.origin) : 0,
           c.draws, c.changes, c.max_us, h.draws ? h.total_select/h.draws : 0,
           h.max_select, h.draws ? h.total_dma/h.draws : 0, err);
    dsh_canvas_set_motion(canvas, NULL);
    return err;
}
