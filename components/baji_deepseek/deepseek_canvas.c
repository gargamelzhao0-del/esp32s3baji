#include "deepseek_review.h"
#include <string.h>
#include <stdlib.h>
#ifdef ESP_PLATFORM
#include "esp_rom_crc.h"
#include "esp_heap_caps.h"
#endif

static unsigned le16(const uint8_t *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return le16(p) | ((uint32_t)le16(p + 2) << 16);
}

static unsigned le24(const uint8_t *p)
{
    return le16(p) | ((unsigned)p[2] << 16);
}

static uint32_t checksum(const uint8_t *data, size_t size)
{
#ifdef ESP_PLATFORM
    return esp_rom_crc32_le(0, data, size);
#else
    uint32_t crc = UINT32_MAX;
    while (size--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xedb88320u);
    }
    return ~crc;
#endif
}

static bool indices_fit(const uint8_t *p, size_t count, unsigned width, unsigned limit)
{
    for (size_t i = 0; i < count; i++, p += width) {
        unsigned index = width == 2 ? le16(p) : le24(p);
        if (index >= limit) return false;
    }
    return true;
}

enum { MOTION_POSES = 108, SPRITE_W = 138, SPRITE_H = 132, MESH_W = 19, MESH_H = 18 };

static bool runs_fit(const uint8_t *block, size_t bytes, unsigned count, bool sprite)
{
    unsigned prefix = (count + 1) * 4;
    if (bytes < prefix || le32(block) || le32(block + count*4) != bytes - prefix) return false;
    const uint8_t *base = block + prefix;
    for (unsigned i = 0; i < count; i++) {
        unsigned start = le32(block + i*4), end = le32(block + (i+1)*4);
        if (start >= end || end > bytes - prefix) return false;
        const uint8_t *p = base + start, *last = base + end;
        unsigned total = 0, expected = sprite ? SPRITE_W * SPRITE_H : 184 * 184;
        while (p < last) {
            unsigned n;
            if (sprite) {
                unsigned token = *p++;
                if ((token & 63) > 40) return false;
                n = (token >> 6) + 1;
                if (n == 4) {
                    if (p == last) return false;
                    n += *p++;
                }
            } else {
                if (last - p < 3 || !*p) return false;
                n = *p;
                p += 3;
            }
            total += n;
            if (total > expected) return false;
        }
        if (total != expected) return false;
    }
    return true;
}

bool dsk_art_open(dsk_art_t *out, const void *data, size_t size)
{
    if (!out) return false;
    *out = (dsk_art_t){0};
    const uint8_t *p = data;
    if (!p || size < 40) return false;
    bool motion = !memcmp(p, "DSK3", 4);
    unsigned header = motion ? 56 : 40;
    if ((!motion && memcmp(p, "DSK2", 4)) || size < header) return false;
    unsigned steps = le32(p + 4), frames = le32(p + 8);
    unsigned n2 = le32(p + 12), n4 = le32(p + 16), n8 = le32(p + 20);
    unsigned n16 = le32(p + 24), rows = le32(p + 28);
    if (!steps || steps > DSK_MAX_BUBBLE_STEPS || frames != 528 ||
        !n2 || n2 > 65536 || !rows || rows > 65536 ||
        !n4 || n4 > 0x1000000 || !n8 || n8 > 0x1000000 || !n16 || n16 > 0x1000000)
        return false;
    unsigned triples = motion ? le32(p+40) : 0, pairs = motion ? le32(p+44) : 0;
    unsigned sprite_bytes = motion ? le32(p+48) : 0, plate_bytes = motion ? le32(p+52) : 0;
    if (triples > 65536 || pairs > 65536 || (motion && (!pairs || !triples))) return false;
    size_t maps = (size_t)steps * frames * DSK_COLUMNS;
    size_t cells = (size_t)rows * (motion ? 8 : DSK_COLUMNS);
    size_t mask_count = DSK_CLIPS * steps * DSK_COLUMNS * DSK_COLUMNS;
    uint64_t payload = maps*2 + cells*(motion ? 2 : 3) + triples*9 + (n16+n8)*12 +
                       n4*(motion ? 4 : 8) + pairs*4 + n2*8;
    if (motion) payload += 82ull + sprite_bytes + MOTION_POSES*(1+MESH_W*MESH_H*2+7*DSK_COLUMNS*3) +
                           plate_bytes + mask_count*2;
    if (payload > size - header || size - header != le32(p+32)) return false;
    size_t remaining = size - header - (size_t)payload;
    if ((!motion && remaining) || (motion && (!remaining || remaining%256 || remaining/256>65536)) ||
        checksum(p+header, size-header) != le32(p+36)) return false;
    dsk_art_t a = {.maps = p+header, .bubble_steps = steps};
    a.rows = a.maps + maps*2;
    const uint8_t *q = a.rows + cells*(motion ? 2 : 3);
    if (motion) { a.row_triples = q; q += triples*9; }
    a.quad16 = q;
    a.quad8 = a.quad16 + n16*12;
    a.quad4 = a.quad8 + n8*12;
    q = a.quad4 + n4*(motion ? 4 : 8);
    if (motion) { a.quad_pairs = q; q += pairs*4; }
    a.quad2 = q;
    if (!indices_fit(a.maps, maps, 2, rows) ||
        !indices_fit(a.rows, cells, motion ? 2 : 3, motion ? triples : n16) ||
        !indices_fit(a.quad16, n16*4, 3, n8) || !indices_fit(a.quad8, n8*4, 3, n4) ||
        !indices_fit(a.quad4, n4*(motion ? 2 : 4), 2, motion ? pairs : n2)) return false;
    if (motion) {
        a.palette = a.quad2 + n2*8;
        a.sprites = a.palette + 82;
        a.mesh = a.sprites + sprite_bytes;
        /* First byte per key identifies whether its successor uses the same
         * original painting. Every subframe samples only the current key,
         * including the last sample before a different hand/prop painting. */
        for (unsigned i = 0; i < MOTION_POSES; i++) if (a.mesh[i] > 1) return false;
        a.motion_masks = a.mesh + MOTION_POSES*(1+MESH_W*MESH_H*2);
        a.plates = a.motion_masks + MOTION_POSES*7*DSK_COLUMNS*3;
        for (size_t j = 2; j < MOTION_POSES*7*DSK_COLUMNS*3; j += 3)
            if (a.motion_masks[j] & 128) return false;
        a.mask_maps = a.plates + plate_bytes;
        a.mask_tiles = a.mask_maps + mask_count*2;
        a.clear_tile = (unsigned)(remaining/256-1);
        for (unsigned j = 0; j < 256; j++) {
            if (a.mask_tiles[j] || a.mask_tiles[a.clear_tile*256+j] != 32) return false;
        }
        for (unsigned j = 1; j <= 40; j++) {
            if (!a.palette[j*2] && !a.palette[j*2+1]) return false;
        }
        if (!indices_fit(a.row_triples, triples*3, 3, n16) ||
            !indices_fit(a.quad_pairs, pairs*2, 2, n2) ||
            !runs_fit(a.sprites, sprite_bytes, MOTION_POSES, true) ||
            !runs_fit(a.plates, plate_bytes, DSK_PHASES, false) ||
            !indices_fit(a.mask_maps, mask_count, 2, (unsigned)(remaining/256))) return false;
        for (size_t i = 0; i < remaining; i++) if (a.mask_tiles[i] > 32) return false;
    }
    *out = a;
    return true;
}

unsigned dsk_art_motion_steps(const dsk_art_t *art)
{
    return art && art->mesh ? 8 : 1;
}

void dsk_art_close(dsk_art_t *art)
{
    if (art) *art = (dsk_art_t){0};
}

static const uint8_t *frame_map(const dsk_art_t *art, dsk_frame_t frame)
{
    static const uint8_t base[DSK_CLIPS] = {0, 8, 20, 30, 38, 46};
    if (!art || !art->maps || frame.item >= DSK_ITEMS || frame.phase >= DSK_PHASES ||
        frame.close > 1 || frame.bubble >= art->bubble_steps) return NULL;
    unsigned pose;
    if (frame.item < DSK_CLIPS) {
        if (frame.frame >= dsk_clip_frames(frame.item)) return NULL;
        pose = base[frame.item] + frame.frame;
    } else {
        if (frame.frame != 0) return NULL;
        pose = 54 + frame.item - DSK_CLIPS;
    }
    size_t index = ((frame.phase * 2 + frame.close) * 66 + pose) *
                   art->bubble_steps + frame.bubble;
    return art->maps + index * DSK_COLUMNS * 2;
}

void dsk_canvas_init(dsk_canvas_t *c, const dsk_art_t *art)
{
    c->motion = NULL;
    c->subframe = 0;
    c->selected = (dsk_frame_t){0};
    c->art = art;
    c->map = NULL;
    c->upper = NULL;
    c->mix = 0;
    unsigned tile = 0;
    for (unsigned x = 0; x < DSK_SIDE; x++) {
        unsigned src = ((2 * x + 1) * DSK_SOURCE) / (2 * DSK_SIDE);
        c->source[x] = src;
        c->tile_x[x] = src % DSK_TILE;
        if (src / DSK_TILE == tile) c->tile_edges[tile++] = x;
    }
    c->tile_edges[DSK_COLUMNS] = DSK_SIDE;
}

bool dsk_canvas_select_blend(dsk_canvas_t *c, dsk_frame_t frame, unsigned mix)
{
    const uint8_t *map = frame_map(c->art, frame);
    if (!map || mix >= 8 || (mix && (unsigned)frame.bubble + 1 >= c->art->bubble_steps)) return false;
    c->selected = frame;
    c->subframe = 0;
    c->map = map;
    c->upper = mix ? map + DSK_COLUMNS * 2 : map;
    c->mix = mix;
    return true;
}

bool dsk_canvas_select(dsk_canvas_t *c, dsk_frame_t frame)
{
    return dsk_canvas_select_blend(c, frame, 0);
}

static const uint8_t *tile_row(const dsk_art_t *art, const uint8_t *map, unsigned y)
{
    return art->rows + le16(map + 2 * y) * (art->row_triples ? 16 : DSK_COLUMNS * 3);
}

static inline unsigned tile_index(const dsk_art_t *a, const uint8_t *row, unsigned x)
{
    if (a->row_triples) return le24(a->row_triples + le16(row + (x/3)*2)*9 + x%3*3);
    return le24(row + x*3);
}

static bool anchor_damage(const dsk_canvas_t *c, dsk_frame_t frame, unsigned mix,
                             dsk_damage_fn mark, void *context)
{
    const uint8_t *next = frame_map(c->art, frame);
    if (!mark || !next || mix >= 8 || (mix && (unsigned)frame.bubble + 1 >= c->art->bubble_steps))
        return false;
    if (!c->map) {
        mark(context, (baji_rect_t){0, 0, DSK_SIDE, DSK_SIDE});
        return true;
    }
    if (next == c->map && mix == c->mix) return true;
    const uint8_t *upper = mix ? next + DSK_COLUMNS * 2 : next;
    for (unsigned y = 0; y < DSK_COLUMNS; y++) {
        const uint8_t *a = tile_row(c->art, c->map, y);
        const uint8_t *b = tile_row(c->art, next, y);
        const uint8_t *ah = c->mix ? tile_row(c->art, c->upper, y) : a;
        const uint8_t *bh = mix ? tile_row(c->art, upper, y) : b;
        if (a == b && ah == bh && (a == ah || mix == c->mix)) continue;
        unsigned left = DSK_COLUMNS;
        for (unsigned x = 0; x <= DSK_COLUMNS; x++) {
            bool dirty = false;
            if (x < DSK_COLUMNS) {
                unsigned al = tile_index(c->art, a, x), bl = tile_index(c->art, b, x);
                unsigned at = a == ah ? al : tile_index(c->art, ah, x);
                unsigned bt = b == bh ? bl : tile_index(c->art, bh, x);
                dirty = al != bl || at != bt || (mix != c->mix && al != at);
            }
            if (dirty && left == DSK_COLUMNS) left = x;
            if (!dirty && left < DSK_COLUMNS) {
                mark(context, (baji_rect_t){c->tile_edges[left], c->tile_edges[y],
                     c->tile_edges[x] - c->tile_edges[left],
                     c->tile_edges[y + 1] - c->tile_edges[y]});
                left = DSK_COLUMNS;
            }
        }
    }
    return true;
}

bool dsk_canvas_damage_blend(const dsk_canvas_t *c, dsk_frame_t frame, unsigned mix,
                             dsk_damage_fn mark, void *context)
{
    return dsk_canvas_damage_motion(c,frame,mix,0,mark,context);
}

bool dsk_canvas_damage(const dsk_canvas_t *c, dsk_frame_t frame, dsk_damage_fn mark, void *context)
{
    return dsk_canvas_damage_blend(c, frame, 0, mark, context);
}

/* Decode only touched pairs of source scanlines in a 16x16 tile. Nearest
 * expansion then reads the stack scratchpad rather than chasing an external
 * flash/PSRAM dictionary for each transmitted pixel. */
static void decode_tile(const dsk_art_t *a, unsigned id, unsigned first_y,
                        unsigned last_y, uint8_t tile[16][32])
{
    const uint8_t *q16 = a->quad16 + id * 12;
    for (unsigned y8 = first_y & ~7u; y8 <= last_y; y8 += 8) {
        for (unsigned x8 = 0; x8 < 16; x8 += 8) {
            unsigned half = (y8 / 8) * 2 + x8 / 8;
            const uint8_t *q8 = a->quad8 + le24(q16 + half * 3) * 12;
            for (unsigned i4 = 0; i4 < 4; i4++) {
                unsigned y4 = y8 + (i4 / 2) * 4, x4 = x8 + (i4 % 2) * 4;
                if (y4 + 3 < first_y || y4 > last_y) continue;
                const uint8_t *q4 = a->quad4 + le24(q8 + i4 * 3) * (a->quad_pairs ? 4 : 8);
                for (unsigned i2 = 0; i2 < 4; i2++) {
                    unsigned y2 = y4 + (i2 / 2) * 2, x2 = x4 + (i2 % 2) * 2;
                    if (y2 + 1 < first_y || y2 > last_y) continue;
                    const uint8_t *entry = a->quad_pairs ?
                        a->quad_pairs + le16(q4 + i2/2*2)*4 + i2%2*2 : q4 + i2*2;
                    const uint8_t *pair = a->quad2 + le16(entry) * 8;
                    for (unsigned byte = 0; byte < 4; byte++) {
                        tile[y2][x2*2+byte] = pair[byte];
                        tile[y2+1][x2*2+byte] = pair[4+byte];
                    }
                }
            }
        }
    }
}

/* Blend the three RGB565 channels independently, with rounding to the closest
 * code. Keep red/blue separated by 16 bits so each lane can add safely. */
static void blend_rows(uint8_t lo[16][32], const uint8_t hi[16][32],
                       unsigned first, unsigned last, unsigned mix)
{
    unsigned low_weight = 8 - mix;
    for (unsigned y = first; y <= last; y++) {
        for (unsigned x = 0; x < 32; x += 2) {
            uint16_t a = (uint16_t)lo[y][x] << 8 | lo[y][x+1];
            uint16_t b = (uint16_t)hi[y][x] << 8 | hi[y][x+1];
            if (a == b) continue;
            unsigned a_rb = ((unsigned)(a & 0xf800) << 5) | (a & 31);
            unsigned b_rb = ((unsigned)(b & 0xf800) << 5) | (b & 31);
            unsigned rb = ((a_rb * low_weight + b_rb * mix + 0x40004) >> 3) & 0x1f001f;
            unsigned g = (((a >> 5 & 63) * low_weight + (b >> 5 & 63) * mix + 4) >> 3);
            unsigned color = ((rb >> 5) & 0xf800) | (g << 5) | (rb & 31);
            lo[y][x] = color >> 8;
            lo[y][x+1] = color;
        }
    }
}

/* Mesh nodes correspond to the original 680px rig, expressed in eighths of
 * atlas pixels. The nearest sample stays a single authored palette index.
 * The browser canvas doubles a 138x132 frame before it reaches our rescaler. */
enum { SPRITE_X = 46, MASK_CELLS = DSK_COLUMNS * DSK_COLUMNS };
struct motion_tile {
    uint32_t low, high, pose;
    uint8_t mix, warp_mix;
    _Alignas(4) uint8_t pixels[16][32], displaced[16][32];
};
typedef uint16_t wire_pixel_t __attribute__((may_alias));
enum { PAD = 16, TEX_STRIDE = SPRITE_W+2*PAD };
struct motion_source {
    uint16_t sprite[(SPRITE_H+2*PAD)*TEX_STRIDE], colors[41];
    int16_t mesh_rows[SPRITE_H*2][MESH_W*2];
};
struct dsk_motion_work {
    struct motion_source *hot;
    unsigned warped_rows[DSK_COLUMNS];
    uint16_t bg[184*184];
    int source_id, phase_id;
    struct motion_tile tiles[MASK_CELLS];
};

static unsigned motion_pose(dsk_frame_t f)
{
    static const unsigned bases[] = {0, 8, 20, 30, 38, 46};
    return f.close*54 + bases[f.item] + f.frame;
}

static unsigned sprite_top(dsk_frame_t f) { return f.close ? 44 : 36; }

static inline const int8_t *field_for(const dsk_art_t *a, unsigned id)
{
    return (const int8_t *)(a->mesh + MOTION_POSES + id*MESH_W*MESH_H*2);
}

static unsigned be_color(const uint8_t *p) { return (unsigned)p[0]*256+p[1]; }

static void unpack_pose(struct dsk_motion_work *w, const dsk_art_t *a, unsigned pose)
{
    if (w->source_id == (int)pose) return;
    const uint8_t *p = a->sprites + (MOTION_POSES+1)*4 + le32(a->sprites + 4*pose);
    struct motion_source *hot = w->hot;
    uint16_t *dst = hot->sprite + PAD*TEX_STRIDE+PAD;
    unsigned col = 0, drawn = 0;
    while (drawn < SPRITE_W*SPRITE_H) {
        unsigned token = *p++, n = (token >> 6) + 1;
        if (n == 4) n += *p++;
        uint16_t color = hot->colors[token & 63];
        drawn += n;
        while (n) {
            unsigned k = n < SPRITE_W-col ? n : SPRITE_W-col;
            n -= k;
            col += k;
            while (k--) *dst++ = color;
            if (col == SPRITE_W) { dst += PAD*2; col = 0; }
        }
    }
    const int8_t *field = field_for(a,pose);
    for (unsigned y = 0; y < SPRITE_H*2; y++) {
        const int8_t *nodes = field + y/16*MESH_W*2;
        int fy = (int)y%16;
        for (unsigned x = 0; x < MESH_W*2; x++) {
            hot->mesh_rows[y][x] = nodes[x]*(16-fy) + nodes[MESH_W*2+x]*fy;
        }
    }
    w->source_id = pose;
}

static void unpack_plate(struct dsk_motion_work *w, const dsk_art_t *a, unsigned phase)
{
    if (w->phase_id == (int)phase) return;
    const uint8_t *p = a->plates + (DSK_PHASES+1)*4 + le32(a->plates + phase*4);
    uint16_t *dst = w->bg, *end = dst + 184*184;
    while (dst < end) {
        unsigned n = *p++, color = le16(p);
        p += 2;
        while (n--) *dst++ = color;
    }
    w->phase_id = phase;
}

/* The pack stores exact deltas for successive fractional poses. Their union
 * bounds any skipped interval and has a fixed work bound of seven 23-bit rows. */
static unsigned interval_tiles(const dsk_art_t *art, dsk_frame_t f, unsigned lo, unsigned hi, unsigned y)
{
    if (lo == hi) return 0;
    const uint8_t *row = art->motion_masks + ((motion_pose(f)*7+lo)*DSK_COLUMNS+y)*3;
    unsigned bits = 0;
    for (unsigned sub = lo; sub < hi; sub++, row += DSK_COLUMNS*3) bits |= le24(row);
    return bits;
}

bool dsk_canvas_select_motion(dsk_canvas_t *c, dsk_frame_t f, unsigned mix, unsigned sub)
{
    if (sub >= dsk_art_motion_steps(c->art) || (sub && f.item >= DSK_CLIPS) ||
        !frame_map(c->art, f) || mix >= 8 || (mix && (unsigned)f.bubble+1 >= c->art->bubble_steps))
        return false;
    if (sub) {
        if (!c->motion) {
#ifdef ESP_PLATFORM
            c->motion = heap_caps_calloc(1, sizeof(*c->motion), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
            c->motion = calloc(1, sizeof(*c->motion));
#endif
            if (!c->motion) return false;
#ifdef ESP_PLATFORM
            c->motion->hot = heap_caps_calloc(1,sizeof(*c->motion->hot),MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!c->motion->hot)
                c->motion->hot = heap_caps_calloc(1,sizeof(*c->motion->hot),MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
            c->motion->hot = calloc(1,sizeof(*c->motion->hot));
#endif
            if (!c->motion->hot) { dsk_canvas_release_motion(c); return false; }
            c->motion->source_id = c->motion->phase_id = -1;
            for (unsigned i = 1; i < 41; i++) c->motion->hot->colors[i] = le16(c->art->palette+i*2);
        }
        unpack_pose(c->motion, c->art, motion_pose(f));
        unpack_plate(c->motion, c->art, f.phase);
        for (unsigned y = 0; y < DSK_COLUMNS; y++)
            c->motion->warped_rows[y] = interval_tiles(c->art,f,0,sub,y);
    }
    dsk_canvas_select_blend(c, f, mix);
    c->subframe = sub;
    return true;
}

void dsk_canvas_release_motion(dsk_canvas_t *c)
{
    if (!c) return;
    if (c->motion) free(c->motion->hot);
    free(c->motion);
    c->motion = NULL;
    if (c->subframe) c->map = c->upper = NULL;
    c->subframe = 0;
}

static unsigned cloud_damage(const dsk_art_t *art, dsk_frame_t a, unsigned mix_a,
                             dsk_frame_t b, unsigned mix_b, unsigned y, unsigned bits)
{
    const uint8_t *row = art->mask_maps + (a.item*art->bubble_steps*MASK_CELLS+y*DSK_COLUMNS)*2;
    const uint8_t *old = row + a.bubble*MASK_CELLS*2, *next = row + b.bubble*MASK_CELLS*2;
    unsigned changed = 0;
    while (bits) {
        unsigned x = (unsigned)__builtin_ctz(bits);
        unsigned low_a = le16(old+2*x), low_b = le16(next+2*x);
        unsigned high_a = mix_a ? le16(old+(MASK_CELLS+x)*2) : low_a;
        unsigned high_b = mix_b ? le16(next+(MASK_CELLS+x)*2) : low_b;
        if (low_a != low_b || high_a != high_b || (mix_a != mix_b && low_a != high_a))
            changed |= 1u << x;
        bits &= bits-1;
    }
    return changed;
}

bool dsk_canvas_damage_motion(const dsk_canvas_t *c, dsk_frame_t f, unsigned mix, unsigned sub,
                              dsk_damage_fn mark, void *context)
{
    if (sub >= dsk_art_motion_steps(c->art) || (sub && f.item >= DSK_CLIPS) ||
        !anchor_damage(c, f, mix, mark, context)) return false;
    if (!c->map || (!sub && !c->subframe)) return true;
    if (c->subframe == sub && !memcmp(&f,&c->selected,sizeof(f)) && mix == c->mix) return true;
    bool same_pose = f.item == c->selected.item && f.frame == c->selected.frame &&
                     f.close == c->selected.close && f.phase == c->selected.phase;
    bool cloud_changed = f.bubble != c->selected.bubble || mix != c->mix;
    unsigned first = sub < c->subframe ? sub : c->subframe;
    unsigned last = sub > c->subframe ? sub : c->subframe;
    for (unsigned y = 0; y < DSK_COLUMNS; y++) {
        unsigned old = c->subframe ? c->motion->warped_rows[y] : 0;
        unsigned bits;
        if (same_pose) {
            bits = interval_tiles(c->art,f,first,last,y);
            /* Include changed transmission over a stationary, displaced texel
             * even where the browser's original texel was transparent. */
            if (cloud_changed && old)
                bits |= cloud_damage(c->art,c->selected,c->mix,f,mix,y,old);
        } else {
            bits = old | interval_tiles(c->art,f,0,sub,y);
        }
        while (bits) {
            unsigned left = (unsigned)__builtin_ctz(bits);
            unsigned span = (unsigned)__builtin_ctz(~(bits >> left));
            unsigned right = left + span;
            mark(context,(baji_rect_t){c->tile_edges[left],c->tile_edges[y],
                 c->tile_edges[right]-c->tile_edges[left], c->tile_edges[y+1]-c->tile_edges[y]});
            bits &= ~(((1u << span)-1) << left);
        }
    }
    return true;
}

/* Change the RGB565 anchor by the transmitted fraction of the underlying
 * sprite difference. This keeps the captured cloud, bezel, icon and HUD in
 * front, including their anti-aliased outlines and shadows. */
static inline int clamp_color(int v, int max) { return v < 0 ? 0 : v > max ? max : v; }
static inline unsigned replace_under(unsigned actual, unsigned old, unsigned color, unsigned t)
{
    if (t == 32) return color;
    int r = (int)(actual>>11) + ((((int)(color>>11)-(int)(old>>11))*(int)t+16) >> 5);
    int g = (int)(actual>>5 & 63) + ((((int)(color>>5 & 63)-(int)(old>>5 & 63))*(int)t+16) >> 5);
    int b = (int)(actual & 31) + ((((int)(color & 31)-(int)(old & 31))*(int)t+16) >> 5);
    return (unsigned)(clamp_color(r,31)*2048 + clamp_color(g,63)*32 + clamp_color(b,31));
}

/* Q14 coordinates sample one authored texel. A repeated eight-pixel lattice
 * records fractional edge coverage in the two slow loops without filtering
 * together ink, eyes or props. It stays inside the original texel at rest. */
static inline int sample_bias(unsigned native)
{
    static const int16_t bias[8] = {-3328, 768, -1280, 2816, -2304, 1792, -256, 3840};
    return bias[native & 7];
}

static void warp_tile(dsk_canvas_t *c, unsigned tx, unsigned ty, uint8_t pixels[16][32])
{
    const dsk_frame_t f = c->selected;
    int top = (int)sprite_top(f);
    int x0 = (int)tx*16 - SPRITE_X, y0 = (int)ty*16 - top;
    unsigned sub = c->subframe, steps = c->art->bubble_steps;
    struct dsk_motion_work *w = c->motion;
    struct motion_source *hot = w->hot;
    const uint8_t *map = c->art->mask_maps + ((f.item*steps + f.bubble)*MASK_CELLS + ty*DSK_COLUMNS + tx)*2;
    unsigned lo = le16(map), hi = c->mix ? le16(map + MASK_CELLS*2) : lo;
    if (!lo && !hi) return;   /* sorted calibration dictionary begins with zero */
    bool clear = lo == c->art->clear_tile && hi == lo;
    const uint8_t *alo = c->art->mask_tiles + lo*256, *ahi = c->art->mask_tiles + hi*256;
    bool gentle = f.item == 0 || f.item == 5;
    for (int py = 0; py < 16; py++) {
        int y = y0+py;
        if ((unsigned)y >= SPRITE_H*2) continue;
        const int16_t *row = hot->mesh_rows[y];
        const uint16_t *bg = w->bg + (ty*16+py)/2*184 + tx*8;
        const uint16_t *src = hot->sprite + (y/2+PAD)*TEX_STRIDE + PAD;
        int px = x0 < 0 ? -x0 : 0;
        while (px < 16 && x0+px < SPRITE_W*2) {
            int x = x0+px, fx = x%16;
            const int16_t *a = row + x/16*2;
            int dx = (a[2]-a[0])*(int)sub, dy = (a[3]-a[1])*(int)sub;
            int sx = x*8192 + 4096 + a[0]*16*(int)sub + dx*fx;
            int sy = y*8192 + 4096 + a[1]*16*(int)sub + dy*fx;
            dx += 8192;
            int end = px + 16-fx;
            if (end > 16) end = 16;
            if (x0+end > SPRITE_W*2) end = SPRITE_W*2-x0;
            /* Padding provides transparent texture coordinates at the largest
             * legal Q3 node displacement. Textures and output use wire words. */
            if (clear && !gentle) {
                for (; px < end; px++,sx+=dx,sy+=dy) {
                    uint16_t value = hot->sprite[((sy>>14)+PAD)*TEX_STRIDE + (sx>>14)+PAD];
                    ((wire_pixel_t *)pixels[py])[px] = value ? value : bg[px/2];
                }
            } else {
                for (; px < end; px++,sx+=dx,sy+=dy) {
                    int bx = gentle ? sample_bias((unsigned)y/2) : 0;
                    int by = gentle ? sample_bias((unsigned)(x0+px)/2+3) : 0;
                    unsigned current = src[(x0+px)/2];
                    unsigned shifted = hot->sprite[(((sy+by)>>14)+PAD)*TEX_STRIDE + ((sx+bx)>>14)+PAD];
                    if (current == shifted) continue;
                    unsigned at = py*16+px, alpha = alo[at];
                    if (hi != lo) alpha = (alpha*(8-c->mix) + ahi[at]*c->mix+4)>>3;
                    if (!alpha) continue;
                    unsigned color = shifted ? shifted : bg[px/2];
                    if (alpha < 32) {
                        unsigned old = current ? current : bg[px/2];
                        unsigned actual = be_color(pixels[py]+px*2);
                        color = __builtin_bswap16(replace_under(actual,__builtin_bswap16(old),
                                                               __builtin_bswap16(color),alpha));
                    }
                    ((wire_pixel_t *)pixels[py])[px] = color;
                }
            }
        }
    }
}




typedef uint16_t scan_pixel_t __attribute__((may_alias));

static inline void expand_four(scan_pixel_t *out, const scan_pixel_t *src, const uint8_t *column)
{
    out[0] = src[column[0]];
    out[1] = src[column[1]];
    out[2] = src[column[2]];
    out[3] = src[column[3]];
}

/* A complete 16-pixel source tile covers 20 or 21 physical columns.
 * Supply the shared x map once per tile and keep complete wire-order words
 * in registers. The odd-address path is only needed for arbitrary callers. */
static inline void expand_scanline(uint8_t *dst, const uint8_t *line,
                                   const uint8_t *column, unsigned width)
{
    if ((uintptr_t)dst & 1) {
        for (unsigned i = 0; i < width; i++) {
            unsigned u = 2*column[i];
            dst[2*i] = line[u];
            dst[2*i+1] = line[u+1];
        }
        return;
    }
    const scan_pixel_t *src = (const scan_pixel_t *)line;
    scan_pixel_t *out = (scan_pixel_t *)dst;
    if (width >= 20) {
        expand_four(out, src, column);
        expand_four(out+4, src, column+4);
        expand_four(out+8, src, column+8);
        expand_four(out+12, src, column+12);
        expand_four(out+16, src, column+16);
        if (width == 21) out[20] = src[column[20]];
    } else {
        for (unsigned i = 0; i < width; i++) out[i] = src[column[i]];
    }
}

void dsk_paint(const baji_rect_t *band, uint8_t *out, void *context)
{
    dsk_canvas_t *c = context;
    if (!band || !out || !c || !c->map ||
        (unsigned)band->x + band->width > DSK_SIDE ||
        (unsigned)band->y + band->height > DSK_SIDE) return;
    unsigned x_end = band->x + band->width, y_end = band->y + band->height;
    unsigned stride = band->width * 2;
    _Alignas(4) uint8_t tile[16][32];
    for (unsigned y = band->y; y < y_end;) {
        unsigned ty = c->source[y] / DSK_TILE;
        unsigned bottom = c->tile_edges[ty + 1];
        if (bottom > y_end) bottom = y_end;
        const uint8_t *row = tile_row(c->art, c->map, ty);
        const uint8_t *upper = c->mix ? tile_row(c->art, c->upper, ty) : row;
        unsigned previous = UINT32_MAX, previous_high = UINT32_MAX;
        for (unsigned x = band->x; x < x_end;) {
            unsigned tx = c->source[x] / DSK_TILE, id = tile_index(c->art, row, tx);
            unsigned high = row == upper ? id : tile_index(c->art, upper, tx);
            unsigned end = c->tile_edges[tx + 1];
            if (end > x_end) end = x_end;
            const uint8_t (*raster)[32] = tile;
            if (c->motion) {
                struct motion_tile *entry = &c->motion->tiles[ty*DSK_COLUMNS+tx];
                bool warped = c->subframe && (c->motion->warped_rows[ty] & (1u << tx));
                unsigned pose = warped ? 2 + c->subframe + 8*(motion_pose(c->selected) +
                    MOTION_POSES*(c->selected.phase + 4*c->selected.bubble)) : 1;
                unsigned mix = (warped || high != id) ? c->mix : 0;
                bool base_changed = !entry->pose || entry->low != id || entry->high != high ||
                                    (id != high && entry->mix != mix);
                if (base_changed) {
                    decode_tile(c->art,id,0,15,entry->pixels);
                    if (id != high) {
                        decode_tile(c->art,high,0,15,c->blend_tile);
                        blend_rows(entry->pixels,c->blend_tile,0,15,c->mix);
                    }
                    entry->low = id;
                    entry->high = high;
                    entry->mix = mix;
                }
                if (warped && (base_changed || entry->pose != pose || entry->warp_mix != mix)) {
                    memcpy(entry->displaced,entry->pixels,sizeof(entry->pixels));
                    warp_tile(c,tx,ty,entry->displaced);
                }
                entry->warp_mix = mix;
                entry->pose = pose;
                raster = warped ? entry->displaced : entry->pixels;
            } else if (id != previous || high != previous_high) {
                unsigned first = c->source[y] % 16, last = c->source[bottom-1] % 16;
                decode_tile(c->art, id, first, last, tile);
                if (id != high) {
                    decode_tile(c->art, high, first, last, c->blend_tile);
                    blend_rows(tile, c->blend_tile, first, last, c->mix);
                }
                previous = id;
                previous_high = high;
            }
            unsigned width = end - x;
            uint8_t *dst = out + (y - band->y)*stride + (x - band->x)*2;
            for (unsigned py = y; py < bottom; py++, dst += stride) {
                const uint8_t *line = raster[c->source[py] % DSK_TILE];
                expand_scanline(dst, line, c->tile_x + x, width);
            }
            x = end;
        }
        y = bottom;
    }
}
