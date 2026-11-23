#include "deepseek_review.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_rom_crc.h"
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

bool dsk_art_open(dsk_art_t *out, const void *data, size_t size)
{
    if (!out) return false;
    *out = (dsk_art_t){0};
    const uint8_t *p = data;
    if (!p || size < 40 || memcmp(p, "DSK2", 4) != 0) return false;
    unsigned steps = le32(p + 4), frames = le32(p + 8);
    unsigned n2 = le32(p + 12), n4 = le32(p + 16), n8 = le32(p + 20);
    unsigned n16 = le32(p + 24), rows = le32(p + 28);
    if (!steps || steps > DSK_MAX_BUBBLE_STEPS || frames != 528 ||
        !n2 || n2 > 65536 || !rows || rows > 65536 ||
        !n4 || n4 > 0x1000000 || !n8 || n8 > 0x1000000 || !n16 || n16 > 0x1000000)
        return false;
    size_t maps = (size_t)steps * frames * DSK_COLUMNS, row_cells = rows * DSK_COLUMNS;
    size_t payload = maps * 2 + row_cells * 3 + (n16 + n8) * 12 + n4 * 8 + n2 * 8;
    if (size - 40 != payload || payload != le32(p + 32) ||
        checksum(p + 40, payload) != le32(p + 36)) return false;
    dsk_art_t a = {.maps = p + 40, .bubble_steps = steps};
    a.rows = a.maps + maps * 2;
    a.quad16 = a.rows + row_cells * 3;
    a.quad8 = a.quad16 + n16 * 12;
    a.quad4 = a.quad8 + n8 * 12;
    a.quad2 = a.quad4 + n4 * 8;
    if (!indices_fit(a.maps, maps, 2, rows) || !indices_fit(a.rows, row_cells, 3, n16) ||
        !indices_fit(a.quad16, n16 * 4, 3, n8) || !indices_fit(a.quad8, n8 * 4, 3, n4) ||
        !indices_fit(a.quad4, n4 * 4, 2, n2)) return false;
    *out = a;
    return true;
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
    c->art = art;
    c->map = NULL;
    c->upper = NULL;
    c->mix = 0;
    unsigned tile = 0;
    for (unsigned x = 0; x < DSK_SIDE; x++) {
        unsigned src = ((2 * x + 1) * DSK_SOURCE) / (2 * DSK_SIDE);
        c->source[x] = src;
        if (src / DSK_TILE == tile) c->tile_edges[tile++] = x;
    }
    c->tile_edges[DSK_COLUMNS] = DSK_SIDE;
}

bool dsk_canvas_select_blend(dsk_canvas_t *c, dsk_frame_t frame, unsigned mix)
{
    const uint8_t *map = frame_map(c->art, frame);
    if (!map || mix >= 8 || (mix && (unsigned)frame.bubble + 1 >= c->art->bubble_steps)) return false;
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
    return art->rows + le16(map + 2 * y) * (DSK_COLUMNS * 3);
}

bool dsk_canvas_damage_blend(const dsk_canvas_t *c, dsk_frame_t frame, unsigned mix,
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
                unsigned al = le24(a + 3*x), bl = le24(b + 3*x);
                unsigned at = a == ah ? al : le24(ah + 3*x);
                unsigned bt = b == bh ? bl : le24(bh + 3*x);
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
                const uint8_t *q4 = a->quad4 + le24(q8 + i4 * 3) * 8;
                for (unsigned i2 = 0; i2 < 4; i2++) {
                    unsigned y2 = y4 + (i2 / 2) * 2, x2 = x4 + (i2 % 2) * 2;
                    if (y2 + 1 < first_y || y2 > last_y) continue;
                    const uint8_t *pair = a->quad2 + le16(q4 + i2 * 2) * 8;
                    memcpy(tile[y2] + x2 * 2, pair, 4);
                    memcpy(tile[y2+1] + x2 * 2, pair + 4, 4);
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

void dsk_paint(const baji_rect_t *band, uint8_t *out, void *context)
{
    dsk_canvas_t *c = context;
    if (!band || !out || !c || !c->map ||
        (unsigned)band->x + band->width > DSK_SIDE ||
        (unsigned)band->y + band->height > DSK_SIDE) return;
    unsigned x_end = band->x + band->width, y_end = band->y + band->height;
    unsigned stride = band->width * 2;
    uint8_t tile[16][32];
    for (unsigned y = band->y; y < y_end;) {
        unsigned ty = c->source[y] / DSK_TILE;
        unsigned bottom = c->tile_edges[ty + 1];
        if (bottom > y_end) bottom = y_end;
        const uint8_t *row = tile_row(c->art, c->map, ty);
        const uint8_t *upper = c->mix ? tile_row(c->art, c->upper, ty) : row;
        unsigned previous = UINT32_MAX, previous_high = UINT32_MAX;
        for (unsigned x = band->x; x < x_end;) {
            unsigned tx = c->source[x] / DSK_TILE, id = le24(row + 3 * tx);
            unsigned high = row == upper ? id : le24(upper + 3 * tx);
            unsigned end = c->tile_edges[tx + 1];
            if (end > x_end) end = x_end;
            if (id != previous || high != previous_high) {
                unsigned first = c->source[y] % 16, last = c->source[bottom-1] % 16;
                decode_tile(c->art, id, first, last, tile);
                if (id != high) {
                    decode_tile(c->art, high, first, last, c->blend_tile);
                    blend_rows(tile, c->blend_tile, first, last, c->mix);
                }
                previous = id;
                previous_high = high;
            }
            for (unsigned py = y; py < bottom; py++) {
                const uint8_t *line = tile[c->source[py] % DSK_TILE];
                uint8_t *dst = out + (py - band->y) * stride + (x - band->x) * 2;
                for (unsigned px = x; px < end; px++) {
                    unsigned u = (c->source[px] % DSK_TILE) * 2;
                    *dst++ = line[u];
                    *dst++ = line[u + 1];
                }
            }
            x = end;
        }
        y = bottom;
    }
}
