#pragma once
#include "baji_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    baji_view_t view;
    char model[33];
    bool has_session;
    /* Changes across a handoff between two selected Agent identities. Heartbeats,
     * metrics and the first selection do not advance this local counter. */
    uint32_t generation;
} baji_agent_display_t;

/* The application borrows this port until its synchronous run returns.
 * read copies the current projection and returns false before the first
 * snapshot. view.pose is an immutable name from the state model. */
typedef struct {
    void *context;
    bool (*read)(void *context, uint64_t now_ms, baji_agent_display_t *out);
    SemaphoreHandle_t changed;
} baji_agent_feed_t;

#ifdef __cplusplus
}
#endif
