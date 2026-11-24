#pragma once
#include "baji_agent_ui.h"
#include "baji_input.h"
#include "baji_pet_clips.h"
#include "pet_effects.h"

typedef enum {
    PET_CMD_INVITE, PET_CMD_FEED, PET_CMD_STAR, PET_CMD_NAP, PET_CMD_SCENE,
    PET_CMD_TAIL, PET_CMD_WING_LEFT, PET_CMD_WING_RIGHT, PET_CMD_NOSE
} pet_command_t;
typedef enum {
    PET_TOY_OFF, PET_TOY_INVITE, PET_TOY_HELD, PET_TOY_HAND,
    PET_TOY_LANDING, PET_TOY_RETURN, PET_TOY_CAUGHT, PET_TOY_DEPART
} pet_star_phase_t;

typedef struct {
    const char *action, *scene;
    const pet_clip_t *clip;
    unsigned frame;
    int64_t elapsed_us, next_us;
    /* event is emitted once on the tick which observes it. Coordinates are
     * physical 466px screen positions. Negative pillow_depth is rebound. */
    const char *event;
    int bond_delta, pillow_depth, contact_x, contact_y;
    pet_star_phase_t star_phase;
    int star_x, star_y, hand, glow;
    bool sulking;
    pet_effects_t fx;
} pet_interaction_view_t;

typedef struct {
    const char *name;
    int first, last, step_us;
} pet_transition_t;

typedef struct {
    const pet_clips_t *art;
    const char *action, *base, *gear, *event;
    pet_interaction_view_t shown;
    baji_agent_cue_t cue;
    int64_t since, last_tick, quiet_since, last_nose, tap_times[2], tail_times[2];
    int64_t part_cooldown[4], sulk_until, next_micro, contact_since, release_at, brush_release;
    unsigned tap_count, tail_count, micro_seed, bond;
    int event_bond, direction, turns, extreme_x, side, next_side;
    int contact_x, contact_y, sleep_offset, brush_pos, brush_depth;
    int page_frame, page_from, brow_touch, brow_branch, micro_first, micro_last;
    int64_t brow_since, star_since;
    unsigned presses;
    pet_hit_t part;
    bool down, consumed, moved, held_cheek, moon, page_drag, page_commit, page_glance;
    bool brush_stroked, brush_reward, brow_offered, rematch, micro, nap_stretch;
    int pillow_release_depth;
    pet_transition_t moves[3];
    unsigned move_count;
    int64_t move_since;
    pet_star_phase_t star_phase;
    int star_x, star_y, star_from_x, star_from_y, star_dx, star_dy, hand, glow;
    int star_prev_x, star_prev_y, star_vx, star_vy;
    int64_t star_sample, star_vtime;
    int bubble_x, bubble_y, bubble_radius;
    pet_fx_point_t trail[4];
    int64_t trail_at[4], mark_since;
    unsigned trail_count;
    pet_fx_mark_t mark;
    bool tool_contact, star_thrown;
    bool pillow_held, brush_auto;
    int64_t last_visit, reunion_until, assisted_until, periodic_at;
    unsigned energy, affection, curiosity;
    const char *last_micro, *mark_action;
    const char *saved_scene;
    int64_t saved_us, pose_offset;
    pet_fx_point_t hair[3], last_hair;
    int64_t hair_at[3];
    unsigned hair_count;
    int page_start_x, brush_origin, brow_from;
} pet_interactions_t;

void pet_interactions_init(pet_interactions_t *s, const pet_clips_t *art, unsigned bond, int64_t now_us);
/* The next tick opens the new session's scene even when both sessions report
 * the same activity. Local bond and care history belong to the character. */
void pet_interactions_new_session(pet_interactions_t *s);
void pet_interactions_command(pet_interactions_t *s, pet_command_t command, int64_t now_us);
void pet_interactions_tick(pet_interactions_t *s, const baji_input_t *input, baji_agent_cue_t cue,
                           int64_t now_us, pet_interaction_view_t *view);
