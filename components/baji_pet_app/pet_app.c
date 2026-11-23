#include "pet_app.h"
#include "pet_player.h"
#include "baji_agent_ui.h"
#include "baji_runtime.h"
#include "esp_timer.h"

typedef struct {
    pet_player_t player;
    baji_board_t *board;
    baji_agent_ui_t *agent;
} pet_app_t;

static void paint_connected(const baji_rect_t *band, uint8_t *pixels, void *context)
{
    pet_app_t *app = context;
    baji_pet_paint(band, pixels, app->player.art);
    baji_agent_ui_paint(app->agent, band, pixels);
}

/* Gaze follows the task's prop; contact still takes precedence in the player. */
static void agent_attention(pet_player_t *p, baji_agent_cue_t cue, int64_t now)
{
    int x = 0, y = 0;
    switch (cue) {
        case BAJI_CUE_THINK: x = -1; y = -1; break;
        case BAJI_CUE_READ: x = ((now / 900000) % 2) ? 0 : -1; y = 1; break;
        case BAJI_CUE_WRITE: x = 1; y = 1; break;
        case BAJI_CUE_RUN: x = 2; break;
        case BAJI_CUE_SEARCH: x = ((now / 1200000) % 2) ? 2 : -2; break;
        case BAJI_CUE_COMPACT: y = 1; break;
        case BAJI_CUE_SUCCESS: y = -1; break;
        case BAJI_CUE_LIMIT: x = -1; y = 1; break;
        case BAJI_CUE_ERROR: x = 1; y = -1; break;
        default: break;
    }
    p->rest_gaze_x = x;
    p->rest_gaze_y = y;
}

static esp_err_t draw(void *context, const baji_input_t *input, int64_t now, int64_t *next)
{
    pet_app_t *app = context;
    if (app->agent) {
        baji_agent_cue_t cue = baji_agent_ui_update(app->agent, now, &app->player.damage);
        agent_attention(&app->player, cue, now);
        bool visible = baji_agent_ui_visible(app->agent);
        app->player.paint = visible ? paint_connected : baji_pet_paint;
        app->player.paint_context = visible ? (void *)app : (void *)app->player.art;
    }
    bool submitted;
    esp_err_t err = pet_player_tick(&app->player, app->board, input, now, &submitted);
    if (err == ESP_OK) {
        *next = pet_player_next_update(&app->player, input, now);
        if (app->agent) *next = baji_agent_ui_deadline(app->agent, now, *next);
    }
    return err;
}

esp_err_t pet_app_run_loaded_connected(baji_board_t *board, const baji_board_info_t *info,
                                       baji_pet_t *art, const baji_agent_feed_t *feed)
{
    if (!board || !info || !art || (feed && !feed->read)) return ESP_ERR_INVALID_ARG;
    pet_app_t app = {.board = board};
    if (feed && !(app.agent = baji_agent_ui_open(feed))) return ESP_ERR_NO_MEM;
    pet_player_init(&app.player, art, info, esp_timer_get_time());
    const baji_runtime_config_t config = {.brightness = 45, .changed = feed ? feed->changed : NULL};
    esp_err_t err = baji_runtime_run(board, info, &config, draw, &app);
    baji_agent_ui_close(app.agent);
    return err;
}

esp_err_t pet_app_run_loaded(baji_board_t *board, const baji_board_info_t *info, baji_pet_t *art)
{
    return pet_app_run_loaded_connected(board, info, art, NULL);
}

esp_err_t pet_app_run_connected(baji_board_t *board, const baji_board_info_t *info,
                                const baji_agent_feed_t *feed)
{
    baji_pet_t *art = NULL;
    esp_err_t err = baji_pet_open(&art);
    if (err == ESP_OK) err = pet_app_run_loaded_connected(board, info, art, feed);
    baji_pet_close(art);
    return err;
}

esp_err_t pet_app_run(baji_board_t *board, const baji_board_info_t *info)
{
    return pet_app_run_connected(board, info, NULL);
}
