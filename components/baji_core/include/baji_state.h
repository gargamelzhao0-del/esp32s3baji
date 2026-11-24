#pragma once
#include <stdbool.h>
#include <stdint.h>

#define BAJI_LINK_TIMEOUT_MS 8000U
#define BAJI_ENDED_EFFECT_MS 1600U

typedef enum {
    BAJI_IDLE, BAJI_WORKING, BAJI_TOOL, BAJI_WAITING,
    BAJI_ENDED, BAJI_ERROR, BAJI_LIMITED, BAJI_PHASE_COUNT
} baji_phase_t;
typedef enum {
    BAJI_NONE, BAJI_THINKING, BAJI_READING, BAJI_WRITING,
    BAJI_RUNNING, BAJI_SEARCHING, BAJI_COMPACT, BAJI_ACTIVITY_COUNT
} baji_activity_t;
typedef enum {
    BAJI_OUTCOME_UNKNOWN, BAJI_SUCCEEDED, BAJI_FAILED, BAJI_CANCELLED,
    BAJI_OUTCOME_COUNT
} baji_outcome_t;
typedef enum { BAJI_CONNECTING, BAJI_ONLINE, BAJI_OFFLINE } baji_connection_t;
typedef enum { BAJI_APPLIED, BAJI_OLD, BAJI_INVALID } baji_apply_result_t;

/* Values are used percentages, in 0.1 percent units; unknown != zero. */
typedef struct {
    bool known;
    bool estimated;
    uint16_t used_permille;
    uint32_t age_ms;
    uint32_t ttl_ms;
} baji_percent_t;

/* Transport validates identities, bounded strings and JSON before this model.
 * Only the selected quota bucket is projected here; UI details retain all buckets.
 */
typedef struct {
    uint32_t seq;
    /* Local projection counter: change when selected session_id/turn_id changes.
     * Heartbeat snapshots for the same selection keep this value unchanged. */
    uint32_t identity_revision;
    baji_phase_t phase;
    baji_activity_t activity;
    baji_outcome_t outcome;
    baji_percent_t quota;
    baji_percent_t context;
} baji_snapshot_t;

typedef struct {
    bool has_snapshot;
    uint64_t received_ms;
    uint64_t phase_since_ms;
    baji_snapshot_t snapshot;
} baji_state_t;

typedef struct {
    baji_connection_t connection;
    baji_phase_t phase;
    const char *pose;
    bool animate;
    bool quota_known;
    bool context_known;
    bool quota_estimated, context_estimated;
    uint16_t quota_used_permille;
    uint16_t context_used_permille;
    baji_outcome_t outcome;
    /* Absolute monotonic deadline for the next time-driven display change.
     * Zero means the projection stays valid until another snapshot arrives. */
    uint64_t next_change_ms;
} baji_view_t;

void baji_state_init(baji_state_t *state);
/* Call only AFTER a transport handshake binds a new bridge connection.
 * seq never wraps in a connection. Rotate bridge_id before UINT32_MAX.
 */
void baji_state_begin_connection(baji_state_t *state);
baji_apply_result_t baji_state_apply(baji_state_t *state,
                                   const baji_snapshot_t *snapshot,
                                   uint64_t monotonic_ms);
baji_view_t baji_state_view(const baji_state_t *state, uint64_t monotonic_ms);
