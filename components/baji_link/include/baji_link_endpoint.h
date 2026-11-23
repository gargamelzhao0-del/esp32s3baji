#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Borrowed until transport stop completes. Callbacks run in task context.
 * A successful receive acknowledges an authenticated, applied snapshot. */
typedef struct {
    void *context;
    void (*hello)(void *context, uint8_t out[16]);
    esp_err_t (*receive)(void *context, const uint8_t *packet, size_t length);
} baji_link_endpoint_t;
