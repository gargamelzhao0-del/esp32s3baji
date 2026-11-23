#pragma once

#include "baji_pet.h"
#include "baji_scene_damage.h"

/* The touch reader can see both edges of a tap between two display scans. */
typedef struct {
    baji_touch_sample_t touch;
    uint32_t presses;
    uint16_t press_x, press_y;
} pet_input_t;

typedef struct {
    baji_pet_t *art;
    baji_damage_t damage;
    baji_pet_state_t shown;
    bool have_frame, patting;
    uint32_t last_press;
    int64_t idle_since, pat_since;
} pet_player_t;

void pet_player_init(pet_player_t *player, baji_pet_t *art,
                     const baji_board_info_t *info, int64_t now_us);
/* submitted is true only after all changed pixels have reached the panel bus. */
esp_err_t pet_player_tick(pet_player_t *player, baji_board_t *board,
                          const pet_input_t *input, int64_t now_us, bool *submitted);
