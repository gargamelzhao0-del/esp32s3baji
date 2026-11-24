#include "baji_state.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    baji_state_t state;
    baji_state_init(&state);
    assert(baji_state_view(&state, 0).connection == BAJI_CONNECTING);
    baji_snapshot_t s = {.seq=1, .phase=BAJI_WORKING, .activity=BAJI_THINKING,
        .quota={.known=true, .used_permille=0, .ttl_ms=90000},
        .context={.known=true, .used_permille=420, .age_ms=29500, .ttl_ms=30000}};
    assert(baji_state_apply(&state, &s, 100) == BAJI_APPLIED);
    assert(baji_state_view(&state, 100).quota_known); /* 0% used is valid */
    assert(baji_state_view(&state, 599).context_known);
    assert(baji_state_view(&state, 599).next_change_ms == 600);
    assert(!baji_state_view(&state, 600).context_known);
    assert(baji_state_view(&state, 600).next_change_ms == 8100);
    assert(baji_state_apply(&state, &s, 7000) == BAJI_OLD);
    assert(baji_state_view(&state, 8100).connection == BAJI_OFFLINE);
    assert(!baji_state_view(&state, 8100).animate);
    assert(baji_state_view(&state, 8100).next_change_ms == 0);
    s.seq=3; s.phase=BAJI_WAITING;
    assert(baji_state_apply(&state, &s, 8200) == BAJI_APPLIED); /* full snapshot skips gap */
    assert(strcmp(baji_state_view(&state, 8200).pose, "asking") == 0);
    s.seq=4; s.quota.used_permille=1001;
    assert(baji_state_apply(&state, &s, 8300) == BAJI_INVALID);
    assert(state.snapshot.seq == 3);
    s.quota.known=false; s.phase=(baji_phase_t)-1;
    assert(baji_state_apply(&state, &s, 8300) == BAJI_INVALID);
    s.phase=BAJI_ENDED; s.outcome=BAJI_OUTCOME_UNKNOWN;
    assert(baji_state_apply(&state, &s, 8400) == BAJI_APPLIED);
    assert(strcmp(baji_state_view(&state, 8400).pose, "idle") == 0);
    s.seq=5; s.outcome=BAJI_SUCCEEDED;
    assert(baji_state_apply(&state, &s, 8500) == BAJI_APPLIED);
    assert(strcmp(baji_state_view(&state, 8500).pose, "happy") == 0);
    s.seq=6;
    assert(baji_state_apply(&state, &s, 10000) == BAJI_APPLIED);
    assert(strcmp(baji_state_view(&state, 10100).pose, "idle") == 0);
    s.seq=7; s.identity_revision=1; /* Different turn, same terminal phase. */
    assert(baji_state_apply(&state, &s, 10150) == BAJI_APPLIED);
    assert(strcmp(baji_state_view(&state, 10150).pose, "happy") == 0);
    /* Frame began before this radio update committed. */
    assert(strcmp(baji_state_view(&state, 10149).pose, "happy") == 0);
    assert(baji_state_view(&state, 10149).next_change_ms == 10650);
    baji_state_begin_connection(&state);
    s.seq=1;
    assert(baji_state_apply(&state, &s, 10200) == BAJI_APPLIED);
    assert(!baji_state_view(&state, 10200).quota_known);
    puts("core state contracts passed");
    return 0;
}
