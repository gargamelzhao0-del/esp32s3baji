#pragma once

#include "baji_board.h"
#include "baji_pet.h"

esp_err_t pet_app_run(baji_board_t *board, const baji_board_info_t *info);
/* The RAM qualification app supplies the same asset pack over USB. */
esp_err_t pet_app_run_loaded(baji_board_t *board, const baji_board_info_t *info, baji_pet_t *art);
