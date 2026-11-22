#pragma once
#include "baji_board.h"

/* Built-in board check: RGB order, orientation, live touch and power readout. */
esp_err_t baji_board_check_run(baji_board_t *board, const baji_board_info_t *info);
