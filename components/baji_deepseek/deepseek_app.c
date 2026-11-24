#include "baji_deepseek_app.h"
#include "baji_runtime.h"
#include "baji_agent_ui.h"
#include "baji_scene_damage.h"
#include "deepseek_controls.h"
#include "esp_timer.h"
#if defined(ESP_PLATFORM) && !defined(BAJI_DEEPSEEK_EXTERNAL_ASSET)
#include "esp_log.h"
#endif
#include <stdlib.h>
#include <string.h>

struct deepseek_app {
    dsk_controls_t ui;
    dsk_art_t pixel;
    dsk_canvas_t canvas;
    dsh_art_t hd;
    dsh_motion_t motion;
    dsh_canvas_t hd_canvas;
    dsh_view_t selected_hd;
    unsigned selected_hd_mix;
    bool hd_initialized, hd_selected;
    baji_damage_t pending;
    baji_board_t *board;
    baji_agent_ui_t *agent;
};

static void mark(void *context, baji_rect_t area)
{
    baji_damage_add(context, area);
}

static void full_screen(deepseek_app_t *app)
{
    baji_damage_mark(&app->pending, 0, 0, DSK_SIDE, DSK_SIDE);
}

static esp_err_t prepare_hd(deepseek_app_t *app)
{
    dsh_view_t view = app->ui.hd;
    view.item = view.gallery ? 0 : app->ui.visible_item;
    if (!app->hd_initialized) {
        dsk_canvas_release_motion(&app->canvas);
        if (!dsh_canvas_init(&app->hd_canvas, &app->hd)) return ESP_ERR_NO_MEM;
        if (app->motion.count) dsh_canvas_set_motion(&app->hd_canvas, &app->motion);
        app->hd_initialized = true;
        full_screen(app);
    }
    unsigned mix = view.gallery ? 0 : app->ui.breath_mix;
    if (app->hd_selected && app->selected_hd_mix == mix &&
        !memcmp(&app->selected_hd, &view, sizeof(view))) return ESP_OK;
    if (!dsh_canvas_select_blend(&app->hd_canvas, view, mix)) {
        /* A failed decode can already have released the previous image.
         * Force reselection even if the next input goes back to that pose. */
        app->hd_selected = false;
        full_screen(app);
        return ESP_FAIL;
    }
    if (!app->hd_selected || view.gallery || app->selected_hd.gallery) full_screen(app);
    else if (view.item != app->selected_hd.item || view.guides != app->selected_hd.guides)
        baji_damage_mark(&app->pending, DSH_INSET, DSH_INSET, DSH_STAGE, DSH_STAGE);
    else {
        baji_rect_t areas[DSH_DAMAGE_STRIPS];
        unsigned from = app->selected_hd.breath * 8 + app->selected_hd_mix;
        unsigned n = dsh_canvas_motion_damage(&app->hd_canvas, from, areas);
        for (unsigned i = 0; i < n; i++) baji_damage_add(&app->pending, areas[i]);
    }
    app->selected_hd = view;
    app->selected_hd_mix = mix;
    app->hd_selected = true;
    return ESP_OK;
}

static esp_err_t prepare_pixel(deepseek_app_t *app)
{
    if (app->hd_initialized) {
        dsh_canvas_close(&app->hd_canvas);
        app->hd_initialized = app->hd_selected = false;
        full_screen(app);
    }
    dsk_frame_t frame = app->ui.pixel.shown;
    unsigned mix = app->ui.pixel.bubble_mix;
    unsigned sub = app->ui.pixel.subframe;
    if (frame.item != app->ui.visible_item) {
        frame.item = app->ui.visible_item;
        frame.frame = sub = 0;
    }
    if (!dsk_canvas_damage_motion(&app->canvas, frame, mix, sub, mark, &app->pending) ||
        !dsk_canvas_select_motion(&app->canvas, frame, mix, sub)) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

/* Pose order is the asset pack's state table after its six local clips. */
static int projected_item(baji_agent_cue_t cue)
{
    switch (cue) {
        case BAJI_CUE_THINK: return DSK_CLIPS + 1;
        case BAJI_CUE_READ: return DSK_CLIPS + 2;
        case BAJI_CUE_WRITE: return DSK_CLIPS + 3;
        case BAJI_CUE_RUN: return DSK_CLIPS + 4;
        case BAJI_CUE_SEARCH: return DSK_CLIPS + 5;
        case BAJI_CUE_WAIT: return DSK_CLIPS + 6;
        case BAJI_CUE_SUCCESS: return DSK_CLIPS + 7;
        case BAJI_CUE_ERROR: return DSK_CLIPS + 8;
        case BAJI_CUE_LIMIT: return DSK_CLIPS + 9;
        case BAJI_CUE_COMPACT: return DSK_CLIPS + 10;
        default: return 0;
    }
}

static void paint_connected(const baji_rect_t *band, uint8_t *pixels, void *context)
{
    deepseek_app_t *app = context;
    if (app->ui.hd_mode) dsh_paint(band, pixels, &app->hd_canvas);
    else dsk_paint(band, pixels, &app->canvas);
    baji_agent_ui_paint(app->agent, band, pixels);
}

esp_err_t deepseek_app_tick(void *context, const baji_input_t *input, int64_t now, int64_t *next)
{
    if (!context || !input || !next) return ESP_ERR_INVALID_ARG;
    deepseek_app_t *app = context;
    bool gallery = app->ui.hd_mode && app->ui.hd.gallery;
    baji_agent_cue_t cue = baji_agent_ui_update(app->agent, now, gallery ? NULL : &app->pending);
    *next = dsk_controls_tick(&app->ui, input, projected_item(cue), now);
    *next = baji_agent_ui_deadline(app->agent, now, *next);
    bool hd = app->ui.hd_mode;
    bool overlay = baji_agent_ui_visible(app->agent) && !(hd && app->ui.hd.gallery);
    esp_err_t err = hd ? prepare_hd(app) : prepare_pixel(app);
    if (err != ESP_OK || !app->pending.count) return err;
    baji_paint_fn painter = hd ? dsh_paint : dsk_paint;
    void *canvas = hd ? (void *)&app->hd_canvas : (void *)&app->canvas;
    err = baji_board_paint_round_regions(app->board, app->pending.regions, app->pending.count,
                                         overlay ? paint_connected : painter, overlay ? app : canvas);
    /* Keep damage across partial failures, then also add changes from the
     * latest selection. The next successful submission restores both. */
    if (err == ESP_OK) baji_damage_clear(&app->pending);
    return err;
}

bool deepseek_app_is_hd(const deepseek_app_t *app)
{
    return app && app->ui.hd_mode;
}

esp_err_t deepseek_app_open_motion(deepseek_app_t **out, baji_board_t *board, const baji_board_info_t *info,
                                  const void *pixel, size_t pixel_bytes, const void *hd, size_t hd_bytes,
                                  const void *motion, size_t motion_bytes, int64_t now)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!board || !info || !pixel || (hd != NULL) != (hd_bytes != 0) ||
        (motion != NULL) != (motion_bytes != 0) || (motion && !hd)) return ESP_ERR_INVALID_ARG;
    if (info->width != DSK_SIDE || info->height != DSK_SIDE) return ESP_ERR_NOT_SUPPORTED;
    deepseek_app_t *app = calloc(1, sizeof(*app));
    if (!app) return ESP_ERR_NO_MEM;
    if (!dsk_art_open(&app->pixel, pixel, pixel_bytes) || (hd && !dsh_art_open(&app->hd, hd, hd_bytes)) ||
        (motion && !dsh_motion_open(&app->motion, motion, motion_bytes))) {
        free(app);
        return ESP_ERR_INVALID_RESPONSE;
    }
    app->board = board;
    dsk_canvas_init(&app->canvas, &app->pixel);
    /* Validation can scan several MB. Anchor the first real pose to the
     * presentation boundary; deterministic replay may supply its own clock. */
    if (now < 0) now = esp_timer_get_time();
    dsk_controls_init(&app->ui, app->pixel.bubble_steps, hd != NULL, now);
    app->ui.pixel.motion_steps = dsk_art_motion_steps(&app->pixel);
    baji_damage_init(&app->pending, info->width, info->height);
    *out = app;
    return ESP_OK;
}

esp_err_t deepseek_app_open(deepseek_app_t **out, baji_board_t *board, const baji_board_info_t *info,
                           const void *pixel, size_t pixel_bytes, const void *hd, size_t hd_bytes, int64_t now)
{
    return deepseek_app_open_motion(out, board, info, pixel, pixel_bytes, hd, hd_bytes, NULL, 0, now);
}

esp_err_t deepseek_app_attach_feed(deepseek_app_t *app, const baji_agent_feed_t *feed)
{
    if (!app || !feed || !feed->read) return ESP_ERR_INVALID_ARG;
    if (app->agent) return ESP_ERR_INVALID_STATE;
    app->agent = baji_agent_ui_open(feed);
    return app->agent ? ESP_OK : ESP_ERR_NO_MEM;
}

void deepseek_app_close(deepseek_app_t *app)
{
    if (!app) return;
    baji_agent_ui_close(app->agent);
    if (app->hd_initialized) dsh_canvas_close(&app->hd_canvas);
    dsk_canvas_release_motion(&app->canvas);
    dsk_art_close(&app->pixel);
    free(app);
}

esp_err_t deepseek_app_run_motion(baji_board_t *board, const baji_board_info_t *info,
                                 const void *pixel, size_t bytes, const void *hd, size_t hd_bytes,
                                 const void *motion, size_t motion_bytes)
{
    deepseek_app_t *app = NULL;
    esp_err_t err = deepseek_app_open_motion(&app, board, info, pixel, bytes, hd, hd_bytes,
                                             motion, motion_bytes, -1);
    if (err == ESP_OK) {
        const baji_runtime_config_t config = {.brightness = 45};
        err = baji_runtime_run(board, info, &config, deepseek_app_tick, app);
    }
    deepseek_app_close(app);
    return err;
}

esp_err_t deepseek_app_run_pair(baji_board_t *board, const baji_board_info_t *info,
                               const void *pixel, size_t bytes, const void *hd, size_t hd_bytes)
{
    return deepseek_app_run_motion(board, info, pixel, bytes, hd, hd_bytes, NULL, 0);
}

esp_err_t deepseek_app_run_loaded(baji_board_t *board, const baji_board_info_t *info,
                                  const void *pixel, size_t bytes)
{
    return deepseek_app_run_pair(board, info, pixel, bytes, NULL, 0);
}

esp_err_t deepseek_app_run_connected(baji_board_t *board, const baji_board_info_t *info,
                                     const baji_agent_feed_t *feed)
{
#ifdef BAJI_DEEPSEEK_EXTERNAL_ASSET
    (void)board;
    (void)info;
    (void)feed;
    return ESP_ERR_INVALID_STATE;
#else
    extern const uint8_t start[] asm("_binary_scene_dsk_start");
    extern const uint8_t end[] asm("_binary_scene_dsk_end");
    size_t hd_bytes = 0;
    const void *hd = dsh_builtin(&hd_bytes);
    if (!hd || !hd_bytes) return ESP_ERR_INVALID_STATE;
    dsh_motion_t motion = {0};
    if (!dsh_partition_motion_open(&motion)) {
#ifdef ESP_PLATFORM
        ESP_LOGW("deepseek", "HD motion pack unavailable; using pose-based breathing");
#endif
    }
    deepseek_app_t *app = NULL;
    esp_err_t err = deepseek_app_open(&app, board, info, start, (size_t)(end - start), hd, hd_bytes, -1);
    if (err == ESP_OK && feed) err = deepseek_app_attach_feed(app, feed);
    if (err == ESP_OK) {
        app->motion = motion;
#ifdef ESP_PLATFORM
        ESP_LOGI("deepseek", "pixel + HD ready: %u breathing frames, %u bytes, default feed-rice/day/full",
                 motion.count, (unsigned)motion.length);
#endif
        const baji_runtime_config_t config = {.brightness = 45, .changed = feed ? feed->changed : NULL};
        err = baji_runtime_run(board, info, &config, deepseek_app_tick, app);
    }
    deepseek_app_close(app);
    dsh_partition_motion_close(&motion);
    return err;
#endif
}

esp_err_t deepseek_app_run(baji_board_t *board, const baji_board_info_t *info)
{
    return deepseek_app_run_connected(board, info, NULL);
}
