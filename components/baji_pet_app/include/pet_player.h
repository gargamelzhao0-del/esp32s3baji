#pragma once

#include "baji_pet.h"
#include "baji_scene_damage.h"
#include "baji_board.h"
#include "baji_input.h"

typedef struct {
    baji_pet_t *art;
    baji_damage_t damage;
    baji_paint_fn paint;
    void *paint_context;
    int8_t rest_gaze_x, rest_gaze_y;
    baji_pet_state_t shown;
    int (*cheek_at)(const baji_pet_t *, baji_pet_state_t, uint16_t, uint16_t);
    baji_pet_clip_t action;
    bool have_frame, cheek_held, cheek_moved;
    uint8_t tap_count, turns;
    int8_t direction;
    int extreme_x;
    uint32_t last_press;
    int64_t idle_since, action_since, tap_times[2];
} pet_player_t;

void pet_player_init(pet_player_t *player, baji_pet_t *art,
                     const baji_board_info_t *info, int64_t now_us);
/* submitted is true only after all changed pixels have reached the panel bus. */
esp_err_t pet_player_tick(pet_player_t *player, baji_board_t *board,
                          const baji_input_t *input, int64_t now_us, bool *submitted);
int64_t pet_player_next_update(const pet_player_t *player, const baji_input_t *input, int64_t now_us);
