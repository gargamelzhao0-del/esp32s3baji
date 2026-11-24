#include "pet_feedback.h"
#include "baji_speaker.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>

typedef struct { const char *event; uint32_t first, count; } tone_t;
static const tone_t tones[] = {
#include "pet_tones.inc"
};
extern const uint8_t pcm_start[] asm("_binary_pet_tones_pcm_start");
extern const uint8_t pcm_end[] asm("_binary_pet_tones_pcm_end");
static const char *const TAG = "pet_feedback";
enum { SOUND_ON = 256, CHUNK = 128 };

typedef struct {
    const tone_t *tone;
    uint16_t settings;
    bool quit;
} notice_t;

struct pet_feedback {
    baji_peripherals_t *peripherals;
    baji_speaker_t *speaker;
    QueueHandle_t inbox;
    SemaphoreHandle_t done;
    nvs_handle_t nvs;
    bool nvs_open;
    uint16_t requested, stored;
};

static uint16_t settings(unsigned bond, bool sound)
{
    return (bond > 100 ? 100 : bond) | (sound ? SOUND_ON : 0);
}

static void save(pet_feedback_t *fb, uint16_t value)
{
    if (!fb->nvs_open || value == fb->stored) return;
    esp_err_t err = nvs_set_u16(fb->nvs, "state", value);
    if (err == ESP_OK) err = nvs_commit(fb->nvs);
    if (err == ESP_OK) fb->stored = value;
    else ESP_LOGW(TAG, "settings write: %s", esp_err_to_name(err));
}

static esp_err_t start(pet_feedback_t *fb)
{
    if (!fb->speaker) {
        esp_err_t err = baji_speaker_open(fb->peripherals, &fb->speaker);
        if (err != ESP_OK) return err;
    }
    return baji_speaker_start(fb->speaker);
}

static void worker(void *arg)
{
    pet_feedback_t *fb = arg;
    const uint8_t *samples = NULL;
    uint32_t remaining = 0;
    uint16_t value = fb->stored;
    notice_t next;
    bool warned = false, confirmed_audio = false;
    for (;;) {
        if (xQueueReceive(fb->inbox, &next, remaining ? 0 : portMAX_DELAY) == pdTRUE) {
            value = next.settings;
            if (next.quit || !(value & SOUND_ON)) {
                baji_speaker_stop(fb->speaker, false);
                remaining = 0;
                save(fb, value);
                if (next.quit) break;
            } else if (next.tone) {
                const tone_t *t = next.tone;
                if ((size_t)(t->first+t->count) <= (size_t)(pcm_end-pcm_start)/2) {
                    esp_err_t err = start(fb);
                    if (err == ESP_OK) {
                        samples = pcm_start + 2*t->first;
                        remaining = t->count;
                        warned = false;
                    } else if (!warned) {
                        ESP_LOGW(TAG, "speaker start: %s", esp_err_to_name(err));
                        warned = true;
                    }
                }
            }
        }
        if (remaining) {
            size_t count = remaining < CHUNK ? remaining : CHUNK;
            esp_err_t err = baji_speaker_write(fb->speaker, samples, count);
            if (err == ESP_OK) {
                samples += 2*count;
                remaining -= count;
            } else {
                remaining = 0;
                ESP_LOGW(TAG, "speaker write: %s", esp_err_to_name(err));
            }
            if (!remaining) {
                baji_speaker_stop(fb->speaker, err == ESP_OK);
                if (err == ESP_OK && !confirmed_audio) {
                    ESP_LOGI(TAG, "first chime sent to ES8311 at 16000 Hz");
                    confirmed_audio = true;
                }
            }
        }
        if (!remaining) save(fb, value);
    }
    baji_speaker_close(fb->speaker);
    xSemaphoreGive(fb->done);
    vTaskDelete(NULL);
}

static void dispose(pet_feedback_t *fb)
{
    if (fb->nvs_open) nvs_close(fb->nvs);
    if (fb->inbox) vQueueDelete(fb->inbox);
    if (fb->done) vSemaphoreDelete(fb->done);
    free(fb);
}

pet_feedback_t *pet_feedback_open(baji_board_t *board, unsigned *bond, bool *sound)
{
    if (!bond || !sound) return NULL;
    *bond = 0;
    *sound = true;
    pet_feedback_t *fb = calloc(1, sizeof(*fb));
    if (!fb) return NULL;
    fb->peripherals = board ? baji_board_peripherals(board) : NULL;
    uint16_t value = SOUND_ON;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_OK) err = nvs_open("baji_pet", NVS_READWRITE, &fb->nvs);
    if (err == ESP_OK) {
        fb->nvs_open = true;
        uint16_t stored;
        err = nvs_get_u16(fb->nvs, "state", &stored);
        if (err == ESP_OK && (stored & ~SOUND_ON) <= 100) value = stored;
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "settings read: %s", esp_err_to_name(err));
    fb->stored = fb->requested = value;
    *bond = value & 0xff;
    *sound = (value & SOUND_ON) != 0;
    fb->inbox = xQueueCreate(1, sizeof(notice_t));
    fb->done = xSemaphoreCreateBinary();
    if (!fb->inbox || !fb->done || xTaskCreate(worker, "pet_audio", 4096, fb, 4, NULL) != pdPASS) {
        dispose(fb);
        return NULL;
    }
    return fb;
}

void pet_feedback_update(pet_feedback_t *fb, const char *event, unsigned bond, bool sound)
{
    if (!fb) return;
    notice_t n = {.settings = settings(bond, sound)};
    if (sound && event) {
        for (size_t i=0; i<sizeof(tones)/sizeof(*tones); i++) {
            if (!strcmp(event, tones[i].event)) { n.tone = &tones[i]; break; }
        }
    }
    if (!n.tone && n.settings == fb->requested) return;
    fb->requested = n.settings;
    xQueueOverwrite(fb->inbox, &n);
}

void pet_feedback_close(pet_feedback_t *fb)
{
    if (!fb) return;
    notice_t n = {.settings = fb->requested, .quit = true};
    xQueueOverwrite(fb->inbox, &n);
    xSemaphoreTake(fb->done, portMAX_DELAY);
    dispose(fb);
}
