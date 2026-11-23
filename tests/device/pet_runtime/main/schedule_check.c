#include "schedule_check.h"
#include "baji_runtime.h"
#include "baji_vsync.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "pet_player.h"
#include <stdio.h>

typedef struct {
    baji_board_t *board;
    pet_player_t player;
    SemaphoreHandle_t changed, joined;
    portMUX_TYPE lock;
    bool armed, consumed, rescue, eye_only;
    unsigned calls, draws, skipped_poses, late, extra_static;
    uint32_t first_seq, last_seq;
    int64_t first_edge, last_edge, max_draw_us, frozen_us;
    int64_t published_us, wake_us, wake_dma_us;
} schedule_t;

/* This producer acts like an Agent event arriving while the scene is static.
 * A second wake makes a lost first publication measurable and fails the run. */
static void publish_later(void *arg)
{
    schedule_t *s = arg;
    vTaskDelay(pdMS_TO_TICKS(120));
    portENTER_CRITICAL(&s->lock);
    s->published_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s->lock);
    xSemaphoreGive(s->changed);

    vTaskDelay(pdMS_TO_TICKS(150));
    portENTER_CRITICAL(&s->lock);
    bool rescue = !s->consumed;
    s->rescue = rescue;
    portEXIT_CRITICAL(&s->lock);
    if (rescue) xSemaphoreGive(s->changed);
    xSemaphoreGive(s->joined);
    vTaskDelete(NULL);
}

static esp_err_t render(void *arg, const baji_input_t *touch, int64_t now, int64_t *next)
{
    (void)touch;
    schedule_t *s = arg;
    baji_input_t input = {0};
    baji_pet_state_t before = s->player.shown;
    bool sent = false;
    if (s->armed) {
        portENTER_CRITICAL(&s->lock);
        int64_t published = s->published_us;
        portEXIT_CRITICAL(&s->lock);
        if (!published || published > now) {
            s->extra_static++;
            *next = INT64_MAX;
            return ESP_OK;
        }
        /* Freeze the body at the last pose so this event paints only the eyes. */
        input.touch = (baji_touch_sample_t){.pressed=true, .x=460, .y=233};
        esp_err_t err = pet_player_tick(&s->player, s->board, &input, s->frozen_us, &sent);
        if (err != ESP_OK) return err;
        s->wake_us = now - published;
        s->wake_dma_us = esp_timer_get_time() - published;
        s->eye_only = sent && s->player.shown.frame == before.frame &&
                      s->player.shown.clip == before.clip && s->player.shown.gaze_x != before.gaze_x;
        portENTER_CRITICAL(&s->lock);
        s->consumed = true;
        portEXIT_CRITICAL(&s->lock);
        *next = BAJI_FRAME_DONE;
        return ESP_OK;
    }

    bool first = !s->player.have_frame;
    baji_vsync_edge_t edge = baji_vsync_snapshot();
    esp_err_t err = pet_player_tick(&s->player, s->board, &input, now, &sent);
    if (err != ESP_OK) return err;
    *next = pet_player_next_update(&s->player, &input, now);
    if (first) return ESP_OK;
    baji_vsync_edge_t end = baji_vsync_snapshot();
    if (!edge.edge_us) return ESP_ERR_INVALID_STATE;
    if (!s->calls) {
        s->first_edge = edge.edge_us;
        s->first_seq = edge.seq;
    }
    ++s->calls;
    s->draws += sent;
    s->late += end.seq != edge.seq;
    s->last_seq = edge.seq;
    s->last_edge = edge.edge_us;
    unsigned count = baji_pet_frame_count(BAJI_PET_IDLE);
    s->skipped_poses += (s->player.shown.frame + count - before.frame) % count > 1;
    if (sent && end.observed_us - edge.edge_us > s->max_draw_us)
        s->max_draw_us = end.observed_us - edge.edge_us;
    if (s->last_edge - s->first_edge >= (int64_t)BAJI_SCHEDULE_CHECK_MS * 1000) {
        s->frozen_us = now;
        if (xTaskCreate(publish_later, "pet_wake_test", 2496, s, 4, NULL) != pdPASS)
            return ESP_ERR_NO_MEM;
        s->armed = true;
        *next = INT64_MAX;
    }
    return ESP_OK;
}

esp_err_t check_runtime_schedule(baji_board_t *board, const baji_board_info_t *info, baji_pet_t *art)
{
    StaticSemaphore_t changed_storage, join_storage;
    schedule_t s = {.board=board, .lock=portMUX_INITIALIZER_UNLOCKED};
    s.changed = xSemaphoreCreateBinaryStatic(&changed_storage);
    s.joined = xSemaphoreCreateBinaryStatic(&join_storage);
    pet_player_init(&s.player, art, info, esp_timer_get_time());
    baji_runtime_config_t config = {.brightness=45, .changed=s.changed};
    /* Keep test input deterministic. The normal live app reenables touch. */
    baji_board_info_t idle_info = *info;
    idle_info.touch_status = ESP_ERR_INVALID_STATE;
    esp_err_t err = baji_runtime_run(board, &idle_info, &config, render, &s);
    if (s.armed) xSemaphoreTake(s.joined, portMAX_DELAY);
    vSemaphoreDelete(s.joined);
    vSemaphoreDelete(s.changed);
    int64_t span = s.last_edge - s.first_edge;
    unsigned edges = s.last_seq - s.first_seq;
    unsigned render_mhz = span > 0 ? (s.calls-1)*INT64_C(1000000000)/span : 0;
    unsigned panel_mhz = span > 0 ? edges*INT64_C(1000000000)/span : 0;
    bool ok = err == ESP_OK && render_mhz >= 11500 && render_mhz <= 12500 &&
              panel_mhz >= 55000 && panel_mhz <= 63000 && s.draws == s.calls &&
              !s.skipped_poses && !s.late && !s.extra_static && !s.rescue && s.eye_only &&
              s.wake_us >= 0 && s.wake_dma_us <= 25000;
    printf("PET_SCHEDULE ok=%d span_us=%u edges=%u callbacks=%u draws=%u callback_millihz=%u "
           "panel_millihz=%u skipped_poses=%u late=%u max_te_dma_us=%u extra_static=%u "
           "rescue=%d eye_only=%d event_callback_us=%u event_dma_us=%u err=%d\n",
           ok, (unsigned)span, edges, s.calls, s.draws, render_mhz, panel_mhz, s.skipped_poses,
           s.late, (unsigned)s.max_draw_us, s.extra_static, s.rescue, s.eye_only,
           (unsigned)s.wake_us, (unsigned)s.wake_dma_us, err);
    return err != ESP_OK ? err : ok ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
