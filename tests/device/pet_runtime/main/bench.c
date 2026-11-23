#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "baji_vsync.h"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pet_app.h"
#include "pet_player.h"
#include "schedule_check.h"
#include "pet_probe.h"

static bool read_exact(void *buffer, size_t size)
{
    uint8_t *dst = buffer;
    while (size) {
        int n = usb_serial_jtag_read_bytes(dst, size, pdMS_TO_TICKS(3000));
        if (n <= 0) return false;
        size -= n;
        dst += n;
    }
    return true;
}

static esp_err_t receive_asset(baji_pet_t **out)
{
    usb_serial_jtag_driver_config_t config = {.tx_buffer_size=512, .rx_buffer_size=8192};
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK) return err;
    uint32_t header[3] = {0};  /* little-endian PET1, packed length, CRC32 */
    unsigned limit = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int received = 0;
    for (int attempt = 0; attempt < 30 && !received; ++attempt) {
        printf("PET_READY max=%u\n", limit);
        received = usb_serial_jtag_read_bytes(header, sizeof(header), pdMS_TO_TICKS(1200));
    }
    err = ESP_ERR_INVALID_RESPONSE;
    uint8_t *packed = NULL;
    uint32_t crc = 0;
    if (received > 0 && read_exact((uint8_t *)header+received, sizeof(header)-received) &&
        header[0] == 0x31544550U && header[1] > 0 && header[1] <= limit) {
        packed = heap_caps_malloc(header[1], MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (packed) {
            printf("PET_RECEIVE bytes=%u\n", (unsigned)header[1]);
            if (read_exact(packed, header[1])) {
                crc = esp_rom_crc32_le(0, packed, header[1]);
                if (crc == header[2]) err = baji_pet_open_data(out, packed, header[1]);
            }
        } else err = ESP_ERR_NO_MEM;
    }
    heap_caps_free(packed);
    usb_serial_jtag_driver_uninstall();
    printf("PET_ASSET bytes=%u crc32=%08x ok=%d\n", (unsigned)header[1], (unsigned)crc, err==ESP_OK);
    return err;
}

enum { IDLE_EDGES = 180, DRAG_EDGES = 180, PAT_EDGES = 240, TOTAL = IDLE_EDGES + DRAG_EDGES + PAT_EDGES };
enum { ACTIVE, NOOP, GAZE_ONLY };
typedef struct {
    unsigned due_us, tick_us;
    bool sent, gaze_only;
} sample_t;
typedef struct {
    unsigned draws, visible, missing, late, poses, gazes, gaze_only, pat_start, pat_end, unchanged_draw;
    unsigned slowest_us, slow_select, slow_bus, old_sample, next_sample, slow_clip;
    int64_t first_us, last_us;
} stage_t;
static sample_t samples[TOTAL];
static unsigned ordered[PAT_EDGES];
static const char *const labels[] = {"idle", "drag", "pat"};
static const unsigned counts[] = {IDLE_EDGES, DRAG_EDGES, PAT_EDGES};

static baji_input_t input_at(unsigned phase, unsigned i)
{
    static const uint16_t path[][2] = {
        {82,330},{136,350},{192,363},{255,359},{330,342},
        {382,312},{372,150},{335,110},{136,110},{94,156}
    };
    baji_input_t in = {0};
    if (phase == 1) {
        size_t point = (i/7) % (sizeof(path)/sizeof(path[0]));
        in.presses = 1;
        in.press_x = path[0][0];
        in.press_y = path[0][1];
        /* Release before the next phase so the forehead receives a new tap. */
        in.touch = (baji_touch_sample_t){i < DRAG_EDGES-12, path[point][0], path[point][1]};
    } else if (phase == 2) {
        in.presses = 2;
        in.press_x = 221;
        in.press_y = 185;
        in.touch = (baji_touch_sample_t){i < 8, in.press_x, in.press_y};
    }
    return in;
}

static int by_time(const void *a, const void *b)
{
    unsigned left = *(const unsigned *)a, right = *(const unsigned *)b;
    return (left > right) - (left < right);
}

typedef struct {
    baji_pet_t *art;
    unsigned bands, pixels;
} column_probe_t;

static void paint_column(const baji_rect_t *band, uint8_t *pixels, void *context)
{
    column_probe_t *probe = context;
    ++probe->bands;
    probe->pixels += (unsigned)band->width * band->height;
    baji_pet_paint(band, pixels, probe->art);
}

static esp_err_t measure_edge_column(baji_board_t *board, baji_pet_t *art)
{
    enum { WARM = 4, N = 32 };
    /* Preserve the former seven aperture boundaries as an A/B control. */
    const baji_rect_t split[] = {{52,60,2,28}, {52,88,2,34}, {52,122,2,32}, {52,154,2,158},
                                 {52,312,2,32}, {52,344,2,34}, {52,378,2,28}};
    const baji_rect_t column = {52,0,2,466};
    unsigned times[2][N], bands[2] = {0};
    for (unsigned i = 0; i < WARM+N; ++i) {
        for (unsigned j = 0; j < 2; ++j) {
            unsigned joined = (i+j) % 2;
            column_probe_t probe = {.art=art};
            int64_t start = esp_timer_get_time();
            esp_err_t err = baji_board_paint_round_regions(board, joined ? &column : split,
                        joined ? 1 : sizeof(split)/sizeof(split[0]), paint_column, &probe);
            unsigned elapsed = (unsigned)(esp_timer_get_time()-start);
            if (err != ESP_OK) return err;
            if (probe.pixels != 692 || probe.bands != (joined ? 1 : 7)) return ESP_ERR_INVALID_STATE;
            bands[joined] = probe.bands;
            if (i >= WARM) times[joined][i-WARM] = elapsed;
        }
    }
    for (unsigned j = 0; j < 2; ++j) {
        qsort(times[j], N, sizeof(times[j][0]), by_time);
        printf("PET_COLUMN mode=%s n=%u pixels=692 dma=%u p50=%u p95=%u max=%u\n",
               j ? "joined" : "split", (unsigned)N, bands[j], (times[j][N/2-1]+times[j][N/2])/2,
               times[j][(N*95+99)/100-1], times[j][N-1]);
    }
    return ESP_OK;
}

static esp_err_t measure_full(baji_board_t *board, const baji_board_info_t *info, baji_pet_t *art)
{
    enum { WARM = 2, MEASURED = 12 };
    unsigned times[MEASURED];
    const baji_rect_t full = {0,0,info->width,info->height};
    unsigned frames = baji_pet_frame_count(BAJI_PET_IDLE);
    if (!frames) return ESP_ERR_INVALID_STATE;
    for (unsigned i = 0; i < WARM+MEASURED; ++i) {
        baji_pet_state_t state = {.clip=BAJI_PET_IDLE, .frame=i%frames};
        int64_t start = esp_timer_get_time();
        baji_pet_select(art, state);
        esp_err_t err = baji_board_paint_round(board, full, baji_pet_paint, art);
        if (err != ESP_OK) return err;
        if (i >= WARM) times[i-WARM] = (unsigned)(esp_timer_get_time()-start);
    }
    qsort(times, MEASURED, sizeof(times[0]), by_time);
    printf("PET_FULL n=%u p50=%u p95=%u max=%u\n", (unsigned)MEASURED,
           (times[MEASURED/2-1]+times[MEASURED/2])/2, times[(MEASURED*95+99)/100-1], times[MEASURED-1]);
    return ESP_OK;
}

typedef struct { unsigned p50, p95, max; } distribution_t;

static distribution_t distribution(const sample_t *first, unsigned n, unsigned group)
{
    unsigned used = 0;
    for (unsigned i = 0; i < n; ++i) {
        bool include = group == GAZE_ONLY ? first[i].gaze_only
                                          : first[i].sent == (group == ACTIVE);
        if (include) ordered[used++] = group == NOOP ? first[i].tick_us : first[i].due_us;
    }
    if (!used) return (distribution_t){0};
    qsort(ordered, used, sizeof(ordered[0]), by_time);
    return (distribution_t){(ordered[(used-1)/2]+ordered[used/2])/2,
                            ordered[(used*95+99)/100-1], ordered[used-1]};
}

static esp_err_t measure(pet_player_t *player, baji_board_t *board, bool *functional, bool *on_time)
{
    stage_t stages[3] = {0};
    baji_vsync_edge_t anchor;
    esp_err_t err = baji_vsync_wait(baji_vsync_snapshot().seq, 150, &anchor);
    if (err != ESP_OK) return err;
    uint32_t seq = anchor.seq, signature = pet_probe_signature();
    unsigned offset = 0;
    for (unsigned phase = 0; phase < 3; ++phase) {
        stage_t *s = &stages[phase];
        for (unsigned i = 0; i < counts[phase]; ++i) {
            baji_vsync_edge_t edge;
            err = baji_vsync_wait(seq, 150, &edge);
            if (err != ESP_OK) return err;
            s->missing += edge.seq-seq-1;
            seq = edge.seq;
            if (!i) s->first_us = edge.edge_us;
            s->last_us = edge.edge_us;

            baji_input_t input = input_at(phase, i);
            baji_pet_state_t old = player->shown;
            bool sent;
            int64_t start = esp_timer_get_time();
            err = pet_player_tick(player, board, &input, start, &sent);
            baji_vsync_edge_t done = baji_vsync_snapshot();
            if (err != ESP_OK) return err;
            baji_pet_state_t next = player->shown;
            bool pose = old.clip != next.clip || old.frame != next.frame || old.phase != next.phase;
            bool gaze = old.gaze_x != next.gaze_x || old.gaze_y != next.gaze_y;
            bool gaze_draw = gaze && !pose && sent;
            samples[offset+i] = (sample_t){done.observed_us-edge.edge_us, done.observed_us-start,
                                           sent, gaze_draw};
            if (sent) {
                uint32_t current = pet_probe_signature();
                s->visible += current != signature;
                signature = current;
            }
            s->draws += sent;
            s->poses += pose;
            s->gazes += gaze;
            s->gaze_only += gaze_draw;
            s->pat_start += old.clip != BAJI_PET_PAT && next.clip == BAJI_PET_PAT;
            s->pat_end += old.clip == BAJI_PET_PAT && next.clip != BAJI_PET_PAT;
            s->unchanged_draw += sent && !pose && !gaze;
            s->late += done.seq != seq;
            unsigned duration=done.observed_us-edge.edge_us;
            if (sent && duration>s->slowest_us) {
                s->slowest_us=duration;
                s->slow_select=pet_probe_bus_start()-start;
                s->slow_bus=done.observed_us-pet_probe_bus_start();
                s->old_sample=old.frame*baji_pet_substeps(player->art,old.clip)+old.phase;
                s->next_sample=next.frame*baji_pet_substeps(player->art,next.clip)+next.phase;
                s->slow_clip=next.clip;
            }
        }
        offset += counts[phase];
    }

    /* Report after all 600 edges so serial output and sorting never create
     * a gap between adjacent phases. */
    offset = 0;
    *functional = *on_time = true;
    for (unsigned phase = 0; phase < 3; ++phase) {
        const stage_t *s = &stages[phase];
        unsigned n = counts[phase];
        distribution_t active = distribution(samples+offset, n, ACTIVE);
        distribution_t noop = distribution(samples+offset, n, NOOP);
        distribution_t gaze = distribution(samples+offset, n, GAZE_ONLY);
        printf("PET_STAGE name=%s ticks=%u sent=%u noop=%u missed=%u late=%u span_us=%u "
               "subject_changes=%u subject_millihz=%u pose_changes=%u gaze_changes=%u gaze_only_sent=%u pat_start=%u pat_end=%u unchanged_sent=%u "
               "te_draw_p50=%u te_draw_p95=%u te_draw_max=%u noop_p95=%u noop_max=%u "
               "te_gaze_p95=%u te_gaze_max=%u\n",
               labels[phase], n, s->draws, n-s->draws, s->missing, s->late, (unsigned)(s->last_us-s->first_us),
               s->visible, (unsigned)(s->visible*INT64_C(1000000000)/(s->last_us-s->first_us)),
               s->poses, s->gazes, s->gaze_only, s->pat_start, s->pat_end, s->unchanged_draw,
               active.p50, active.p95, active.max, noop.p95, noop.max, gaze.p95, gaze.max);
        printf("PET_PEAK name=%s old=%u next=%u clip=%u select_us=%u bus_us=%u\n",
               labels[phase], s->old_sample,s->next_sample,s->slow_clip,s->slow_select,s->slow_bus);
        *functional &= s->visible*INT64_C(1000000) >= 36*(s->last_us-s->first_us);
        *functional &= s->poses > 0 && s->draws > 0 && s->draws < n && s->unchanged_draw == 0;
        *functional &= s->pat_start == (phase == 2) && s->pat_end == (phase == 2);
        if (phase == 0) *functional &= !s->gazes;
        if (phase == 1) *functional &= s->gaze_only > 0;
        *on_time &= !s->missing && !s->late;
        offset += n;
    }
    *functional &= player->shown.clip == BAJI_PET_IDLE;
    return ESP_OK;
}

void app_main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    vTaskDelay(pdMS_TO_TICKS(1400));
    baji_pet_t *art = NULL;
    esp_err_t err = receive_asset(&art);
    baji_board_t *board = NULL;
    baji_board_info_t info;
    if (err == ESP_OK) err = baji_board_open(&board, &info);
    if (err == ESP_OK) err = measure_edge_column(board, art);
    if (err == ESP_OK) err = measure_full(board, &info, art);
    if (err == ESP_OK) {
        printf("PET_MEMORY free_psram=%u free_internal=%u largest_internal_dma=%u\n",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
        pet_player_t player;
        baji_input_t input = {0};
        bool sent = false, functional = false, on_time = false;
        pet_probe_enable(true);
        int64_t start = esp_timer_get_time();
        pet_player_init(&player, art, &info, start);
        err = pet_player_tick(&player, board, &input, start, &sent);
        printf("PET_FIRST us=%u sent=%d\n", (unsigned)(esp_timer_get_time()-start), sent);
        if (err == ESP_OK && !sent) err = ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) err = baji_board_set_brightness(board, 45);
        if (err == ESP_OK) err = baji_vsync_open();
        if (err == ESP_OK) {
            err = measure(&player, board, &functional, &on_time);
            esp_err_t closed = baji_vsync_close();
            if (err == ESP_OK) err = closed;
        }
        if (err == ESP_OK && functional)
            err = check_runtime_schedule(board, &info, art);
        printf("PET_END ok=%d functional=%d deadlines=%d err=%d\n",
               err == ESP_OK && functional && on_time, functional, on_time, err);
        if (err == ESP_OK && functional) {
            pet_probe_enable(false);
            puts("PET_LIVE");
            err = pet_app_run_loaded(board, &info, art);
            printf("PET_LIVE_END err=%d\n", err);
        }
    } else {
        printf("PET_END ok=0 functional=0 deadlines=0 err=%d\n", err);
    }
    if (board) while (baji_board_close(board) != ESP_OK) vTaskDelay(pdMS_TO_TICKS(20));
    if (art) baji_pet_close(art);
}
