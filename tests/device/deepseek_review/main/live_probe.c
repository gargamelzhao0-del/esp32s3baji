#include "live_probe.h"
#include "baji_board.h"
#include "baji_vsync.h"
#include "body_probe.h"
#include "deepseek_review.h"
#include "esp_timer.h"
#include <stdio.h>

typedef struct {
    int64_t first, previous_change, first_edge;
    uint32_t first_seq;
    unsigned frames, partial, peak, body_changes, bubble_only, max_gap, crossed;
    unsigned max_prepare, total_prepare, total_paint;
    unsigned gaps[251];
    unsigned pose_steps, skipped_poses, clip_changes;
    dsk_frame_t last_pose;
    bool have_pose;
} sample_t;

static sample_t sample;
static bool active, sweep, switch_pending, hd;
static unsigned segment;
static int64_t tick_start;

void live_probe_start(bool all_clips)
{
    sample = (sample_t){0};
    active = true;
    hd = false;
    sweep = all_clips;
    segment = 0;
    switch_pending = all_clips;
}

void live_probe_start_hd(void)
{
    live_probe_start(false);
    hd = true;
}

void __real_dsk_advance(dsk_model_t *, int64_t);
void __wrap_dsk_advance(dsk_model_t *model, int64_t now)
{
    if (active) tick_start = esp_timer_get_time();
    if (active && switch_pending) {
        model->clock_us = now;
        dsk_command(model, DSK_SELECT_CLIP, (segment + 1) % DSK_CLIPS);
        switch_pending = false;
    }
    __real_dsk_advance(model, now);
    if (!active) return;
    dsk_frame_t frame = model->shown;
    if (sample.have_pose && frame.item != sample.last_pose.item) ++sample.clip_changes;
    if (sample.have_pose && frame.item == sample.last_pose.item &&
        frame.item < DSK_CLIPS && frame.frame != sample.last_pose.frame) {
        ++sample.pose_steps;
        if (frame.frame != (sample.last_pose.frame + 1u) % dsk_clip_frames(frame.item))
            ++sample.skipped_poses;
    }
    sample.last_pose = frame;
    sample.have_pose = true;
}

static void emit_sample(int64_t end, baji_vsync_edge_t after)
{
    unsigned quantile = (sample.body_changes * 95 + 99) / 100, found = 0, p95 = 0;
    for (; p95 < 250; ++p95) {
        found += sample.gaps[p95];
        if (found >= quantile) break;
    }
    bool last = hd ? segment == 1 : !sweep || segment + 1 == DSK_CLIPS;
    const char *label = hd ? (last ? "DSK_HD_LIVE_OK" : "DSK_HD_COLD") :
                             (last ? "DSK_LIVE_OK" : "DSK_CLIP");
    printf("%s clip=%u frames=%u partial=%u max_partial_us=%u span_us=%u "
           "pose_steps=%u skipped_poses=%u clip_changes=%u body_changes=%u bubble_only=%u "
           "body_gap_p95_ms=%u body_gap_max_us=%u scan_edges=%u scan_span_us=%u crossed=%u "
           "prepare_mean_us=%u prepare_max_us=%u draw_mean_us=%u\n",
           label, sample.last_pose.item, sample.frames,
           sample.partial, sample.peak, (unsigned)(end-sample.first), sample.pose_steps,
           sample.skipped_poses, sample.clip_changes, sample.body_changes, sample.bubble_only,
           p95, sample.max_gap, (unsigned)(after.seq-sample.first_seq),
           (unsigned)(after.edge_us-sample.first_edge), sample.crossed, sample.total_prepare/sample.frames,
           sample.max_prepare, sample.total_paint/sample.frames);
    if (last) active = false;
    else {
        ++segment;
        sample = (sample_t){0};
        switch_pending = sweep;
    }
}

esp_err_t __real_baji_board_paint_round_regions(baji_board_t *, const baji_rect_t *, size_t,
                                                baji_paint_fn, void *);
esp_err_t __wrap_baji_board_paint_round_regions(baji_board_t *board, const baji_rect_t *areas,
                                                size_t count, baji_paint_fn paint, void *scene)
{
    if (!active) return __real_baji_board_paint_round_regions(board, areas, count, paint, scene);
    baji_vsync_edge_t before = baji_vsync_snapshot();
    observed_paint_t observer = {.original=paint, .scene=scene, .watch_cloud=!hd};
    int64_t start = esp_timer_get_time();
    esp_err_t err = __real_baji_board_paint_round_regions(board, areas, count, observe_band, &observer);
    if (err != ESP_OK) { printf("DSK_FAIL display_err=%d\n", err); return err; }
    int64_t end = esp_timer_get_time();
    baji_vsync_edge_t after = baji_vsync_snapshot();
    unsigned prepare = (unsigned)(start-tick_start);
    if (prepare > sample.max_prepare) sample.max_prepare = prepare;
    sample.total_prepare += prepare;
    sample.total_paint += (unsigned)(end-start);
    if (!sample.first_edge && after.edge_us) {
        sample.first_edge = after.edge_us;
        sample.first_seq = after.seq;
    }
    if (sample.frames++ == 0) sample.first = sample.previous_change = end;
    else {
        sample.crossed += after.seq != before.seq;
        sample.bubble_only += observer.cloud_changed && !observer.changed;
        if (observer.changed) {
            unsigned gap = (unsigned)(end-sample.previous_change);
            unsigned ms = (gap + 999) / 1000;
            ++sample.gaps[ms < 250 ? ms : 250];
            if (gap > sample.max_gap) sample.max_gap = gap;
            ++sample.body_changes;
            sample.previous_change = end;
        }
    }
    if (count && (count != 1 || areas[0].width != DSK_SIDE || areas[0].height != DSK_SIDE)) {
        ++sample.partial;
        unsigned us = (unsigned)(end-start);
        if (us > sample.peak) sample.peak = us;
    }
    int64_t duration = sweep ? 30000000 : (int64_t)DSK_LIVE_CHECK_MS * 1000;
    if (hd) duration = segment ? 33600000 : 2800000;
    if (end - sample.first >= duration) emit_sample(end, after);
    return err;
}
