#pragma once
#include "baji_state.h"
#include <stddef.h>

enum { BAJI_BODY_FIXED = 53, BAJI_MODEL_MAX = 32, BAJI_TAG_BYTES = 32 };
typedef struct {
    baji_snapshot_t snapshot;
    bool selected;
    char model[BAJI_MODEL_MAX + 1];
} agent_message_t;
bool agent_wire_decode(const uint8_t *p, size_t n, agent_message_t *out);
