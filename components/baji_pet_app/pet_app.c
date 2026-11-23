#include "pet_app.h"
#include "pet_player.h"
#include "baji_runtime.h"
#include "esp_timer.h"

typedef struct {
    pet_player_t player;
    baji_board_t *board;
} pet_app_t;

static esp_err_t draw(void *context, const baji_input_t *input, int64_t now, int64_t *next)
{
    pet_app_t *app = context;
    bool submitted;
    esp_err_t err = pet_player_tick(&app->player, app->board, input, now, &submitted);
    if (err == ESP_OK) *next = pet_player_next_update(&app->player, input, now);
    return err;
}

esp_err_t pet_app_run_loaded(baji_board_t *board, const baji_board_info_t *info, baji_pet_t *art)
{
    pet_app_t app = {.board = board};
    pet_player_init(&app.player, art, info, esp_timer_get_time());
    const baji_runtime_config_t config = {.brightness = 45};
    return baji_runtime_run(board, info, &config, draw, &app);
}

esp_err_t pet_app_run(baji_board_t *board, const baji_board_info_t *info)
{
    baji_pet_t *art = NULL;
    esp_err_t err = baji_pet_open(&art);
    if (err == ESP_OK) err = pet_app_run_loaded(board, info, art);
    baji_pet_close(art);
    return err;
}
