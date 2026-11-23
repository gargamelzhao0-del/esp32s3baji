#pragma once
#include "baji_deepseek_hd.h"

void dsh_playback_release(dsh_canvas_t *canvas);
bool dsh_select_compact(dsh_canvas_t *canvas, dsh_view_t view, unsigned mix);
void dsh_compact_prepare(dsh_canvas_t *canvas);
void dsh_compact_release(dsh_canvas_t *canvas);
unsigned dsh_compact_damage(const dsh_canvas_t *canvas, baji_rect_t areas[DSH_DAMAGE_STRIPS]);
void dsh_compact_paint(const baji_rect_t *band, uint8_t *dst, const dsh_canvas_t *canvas);
unsigned dsh_compensate(unsigned color, unsigned alpha, unsigned bg_src, unsigned bg_dst);
static inline unsigned dsh_mix565(unsigned a, unsigned b, unsigned weight)
{
    if (!weight || a == b) return a;
    uint32_t ar = ((a & 0xf800u) << 5) | (a & 31u);
    uint32_t br = ((b & 0xf800u) << 5) | (b & 31u);
    uint32_t rb = ((ar << 8) + (br - ar)*weight + 0x800080u) >> 8;
    uint32_t g = a & 0x07e0u;
    g = ((g << 8) + ((b & 0x07e0u) - g)*weight + (128u << 5)) >> 8;
    return ((rb >> 5) & 0xf800u) | (rb & 31u) | (g & 0x07e0u);
}
void dsh_playback_paint(const baji_rect_t *band, uint8_t *pixels, const dsh_canvas_t *canvas);
