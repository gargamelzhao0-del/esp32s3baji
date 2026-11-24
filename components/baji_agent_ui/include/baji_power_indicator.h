#pragma once

#include "baji_input.h"
#include "baji_scene_damage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    baji_power_reading_t reading;
    uint64_t rows[12]; /* 2x bitmap of the current icon and label. */
    uint16_t accent;
} baji_power_indicator_t;

void baji_power_indicator_update(baji_power_indicator_t *indicator, baji_power_reading_t reading,
                                 baji_damage_t *damage);
void baji_power_indicator_paint(const baji_power_indicator_t *indicator, const baji_rect_t *band,
                                uint8_t *pixels);

#ifdef __cplusplus
}
#endif
