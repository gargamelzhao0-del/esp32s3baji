#include "deepseek_pillow.h"
#include <stdlib.h>

enum { RADIUS = 20, HALF_HEIGHT = 10 };

bool dsk_pillow_contains(dsk_cloth_t c, int x, int y)
{
    int dx = abs(x - c.x), dy = abs(y - c.y);
    /* Keep clear of the close view's bottom controls and the connected HUD. */
    if (y >= c.bottom || dx > c.rx || dy > c.ry || !c.rx || !c.ry) return false;
    int a = dx * c.ry, b = dy * c.rx, limit = c.rx * c.ry;
    return a*a + b*b <= limit*limit;
}

baji_rect_t dsk_pillow_bounds(dsk_pillow_t p)
{
    return (baji_rect_t){p.x - RADIUS, p.y - HALF_HEIGHT, 2*RADIUS + 1, 2*HALF_HEIGHT + 1};
}

static int mix_channel(int original, int shifted, int weight, int light, int max)
{
    int value = (original * (16-weight) + shifted * weight + 8) / 16;
    if (light < 0) return (value * (32+light) + 16) / 32;
    return value + ((max-value) * light + 16) / 32;
}

void dsk_pillow_paint(dsk_pillow_t p, const baji_rect_t *band, uint8_t *pixels,
                      baji_paint_fn base, void *canvas)
{
    if (!p.depth) return;
    baji_rect_t patch = dsk_pillow_bounds(p);
    int left = band->x > patch.x ? band->x : patch.x;
    int top = band->y > patch.y ? band->y : patch.y;
    int right = band->x + band->width, bottom = band->y + band->height;
    if (right > patch.x + patch.width) right = patch.x + patch.width;
    if (bottom > patch.y + patch.height) bottom = patch.y + patch.height;
    if (bottom > p.cloth.bottom) bottom = p.cloth.bottom;
    if (left >= right || top >= bottom) return;
    uint8_t sample[(2*RADIUS + 1)*2];
    int depth = abs(p.depth), shift = (depth + 1) / 2;
    if (p.depth < 0) shift = -shift;
    int direction = shift > 0 ? -1 : 1;
    int first = shift > 0 ? bottom-1 : top, last = shift > 0 ? top-1 : bottom;
    for (int y = first; y != last; y += direction) {
        int dy = y - p.y;
        /* Re-sample the underlying fabric, including across a DMA band edge.
         * A depression moves the print down, with shade on the upper lip. */
        int source_y = y-shift;
        const uint8_t *source;
        if (source_y >= band->y && source_y < band->y + band->height) {
            /* Traverse against the displacement so this row is still pristine. */
            source = pixels + 2*((source_y-band->y)*band->width + left-band->x);
        } else {
            baji_rect_t row = {left, source_y, right-left, 1};
            base(&row, sample, canvas);
            source = sample;
        }
        uint8_t *out = pixels + 2*((y-band->y)*band->width + left-band->x);
        for (int x = left; x < right; x++, out += 2) {
            int dx = x - p.x, falloff = RADIUS*RADIUS - dx*dx - 4*dy*dy;
            if (falloff <= 0 || !dsk_pillow_contains(p.cloth, x, y) ||
                !dsk_pillow_contains(p.cloth, x, y-shift)) continue;
            int weight = (falloff * depth + 100) / 200;
            /* Opposite slopes catch opposite light when the cloth rebounds. */
            int light = (dy - 2) * falloff * p.depth / 1800;
            int at = 2*(x-left);
            unsigned a = ((unsigned)out[0] << 8) | out[1];
            unsigned b = ((unsigned)source[at] << 8) | source[at+1];
            unsigned r = mix_channel(a >> 11, b >> 11, weight, light, 31);
            unsigned g = mix_channel((a >> 5) & 63, (b >> 5) & 63, weight, light, 63);
            unsigned blue = mix_channel(a & 31, b & 31, weight, light, 31);
            unsigned color = (r << 11) | (g << 5) | blue;
            out[0] = color >> 8;
            out[1] = color;
        }
    }
}
