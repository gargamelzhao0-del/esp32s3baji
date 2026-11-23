#pragma once
#include "baji_board.h"
#include "baji_deepseek_hd.h"

/* The feed scene's sixteen exact breathing pages are resident during the trace. */
esp_err_t breath_trace(baji_board_t *board, const baji_board_info_t *info,
                       dsh_canvas_t *canvas, const dsh_motion_t pages[16]);
