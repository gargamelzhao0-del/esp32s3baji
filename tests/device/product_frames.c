#include "baji_board.h"
#include "deepseek_review/main/body_probe.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

/* Only the display owner calls the board painter. Sample the outgoing RGB565
 * character region after it is drawn. Exclude the model label and status HUD. */
esp_err_t __real_baji_board_paint_round_regions(baji_board_t *, const baji_rect_t *,
                                                size_t, baji_paint_fn, void *);
esp_err_t __wrap_baji_board_paint_round_regions(baji_board_t *board, const baji_rect_t *regions,
                                                size_t count, baji_paint_fn paint, void *context)
{
    static int64_t start = -1;
    static unsigned submitted, changed, errors;
    int64_t now = esp_timer_get_time();
    if (start < 0) start = now;
    observed_paint_t obs = {.original = paint, .scene = context};
    esp_err_t err = __real_baji_board_paint_round_regions(board, regions, count, observe_band, &obs);
    if (obs.changed) finish_body_observation(err == ESP_OK);
    submitted++;
    if (err != ESP_OK) errors++;
    else if (obs.changed) changed++;
    int64_t end = esp_timer_get_time();
    int64_t elapsed = end - start;
    if (elapsed >= 5000000) {
        ESP_LOGI("baji_frames", "ms=%u submitted=%u character=%u errors=%u dma_free=%u",
                 (unsigned)(elapsed / 1000), submitted, changed, errors,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        start = end;
        submitted = changed = errors = 0;
    }
    return err;
}
