#include "hd_playback.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>
#if defined(ESP_PLATFORM) && !defined(DSH_HOST_TEST)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#if !CONFIG_FREERTOS_UNICORE
#define DSH_PARALLEL 1
#endif
#endif

enum { GROUP = 4, GROUPS = DSH_STAGE / GROUP, STRIP = DSH_STAGE / DSH_DAMAGE_STRIPS,
       ORIGINAL = GROUPS*DSH_STAGE, HASH_SIZE = 8192, SAME = 0x8000 };
typedef uint16_t pixels_t __attribute__((may_alias));
typedef uint32_t pair_t __attribute__((may_alias));

/* Each output row only samples a few neighbouring source rows over the entire
 * CSS breath. Most four-pixel groups can be read verbatim from the original
 * pose or the stationary backdrop, even along partially transparent edges.
 * Only the small remaining set needs its own matte-corrected RGB565 words. */
struct dsh_compact {
    uint32_t offset[DSH_STAGE+1];
    uint16_t *route, *patches, *slots;
    uint8_t *run;
    unsigned used, capacity;
    bool pooled;
    baji_rect_t damage[DSH_DAMAGE_STRIPS];
    uint16_t unaligned_row[DSH_STAGE];
#ifdef DSH_PARALLEL
    TaskHandle_t worker;
    StaticSemaphore_t done_storage;
    SemaphoreHandle_t done;
    BaseType_t worker_core;
    const dsh_canvas_t *canvas;
    baji_rect_t work;
    uint8_t *destination;
#endif
};

static void paint_rows(const baji_rect_t *r, uint8_t *dst, const dsh_canvas_t *c);

#ifdef DSH_PARALLEL
static void paint_worker(void *arg)
{
    struct dsh_compact *p = arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!p->destination) break;
        paint_rows(&p->work, p->destination, p->canvas);
        xSemaphoreGive(p->done);
    }
    xSemaphoreGive(p->done);
    vTaskDelete(NULL);
}
#endif

void dsh_compact_release(dsh_canvas_t *c)
{
    if (!c->compact) return;
#ifdef DSH_PARALLEL
    struct dsh_compact *p = c->compact;
    if (p->worker) {
        p->destination = NULL;
        xTaskNotifyGive(p->worker);
        xSemaphoreTake(p->done, portMAX_DELAY);
    }
    if (p->done) vSemaphoreDelete(p->done);
#endif
    heap_caps_free(c->compact->route);
    heap_caps_free(c->compact->patches);
    heap_caps_free(c->compact->slots);
    heap_caps_free(c->compact->run);
    if (c->compact->pooled) {
        heap_caps_free(c->image);
        c->image = NULL;
        c->image_id = UINT16_MAX;
    }
    free(c->compact);
    c->compact = NULL;
}

static unsigned max_row(unsigned y)
{
    int centre = (DSH_STAGE-1)*128;
    int y256 = centre + ((int)y*256 - centre + 16*64)*3200/(3200+3*16);
    unsigned hi = (unsigned)y256/256+1;
    return hi < DSH_STAGE ? hi : DSH_STAGE-1;
}

static bool intern_group(struct dsh_compact *p, const uint16_t words[GROUP], uint16_t *route)
{
    uint32_t hash = words[0]*65599u + words[1]*257u + words[2]*31u + words[3];
    unsigned pos = (hash ^ (hash >> 13)) & (HASH_SIZE-1);
    while (p->slots[pos]) {
        unsigned i = p->slots[pos]-1;
        if (!memcmp(words, p->patches+i*GROUP, GROUP*2)) {
            *route = ORIGINAL+i;
            return true;
        }
        pos = (pos+1) & (HASH_SIZE-1);
    }
    if (ORIGINAL+p->used >= SAME) return false;
    if (p->used == p->capacity) {
        unsigned size = p->capacity ? 2*p->capacity : 256;
        uint16_t *next = heap_caps_malloc(size*GROUP*2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!next) return false;
        if (p->patches) memcpy(next, p->patches, p->used*GROUP*2);
        heap_caps_free(p->patches);
        p->patches = next;
        p->capacity = size;
    }
    *route = ORIGINAL + p->used;
    p->slots[pos] = p->used+1;
    memcpy(p->patches + p->used++*GROUP, words, GROUP*2);
    return true;
}

void dsh_compact_prepare(dsh_canvas_t *c)
{
    if (c->compact || !c->image || !c->background) return;
    struct dsh_compact *p = calloc(1, sizeof(*p));
    if (!p) return;
    c->compact = p;
    for (unsigned y = 0; y < DSH_STAGE; y++)
        p->offset[y+1] = p->offset[y] + (max_row(y)-y+1)*GROUPS;
    p->route = heap_caps_malloc(p->offset[DSH_STAGE]*2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    p->slots = heap_caps_calloc(HASH_SIZE, sizeof(*p->slots), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p->route || !p->slots) { dsh_compact_release(c); return; }
    const uint8_t *alpha = (const uint8_t *)(c->image + DSH_STAGE*DSH_STAGE);
    for (unsigned strip = 0; strip < DSH_DAMAGE_STRIPS; strip++) {
        unsigned left = DSH_STAGE, right = 0, top = strip*STRIP;
        for (unsigned y = top; y < top+STRIP; y++) {
            const uint16_t *back = c->background + y*DSH_STAGE, *rest = c->image + y*DSH_STAGE;
            uint16_t *route = p->route + p->offset[y];
            unsigned hi = max_row(y);
            for (unsigned sy = y; sy <= hi; sy++) {
                const unsigned base = sy*DSH_STAGE;
                for (unsigned x = 0; x < DSH_STAGE; x += GROUP, route++) {
                    const uint16_t *src = c->image + base+x;
                    uint16_t words[GROUP];
                    for (unsigned j = 0; j < GROUP; j++)
                        words[j] = dsh_compensate(src[j], alpha[base+x+j], c->background[base+x+j], back[x+j]);
                    if (memcmp(words, rest+x, sizeof(words))) {
                        if (x < left) left = x;
                        if (x+GROUP > right) right = x+GROUP;
                    }
                    if (!memcmp(words, src, sizeof(words))) *route = (base+x)/GROUP;
                    else if (!intern_group(p, words, route)) { dsh_compact_release(c); return; }
                }
            }
        }
        if (left < right) p->damage[strip] = (baji_rect_t){DSH_INSET+left, DSH_INSET+top, right-left, STRIP};
    }
    // Routes no longer need alpha. Reuse that space for the sparse matte
    // corrections. All four-pixel groups now share one wire-order address space.
    if (p->used) memcpy(c->image + ORIGINAL*GROUP, p->patches, p->used*GROUP*2);
    for (unsigned i = 0; i < (ORIGINAL+p->used)*GROUP; i++)
        c->image[i] = __builtin_bswap16(c->image[i]);
    uint16_t *guides = c->image + DSH_STAGE*DSH_STAGE*3/2;
    for (unsigned i = 0; i < DSH_STAGE*2; i++) guides[i] = __builtin_bswap16(guides[i]);
    for (unsigned y = 0; y < DSH_STAGE; y++) {
        for (unsigned i = p->offset[y]; i+GROUPS < p->offset[y+1]; i++) {
            const uint16_t *a = c->image + p->route[i]*GROUP;
            const uint16_t *b = c->image + p->route[i+GROUPS]*GROUP;
            if (a == b || !memcmp(a, b, GROUP*2)) p->route[i] |= SAME;
        }
    }
    heap_caps_free(p->patches); p->patches = NULL;
    heap_caps_free(p->slots); p->slots = NULL;
    heap_caps_free(c->background); c->background = NULL;
    p->pooled = true;
#ifdef DSH_PARALLEL
    p->done = xSemaphoreCreateBinaryStatic(&p->done_storage);
    p->worker_core = 1 - xPortGetCoreID();
    p->canvas = c;
    if (p->done && xTaskCreatePinnedToCore(paint_worker, "hd_rows", 3072, p, 3,
                                         &p->worker, p->worker_core) != pdPASS) p->worker = NULL;
#endif
    // Long contiguous runs can go through a two-source linear mixer. Reserve
    // this optional accelerator after releasing the decompressed background.
    p->run = heap_caps_malloc(p->offset[DSH_STAGE], MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p->run) return;
    for (unsigned y = 0; y < DSH_STAGE; y++) {
        for (unsigned row = p->offset[y]; row < p->offset[y+1]; row += GROUPS) {
            bool last = row+GROUPS == p->offset[y+1];
            unsigned hi = last ? row : row+GROUPS;
            p->run[row+GROUPS-1] = 1;
            for (int x = GROUPS-2; x >= 0; x--) {
                unsigned a = p->route[row+x], b = p->route[row+x+1];
                bool adjacent = b == a+1 && ((a & SAME) ||
                    (p->route[hi+x+1] & ~SAME) == (p->route[hi+x] & ~SAME)+1u);
                p->run[row+x] = adjacent ? p->run[row+x+1]+1 : 1;
            }
        }
    }
}

unsigned dsh_compact_damage(const dsh_canvas_t *c, baji_rect_t areas[DSH_DAMAGE_STRIPS])
{
    memcpy(areas, c->compact->damage, sizeof(c->compact->damage));
    return DSH_DAMAGE_STRIPS;
}

static inline uint16_t wire_sample(unsigned a, unsigned b, unsigned w)
{
    return __builtin_bswap16((uint16_t)dsh_mix565(a, b, w));
}

static inline uint16_t mix_wire(unsigned a, unsigned b, unsigned w)
{
    if (a == b) return a;
    return wire_sample(__builtin_bswap16(a), __builtin_bswap16(b), w);
}

static void compact_row(const dsh_canvas_t *c, unsigned y, unsigned left, unsigned right, pixels_t *dst)
{
    const struct dsh_compact *p = c->compact;
    unsigned sy = c->sample_y[y], w = c->weight[y];
    const uint16_t *lo = p->route+p->offset[y]+(sy-y)*GROUPS, *hi = w ? lo+GROUPS : lo;
    const uint8_t *runs = p->run ? p->run + (lo-p->route) : NULL;
    for (unsigned x = left; x < right;) {
        unsigned start = x & ~(GROUP-1u), group = start/GROUP;
        unsigned index = lo[group];
        const uint16_t *aa = c->image + (index & ~SAME)*GROUP;
        const uint16_t *bb = c->image + (hi[group] & ~SAME)*GROUP;
        if (runs && x == start && runs[group] >= 2 && right-x >= 2*GROUP) {
            unsigned n = runs[group]*GROUP;
            if (n > right-x) n = (right-x) & ~(GROUP-1u);
            if (!w || (index & SAME)) memcpy(dst, aa, n*2);
            else {
                for (unsigned i = 0; i < n; i += 2) {
                    uint32_t a = ((const pair_t *)aa)[i/2], b = ((const pair_t *)bb)[i/2];
                    dst[i] = a == b ? (uint16_t)a : mix_wire((uint16_t)a, (uint16_t)b, w);
                    dst[i+1] = a == b ? a >> 16 : mix_wire(a >> 16, b >> 16, w);
                }
            }
            x += n; dst += n;
            continue;
        }
        if (x == start && right-x >= GROUP) {
            if (!w || (index & SAME)) {
                if (!((uintptr_t)dst & 2)) {
                    ((pair_t *)dst)[0] = ((const pair_t *)aa)[0];
                    ((pair_t *)dst)[1] = ((const pair_t *)aa)[1];
                } else {
                    dst[0]=aa[0]; dst[1]=aa[1]; dst[2]=aa[2]; dst[3]=aa[3];
                }
            } else {
                dst[0] = mix_wire(aa[0], bb[0], w);
                dst[1] = mix_wire(aa[1], bb[1], w);
                dst[2] = mix_wire(aa[2], bb[2], w);
                dst[3] = mix_wire(aa[3], bb[3], w);
            }
            x += GROUP; dst += GROUP;
        } else {
            for (; x < right && x < start+GROUP; x++)
                *dst++ = mix_wire(aa[x-start], bb[x-start], w);
        }
    }
}

static void paint_rows(const baji_rect_t *r, uint8_t *dst, const dsh_canvas_t *c)
{
    unsigned right = r->x+r->width < DSH_INSET+DSH_STAGE ? r->x+r->width : DSH_INSET+DSH_STAGE;
    unsigned left = r->x > DSH_INSET ? r->x : DSH_INSET;
    unsigned stride = 2*r->width;
    if (left >= right) { memset(dst, 0, stride*r->height); return; }
    unsigned pad = 2*(left-r->x), width = right-left;
    for (unsigned y = r->y; y < r->y+r->height; y++, dst+=stride) {
        if (y < DSH_INSET || y >= DSH_INSET+DSH_STAGE) { memset(dst, 0, stride); continue; }
        if (pad) memset(dst, 0, pad);
        if (stride > pad+2*width) memset(dst+pad+2*width, 0, stride-pad-2*width);
        unsigned sy = y-DSH_INSET, sx = left-DSH_INSET;
        pixels_t *out = (uintptr_t)dst & 1 ? c->compact->unaligned_row : (pixels_t *)(dst+pad);
        compact_row(c, sy, sx, sx+width, out);
        if (c->view.guides && sy > 0 && sy < DSH_STAGE-1 && sx <= 160 && 160 < sx+width) {
            unsigned i = 160-sx;
            out[i] = wire_sample(__builtin_bswap16(out[i]), 0x8f1b, 141);
        }
        if (c->view.guides && sy == 326) {
            for (unsigned i = 0; i < width; i++)
                if (sx+i && sx+i < DSH_STAGE-1)
                    out[i] = wire_sample(__builtin_bswap16(out[i]), 0xf6d3, 166);
        }
        if ((uintptr_t)dst & 1) memcpy(dst+pad, out, 2*width);
    }
}

void dsh_compact_paint(const baji_rect_t *r, uint8_t *dst, const dsh_canvas_t *c)
{
#ifdef DSH_PARALLEL
    struct dsh_compact *p = c->compact;
    if (p->worker && p->worker_core != xPortGetCoreID() && !((uintptr_t)dst & 1) &&
        r->height >= 4 && (unsigned)r->width*r->height >= 1200) {
        // Both cores read the same immutable selection and fill disjoint DMA
        // rows. Only the original board owner submits the completed band.
        baji_rect_t top = *r;
        top.height = r->height/2;
        p->work = (baji_rect_t){r->x, r->y+top.height, r->width, r->height-top.height};
        p->destination = dst + 2u*r->width*top.height;
        xTaskNotifyGive(p->worker);
        paint_rows(&top, dst, c);
        xSemaphoreTake(p->done, portMAX_DELAY);
        return;
    }
#endif
    paint_rows(r, dst, c);
}
