#pragma once
#include "baji_peripherals.h"
#include <stddef.h>

/* Single playback owner. Frames are little-endian S16 mono at 16 kHz.
 * The driver duplicates each sample into the DAC's two I2S slots. */
typedef struct baji_speaker baji_speaker_t;
esp_err_t baji_speaker_open(baji_peripherals_t *peripherals, baji_speaker_t **out);
esp_err_t baji_speaker_start(baji_speaker_t *spk);
esp_err_t baji_speaker_write(baji_speaker_t *spk, const uint8_t *mono, size_t frames);
/* drain=true plays the remaining DMA tail before muting the amplifier. */
void baji_speaker_stop(baji_speaker_t *spk, bool drain);
void baji_speaker_close(baji_speaker_t *spk);
