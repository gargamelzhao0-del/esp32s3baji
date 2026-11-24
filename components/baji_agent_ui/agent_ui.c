#include "baji_agent_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#include "badge_font.inc"

enum { TOP_W = 224, TOP_H = 30, BOTTOM_W = 228, BOTTOM_H = 56 };
static const baji_rect_t header = {121, 32, TOP_W, TOP_H};
static const baji_rect_t footer = {119, 374, BOTTOM_W, BOTTOM_H};

/* RGB565 cache in panel wire order. All writes happen on the display owner. */
#define RGB(r,g,b) ((((r) & 248) << 8) | (((g) & 252) << 3) | ((b) >> 3))
enum { INK = RGB(18,25,46), EDGE = RGB(63,83,107), PAPER = RGB(238,250,255),
       AQUA = RGB(114,236,208), GOLD = RGB(255,208,120), ROSE = RGB(255,148,169),
       LILAC = RGB(193,173,255), MUTED = RGB(167,192,208) };

struct baji_agent_ui {
    baji_agent_feed_t feed;
    char model[33];
    baji_agent_cue_t cue;
    int64_t next_change_us;
    int quota, context; /* permille, or -1 for unavailable */
    bool quota_estimated, context_estimated;
    uint16_t top[TOP_W * TOP_H], bottom[BOTTOM_W * BOTTOM_H];
};

typedef struct { uint16_t *p; int w, h; } surface_t;

static uint16_t wire(uint16_t c) { return (uint16_t)((c << 8) | (c >> 8)); }

static void box(surface_t s, int x, int y, int w, int h, uint16_t color)
{
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col)
            if ((unsigned)row < (unsigned)s.h && (unsigned)col < (unsigned)s.w)
                s.p[row * s.w + col] = wire(color);
}

static int inset(int y, int h)
{
    int end = h - 1 - y;
    int distance = y < end ? y : end;
    return distance < 5 ? 5 - distance : 0;
}

static void panel(surface_t s)
{
    for (int y = 0; y < s.h; ++y) {
        int pad = inset(y, s.h);
        box(s, pad, y, s.w - 2 * pad, 1, EDGE);
        if (y >= 2 && y < s.h - 2) box(s, pad + 2, y, s.w - 2 * pad - 4, 1, INK);
    }
}

static uint32_t decode(const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    unsigned first = *p++;
    *text = (const char *)p;
    if (first < 128) return first;
    int count = first >= 0xf0 ? 3 : first >= 0xe0 ? 2 : 1;
    if (first < 0xc2 || first > 0xf4) return '?';
    uint32_t code = first & (0x3f >> count);
    for (int i = 0; i < count; ++i) {
        if ((p[i] & 0xc0) != 0x80) return '?';
        code = (code << 6) | (p[i] & 0x3f);
    }
    *text = (const char *)(p + count);
    if ((count == 2 && code < 0x800) || (count == 3 && code < 0x10000) ||
        (code >= 0xd800 && code <= 0xdfff) || code > 0x10ffff) return '?';
    return code;
}

static int glyph_index(uint32_t code)
{
    size_t lo = 0, hi = sizeof(font) / sizeof(font[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (font[mid].code == code) return (int)mid;
        if (font[mid].code < code) lo = mid + 1;
        else hi = mid;
    }
    return -1;
}

/* Display an unbundled Unicode scalar as a single compact hex glyph. */
static void hex_glyph(surface_t s, int x, int y, uint32_t code, int scale, uint16_t color)
{
    static const uint16_t hex[] = {
        0x7b6f,0x2492,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,
        0x7bef,0x7bcf,0x7bed,0x6bae,0x7927,0x6b6e,0x79e7,0x79e4
    };
    unsigned columns = code > 65535 ? 3 : 2;
    for (unsigned i = 0; i < 2 * columns; ++i) {
        unsigned shift = 4 * (2 * columns - 1 - i);
        uint16_t bits = hex[(code >> shift) & 15];
        for (unsigned bit = 0; bit < 15; ++bit)
            if (bits & (1 << (14 - bit)))
                box(s, x + (i % columns * 4 + bit % 3) * scale,
                    y + (i / columns * 6 + bit / 3) * scale, scale, scale, color);
    }
}

static int text_run(surface_t s, int x, int y, const char *str, int scale, uint16_t color)
{
    int start = x;
    while (*str) {
        uint32_t code = decode(&str);
        int idx = glyph_index(code), width = idx >= 0 ? font[idx].width : 12;
        if (s.p && idx < 0) hex_glyph(s, x, y, code, scale, color);
        if (s.p && idx >= 0)
            for (int row = 0; row < 12; ++row)
                for (int col = 0; col < width; ++col)
                    if (font[idx].rows[row] & (1u << col))
                        box(s, x + col * scale, y + row * scale, scale, scale, color);
        x += width * scale;
    }
    return x - start;
}

static void header_draw(baji_agent_ui_t *ui)
{
    surface_t s = {ui->top, TOP_W, TOP_H};
    panel(s);
    const char *name = ui->model[0] ? ui->model : "AGENT";
    int width = text_run((surface_t){0}, 0, 0, name, 1, PAPER);
    int scale = 2 * width <= TOP_W - 16 ? 2 : 1;
    text_run(s, (s.w - width * scale) / 2, (s.h - 12 * scale) / 2, name, scale, PAPER);
}

/* Hand-pixelled 7x7 silhouettes: link, thought, book, nib, terminal, lens,
 * request, check, alert, limit, compress, finish, disconnected. */
static const struct { const char *label; uint16_t color; uint8_t icon[7]; } badges[] = {
    [BAJI_CUE_NO_SESSION] = {"NO SESSION", MUTED, {0,62,34,42,34,62,0}},
    [BAJI_CUE_FAILED]  = {"FAILED", ROSE, {0,34,20,8,20,34,0}},
    [BAJI_CUE_CANCELLED] = {"CANCELLED", GOLD, {0,54,54,54,54,54,0}},
    [BAJI_CUE_LOCAL]   = {"LOCAL", MUTED, {0}},
    [BAJI_CUE_READY]   = {"READY", AQUA, {14,17,34,28,34,68,56}},
    [BAJI_CUE_THINK]   = {"THINKING", LILAC, {62,65,73,65,62,8,16}},
    [BAJI_CUE_READ]    = {"READING", AQUA, {0,119,73,73,73,119,8}},
    [BAJI_CUE_WRITE]   = {"WRITING", GOLD, {6,9,18,36,72,112,0}},
    [BAJI_CUE_RUN]     = {"RUNNING", AQUA, {127,65,97,81,97,77,127}},
    [BAJI_CUE_SEARCH]  = {"SEARCHING", LILAC, {56,68,68,68,58,2,1}},
    [BAJI_CUE_WAIT]    = {"YOUR TURN", GOLD, {28,34,2,12,8,0,8}},
    [BAJI_CUE_SUCCESS] = {"SUCCESS", AQUA, {0,1,2,68,40,16,0}},
    [BAJI_CUE_ERROR]   = {"ERROR", ROSE, {8,28,42,42,65,73,127}},
    [BAJI_CUE_LIMIT]   = {"LIMITED", ROSE, {62,65,65,62,65,65,62}},
    [BAJI_CUE_COMPACT] = {"COMPACTING", LILAC, {65,34,20,8,20,34,65}},
    [BAJI_CUE_DONE]    = {"DONE", MUTED, {60,58,62,32,32,32,32}},
    [BAJI_CUE_OFFLINE] = {"OFFLINE", MUTED, {65,34,8,20,8,34,65}},
};

static void meter(surface_t s, int x, const char *label, int value, bool estimated, uint16_t color)
{
    char number[24];
    const char *prefix = estimated ? "~" : "";
    if (value < 0) memcpy(number, "--", 3);
    else if (value % 10) snprintf(number, sizeof(number), "%s%d.%d%%", prefix, value / 10, value % 10);
    else snprintf(number, sizeof(number), "%s%d%%", prefix, value / 10);
    text_run(s, x, 19, label, 1, MUTED);
    text_run(s, x, 27, number, 2, PAPER);
    box(s, x, 50, 88, 3, EDGE);
    if (value > 0) box(s, x, 50, (88 * value + 999) / 1000, 3, color);
}

static void footer_draw(baji_agent_ui_t *ui)
{
    surface_t s = {ui->bottom, BOTTOM_W, BOTTOM_H};
    panel(s);
    unsigned cue = ui->cue;
    uint16_t color = badges[cue].color;
    int text_width = text_run((surface_t){0}, 0, 0, badges[cue].label, 2, color);
    int x = (s.w - text_width - 20) / 2;
    for (int y = 0; y < 7; ++y)
        for (int col = 0; col < 7; ++col)
            if (badges[cue].icon[y] & (1u << (6 - col)))
                box(s, x + col * 2, 5 + y * 2, 2, 2, color);
    text_run(s, x + 20, -1, badges[cue].label, 2, color);
    meter(s, 16, "QUOTA LEFT", ui->quota, ui->quota_estimated, AQUA);
    meter(s, 124, "CTX USED", ui->context, ui->context_estimated, LILAC);
}

static baji_agent_cue_t state_cue(const baji_agent_display_t *state)
{
    if (state->view.connection != BAJI_ONLINE) return BAJI_CUE_OFFLINE;
    if (!state->has_session) return BAJI_CUE_NO_SESSION;
    static const struct { const char *pose; baji_agent_cue_t cue; } poses[] = {
        {"thinking", BAJI_CUE_THINK}, {"reading", BAJI_CUE_READ}, {"writing", BAJI_CUE_WRITE},
        {"running", BAJI_CUE_RUN}, {"searching", BAJI_CUE_SEARCH}, {"asking", BAJI_CUE_WAIT},
        {"happy", BAJI_CUE_SUCCESS}, {"oops", BAJI_CUE_ERROR}, {"tired", BAJI_CUE_LIMIT},
        {"compact", BAJI_CUE_COMPACT},
    };
    for (unsigned i = 0; state->view.pose && i < sizeof(poses)/sizeof(poses[0]); ++i)
        if (!strcmp(state->view.pose, poses[i].pose)) return poses[i].cue;
    if (state->view.phase != BAJI_ENDED) return BAJI_CUE_READY;
    if (state->view.outcome == BAJI_FAILED) return BAJI_CUE_FAILED;
    if (state->view.outcome == BAJI_CANCELLED) return BAJI_CUE_CANCELLED;
    return BAJI_CUE_DONE;
}

baji_agent_ui_t *baji_agent_ui_open(const baji_agent_feed_t *feed)
{
    if (!feed || !feed->read) return NULL;
#ifdef ESP_PLATFORM
    baji_agent_ui_t *ui = heap_caps_calloc(1, sizeof(*ui), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    baji_agent_ui_t *ui = calloc(1, sizeof(*ui));
#endif
    if (ui) {
        ui->feed = *feed;
        ui->next_change_us = INT64_MAX;
    }
    return ui;
}

void baji_agent_ui_close(baji_agent_ui_t *ui) { free(ui); }

bool baji_agent_ui_visible(const baji_agent_ui_t *ui) { return ui && ui->cue != BAJI_CUE_LOCAL; }

baji_agent_cue_t baji_agent_ui_update(baji_agent_ui_t *ui, int64_t now_us, baji_damage_t *damage)
{
    if (!ui) return BAJI_CUE_LOCAL;
    baji_agent_display_t state = {0};
    bool visible = ui->feed.read(ui->feed.context, (uint64_t)now_us / 1000, &state);
    uint64_t at = state.view.next_change_ms;
    ui->next_change_us = visible && at && at <= INT64_MAX / 1000 ? (int64_t)at * 1000 : INT64_MAX;
    state.model[sizeof(state.model) - 1] = 0;
    bool transition = visible != baji_agent_ui_visible(ui);
    if (transition || (visible && strcmp(state.model, ui->model))) {
        memcpy(ui->model, state.model, sizeof(ui->model));
        if (visible) header_draw(ui);
        if (damage) baji_damage_add(damage, header);
    }
    baji_agent_cue_t cue = visible ? state_cue(&state) : BAJI_CUE_LOCAL;
    int q = state.view.quota_known ? 1000 - state.view.quota_used_permille : -1;
    int c = state.view.context_known ? state.view.context_used_permille : -1;
    if (transition || (visible && (ui->cue != cue || ui->quota != q || ui->context != c ||
                                  ui->quota_estimated != state.view.quota_estimated ||
                                  ui->context_estimated != state.view.context_estimated))) {
        ui->cue = cue;
        ui->quota_estimated = state.view.quota_estimated;
        ui->context_estimated = state.view.context_estimated;
        ui->quota = q;
        ui->context = c;
        if (visible) footer_draw(ui);
        if (damage) baji_damage_add(damage, footer);
    }
    return cue;
}

int64_t baji_agent_ui_deadline(const baji_agent_ui_t *ui, int64_t now_us, int64_t scene_due)
{
    if (!ui) return scene_due;
    if (ui->next_change_us < scene_due) scene_due = ui->next_change_us;
    /* Providers without a change notification must still be polled. */
    if (!ui->feed.changed && scene_due > now_us && scene_due - now_us > 250000)
        return now_us + 250000;
    return scene_due;
}

static void copy_panel(const baji_rect_t *band, uint8_t *pixels, const baji_rect_t *rect,
                       const uint16_t *cache)
{
    int y0 = band->y > rect->y ? band->y : rect->y;
    int y1 = band->y + band->height, bottom = rect->y + rect->height;
    if (y1 > bottom) y1 = bottom;
    for (int y = y0; y < y1; ++y) {
        int row = y - rect->y, pad = inset(row, rect->height);
        int left = rect->x + pad, right = rect->x + rect->width - pad;
        if (left < band->x) left = band->x;
        if (right > band->x + band->width) right = band->x + band->width;
        if (right > left)
            memcpy(pixels + 2 * ((y - band->y) * band->width + left - band->x),
                   cache + row * rect->width + left - rect->x, 2 * (right - left));
    }
}

void baji_agent_ui_paint(const baji_agent_ui_t *ui, const baji_rect_t *band, uint8_t *pixels)
{
    if (!baji_agent_ui_visible(ui)) return;
    copy_panel(band, pixels, &header, ui->top);
    copy_panel(band, pixels, &footer, ui->bottom);
}
