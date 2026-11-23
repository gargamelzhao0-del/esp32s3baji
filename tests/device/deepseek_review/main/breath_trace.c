#include "breath_trace.h"
#include "baji_vsync.h"
#include "esp_timer.h"
#include "motion_curve.h"
#include <stdio.h>
#include <stdlib.h>

esp_err_t breath_trace(baji_board_t *board, dsh_canvas_t *canvas, const dsh_motion_t pages[16])
{
    enum { HALF_US = 1400000, CYCLES = 3 };
    uint32_t edges[17];
    dsk_curve_edges(edges, 17, HALF_US);
    const baji_rect_t stage = {DSH_INSET-1, DSH_INSET-1, DSH_STAGE+2, DSH_STAGE+2};
    dsh_view_t v = {.item=1, .guides=true};
    dsh_canvas_set_motion(canvas, NULL);
    if (!dsh_canvas_select(canvas, v)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = baji_board_paint_round(board, stage, dsh_paint, canvas);
    if (err != ESP_OK || (err = baji_vsync_open()) != ESP_OK) return err;
    baji_vsync_edge_t edge = baji_vsync_snapshot();
    err = baji_vsync_wait(edge.seq, 150, &edge);
    int64_t origin = edge.edge_us;
    unsigned changes = 0, skipped = 0, after_next = 0, peak = 0;
    while (err == ESP_OK) {
        err = baji_vsync_wait(edge.seq, 150, &edge);
        if (err != ESP_OK) break;
        int64_t now = esp_timer_get_time();
        int64_t elapsed = now - origin;
        if (elapsed >= 2LL * HALF_US * CYCLES) break;
        uint32_t until;
        unsigned level = dsk_curve_at(edges, 17, HALF_US, elapsed, &until);
        if (level == v.breath) continue;
        skipped += (unsigned)abs((int)level - (int)v.breath) - 1;
        v.breath = level;
        dsh_canvas_set_motion(canvas, level ? &pages[level-1] : NULL);
        if (!dsh_canvas_select(canvas, v)) { err = ESP_ERR_INVALID_STATE; break; }
        err = baji_board_paint_round(board, stage, dsh_paint, canvas);
        unsigned cost = (unsigned)(esp_timer_get_time()-now);
        if (cost > peak) peak = cost;
        if (cost > until) ++after_next;
        ++changes;
    }
    esp_err_t closed = baji_vsync_close();
    if (err == ESP_OK) err = closed;
    printf("DSK_BREATH cycles=%u steps=%u expected=%u skipped=%u past_next=%u max_us=%u err=%d\n",
           CYCLES, changes, 32*CYCLES, skipped, after_next, peak, err);
    dsh_canvas_set_motion(canvas, NULL);
    return err;
}
