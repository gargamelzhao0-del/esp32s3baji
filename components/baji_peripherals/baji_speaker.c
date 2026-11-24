#include "baji_speaker.h"
#include "baji_board_profile.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include <stdlib.h>
#include <string.h>

enum { FRAMES = 128, DESCRIPTORS = 3 };
struct baji_speaker {
    i2s_chan_handle_t tx;
    baji_peripherals_t *peripherals;
    bool running;
    int16_t stereo[FRAMES * 2];
};

esp_err_t baji_speaker_open(baji_peripherals_t *p, baji_speaker_t **out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!p) return ESP_ERR_INVALID_ARG;
    baji_speaker_t *s = calloc(1, sizeof(*s));
    if (!s) return ESP_ERR_NO_MEM;
    s->peripherals = p;
    gpio_config_t pa = {.pin_bit_mask = 1ULL << BAJI_AUDIO_PA, .mode = GPIO_MODE_OUTPUT};
    esp_err_t err = gpio_set_level(BAJI_AUDIO_PA, 0);
    if (err == ESP_OK) err = gpio_config(&pa);
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channel.dma_desc_num = DESCRIPTORS;
    channel.dma_frame_num = FRAMES;
    channel.auto_clear_after_cb = true;
    if (err == ESP_OK) err = i2s_new_channel(&channel, &s->tx, NULL);
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = BAJI_AUDIO_MCLK, .bclk = BAJI_AUDIO_BCLK, .ws = BAJI_AUDIO_WS,
                     .dout = BAJI_AUDIO_OUT, .din = I2S_GPIO_UNUSED}
    };
    if (err == ESP_OK) err = i2s_channel_init_std_mode(s->tx, &cfg);
    if (err != ESP_OK) {
        baji_speaker_close(s);
        return err;
    }
    *out = s;
    return ESP_OK;
}

esp_err_t baji_speaker_start(baji_speaker_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    if (s->running) return ESP_OK;
    esp_err_t err = i2s_channel_enable(s->tx);
    if (err != ESP_OK) return err;
    s->running = true;
    err = baji_peripherals_speaker_init(s->peripherals);
    if (err == ESP_OK) err = gpio_set_level(BAJI_AUDIO_PA, 1);
    if (err != ESP_OK) baji_speaker_stop(s, false);
    return err;
}

static esp_err_t write_stereo(baji_speaker_t *s, size_t bytes)
{
    size_t written = 0;
    esp_err_t err = i2s_channel_write(s->tx, s->stereo, bytes, &written, 64);
    if (err == ESP_OK && written != bytes) return ESP_ERR_TIMEOUT;
    return err;
}

esp_err_t baji_speaker_write(baji_speaker_t *s, const uint8_t *mono, size_t frames)
{
    if (!s || !mono) return ESP_ERR_INVALID_ARG;
    if (!s->running) return ESP_ERR_INVALID_STATE;
    while (frames) {
        size_t chunk = frames < FRAMES ? frames : FRAMES;
        for (size_t i=0; i<chunk; i++, mono+=2)
            s->stereo[2*i] = s->stereo[2*i+1] = (int16_t)(mono[0] | (uint16_t)mono[1]<<8);
        esp_err_t err = write_stereo(s, chunk * 2 * sizeof(int16_t));
        if (err != ESP_OK) return err;
        frames -= chunk;
    }
    return ESP_OK;
}

void baji_speaker_stop(baji_speaker_t *s, bool drain)
{
    if (!s || !s->running) return;
    if (!drain) gpio_set_level(BAJI_AUDIO_PA, 0);
    /* Flush a full ring plus the partial descriptor under construction before
     * stopping clocks, so the following unmute starts with silence. */
    memset(s->stereo, 0, sizeof(s->stereo));
    for (int i=0; i<=DESCRIPTORS; i++)
        if (write_stereo(s, sizeof(s->stereo)) != ESP_OK) break;
    gpio_set_level(BAJI_AUDIO_PA, 0);
    i2s_channel_disable(s->tx);
    s->running = false;
}

void baji_speaker_close(baji_speaker_t *s)
{
    if (!s) return;
    baji_speaker_stop(s, false);
    if (s->tx) i2s_del_channel(s->tx);
    free(s);
}
