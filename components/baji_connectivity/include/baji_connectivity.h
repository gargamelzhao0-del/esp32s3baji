#pragma once
#include "baji_agent_feed.h"
#include "esp_err.h"

/* Product-lifetime service, owned by app_main. An unavailable radio does not
 * take the other radio or local pet offline. A null feed selects local play. */
esp_err_t baji_connectivity_start(void);
const baji_agent_feed_t *baji_connectivity_feed(void);
esp_err_t baji_connectivity_stop(void);
