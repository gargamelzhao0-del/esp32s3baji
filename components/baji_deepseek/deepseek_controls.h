#pragma once
#include "deepseek_review.h"
#include "baji_deepseek_hd.h"

typedef struct {
    dsk_model_t pixel;
    dsk_gesture_t touch;
    dsh_view_t hd;
    bool hd_available, hd_mode, hold_cancelled, breathing;
    int8_t start_pan_x, start_pan_y;
    int64_t press_time, breath_origin, freeze_time;
    uint32_t breath_edges[17];
} dsk_controls_t;

void dsk_controls_init(dsk_controls_t *ui, unsigned bubble_steps, bool hd_available, int64_t now);
int64_t dsk_controls_tick(dsk_controls_t *ui, uint32_t presses, uint16_t px, uint16_t py,
                          bool down, uint16_t x, uint16_t y, int64_t now);
