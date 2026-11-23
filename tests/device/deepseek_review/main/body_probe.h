#pragma once
#include <stdbool.h>
#include "baji_graphics.h"

typedef struct {
    baji_paint_fn original;
    void *scene;
    bool changed, watch_cloud, cloud_changed;
} observed_paint_t;

void observe_band(const baji_rect_t *area, uint8_t *pixels, void *context);
