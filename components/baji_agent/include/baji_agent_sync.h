#pragma once
#include "baji_agent_feed.h"
#include "baji_link_endpoint.h"

typedef struct baji_agent_sync baji_agent_sync_t;
esp_err_t baji_agent_sync_open(baji_agent_sync_t **out, const uint8_t key[32]);
baji_link_endpoint_t baji_agent_endpoint(baji_agent_sync_t *sync);
baji_agent_feed_t baji_agent_feed(baji_agent_sync_t *sync);
/* All transport callbacks and the display owner must have stopped. */
void baji_agent_sync_close(baji_agent_sync_t *sync);
