#include "deepseek_review.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void motion(void)
{
    dsk_model_t m;
    dsk_model_init(&m, 7, 300);
    assert(m.shown.item == 1 && m.shown.phase == 1 && m.shown.frame == 0 &&
           !m.shown.close && m.playing);
    for (unsigned clip = 0; clip < DSK_CLIPS; clip++) {
        dsk_model_init(&m, 7, 300);
        assert(dsk_command(&m, DSK_SELECT_CLIP, clip));
        unsigned fps = dsk_clip_fps(clip), n = dsk_clip_frames(clip);
        int64_t period = (1000000 + fps - 1) / fps;
        for (unsigned i = 1; i <= n; i++) {
            dsk_advance(&m, 300 + i * period - 1);
            assert(m.shown.frame == i - 1);
            dsk_advance(&m, 300 + i * period);
            assert(m.shown.frame == i % n);
        }
    }
    assert(!dsk_command(&m, DSK_SELECT_STATE, 12));
    assert(!dsk_command(&m, DSK_SELECT_CLIP, 6));
    assert(!dsk_command(&m, DSK_SET_PHASE, 4));
    assert(!dsk_command(&m, DSK_SET_VIEW, 2));
    for (unsigned state = 0; state < 12; state++) {
        assert(dsk_command(&m, DSK_SELECT_STATE, state));
        dsk_advance(&m, 12345678);
        assert(m.shown.item == 6 + state && m.shown.frame == 0);
    }
    dsk_command(&m, DSK_STEP_FORWARD, 0);
    assert(m.shown.item == 6 && !m.playing);
    dsk_command(&m, DSK_STEP_BACK, 0);
    assert(m.shown.item == 17 && !m.playing);

    /* A paused character still floats in CSS; every returned deadline
     * must change the raster and schedule a subsequent strictly future wake. */
    for (unsigned steps = 2; steps <= 65; steps++) {
        dsk_model_init(&m, steps, 8000);
        dsk_command(&m, DSK_TOGGLE_PLAY, 0);
        int64_t now = 8000;
        for (unsigned i = 0; i < steps * 5; i++) {
            unsigned old = m.shown.bubble;
            int64_t next = dsk_next_update(&m, now);
            assert(next > now);
            dsk_advance(&m, next - 1);
            assert(m.shown.bubble == old && m.shown.frame == 0);
            dsk_advance(&m, next);
            assert(abs((int)m.shown.bubble - (int)old) == 1 && m.shown.frame == 0);
            now = next;
        }
    }
    dsk_model_init(&m, 1, 0);
    dsk_command(&m, DSK_SELECT_STATE, 0);
    assert(dsk_next_update(&m, 0) == INT64_MAX);
    dsk_model_init(&m, 7, 0);
    dsk_model_smooth_bubble(&m);
    dsk_command(&m, DSK_TOGGLE_PLAY, 0);
    int64_t time = 0;
    for (unsigned i = 1; i <= 96; i++) {
        time = dsk_next_update(&m, time);
        dsk_advance(&m, time);
        unsigned position = i <= 48 ? i : 96 - i;
        assert(m.shown.bubble == position / 8 && m.bubble_mix == position % 8);
    }
}

static void gestures(void)
{
    dsk_model_t m;
    dsk_gesture_t g = {0};
    dsk_model_init(&m, 7, 0);
    /* Entire tap captured before the application woke; no repeated action. */
    dsk_touch(&g, &m, 1, 230, 220, false, 230, 220);
    assert(m.shown.item == 2);
    dsk_touch(&g, &m, 1, 230, 220, false, 230, 220);
    assert(m.shown.item == 2);
    dsk_touch(&g, &m, 2, 65, 215, false, 65, 215);
    assert(m.shown.item == 6);
    dsk_touch(&g, &m, 3, 65, 215, false, 65, 215);
    assert(m.shown.item == 7);
    dsk_touch(&g, &m, 4, 227, 45, false, 227, 45);
    assert(m.shown.phase == 2 && m.shown.item == 7);
    dsk_touch(&g, &m, 5, 227, 400, false, 227, 400);
    assert(!m.playing);
    dsk_touch(&g, &m, 6, 227, 220, true, 227, 220);
    dsk_touch(&g, &m, 6, 227, 220, true, 220, 100);
    dsk_touch(&g, &m, 6, 227, 220, false, 220, 100);
    assert(m.shown.close == 1);
    dsk_touch(&g, &m, 7, 227, 120, true, 227, 220);
    dsk_touch(&g, &m, 7, 227, 120, false, 227, 220);
    assert(m.shown.close == 0);
    dsk_touch(&g, &m, 8, 200, 200, true, 130, 207);
    dsk_touch(&g, &m, 8, 200, 200, false, 130, 207);
    assert(m.shown.item == 8 && !m.playing);
    dsk_touch(&g, &m, 9, 200, 200, true, 280, 203);
    dsk_touch(&g, &m, 9, 200, 200, false, 280, 203);
    assert(m.shown.item == 7 && !m.playing);
    dsk_touch(&g, &m, 10, 210, 250, false, 210, 100);
    assert(m.shown.close == 1 && m.shown.item == 7);
}

/* Test disjoint dirty updates by applying precisely what the production
 * painter writes to the simulated retained GRAM, including odd bounds. */
static uint8_t retained[DSK_SIDE * DSK_SIDE * 2], reference[sizeof(retained)], band[sizeof(retained)];
static uint8_t upper[sizeof(retained)];
static baji_rect_t regions[DSK_COLUMNS * DSK_COLUMNS];
static size_t marked;
static void mark(void *unused, baji_rect_t area)
{
    (void)unused;
    assert(marked < sizeof(regions) / sizeof(regions[0]));
    regions[marked++] = area;
}

static void apply(baji_rect_t area, dsk_canvas_t *c)
{
    dsk_paint(&area, band, c);
    for (unsigned y = 0; y < area.height; y++)
        memcpy(retained + ((area.y + y) * DSK_SIDE + area.x) * 2,
               band + y * area.width * 2, area.width * 2);
}

static void canvases(const uint8_t *data, size_t size)
{
    dsk_art_t art;
    assert(dsk_art_open(&art, data, size));
    dsk_canvas_t c;
    dsk_canvas_init(&c, &art);
    assert(!dsk_canvas_select(&c, (dsk_frame_t){.item = 18}));
    assert(!dsk_canvas_select(&c, (dsk_frame_t){.item = 17, .frame = 1}));
    assert(!dsk_canvas_select(&c, (dsk_frame_t){.bubble = art.bubble_steps}));
    assert(!dsk_canvas_select_blend(&c, (dsk_frame_t){.bubble = art.bubble_steps - 1}, 1));
    assert(!dsk_canvas_select_blend(&c, (dsk_frame_t){0}, 8));
    uint32_t seed = 98;
    baji_rect_t full = {0,0,DSK_SIDE,DSK_SIDE};
    for (unsigned i = 0; i < 150; i++) {
        seed = seed * 1664525u + 1013904223u;
        unsigned item = (seed >> 8) % DSK_ITEMS;
        unsigned frame = item < DSK_CLIPS ? (seed >> 12) % dsk_clip_frames(item) : 0;
        dsk_frame_t f = {item, frame, seed % 4, (seed >> 9) % 2, (seed >> 1) % art.bubble_steps};
        unsigned mix = (unsigned)f.bubble + 1 < art.bubble_steps ? (seed >> 4) % 8 : 0;
        marked = 0;
        assert(dsk_canvas_damage_blend(&c, f, mix, mark, NULL));
        assert(dsk_canvas_select_blend(&c, f, mix));
        for (size_t j = 0; j < marked; j++) apply(regions[j], &c);
        dsk_paint(&full, reference, &c);
        assert(!memcmp(retained, reference, sizeof(reference)));
        marked = 0;
        assert(dsk_canvas_damage_blend(&c, f, mix, mark, NULL) && !marked);

        unsigned x = (seed >> 8) % DSK_SIDE, y = (seed >> 17) % DSK_SIDE;
        unsigned w = 1 + seed % (DSK_SIDE - x), h = 1 + (seed >> 11) % (DSK_SIDE - y);
        baji_rect_t crop = {x, y, w, h};
        dsk_paint(&crop, band, &c);
        for (unsigned line = 0; line < h; line++)
            assert(!memcmp(band + line*w*2, reference + ((y + line)*DSK_SIDE + x)*2, w*2));
    }
    /* Compare each mixed pixel to independent per-channel arithmetic on
     * two original captures. Exercise the complete float under each sky. */
    for (unsigned phase = 0; phase < 4; phase++) {
        for (unsigned offset = 0; offset + 1 < art.bubble_steps; offset++) {
            dsk_frame_t f = {.item = 1, .phase = phase, .bubble = offset};
            dsk_frame_t high = f;
            high.bubble++;
            dsk_canvas_select(&c, f);
            dsk_paint(&full, retained, &c);
            dsk_canvas_select(&c, high);
            dsk_paint(&full, upper, &c);
            for (unsigned mix = 1; mix < 8; mix++) {
                assert(dsk_canvas_select_blend(&c, f, mix));
                dsk_paint(&full, band, &c);
                for (unsigned at = 0; at < sizeof(band); at += 2) {
                    unsigned a = retained[at]*256u + retained[at+1], b = upper[at]*256u + upper[at+1];
                    unsigned r = ((a >> 11)*(8-mix) + (b >> 11)*mix + 4) / 8;
                    unsigned g = (((a >> 5)&63)*(8-mix) + ((b >> 5)&63)*mix + 4) / 8;
                    unsigned blue = ((a&31)*(8-mix) + (b&31)*mix + 4) / 8;
                    assert((r*2048 + g*32 + blue) == (unsigned)band[at]*256 + band[at+1]);
                }
            }
        }
    }
    dsk_art_close(&art);
}

int main(int argc, char **argv)
{
    motion();
    gestures();
    if (argc == 2) {
        FILE *f = fopen(argv[1], "rb");
        assert(f && !fseek(f, 0, SEEK_END));
        long length = ftell(f);
        assert(length > 24 && !fseek(f, 0, SEEK_SET));
        uint8_t *bytes = malloc(length);
        assert(bytes && fread(bytes, 1, length, f) == (size_t)length);
        fclose(f);
        canvases(bytes, length);
        dsk_art_t out;
        assert(!dsk_art_open(&out, bytes, length - 1));
        bytes[length-1] ^= 1;
        assert(!dsk_art_open(&out, bytes, length));
        free(bytes);
    }
    puts("timing, controls and supplied pack checks passed");
    return 0;
}
