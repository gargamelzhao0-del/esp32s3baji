#include "baji_scene_damage.h"

/* 480 RGB565 pixels take 48 us on the board's 40 MHz quad bus. A separate
 * window needs column/row setup and a DMA submission even for a few pixels. */
enum { WINDOW_SETUP_PIXELS = 480 };

static uint32_t area(baji_rect_t r)
{
    return (uint32_t)r.width * r.height;
}

static baji_rect_t bounds(baji_rect_t a, baji_rect_t b)
{
    int x = a.x < b.x ? a.x : b.x;
    int y = a.y < b.y ? a.y : b.y;
    int right = a.x + a.width > b.x + b.width ? a.x + a.width : b.x + b.width;
    int bottom = a.y + a.height > b.y + b.height ? a.y + a.height : b.y + b.height;
    return (baji_rect_t){x, y, right - x, bottom - y};
}

/* Negative cost means the two rectangles would transmit duplicate pixels. */
static int64_t extra_pixels(baji_rect_t a, baji_rect_t b)
{
    return (int64_t)area(bounds(a, b)) - area(a) - area(b);
}

static void remove_region(baji_damage_t *d, size_t index)
{
    d->regions[index] = d->regions[--d->count];
}

static void insert(baji_damage_t *d, baji_rect_t rect)
{
    while (d->count) {
        size_t best = 0;
        int64_t cost = extra_pixels(rect, d->regions[0]);
        for (size_t i = 1; i < d->count; ++i) {
            int64_t candidate = extra_pixels(rect, d->regions[i]);
            if (candidate < cost) {
                cost = candidate;
                best = i;
            }
        }
        if (cost > WINDOW_SETUP_PIXELS) {
            if (d->count < BAJI_DAMAGE_CAPACITY) break;

            /* At capacity the least expensive pair may be two older regions.
             * Keep an isolated new touch or status icon independent in that case. */
            size_t first = 0, second = 0;
            for (size_t i = 0; i < d->count; ++i) {
                for (size_t j = i + 1; j < d->count; ++j) {
                    int64_t candidate = extra_pixels(d->regions[i], d->regions[j]);
                    if (candidate < cost) {
                        cost = candidate;
                        first = i;
                        second = j;
                    }
                }
            }
            if (first != second) {
                d->regions[first] = bounds(d->regions[first], d->regions[second]);
                remove_region(d, second);
                break;
            }
        }
        rect = bounds(rect, d->regions[best]);
        remove_region(d, best);
    }
    d->regions[d->count++] = rect;
}

void baji_damage_init(baji_damage_t *d, uint16_t width, uint16_t height)
{
    d->width = width;
    d->height = height;
    d->count = 0;
}

void baji_damage_clear(baji_damage_t *d)
{
    d->count = 0;
}

static unsigned clipped_even_end(int origin, int length, uint16_t limit)
{
    int64_t end = (int64_t)origin + length;
    if (end <= 0) return 0;
    if (end >= limit) return limit;
    return ((unsigned)end + 1U) & ~1U;
}

void baji_damage_mark(baji_damage_t *d, int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0 || x >= d->width || y >= d->height) return;
    unsigned left = x < 0 ? 0U : (unsigned)x & ~1U;
    unsigned top = y < 0 ? 0U : (unsigned)y & ~1U;
    unsigned right = clipped_even_end(x, width, d->width);
    unsigned bottom = clipped_even_end(y, height, d->height);
    if (left >= right || top >= bottom) return;

    baji_rect_t full = {0, 0, d->width, d->height};
    if (left == 0 && top == 0 && right == d->width && bottom == d->height) {
        d->regions[0] = full;
        d->count = 1;
        return;
    }
    insert(d, (baji_rect_t){left, top, right - left, bottom - top});

    if (d->count > 1) {
        uint64_t cost = (d->count - 1) * WINDOW_SETUP_PIXELS;
        for (size_t i = 0; i < d->count; ++i) cost += area(d->regions[i]);
        if (cost >= area(full)) {
            d->regions[0] = full;
            d->count = 1;
        }
    }
}

void baji_damage_add(baji_damage_t *d, baji_rect_t rect)
{
    baji_damage_mark(d, rect.x, rect.y, rect.width, rect.height);
}
