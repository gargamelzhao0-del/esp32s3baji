#pragma once
#include <stddef.h>
#include "baji_board.h"
#include "baji_input.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t deepseek_app_run(baji_board_t *board, const baji_board_info_t *info);
/* The caller owns the immutable buffer for the entire synchronous run. */
esp_err_t deepseek_app_run_loaded(baji_board_t *board, const baji_board_info_t *info,
                                  const void *pack, size_t bytes);
esp_err_t deepseek_app_run_pair(baji_board_t *board, const baji_board_info_t *info,
                               const void *pixel, size_t pixel_bytes, const void *hd, size_t hd_bytes);
esp_err_t deepseek_app_run_motion(baji_board_t *board, const baji_board_info_t *info,
                                 const void *pixel, size_t pixel_bytes, const void *hd, size_t hd_bytes,
                                 const void *motion, size_t motion_bytes);

/* Single-owner presentation state. open validates the borrowed packs; an
 * absent HD pack selects pixel-only operation. tick accepts the same input
 * snapshot and clock as the board runtime and completes all submitted DMA. */
typedef struct deepseek_app deepseek_app_t;
esp_err_t deepseek_app_open(deepseek_app_t **out, baji_board_t *board,
                           const baji_board_info_t *info, const void *pixel, size_t pixel_bytes,
                           const void *hd, size_t hd_bytes, int64_t now_us);
esp_err_t deepseek_app_open_motion(deepseek_app_t **out, baji_board_t *board,
                                  const baji_board_info_t *info, const void *pixel, size_t pixel_bytes,
                                  const void *hd, size_t hd_bytes, const void *motion, size_t motion_bytes,
                                  int64_t now_us);
esp_err_t deepseek_app_tick(void *app, const baji_input_t *input, int64_t now_us, int64_t *next);
bool deepseek_app_is_hd(const deepseek_app_t *app);
void deepseek_app_close(deepseek_app_t *app);

#ifdef __cplusplus
}
#endif
