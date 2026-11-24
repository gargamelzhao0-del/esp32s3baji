#include "pet_companion.h"
#include "pet_feedback.h"
#include "pet_menu.h"
#include "pet_player.h"
#include "baji_power_indicator.h"
#include "baji_runtime.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    baji_board_t *board;
    pet_clips_t *album;
    baji_agent_ui_t *agent;
    pet_feedback_t *feedback;
    pet_interactions_t actor;
    pet_interaction_view_t view;
    pet_player_t player;
    pet_menu_t menu;
    baji_power_indicator_t power;
    baji_agent_cue_t last_cue;
    uint32_t session_generation;
    bool smooth;
} companion_t;

static void paint_base(const baji_rect_t *band, uint8_t *pixels, void *context)
{
    companion_t *p = context;
    if (p->smooth) baji_pet_paint(band, pixels, p->player.art);
    else pet_clips_paint(band, pixels, p->album);
}

static bool fabric(void *context, int x, int y)
{
    companion_t *p = context;
    return pet_clip_pillow_contains(p->album, p->view.clip, p->view.frame, x, y);
}

static void paint(const baji_rect_t *band, uint8_t *pixels, void *context)
{
    companion_t *p = context;
    paint_base(band, pixels, p);
    pet_effects_paint(&p->view.fx, band, pixels, paint_base, fabric, p);
    baji_agent_ui_paint(p->agent, band, pixels);
    baji_power_indicator_paint(&p->power, band, pixels);
    pet_menu_paint(&p->menu, band, pixels);
}

static bool smooth_clip(const pet_interaction_view_t *view, baji_pet_clip_t *clip)
{
    static const char *const tracks[] = {
        [BAJI_PET_IDLE]="idle", [BAJI_PET_PAT]="pat", [BAJI_PET_CHEEK_LEFT]="cheekLeft",
        [BAJI_PET_CHEEK_RIGHT]="cheekRight", [BAJI_PET_TICKLE]="tickle"
    };
    if (view->scene && strcmp(view->scene, "idle")) return false;
    for (unsigned i = 0; i < sizeof(tracks)/sizeof(tracks[0]); ++i) {
        if (!strcmp(view->action, tracks[i])) { *clip = (baji_pet_clip_t)i; return true; }
    }
    return false;
}

static void select_smooth(companion_t *p, baji_pet_clip_t clip, const baji_input_t *input, int64_t now,
                          int64_t *deadline)
{
    pet_player_t *player = &p->player;
    unsigned fps = baji_pet_fps(clip), steps = baji_pet_substeps(player->art, clip);
    unsigned frames = baji_pet_frame_count(clip);
    int64_t age = p->view.elapsed_us > 0 ? p->view.elapsed_us : 0;
    unsigned rate = fps * steps;
    /* Use the actor's explicit source frame for release/hold/reverse paths. */
    bool held = p->actor.held_cheek && p->view.frame >= 3;
    uint64_t sample = (uint64_t)age * rate / 1000000;
    baji_pet_state_t state = {
        .clip=clip, .frame=p->view.frame % frames, .phase=held ? 0 : sample % steps,
        .gaze_x=player->shown.gaze_x, .gaze_y=player->shown.gaze_y
    };
    if (clip == BAJI_PET_IDLE && frames > 1) {
        unsigned last=(frames-1)*steps;
        unsigned position=sample%(2*last);
        if (position>last) position=2*last-position;
        state.frame=position/steps;
        state.phase=position%steps;
    }
    pet_player_update_gaze(player, &state, input);
    if (!held) {
        int64_t due = now + (int64_t)(((sample+1)*1000000+rate-1)/rate) - age;
        if (due < *deadline) *deadline = due;
    }
    if (!p->smooth || !player->have_frame) {
        baji_damage_mark(&player->damage, 0, 0, player->damage.width, player->damage.height);
    } else if (state.clip != player->shown.clip || state.frame != player->shown.frame ||
               state.phase != player->shown.phase || state.gaze_x != player->shown.gaze_x ||
               state.gaze_y != player->shown.gaze_y) {
        baji_rect_t boxes[2*BAJI_DAMAGE_CAPACITY];
        size_t count = baji_pet_damage(player->art, &player->shown, &state, boxes,
                                      sizeof(boxes)/sizeof(boxes[0]));
        for (size_t i = 0; i < count; ++i) baji_damage_add(&player->damage, boxes[i]);
    }
    baji_pet_select(player->art, state);
    player->shown = state;
}

static esp_err_t draw_companion(void *context, const baji_input_t *input, int64_t now, int64_t *next)
{
    companion_t *p = context;
    baji_damage_t *damage = &p->player.damage;
    baji_agent_cue_t cue = baji_agent_ui_update(p->agent, now, damage);
    uint32_t generation = baji_agent_ui_generation(p->agent);
    bool new_session = p->session_generation != generation;
    p->session_generation=generation;
    bool live = cue != BAJI_CUE_LOCAL && cue != BAJI_CUE_NO_SESSION && cue != BAJI_CUE_OFFLINE;
    if (live) p->menu.preview = BAJI_CUE_LOCAL;
    int command;
    bool sound_changed;
    baji_input_t touch = *input;
    if (pet_menu_input(&p->menu, input, live, &command, &sound_changed, damage)) {
        p->actor.presses = input->presses;
        touch.touch.pressed = false;
        if (p->menu.page != PET_MENU_CLOSED) p->actor.quiet_since = now;
    }
    if (new_session) pet_interactions_new_session(&p->actor);
    else if (command >= 0) pet_interactions_command(&p->actor, (pet_command_t)command, now);
    baji_power_indicator_update(&p->power, input->power, damage);
    const char *previous = p->view.action;
    pet_effects_t old_fx = p->view.fx;
    baji_agent_cue_t displayed = live ? cue : p->menu.preview;
    pet_interactions_tick(&p->actor, &touch, displayed, now, &p->view);
    if (!p->view.clip) return ESP_ERR_NOT_FOUND;
    pet_menu_update(&p->menu,&p->view,damage);
    const char *sound = p->view.event;
    if (!previous || strcmp(previous, p->view.action)) {
        ESP_LOGI("gpt_pet", "action=%s scene=%s", p->view.action, p->view.scene ? p->view.scene : "idle");
        if (!sound && (!strcmp(p->view.action,"doze") || !strncmp(p->view.action,"peek",4) ||
                       !strncmp(p->view.action,"wake",4))) sound=p->view.action;
    }
    if (!sound && p->last_cue != displayed) {
        switch (displayed) {
        case BAJI_CUE_SUCCESS: sound="succeeded"; break;
        case BAJI_CUE_WAIT: sound="waiting"; break;
        case BAJI_CUE_FAILED: case BAJI_CUE_ERROR: sound="error"; break;
        case BAJI_CUE_DONE: sound="ended"; break;
        case BAJI_CUE_LIMIT: sound="limited"; break;
        default: break;
        }
    }
    p->last_cue=displayed;
    if (sound_changed && p->menu.sound) sound="pat";
    if (sound || p->view.bond_delta || sound_changed)
        pet_feedback_update(p->feedback, sound, p->actor.bond, p->menu.sound);
    pet_effects_damage(&old_fx, &p->view.fx, damage);
    *next = p->view.next_us;
    baji_pet_clip_t track;
    bool smooth = smooth_clip(&p->view, &track);
    if (smooth) select_smooth(p, track, &touch, now, next);
    else {
        baji_rect_t area;
        esp_err_t err = pet_clips_select(p->album, p->view.clip, p->view.frame, &area);
        if (err != ESP_OK) return err;
        if (p->smooth || !p->player.have_frame) baji_damage_mark(damage, 0, 0, damage->width, damage->height);
        else baji_damage_add(damage, area);
    }
    p->smooth = smooth;
    *next = baji_agent_ui_deadline(p->agent, now, *next);
    if (damage->count) {
        esp_err_t err = baji_board_paint_round_regions(p->board, damage->regions, damage->count, paint, p);
        if (err != ESP_OK) return err;
        baji_damage_clear(damage);
    }
    p->player.have_frame = true;
    return ESP_OK;
}

esp_err_t pet_companion_run(baji_board_t *board, const baji_board_info_t *info, baji_pet_t *smooth,
                            pet_clips_t *album, const baji_agent_feed_t *feed)
{
    if (!board || !info || !smooth || !album || (feed && !feed->read)) return ESP_ERR_INVALID_ARG;
    companion_t *p = calloc(1, sizeof(*p));
    if (!p) return ESP_ERR_NO_MEM;
    p->board = board;
    p->album = album;
    p->agent = baji_agent_ui_open_panel(feed);
    if (!p->agent) { free(p); return ESP_ERR_NO_MEM; }
    unsigned bond = 0;
    p->menu.sound = true;
    p->feedback = pet_feedback_open(board, &bond, &p->menu.sound);
    int64_t now = esp_timer_get_time();
    pet_player_init(&p->player, smooth, info, now);
    pet_interactions_init(&p->actor, album, bond, now);
    const baji_runtime_config_t config = {.brightness=45, .watch_power=true, .changed=feed ? feed->changed : NULL};
    esp_err_t err = baji_runtime_run(board, info, &config, draw_companion, p);
    pet_feedback_close(p->feedback);
    baji_agent_ui_close(p->agent);
    free(p);
    return err;
}
