#include "body_probe.h"

/* Sample the completed character image before the display driver takes each
 * band. These rows are below the floating cloud and above the status plate.
 * Retaining the last RGB565 value also covers bands omitted by dirty drawing. */
enum { PROBE_X = 106, PROBE_Y = 166, PROBE_DX = 4, PROBE_DY = 8, PROBE_COLS = 65, PROBE_ROWS = 27 };
static uint16_t body_pixels[PROBE_ROWS * PROBE_COLS];
static uint16_t cloud_pixels[8 * 26];
static bool compare_grid(const baji_rect_t *area, const uint8_t *pixels, uint16_t *previous,
                         unsigned x0, unsigned y0, unsigned dx, unsigned dy, unsigned cols, unsigned rows)
{
    bool changed = false;
    unsigned first_col = area->x <= x0 ? 0 : (area->x - x0 + dx - 1)/dx;
    unsigned first_row = area->y <= y0 ? 0 : (area->y - y0 + dy - 1)/dy;
    unsigned right = area->x + area->width, bottom = area->y + area->height;
    for (unsigned row = first_row; row < rows; row++) {
        unsigned y = y0 + row * dy;
        if (y >= bottom) break;
        for (unsigned col = first_col; col < cols; col++) {
            unsigned x = x0 + col * dx;
            if (x >= right) break;
            const uint8_t *src = pixels + ((y-area->y)*area->width + x-area->x)*2;
            uint16_t next = (uint16_t)((src[0] << 8) | src[1]);
            if (previous[row*cols + col] != next) {
                previous[row*cols + col] = next;
                changed = true;
            }
        }
    }
    return changed;
}

void observe_band(const baji_rect_t *area, uint8_t *pixels, void *context)
{
    observed_paint_t *p = context;
    p->original(area, pixels, p->scene);
    p->changed |= compare_grid(area, pixels, body_pixels, PROBE_X, PROBE_Y,
                               PROBE_DX, PROBE_DY, PROBE_COLS, PROBE_ROWS);
    if (p->watch_cloud)
        p->cloud_changed |= compare_grid(area, pixels, cloud_pixels, 244, 74, 4, 4, 26, 8);
}
