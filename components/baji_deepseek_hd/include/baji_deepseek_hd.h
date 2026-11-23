#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "baji_graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { DSH_PANEL = 466, DSH_STAGE = 340, DSH_INSET = (DSH_PANEL - DSH_STAGE) / 2 };
typedef struct { const uint8_t *bytes; } dsh_art_t;
/* In-memory pack, borrowed single frame, or a flash partition with an owned index. */
typedef struct {
    const uint8_t *bytes;
    size_t length;
    unsigned first, count, mode;
    bool wire;
    const void *partition;
    size_t max_page;
} dsh_motion_t;
bool dsh_motion_open(dsh_motion_t *out, const void *pack, size_t bytes);
/* Page modes: 0 native words, 1 native words XOR the base, 2 wire-order words. */
bool dsh_motion_open_frame(dsh_motion_t *out, unsigned entry, unsigned level, unsigned mode,
                           const void *compressed, size_t bytes);
#ifdef ESP_PLATFORM
/* Pass a zero-initialized handle. Close after all canvases have detached. */
bool dsh_partition_motion_open(dsh_motion_t *out);
void dsh_partition_motion_close(dsh_motion_t *out);
#endif

/* item uses the review's six clip buttons followed by its twelve state buttons.
 * breath 0..16 traces the first half of hdBreathe; pan is -100..100. */
typedef struct {
    uint8_t item, sheet, zoom, breath;
    bool gallery, guides;
    int8_t pan_x, pan_y;
} dsh_view_t;

typedef struct {
    const dsh_art_t *art;
    const dsh_motion_t *motion;
    uint16_t *exact;
    unsigned exact_id;
    void *decoder;
    uint8_t *motion_page;
    size_t motion_page_capacity;
    uint16_t *background, *image;
    unsigned image_id, width, height;
    dsh_view_t view;
    uint16_t sample_x[DSH_PANEL], sample_y[DSH_PANEL], weight[DSH_STAGE];
} dsh_canvas_t;

bool dsh_art_open(dsh_art_t *out, const void *bytes, size_t length);
bool dsh_canvas_init(dsh_canvas_t *canvas, const dsh_art_t *art);
/* Call again when replacing the contents of a borrowed single-frame page. */
void dsh_canvas_set_motion(dsh_canvas_t *canvas, const dsh_motion_t *motion);
bool dsh_canvas_select(dsh_canvas_t *canvas, dsh_view_t view);
void dsh_paint(const baji_rect_t *band, uint8_t *pixels, void *canvas);
void dsh_canvas_close(dsh_canvas_t *canvas);
const void *dsh_builtin(size_t *bytes);

#ifdef __cplusplus
}
#endif
