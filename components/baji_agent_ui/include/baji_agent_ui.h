#pragma once
#include "baji_agent_feed.h"
#include "baji_scene_damage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BAJI_CUE_LOCAL, BAJI_CUE_READY, BAJI_CUE_THINK, BAJI_CUE_READ, BAJI_CUE_WRITE,
    BAJI_CUE_RUN, BAJI_CUE_SEARCH, BAJI_CUE_WAIT, BAJI_CUE_SUCCESS, BAJI_CUE_ERROR,
    BAJI_CUE_LIMIT, BAJI_CUE_COMPACT, BAJI_CUE_DONE, BAJI_CUE_OFFLINE,
    BAJI_CUE_NO_SESSION, BAJI_CUE_FAILED, BAJI_CUE_CANCELLED
} baji_agent_cue_t;

typedef struct baji_agent_ui baji_agent_ui_t;
baji_agent_ui_t *baji_agent_ui_open(const baji_agent_feed_t *feed);
void baji_agent_ui_close(baji_agent_ui_t *ui);
/* Display owner only. Invalidates only panels whose displayed contents change.
 * The caller keeps damage until its normal display submission succeeds. */
baji_agent_cue_t baji_agent_ui_update(baji_agent_ui_t *ui, int64_t now_us, baji_damage_t *damage);
int64_t baji_agent_ui_deadline(const baji_agent_ui_t *ui, int64_t now_us, int64_t scene_due);
bool baji_agent_ui_visible(const baji_agent_ui_t *ui);
/* Read on the display owner after update(), including when the pose is unchanged. */
uint32_t baji_agent_ui_generation(const baji_agent_ui_t *ui);
void baji_agent_ui_paint(const baji_agent_ui_t *ui, const baji_rect_t *band, uint8_t *pixels);

#ifdef __cplusplus
}
#endif
