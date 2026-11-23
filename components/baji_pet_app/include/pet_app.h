#pragma once

#include "baji_board.h"
#include "baji_pet.h"
#include "baji_agent_feed.h"

esp_err_t pet_app_run_connected(baji_board_t *board, const baji_board_info_t *info,
                                const baji_agent_feed_t *feed);
esp_err_t pet_app_run_loaded_connected(baji_board_t *board, const baji_board_info_t *info,
                                       baji_pet_t *art, const baji_agent_feed_t *feed);

esp_err_t pet_app_run(baji_board_t *board, const baji_board_info_t *info);
/* The RAM qualification app supplies the same asset pack over USB. */
esp_err_t pet_app_run_loaded(baji_board_t *board, const baji_board_info_t *info, baji_pet_t *art);
