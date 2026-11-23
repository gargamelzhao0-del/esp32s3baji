#include "baji_agent_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SIDE = 466 };
static baji_state_t state;
static const char *model = "DeepSeek";
static bool read_feed(void *ctx, uint64_t now, baji_agent_display_t *out)
{
    baji_state_t *s = ctx;
    if (!s->has_snapshot) return false;
    out->view = baji_state_view(s, now);
    out->has_session = true;
    snprintf(out->model, sizeof(out->model), "%s", model);
    return true;
}

static uint16_t screen[SIDE * SIDE];
static const baji_rect_t full = {0, 0, SIDE, SIDE};

static void check_crops(baji_agent_ui_t *ui)
{
    memset(screen, 0x17, sizeof(screen));
    baji_agent_ui_paint(ui, &full, (uint8_t *)screen);
    assert(screen[200 * SIDE + 155] == 0x1717 && screen[60 * SIDE] == 0x1717);
    unsigned changed = 0;
    for (unsigned y = 0; y < SIDE; ++y) {
        for (unsigned x = 0; x < SIDE; ++x) {
            if (screen[y * SIDE + x] == 0x1717) continue;
            int dx = (int)x - SIDE / 2, dy = (int)y - SIDE / 2;
            assert(dx * dx + dy * dy < SIDE * SIDE / 4);
            ++changed;
        }
    }
    assert(changed > 15000);
    uint16_t band[51 * 17];
    for (unsigned y = 0; y < SIDE; y += 17) {
        for (unsigned x = 0; x < SIDE; x += 51) {
            baji_rect_t r = {x, y, x + 51 > SIDE ? SIDE - x : 51, y + 17 > SIDE ? SIDE - y : 17};
            memset(band, 0x17, sizeof(band));
            baji_agent_ui_paint(ui, &r, (uint8_t *)band);
            for (unsigned j = 0; j < r.height; ++j)
                assert(!memcmp(band + j * r.width, screen + (y + j) * SIDE + x, r.width * 2));
        }
    }
}

static void preview(baji_agent_ui_t *ui, const char *input, const char *output)
{
    FILE *fp = fopen(input, "rb");
    assert(fp && fread(screen, 1, sizeof(screen), fp) == sizeof(screen));
    fclose(fp);
    baji_agent_ui_paint(ui, &full, (uint8_t *)screen);
    fp = fopen(output, "wb");
    assert(fp && fwrite(screen, 1, sizeof(screen), fp) == sizeof(screen));
    fclose(fp);
}

int main(int argc, char **argv)
{
    baji_damage_t damage;
    baji_damage_init(&damage, SIDE, SIDE);
    baji_agent_feed_t feed = {.read = read_feed, .context = &state, .changed = &state};
    baji_agent_ui_t *ui = baji_agent_ui_open(&feed);
    assert(ui && baji_agent_ui_update(ui, 0, &damage) == BAJI_CUE_LOCAL && !damage.count);
    assert(baji_agent_ui_deadline(ui, 0, INT64_MAX) == INT64_MAX);
    baji_snapshot_t snapshot = {
        .seq = 1, .phase = BAJI_TOOL, .activity = BAJI_READING,
        .quota = {.known = true, .used_permille = 1000, .ttl_ms = 500},
        .context = {.known = true, .used_permille = 0, .ttl_ms = 4000}
    };
    assert(baji_state_apply(&state, &snapshot, 1000) == BAJI_APPLIED);
    assert(baji_agent_ui_update(ui, 1000000, &damage) == BAJI_CUE_READ && damage.count == 2);
    assert(baji_agent_ui_deadline(ui, 1000000, INT64_MAX) == 1250000);
    assert(baji_agent_ui_deadline(ui, 1000000, 1010000) == 1010000);
    check_crops(ui);
    uint16_t numbers[228 * 56];
    const baji_rect_t footer = {119,374,228,56};
    memset(numbers, 0, sizeof(numbers));
    baji_agent_ui_paint(ui, &footer, (uint8_t *)numbers);
    baji_damage_clear(&damage);
    ++snapshot.seq;
    assert(baji_state_apply(&state, &snapshot, 1100) == BAJI_APPLIED);
    assert(baji_agent_ui_update(ui, 1100000, &damage) == BAJI_CUE_READ && damage.count == 0);
    assert(baji_agent_ui_update(ui, 1600000, &damage) == BAJI_CUE_READ && damage.count == 1);
    uint16_t expired[228 * 56] = {0};
    baji_agent_ui_paint(ui, &footer, (uint8_t *)expired);
    assert(memcmp(expired, numbers, sizeof(numbers)) != 0); /* unknown and zero differ */
    for (unsigned y = 0; y < footer.height; ++y)
        assert(!memcmp(expired + y * footer.width + 115, numbers + y * footer.width + 115, 110 * 2));
    baji_damage_clear(&damage);
    /* Valid three/four-byte scalars, a maximum-length identifier and a torn final byte. */
    const char *cases[] = {"小鲸🐳", "0123456789abcdef0123456789abcdef", "0123456789abcdef0123456789abcde\xf0"};
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        model = cases[i];
        assert(baji_agent_ui_update(ui, 1600000, &damage) == BAJI_CUE_READ && damage.count == 1);
        assert(damage.regions[0].y < 100);
        check_crops(ui);
        baji_damage_clear(&damage);
    }
    ++snapshot.seq;
    snapshot.context.estimated = true;
    assert(baji_state_apply(&state, &snapshot, 1650) == BAJI_APPLIED);
    assert(baji_agent_ui_update(ui, 1650000, &damage) == BAJI_CUE_READ && damage.count == 1);
    uint16_t estimated[228 * 56] = {0};
    baji_agent_ui_paint(ui, &footer, (uint8_t *)estimated);
    bool changed = false;
    for (unsigned y = 28; y < 49; ++y)
        changed |= memcmp(estimated + y * footer.width + 115, expired + y * footer.width + 115, 110 * 2) != 0;
    assert(changed); /* same numeric value has a different estimated label */
    ++snapshot.seq;
    snapshot.quota.used_permille = 999;
    baji_damage_clear(&damage);
    assert(baji_state_apply(&state, &snapshot, 1660) == BAJI_APPLIED);
    assert(baji_agent_ui_update(ui, 1660000, &damage) == BAJI_CUE_READ && damage.count == 1);
    uint16_t fractional[228 * 56] = {0};
    baji_agent_ui_paint(ui, &footer, (uint8_t *)fractional);
    assert(fractional[50 * 228 + 16] != estimated[50 * 228 + 16]); /* 0.1% remaining still has a bar */
    ++snapshot.seq;
    snapshot.phase = BAJI_ENDED;
    snapshot.outcome = BAJI_SUCCEEDED;
    assert(baji_state_apply(&state, &snapshot, 1700) == BAJI_APPLIED);
    assert(baji_agent_ui_update(ui, 1700000, &damage) == BAJI_CUE_SUCCESS);
    assert(baji_agent_ui_update(ui, 3300000, &damage) == BAJI_CUE_DONE);
    assert(baji_agent_ui_update(ui, 9700000, &damage) == BAJI_CUE_OFFLINE);
    assert(baji_agent_ui_deadline(ui, 9700000, INT64_MAX) == INT64_MAX);
    baji_state_begin_connection(&state);
    baji_damage_clear(&damage);
    assert(baji_agent_ui_update(ui, 9800000, &damage) == BAJI_CUE_LOCAL && damage.count == 2);
    assert(!baji_agent_ui_visible(ui));

    snapshot.seq = 1;
    snapshot.outcome = BAJI_FAILED;
    assert(baji_state_apply(&state, &snapshot, 10000) == BAJI_APPLIED);
    assert(baji_agent_ui_update(ui, 10000000, NULL) == BAJI_CUE_ERROR);
    assert(baji_agent_ui_update(ui, 11600000, NULL) == BAJI_CUE_FAILED);
    ++snapshot.seq;
    snapshot.outcome = BAJI_CANCELLED;
    assert(baji_state_apply(&state, &snapshot, 11700) == BAJI_APPLIED);
    assert(baji_agent_ui_update(ui, 11700000, NULL) == BAJI_CUE_CANCELLED);
    baji_state_begin_connection(&state);

    if (argc == 4) {
        model = argv[3];
        snapshot = (baji_snapshot_t){
            .seq = 1, .phase = BAJI_TOOL, .activity = BAJI_READING,
            .quota = {.known = true, .used_permille = 270, .ttl_ms = 1000},
            .context = {.known = true, .used_permille = 420, .ttl_ms = 1000}
        };
        assert(baji_state_apply(&state, &snapshot, 0) == BAJI_APPLIED);
        assert(baji_agent_ui_update(ui, 0, NULL) == BAJI_CUE_READ);
        preview(ui, argv[1], argv[2]);
    }
    baji_agent_ui_close(ui);
    puts("HUD TTL, no-op heartbeat, 0/unknown, Unicode model names, circle fit and clipped RGB565 composition passed");
    return 0;
}
