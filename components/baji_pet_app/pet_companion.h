#pragma once
#include "pet_app.h"
#include "baji_pet_clips.h"

esp_err_t pet_companion_run(baji_board_t *board, const baji_board_info_t *info, baji_pet_t *smooth,
                            pet_clips_t *album, const baji_agent_feed_t *feed);
