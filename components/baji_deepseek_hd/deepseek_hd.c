#include "baji_deepseek_hd.h"
#include "hd_playback.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "miniz.h"
#include <limits.h>
#include <string.h>
#if defined(ESP_PLATFORM) && !defined(BAJI_DEEPSEEK_EXTERNAL_ASSET)
#include "esp_partition.h"
#define DSH_FLASH_PARTITION 1
#endif

enum { ENTRIES = 22, FIRST_SHEET = 18, HEADER = 12, ENTRY = 16, MISSING = UINT16_MAX };
enum { MOTION_POSES = 17, LEVELS = 16, MOTION_COUNT = MOTION_POSES * LEVELS,
       MOTION_ENTRY = 12, MOTION_DIR = HEADER + MOTION_COUNT * MOTION_ENTRY,
       MOTION_RAW = 2 * (DSH_STAGE * DSH_STAGE + 2 * DSH_STAGE) };

static unsigned read16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static uint32_t read32(const uint8_t *p) { return read16(p) | ((uint32_t)read16(p + 2) << 16); }

bool dsh_art_open(dsh_art_t *art, const void *bytes, size_t size)
{
    if (!art) return false;
    art->bytes = NULL;
    const uint8_t *p = bytes;
    size_t end = HEADER + ENTRIES * ENTRY;
    if (!p || size < end || size > UINT32_MAX || memcmp(p, "HDS1", 4) ||
        read32(p + 4) != ENTRIES || read32(p + 8) != esp_rom_crc32_le(0, p + HEADER, size - HEADER))
        return false;
    for (unsigned i = 0; i < ENTRIES; i++) {
        const uint8_t *e = p + HEADER + i * ENTRY;
        unsigned w = read16(e), h = read16(e + 2);
        bool pose = i > 0 && i < FIRST_SHEET;
        if (!w || !h || w > 1536 || h > 1536 ||
            (i < FIRST_SHEET && (w != DSH_STAGE || h != DSH_STAGE))) return false;
        size_t raw = w * h * (pose ? 3u : 2u) + (pose ? 4u * DSH_STAGE : 0u);
        unsigned compressed = read32(e + 12);
        if (read32(e + 4) != raw || read32(e + 8) != end || !compressed || compressed > size - end)
            return false;
        end += compressed;
    }
    if (end != size) return false;
    art->bytes = p;
    return true;
}

/* The partition may have trailing erased sectors. Establish the asset's
 * actual end from its contiguous entries before reading or checksumming data. */
static size_t motion_extent(const uint8_t *p, size_t limit, size_t *max_page)
{
    size_t end = MOTION_DIR;
    *max_page = 0;
    if (!p || limit < end || limit > UINT32_MAX || (memcmp(p, "HDA1", 4) && memcmp(p, "HDA2", 4)) ||
        read32(p + 4) != MOTION_COUNT) return 0;
    unsigned max_mode = p[3] == '2' ? 0 : 1;
    for (unsigned i = 0; i < MOTION_COUNT; i++) {
        const uint8_t *e = p + HEADER + i * MOTION_ENTRY;
        size_t n = read32(e + 4);
        if (read32(e) != end || !n || n > limit - end || read32(e + 8) > max_mode) return 0;
        end += n;
        if (*max_page < n) *max_page = n;
    }
    return end;
}

bool dsh_motion_open(dsh_motion_t *out, const void *bytes, size_t size)
{
    if (!out) return false;
    *out = (dsh_motion_t){0};
    const uint8_t *p = bytes;
    size_t max_page;
    if (!size || motion_extent(p, size, &max_page) != size ||
        read32(p + 8) != esp_rom_crc32_le(0, p + HEADER, size - HEADER)) return false;
    *out = (dsh_motion_t){.bytes = p, .length = size, .count = MOTION_COUNT, .wire = p[3] == '2'};
    return true;
}

#ifdef ESP_PLATFORM
bool dsh_partition_motion_open(dsh_motion_t *out)
{
#ifdef DSH_FLASH_PARTITION
    if (!out || out->bytes) return false;
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "hdmotion");
    if (!part || part->size < MOTION_DIR) return false;
    uint8_t *index = heap_caps_malloc(MOTION_DIR, MALLOC_CAP_8BIT);
    if (!index) return false;
    size_t max_page = 0, length = 0;
    if (esp_partition_read(part, 0, index, MOTION_DIR) == ESP_OK)
        length = motion_extent(index, part->size, &max_page);
    enum { CRC_CHUNK = 8192 };
    uint8_t *block = length ? heap_caps_malloc(CRC_CHUNK, MALLOC_CAP_8BIT) : NULL;
    if (!block) {
        heap_caps_free(index);
        return false;
    }
    uint32_t crc = esp_rom_crc32_le(0, index + HEADER, MOTION_DIR - HEADER);
    size_t offset = MOTION_DIR;
    while (offset < length) {
        size_t amount = length - offset < CRC_CHUNK ? length - offset : CRC_CHUNK;
        if (esp_partition_read(part, offset, block, amount) != ESP_OK) break;
        crc = esp_rom_crc32_le(crc, block, amount);
        offset += amount;
    }
    heap_caps_free(block);
    if (offset != length || crc != read32(index + 8)) {
        heap_caps_free(index);
        return false;
    }
    *out = (dsh_motion_t){.bytes = index, .length = length, .count = MOTION_COUNT, .wire = index[3] == '2',
                          .partition = part, .max_page = max_page};
    return true;
#else
    (void)out;
    return false;
#endif
}

void dsh_partition_motion_close(dsh_motion_t *out)
{
    if (out && out->partition) {
        heap_caps_free((void *)out->bytes);
        *out = (dsh_motion_t){0};
    }
}
#endif

bool dsh_motion_open_frame(dsh_motion_t *out, unsigned entry, unsigned level, unsigned mode,
                           const void *compressed, size_t bytes)
{
    if (!out) return false;
    *out = (dsh_motion_t){0};
    if (!entry || entry > MOTION_POSES || !level || level > LEVELS ||
        mode > 2 || !compressed || !bytes) return false;
    *out = (dsh_motion_t){.bytes = compressed, .length = bytes, .mode = mode == 1, .count = 1,
                           .wire = mode == 2,
                           .first = (entry - 1) * LEVELS + level - 1};
    return true;
}

static bool inflate_into(dsh_canvas_t *c, uint8_t *data, size_t capacity,
                         const uint8_t *input, size_t compressed)
{
    if (!c->decoder) {
        c->decoder = heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!c->decoder)
            c->decoder = heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    tinfl_decompressor *decoder = c->decoder;
    if (!decoder) return false;
    size_t n = compressed, out = capacity;
    tinfl_init(decoder);
    return tinfl_decompress(decoder, input, &n, data, data, &out,
                            TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF)
           == TINFL_STATUS_DONE && out == capacity && n == compressed;
}

static uint16_t *unpack(dsh_canvas_t *c, unsigned id)
{
    const uint8_t *e = c->art->bytes + HEADER + id * ENTRY;
    size_t capacity = read32(e + 4);
    uint8_t *data = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data && inflate_into(c, data, capacity, c->art->bytes + read32(e + 8), read32(e + 12)))
        return (uint16_t *)data;
    heap_caps_free(data);
    return NULL;
}

bool dsh_canvas_init(dsh_canvas_t *c, const dsh_art_t *art)
{
    if (!c || !art || !art->bytes) return false;
    *c = (dsh_canvas_t){.art = art, .image_id = MISSING, .exact_id = MISSING};
    return true;
}

static void release_motion_page(dsh_canvas_t *c)
{
    heap_caps_free(c->motion_page);
    c->motion_page = NULL;
    c->motion_page_capacity = 0;
}

void dsh_canvas_set_motion(dsh_canvas_t *c, const dsh_motion_t *motion)
{
    if (!c) return;
    dsh_playback_release(c);
    if (motion != c->motion) release_motion_page(c);
    c->motion = motion;
    c->exact_id = MISSING;
}

static int clamp(int v, int low, int high)
{
    return v < low ? low : (v > high ? high : v);
}

/* The overview fits the complete rectangular sheet inside the physical circle.
 * At zoom, the image covers the screen and pan traverses the complete source. */
static void gallery_axis(uint16_t *axis, int extent, int drawn, int pan, bool zoom)
{
    int origin = zoom ? -(drawn - DSH_PANEL) * (pan + 100) / 200 : (DSH_PANEL - drawn) / 2;
    for (int i = 0; i < DSH_PANEL; i++) {
        int local = i - origin;
        axis[i] = local < 0 || local >= drawn ? MISSING : (2 * local + 1) * extent / (2 * drawn);
    }
}

static void select_gallery(dsh_canvas_t *c, dsh_view_t v)
{
    int w = (int)c->width, h = (int)c->height, dw, dh;
    if (v.zoom) {
        // The short edge spans at least a diameter; retain source detail when larger.
        int short_edge = w < h ? w : h;
        dw = short_edge >= DSH_PANEL ? w : w * DSH_PANEL / short_edge;
        dh = short_edge >= DSH_PANEL ? h : h * DSH_PANEL / short_edge;
    } else {
        // Integer fit by diagonal keeps all four corners on the round display.
        dw = DSH_PANEL;
        while ((int64_t)dw * dw * ((int64_t)w * w + (int64_t)h * h) >
               (int64_t)(DSH_PANEL - 4) * (DSH_PANEL - 4) * w * w) dw--;
        dh = dw * h / w;
    }
    gallery_axis(c->sample_x, w, dw, v.pan_x, v.zoom);
    gallery_axis(c->sample_y, h, dh, v.pan_y, v.zoom);
}

static void select_breath(dsh_canvas_t *c, unsigned eighths)
{
    /* Inverse CSS translateY(-4px) scaleY(1.015) about the image centre,
     * evaluated once per row at pixel centres. Source and stage share x. */
    for (int y = 0; y < DSH_STAGE; y++) {
        int centre = (DSH_STAGE - 1) * 128;
        int y256 = centre + (y * 256 - centre + (int)eighths * 8) * 25600 / (25600 + 3 * (int)eighths);
        y256 = clamp(y256, 0, (DSH_STAGE - 1) * 256);
        c->sample_y[y] = y256 / 256;
        c->weight[y] = y256 % 256;
    }
}

static bool select_image(dsh_canvas_t *c, unsigned id)
{
    if (c->image_id == id) return true;
    heap_caps_free(c->image);
    c->image = unpack(c, id);
    c->image_id = c->image ? id : MISSING;
    if (!c->image) return false;
    const uint8_t *entry = c->art->bytes + HEADER + id * ENTRY;
    c->width = read16(entry);
    c->height = read16(entry + 2);
    return true;
}

static bool select_exact(dsh_canvas_t *c, unsigned id, unsigned key)
{
    if (c->exact_id == key) return true;
    c->exact_id = MISSING;
    const dsh_motion_t *m = c->motion;
    const uint8_t *bytes = m->bytes;
    unsigned mode = m->mode;
    size_t size = m->length;
    size_t offset = 0;
    if (m->count == MOTION_COUNT) {
        const uint8_t *record = m->bytes + HEADER + key * MOTION_ENTRY;
        offset = read32(record);
        size = read32(record + 4);
        mode = read32(record + 8);
    }
    /* A full-screen gallery image can be nearly 1 MB. Release unrelated
     * imagery before reserving the exact stage buffer on a mode change. */
    if (c->image_id != id) {
        heap_caps_free(c->image);
        c->image = NULL;
        c->image_id = MISSING;
    }
    heap_caps_free(c->background);
    c->background = NULL;
    if (mode && !select_image(c, id)) return false;
#ifdef DSH_FLASH_PARTITION
    if (m->partition) {
        // The pack crosses the 16 MiB cache address boundary. Read using the
        // flash driver, retaining one compressed frame per canvas, no mapping.
        if (c->motion_page_capacity < m->max_page) {
            heap_caps_free(c->motion_page);
            c->motion_page = heap_caps_malloc(m->max_page, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            c->motion_page_capacity = c->motion_page ? m->max_page : 0;
        }
        if (!c->motion_page || esp_partition_read(m->partition, offset, c->motion_page, size) != ESP_OK)
            return false;
        bytes = c->motion_page;
    } else
#endif
    {
        bytes += offset;
    }
    if (!c->exact)
        c->exact = heap_caps_malloc(MOTION_RAW, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!c->exact || !inflate_into(c, (uint8_t *)c->exact, MOTION_RAW, bytes, size)) return false;
    if (mode) {
        for (unsigned i = 0; i < DSH_STAGE * DSH_STAGE; ++i) c->exact[i] ^= c->image[i];
    }
    c->exact_id = key;
    return true;
}

bool dsh_canvas_select(dsh_canvas_t *c, dsh_view_t v)
{
    if (!c || !c->art || v.item >= 18 || v.sheet >= 4 || v.breath > 16 ||
        v.zoom > 1 || v.pan_x < -100 || v.pan_x > 100 || v.pan_y < -100 || v.pan_y > 100) return false;
    c->playing_sample = false;
    c->fractional_breath = false;
    if (v.gallery) dsh_playback_release(c);
    unsigned id = v.gallery ? (unsigned)FIRST_SHEET + v.sheet :
                  v.item < 6 ? v.item + 1u : v.item == 6 ? 1u : v.item;
    if (c->compact && c->image_id != id) dsh_compact_release(c);
    if (!v.gallery && v.breath && c->motion) {
        unsigned key = (id - 1) * LEVELS + v.breath - 1;
        if (key >= c->motion->first && key - c->motion->first < c->motion->count) {
            if (!select_exact(c, id, key)) return false;
            c->view = v;
            return true;
        }
    }
    c->exact_id = MISSING;
    if (v.gallery) {
        release_motion_page(c);
        heap_caps_free(c->background);
        c->background = NULL;
        heap_caps_free(c->exact);
        c->exact = NULL;
    }
    if (!select_image(c, id)) return false;
    if (!v.gallery && v.breath && !c->compact && !c->background && !(c->background = unpack(c, 0))) return false;
    c->view = v;
    if (v.gallery) select_gallery(c, v);
    else select_breath(c, 8 * v.breath);
    return true;
}

bool dsh_select_compact(dsh_canvas_t *c, dsh_view_t v, unsigned mix)
{
    if (!dsh_canvas_select(c, v)) return false;
    if (!mix) return true;
    if (!c->compact && !c->background && !(c->background = unpack(c, 0))) return false;
    select_breath(c, 8 * v.breath + mix);
    c->fractional_breath = true;
    return true;
}

unsigned dsh_compensate(unsigned color, unsigned alpha, unsigned bg_src, unsigned bg_dst)
{
    if (alpha == 255 || bg_src == bg_dst) return color;
    if (!alpha) return bg_dst;
    unsigned left = 255 - alpha;
    int r = (int)(color >> 11) + ((int)(bg_dst >> 11) - (int)(bg_src >> 11)) * (int)left / 255;
    int g = (int)(color >> 5 & 63) + ((int)(bg_dst >> 5 & 63) - (int)(bg_src >> 5 & 63)) * (int)left / 255;
    int b = (int)(color & 31) + ((int)(bg_dst & 31) - (int)(bg_src & 31)) * (int)left / 255;
    return (unsigned)((clamp(r, 0, 31) << 11) | (clamp(g, 0, 63) << 5) | clamp(b, 0, 31));
}

static inline unsigned pose_pixel(const dsh_canvas_t *c, int x, int y)
{
    const int n = DSH_STAGE * DSH_STAGE, at = y * DSH_STAGE + x;
    const uint8_t *alpha = (const uint8_t *)(c->image + n);
    const bool vertical = x == 160 && y >= 1 && y < DSH_STAGE - 1;
    const bool horizontal = y == 326 && x >= 1 && x < DSH_STAGE - 1;
    int p = c->sample_y[y] * DSH_STAGE + x;
    unsigned w = c->weight[y], bg = c->background[at];
    unsigned color = dsh_compensate(c->image[p], alpha[p], c->background[p], bg);
    if (w) {
        p += DSH_STAGE;
        unsigned other = dsh_compensate(c->image[p], alpha[p], c->background[p], bg);
        color = dsh_mix565(color, other, w);
    }
    if (c->view.guides && vertical) color = dsh_mix565(color, 0x8f1b, 141);
    if (c->view.guides && horizontal) color = dsh_mix565(color, 0xf6d3, 166);
    return color;
}

/* RGB565 dictionaries hold native little-endian words; the display consumes
 * high byte first. Emit word-aligned pixel pairs whenever the band permits.
 * memcpy keeps unaligned source rows and arbitrary host test buffers valid. */
static void write_row(uint8_t *dst, const uint16_t *src, unsigned count)
{
    while (count && ((uintptr_t)dst & 3u)) {
        uint16_t color = *src++;
        *dst++ = color >> 8;
        *dst++ = color;
        count--;
    }
    while (count >= 2) {
        uint32_t pair;
        memcpy(&pair, src, 4);
        pair = ((pair & 0x00ff00ffu) << 8) | ((pair >> 8) & 0x00ff00ffu);
        memcpy(__builtin_assume_aligned(dst, 4), &pair, 4);
        dst += 4;
        src += 2;
        count -= 2;
    }
    if (count) { dst[0] = *src >> 8; dst[1] = *src; }
}

static void paint_captured(const baji_rect_t *b, uint8_t *dst, const uint16_t *image,
                           const uint16_t *guides, bool wire)
{
    unsigned left = b->x < DSH_INSET ? DSH_INSET : b->x;
    unsigned right = b->x + b->width;
    if (right > DSH_INSET + DSH_STAGE) right = DSH_INSET + DSH_STAGE;
    unsigned stride = 2 * b->width;
    if (left >= right) { memset(dst, 0, (size_t)stride * b->height); return; }
    unsigned before = (left - b->x) * 2, n = right - left, gx = DSH_INSET + 160;
    unsigned after = stride - before - n * 2;
    for (unsigned y = b->y; y < b->y + b->height; y++, dst += stride) {
        if (y < DSH_INSET || y >= DSH_INSET + DSH_STAGE) {
            memset(dst, 0, stride);
            continue;
        }
        unsigned sy = y - DSH_INSET;
        const uint16_t *row = image + sy * DSH_STAGE;
        if (guides && sy == 326) row = guides + DSH_STAGE;
        if (before) memset(dst, 0, before);
        const uint16_t *src = row + (left - DSH_INSET);
        if (wire) memcpy(dst + before, src, n * 2);
        else write_row(dst + before, src, n);
        if (guides && sy != 326 && left <= gx && gx < right) {
            uint8_t *guide = dst + 2 * (gx - b->x);
            if (wire) memcpy(guide, guides + sy, 2);
            else { guide[0] = guides[sy] >> 8; guide[1] = guides[sy]; }
        }
        if (after) memset(dst + stride - after, 0, after);
    }
}

void dsh_paint(const baji_rect_t *band, uint8_t *pixels, void *opaque)
{
    const dsh_canvas_t *c = opaque;
    if (!c || !c->art || !pixels || !band || band->x + band->width > DSH_PANEL ||
        band->y + band->height > DSH_PANEL) return;
    if (c->playing_sample) {
        dsh_playback_paint(band, pixels, c);
        return;
    }
    if (!c->image && c->exact_id == MISSING) return;
    if (!c->view.gallery && (c->exact_id != MISSING || (!c->view.breath && !c->fractional_breath))) {
        unsigned n = DSH_STAGE * DSH_STAGE;
        bool exact = c->exact_id != MISSING;
        const uint16_t *src = exact ? c->exact : c->image;
        const uint16_t *guides = exact ? src + n : src + n * 3 / 2;
        paint_captured(band, pixels, src, c->view.guides ? guides : NULL, exact ? c->motion->wire : c->compact != NULL);
        return;
    }
    if (c->compact && !c->view.gallery && c->exact_id == MISSING) {
        dsh_compact_paint(band, pixels, c);
        return;
    }
    for (unsigned y = band->y; y < band->y + band->height; y++) {
        for (unsigned x = band->x; x < band->x + band->width; x++) {
            unsigned color = 0;
            if (c->view.gallery) {
                unsigned u = c->sample_x[x], v = c->sample_y[y];
                if (u != MISSING && v != MISSING) color = c->image[v * c->width + u];
            } else if (x >= DSH_INSET && x < DSH_INSET + DSH_STAGE &&
                       y >= DSH_INSET && y < DSH_INSET + DSH_STAGE) {
                color = pose_pixel(c, (int)x - DSH_INSET, (int)y - DSH_INSET);
            }
            *pixels++ = color >> 8;
            *pixels++ = color;
        }
    }
}

void dsh_canvas_close(dsh_canvas_t *c)
{
    if (!c) return;
    dsh_playback_release(c);
    heap_caps_free(c->image);
    heap_caps_free(c->background);
    heap_caps_free(c->exact);
    heap_caps_free(c->decoder);
    heap_caps_free(c->motion_page);
    *c = (dsh_canvas_t){0};
}

const void *dsh_builtin(size_t *size)
{
#ifdef BAJI_DEEPSEEK_EXTERNAL_ASSET
    if (size) *size = 0;
    return NULL;
#else
    extern const uint8_t _binary_scene_hds_start[], _binary_scene_hds_end[];
    if (size) *size = (size_t)(_binary_scene_hds_end - _binary_scene_hds_start);
    return _binary_scene_hds_start;
#endif
}
