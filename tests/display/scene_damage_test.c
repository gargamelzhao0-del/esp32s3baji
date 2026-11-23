#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "baji_scene_damage.h"

enum { W = 466, H = 466 };
static uint8_t changed[H][W], painted[H][W];

static bool same(baji_rect_t a, baji_rect_t b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

static void edge_cases(void)
{
    baji_damage_t d;
    baji_damage_init(&d, W, H);
    baji_damage_mark(&d, INT_MIN, 0, INT_MAX, 9);
    baji_damage_mark(&d, 0, -5, 9, 5);
    baji_damage_mark(&d, INT_MAX, 0, INT_MAX, INT_MAX);
    baji_damage_mark(&d, 9, 9, 0, INT_MAX);
    baji_damage_mark(&d, 9, 9, INT_MAX, -1);
    assert(d.count == 0);
    baji_damage_mark(&d, 465, 465, INT_MAX, INT_MAX);
    assert(d.count == 1 && same(d.regions[0], (baji_rect_t){464,464,2,2}));
    baji_damage_mark(&d, -3, -2, 8, 11);
    assert(d.count == 2 && same(d.regions[1], (baji_rect_t){0,0,6,10}));
    baji_damage_add(&d, (baji_rect_t){1,1,3,1});
    assert(d.count == 2);
    baji_damage_mark(&d, -100, -100, INT_MAX, INT_MAX);
    assert(d.count == 1 && same(d.regions[0], (baji_rect_t){0,0,W,H}));
    baji_damage_clear(&d);
    baji_damage_mark(&d, 3, 3, 1, 1);
    assert(d.count == 1 && same(d.regions[0], (baji_rect_t){2,2,2,2}));

    /* The panel is even, but clamping is safe with odd or zero limits too. */
    baji_damage_init(&d, 5, 3);
    baji_damage_mark(&d, 4, 2, INT_MAX, INT_MAX);
    assert(d.count == 1 && same(d.regions[0], (baji_rect_t){4,2,1,1}));
    baji_damage_init(&d, 0, 0);
    baji_damage_mark(&d, -1, -1, INT_MAX, INT_MAX);
    assert(d.count == 0);
}

static void capacity_and_merge(void)
{
    baji_damage_t d;
    baji_damage_init(&d, W, H);
    baji_damage_mark(&d, 50, 50, 10, 20);
    baji_damage_mark(&d, 70, 50, 10, 20);
    assert(d.count == 1 && same(d.regions[0], (baji_rect_t){50,50,30,20}));
    baji_damage_clear(&d);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) baji_damage_mark(&d, 10 + x*72, 10 + y*72, 10, 10);
    assert(d.count == BAJI_DAMAGE_CAPACITY);
    baji_damage_mark(&d, 450, 450, 10, 10);
    assert(d.count == BAJI_DAMAGE_CAPACITY);
    bool isolated = false;
    for (size_t i = 0; i < d.count; ++i)
        isolated |= same(d.regions[i], (baji_rect_t){450,450,10,10});
    assert(isolated);
}

static void stamp(uint8_t target[H][W], baji_rect_t r)
{
    assert(r.width && r.height && r.x+r.width <= W && r.y+r.height <= H);
    for (int y = r.y; y < r.y+r.height; ++y) memset(&target[y][r.x], 1, r.width);
}

static uint32_t random_state = 125614;
static int below(unsigned limit)
{
    random_state = random_state * 1664525U + 1013904223U;
    return (random_state >> 8) % limit;
}

static void randomized_coverage(void)
{
    baji_damage_t d;
    baji_damage_init(&d, W, H);
    for (unsigned pass = 0; pass < 96; ++pass) {
        memset(changed, 0, sizeof(changed));
        memset(painted, 0, sizeof(painted));
        baji_damage_clear(&d);
        for (int i = 0; i < 1 + (int)(pass % 48); ++i) {
            int x = below(W+100)-50, y = below(H+100)-50;
            int width = below(62), height = below(62);
            baji_damage_mark(&d, x, y, width, height);
            for (int row = y; row < y+height; ++row) {
                if (row < 0 || row >= H) continue;
                for (int col = x; col < x+width; ++col)
                    if (col >= 0 && col < W) changed[row][col] = 1;
            }
        }
        assert(d.count <= BAJI_DAMAGE_CAPACITY);
        for (size_t i = 0; i < d.count; ++i) {
            baji_rect_t r = d.regions[i];
            assert(!((r.x | r.y | r.width | r.height) & 1));
            stamp(painted, r);
        }
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) assert(!changed[y][x] || painted[y][x]);
    }
}

/* A partially completed submission may leave the panel at a mixture of two
 * revisions. Repainting all pending rectangles with the latest scene must
 * erase both the previous pose and any pixels from the interrupted pose. */
static void interrupted_motion(void)
{
    baji_damage_t d;
    baji_damage_init(&d, W, H);
    const baji_rect_t old = {130,112,20,20}, middle = {280,215,20,20}, last = {78,310,20,20};
    memset(painted, 0, sizeof(painted));
    stamp(painted, old);
    baji_damage_add(&d, old);
    baji_damage_add(&d, middle);
    assert(d.count == 2);
    stamp(painted, middle); /* Only one window reached the panel. */
    baji_damage_add(&d, middle);
    baji_damage_add(&d, last);
    memset(changed, 0, sizeof(changed));
    stamp(changed, last);
    for (size_t i = 0; i < d.count; ++i) {
        baji_rect_t r = d.regions[i];
        for (int y = r.y; y < r.y+r.height; ++y)
            memcpy(&painted[y][r.x], &changed[y][r.x], r.width);
    }
    assert(!memcmp(changed, painted, sizeof(painted)));
    baji_damage_clear(&d);
    assert(d.count == 0);
}

int main(void)
{
    edge_cases();
    capacity_and_merge();
    randomized_coverage();
    interrupted_motion();
    puts("Scene damage clipping, bounded merging, random pixel coverage and interrupted repaint passed.");
    return 0;
}
