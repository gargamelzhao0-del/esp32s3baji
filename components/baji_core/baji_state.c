#include "baji_state.h"
#include <string.h>

static bool valid_percent(baji_percent_t value) {
    return !value.known || (value.used_permille <= 1000 && value.ttl_ms > 0);
}

void baji_state_init(baji_state_t *state) {
    memset(state, 0, sizeof(*state));
}

void baji_state_begin_connection(baji_state_t *state) {
    baji_state_init(state);
}

baji_apply_result_t baji_state_apply(baji_state_t *state,
                                   const baji_snapshot_t *next,
                                   uint64_t now) {
    if (!state || !next || next->seq == 0 ||
        (unsigned)next->phase >= BAJI_PHASE_COUNT ||
        (unsigned)next->activity >= BAJI_ACTIVITY_COUNT ||
        (unsigned)next->outcome >= BAJI_OUTCOME_COUNT ||
        !valid_percent(next->quota) || !valid_percent(next->context)) {
        return BAJI_INVALID;
    }
    if (state->has_snapshot &&
        (next->seq <= state->snapshot.seq || now < state->received_ms)) {
        return BAJI_OLD;
    }
    if (!state->has_snapshot || next->identity_revision != state->snapshot.identity_revision ||
        next->phase != state->snapshot.phase ||
        next->outcome != state->snapshot.outcome) {
        state->phase_since_ms = now;
    }
    state->snapshot = *next;
    state->received_ms = now;
    state->has_snapshot = true;
    return BAJI_APPLIED;
}

static const char *working_pose(baji_activity_t activity) {
    switch (activity) {
        case BAJI_READING: return "reading";
        case BAJI_WRITING: return "writing";
        case BAJI_RUNNING: return "running";
        case BAJI_SEARCHING: return "searching";
        case BAJI_COMPACT: return "compact";
        default: return "thinking";
    }
}

static uint64_t deadline(uint64_t origin, uint32_t delay) {
    return UINT64_MAX - origin < delay ? UINT64_MAX : origin + delay;
}

/* Schedule the same boundary used to decide freshness. A snapshot supplied
 * with an already expired measurement adds no future work to the UI. */
static bool metric_fresh(baji_percent_t value, uint64_t received, uint64_t now,
                         uint64_t *next_change) {
    if (!value.known || value.age_ms >= value.ttl_ms) return false;
    uint32_t remaining = value.ttl_ms - value.age_ms;
    if (now - received >= remaining) return false;
    uint64_t expires = deadline(received, remaining);
    if (expires < *next_change) *next_change = expires;
    return true;
}

baji_view_t baji_state_view(const baji_state_t *state, uint64_t now) {
    baji_view_t view = {
        .connection = BAJI_CONNECTING, .phase = BAJI_IDLE, .pose = "idle"
    };
    if (!state || !state->has_snapshot) return view;
    /* A radio callback can commit between the frame timestamp and this read. */
    if (now < state->received_ms) now = state->received_ms;
    uint64_t elapsed = now - state->received_ms;
    view.connection = elapsed >= BAJI_LINK_TIMEOUT_MS ? BAJI_OFFLINE : BAJI_ONLINE;
    view.phase = state->snapshot.phase;
    view.outcome = state->snapshot.outcome;
    view.animate = view.connection == BAJI_ONLINE;
    if (view.animate) view.next_change_ms = deadline(state->received_ms, BAJI_LINK_TIMEOUT_MS);
    switch (view.phase) {
        case BAJI_WORKING:
        case BAJI_TOOL: view.pose = working_pose(state->snapshot.activity); break;
        case BAJI_WAITING: view.pose = "asking"; break;
        case BAJI_ERROR: view.pose = "oops"; break;
        case BAJI_LIMITED: view.pose = "tired"; break;
        case BAJI_ENDED:
            if ((view.outcome == BAJI_SUCCEEDED || view.outcome == BAJI_FAILED) &&
                now - state->phase_since_ms < BAJI_ENDED_EFFECT_MS) {
                if (view.outcome == BAJI_SUCCEEDED) view.pose = "happy";
                else view.pose = "oops";
                uint64_t ends = deadline(state->phase_since_ms, BAJI_ENDED_EFFECT_MS);
                if (ends < view.next_change_ms) view.next_change_ms = ends;
            }
            break;
        default: break;
    }
    baji_percent_t q = state->snapshot.quota, c = state->snapshot.context;
    if (view.animate) {
        view.quota_known = metric_fresh(q, state->received_ms, now, &view.next_change_ms);
        view.context_known = metric_fresh(c, state->received_ms, now, &view.next_change_ms);
    }
    view.quota_estimated = view.quota_known && q.estimated;
    view.context_estimated = view.context_known && c.estimated;
    view.quota_used_permille = q.used_permille;
    view.context_used_permille = c.used_permille;
    return view;
}
