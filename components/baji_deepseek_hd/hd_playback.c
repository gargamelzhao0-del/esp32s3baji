#include "hd_playback.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

enum { ANCHORS = 17, PIXELS = DSH_STAGE * DSH_STAGE, WORDS = PIXELS + 2 * DSH_STAGE };

typedef struct dsh_playback {
    unsigned item, mix;
    bool compact_only;
    const dsh_motion_t *pages;
    uint16_t *anchors[ANCHORS];  // Wire bytes, including the two exact guide strips.
    const uint16_t *lower, *upper;
    uint32_t measured;
    baji_rect_t damage[ANCHORS - 1][DSH_DAMAGE_STRIPS];
} dsh_playback_t;

void dsh_playback_release(dsh_canvas_t *c)
{
    dsh_compact_release(c);
    c->playing_sample = false;
    if (!c->playback) return;
    for (unsigned i = 0; i < ANCHORS; i++) heap_caps_free(c->playback->anchors[i]);
    free(c->playback);
    c->playback = NULL;
}

static bool activate(dsh_canvas_t *c, unsigned item)
{
    if (c->playback && c->playback->item == item) return true;
    dsh_playback_release(c);
    c->playback = calloc(1, sizeof(*c->playback));
    if (!c->playback) return false;
    c->playback->item = item;
    return true;
}

bool dsh_canvas_seed_motion(dsh_canvas_t *c, unsigned item, const dsh_motion_t pages[16])
{
    if (!c || !pages || item >= 18) return false;
    unsigned entry = item < 6 ? item + 1 : item == 6 ? 1 : item;
    for (unsigned i = 0; i < 16; i++)
        if (!pages[i].bytes || pages[i].count != 1 || pages[i].first != (entry-1)*16+i) return false;
    dsh_canvas_set_motion(c, NULL);
    if (!activate(c, item)) return false;
    c->playback->pages = pages;
    return true;
}

static int farthest(const dsh_playback_t *p, unsigned level, int pinned)
{
    int victim = -1, distance = -1;
    for (int i = 0; i < ANCHORS; i++) {
        int d = abs(i - (int)level);
        if (i != pinned && p->anchors[i] && d > distance) { victim = i; distance = d; }
    }
    return victim;
}

static bool load_anchor(dsh_canvas_t *c, dsh_view_t v, int pinned)
{
    dsh_playback_t *p = c->playback;
    unsigned level = v.breath;
    if (p->anchors[level]) return true;
    const dsh_motion_t *saved = c->motion;
    if (level && p->pages) c->motion = &p->pages[level - 1];
    v.guides = false;
    bool ok = dsh_canvas_select(c, v);
    if (!ok && level && c->motion && c->motion->wire && !c->exact) {
        int old = farthest(p, level, pinned);
        if (old >= 0) {
            c->exact = p->anchors[old];
            p->anchors[old] = NULL;
            ok = dsh_canvas_select(c, v);
        }
    }
    if (ok && c->exact_id != UINT16_MAX && c->motion && c->motion->wire) {
        // The ROM decoder already produced a complete wire-order frame.
        // Transfer its buffer to the cache without painting or copying it.
        p->anchors[level] = c->exact;
        c->exact = NULL;
        c->exact_id = UINT16_MAX;
    } else if (ok) {
        uint16_t *frame = heap_caps_malloc(WORDS * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!frame) {
            int old = farthest(p, level, pinned);
            if (old >= 0) { frame = p->anchors[old]; p->anchors[old] = NULL; }
        }
        if (frame) {
            const baji_rect_t stage = {DSH_INSET, DSH_INSET, DSH_STAGE, DSH_STAGE};
            const baji_rect_t vertical = {DSH_INSET + 160, DSH_INSET, 1, DSH_STAGE};
            const baji_rect_t horizontal = {DSH_INSET, DSH_INSET + 326, DSH_STAGE, 1};
            dsh_paint(&stage, (uint8_t *)frame, c);
            c->view.guides = true;
            dsh_paint(&vertical, (uint8_t *)(frame + PIXELS), c);
            dsh_paint(&horizontal, (uint8_t *)(frame + PIXELS + DSH_STAGE), c);
            p->anchors[level] = frame;
        } else ok = false;
    }
    c->motion = saved;
    return ok;
}

bool dsh_canvas_select_blend(dsh_canvas_t *c, dsh_view_t v, unsigned mix)
{
    if (!c || !c->art || v.item >= 18 || v.sheet >= 4 || v.zoom > 1 ||
        v.pan_x < -100 || v.pan_x > 100 || v.pan_y < -100 || v.pan_y > 100 ||
        v.breath >= ANCHORS || mix > 7 ||
        (v.breath == ANCHORS - 1 && mix)) return false;
    if (v.gallery) return dsh_canvas_select(c, v);
    if (!activate(c, v.item)) return false;
    c->playing_sample = false;
    dsh_playback_t *p = c->playback;
    unsigned base = v.breath;
    if (!c->motion && !p->pages) {
        if (!dsh_select_compact(c, v, mix)) return false;
        if ((v.breath || mix) && !p->compact_only && !c->compact) {
            dsh_compact_prepare(c);
            p->compact_only = !c->compact;
        }
        return true;
    }
    bool ready = load_anchor(c, v, -1);
    if (ready && mix) {
        dsh_view_t next = v;
        next.breath++;
        ready = load_anchor(c, next, (int)base);
    }
    if (!ready) return false;
    p->lower = p->anchors[base];
    p->upper = mix ? p->anchors[base + 1] : p->lower;
    p->mix = mix;
    c->view = v;
    c->playing_sample = true;
    return true;
}

static inline uint16_t tween(uint16_t a, uint16_t b, unsigned m)
{
    if (a == b) return a;
    unsigned ca = __builtin_bswap16(a), cb = __builtin_bswap16(b);
    // Three lanes with room for the x8 interpolation weight, rounded to RGB565.
    const uint32_t mask = 0x07e0f81f;
    ca = (ca | ca << 16) & mask;
    cb = (cb | cb << 16) & mask;
    uint32_t out = ((ca * (8 - m) + cb * m + 0x00802004u) >> 3) & mask;
    return __builtin_bswap16((uint16_t)(out | out >> 16));
}

static void extend_row(const uint16_t *a, const uint16_t *b, unsigned *left, unsigned *right)
{
    unsigned x = 0;
    while (x < *left && a[x] == b[x]) x++;
    *left = x;
    x = DSH_STAGE;
    while (x > *right && a[x-1] == b[x-1]) x--;
    *right = x;
}

static bool measure_pair(dsh_playback_t *p, unsigned level)
{
    const uint32_t bit = 1u << level;
    if (p->measured & bit) return true;
    const uint16_t *a = p->anchors[level], *b = p->anchors[level+1];
    if (!a || !b) return false;
    const unsigned height = DSH_STAGE / DSH_DAMAGE_STRIPS;
    for (unsigned strip = 0; strip < DSH_DAMAGE_STRIPS; strip++) {
        unsigned top = strip*height, left = DSH_STAGE, right = 0;
        for (unsigned y = top; y < top+height; y++) {
            extend_row(a + y*DSH_STAGE, b + y*DSH_STAGE, &left, &right);
            if (a[PIXELS+y] != b[PIXELS+y]) {
                if (left > 160) left = 160;
                if (right < 161) right = 161;
            }
            if (y == 326) extend_row(a+PIXELS+DSH_STAGE, b+PIXELS+DSH_STAGE, &left, &right);
        }
        if (left < right) p->damage[level][strip] = (baji_rect_t){
            DSH_INSET + left, DSH_INSET + top, right-left, height};
    }
    p->measured |= bit;
    return true;
}

unsigned dsh_canvas_motion_damage(dsh_canvas_t *c, unsigned from, baji_rect_t areas[DSH_DAMAGE_STRIPS])
{
    if (!c || !areas) return 0;
    if (c->compact) {
        // At rest the compact pack carries Chrome's exact guide strips. The
        // procedural overlay can differ by a code along either complete line.
        if (c->view.guides && (!from || (!c->view.breath && !c->fractional_breath))) goto stage;
        return dsh_compact_damage(c, areas);
    }
    dsh_playback_t *p = c->playback;
    if (!c->playing_sample || !p || from > 128) goto stage;
    unsigned to = 8 * c->view.breath + p->mix;
    unsigned low = from <= to ? from : to, high = from <= to ? to : from;
    if (low == high) return 0;
    unsigned first = low/8, end = (high+7)/8;
    for (unsigned i = first; i < end; i++)
        if (!measure_pair(p, i)) goto stage;
    unsigned count = 0;
    for (unsigned strip = 0; strip < DSH_DAMAGE_STRIPS; strip++) {
        unsigned left = DSH_PANEL, right = 0;
        for (unsigned i = first; i < end; i++) {
            baji_rect_t box = p->damage[i][strip];
            if (!box.width) continue;
            if (left > box.x) left = box.x;
            if (right < (unsigned)box.x + box.width) right = box.x + box.width;
        }
        if (left < right) areas[count++] = (baji_rect_t){left, DSH_INSET + strip*(DSH_STAGE / DSH_DAMAGE_STRIPS),
                                                        right-left, DSH_STAGE / DSH_DAMAGE_STRIPS};
    }
    return count;
stage:
    areas[0] = (baji_rect_t){DSH_INSET, DSH_INSET, DSH_STAGE, DSH_STAGE};
    return 1;
}

typedef uint32_t word_alias_t __attribute__((may_alias));
typedef uint16_t half_alias_t __attribute__((may_alias));

static inline uint32_t blend_pair(uint32_t x, uint32_t y, unsigned m)
{
    if (x == y) return x;
    return tween((uint16_t)x, (uint16_t)y, m) | ((uint32_t)tween(x >> 16, y >> 16, m) << 16);
}

static void blend_row(uint8_t *dst, const uint16_t *a, const uint16_t *b, unsigned n, unsigned m)
{
    if (!m) { memcpy(dst, a, n*2); return; }
    if ((uintptr_t)dst & 1) {
        for (unsigned i = 0; i < n; i++) {
            uint16_t pixel = tween(a[i], b[i], m);
            dst[2*i] = (uint8_t)pixel;
            dst[2*i+1] = pixel >> 8;
        }
        return;
    }
    // Both PSRAM pages are word aligned and have the same origin. The panel
    // clips the stage at an odd x, so DMA and source alignment may differ.
    if (n && ((uintptr_t)a & 2)) {
        *(half_alias_t *)dst = tween(*a++, *b++, m);
        dst += 2;
        n--;
    }
    const word_alias_t *wa = (const word_alias_t *)a, *wb = (const word_alias_t *)b;
    const unsigned pairs = n / 2;
    if ((uintptr_t)dst & 2) {
        half_alias_t *out = (half_alias_t *)dst;
        for (unsigned i = 0; i < pairs; i++) {
            uint32_t pair = blend_pair(wa[i], wb[i], m);
            out[2*i] = (uint16_t)pair;
            out[2*i+1] = pair >> 16;
        }
    } else {
        word_alias_t *out = (word_alias_t *)dst;
        for (unsigned i = 0; i < pairs; i++) out[i] = blend_pair(wa[i], wb[i], m);
    }
    if (n & 1) ((half_alias_t *)dst)[n-1] = tween(a[n-1], b[n-1], m);
}

void dsh_playback_paint(const baji_rect_t *r, uint8_t *dst, const dsh_canvas_t *c)
{
    const dsh_playback_t *p = c->playback;
    unsigned left = r->x < DSH_INSET ? DSH_INSET : r->x, right = r->x + r->width;
    if (right > DSH_INSET + DSH_STAGE) right = DSH_INSET + DSH_STAGE;
    unsigned stride = 2u * r->width;
    if (left >= right) { memset(dst, 0, stride * r->height); return; }
    unsigned before = 2 * (left - r->x), n = right - left, gx = DSH_INSET + 160;
    unsigned after = stride - before - n*2;
    for (unsigned y = r->y; y < r->y + r->height; y++, dst += stride) {
        if (y < DSH_INSET || y >= DSH_INSET + DSH_STAGE) { memset(dst, 0, stride); continue; }
        unsigned sy = y - DSH_INSET;
        unsigned row = c->view.guides && sy == 326 ? PIXELS + DSH_STAGE : sy * DSH_STAGE;
        unsigned i = row + left - DSH_INSET;
        if (before) memset(dst, 0, before);
        blend_row(dst + before, p->lower + i, p->upper + i, n, p->mix);
        if (c->view.guides && sy != 326 && left <= gx && gx < right) {
            unsigned index = PIXELS + sy;
            uint16_t value = tween(p->lower[index], p->upper[index], p->mix);
            memcpy(dst + 2*(gx - r->x), &value, 2);
        }
        if (after) memset(dst + stride - after, 0, after);
    }
}
