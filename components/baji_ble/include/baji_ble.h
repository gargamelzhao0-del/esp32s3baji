#pragma once
#include "baji_link_endpoint.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NVS and the endpoint are supplied by the application. Start schedules radio
 * synchronization; advertising and reconnection run on the NimBLE host task.
 * The endpoint context must stay alive until stop returns ESP_OK. Call
 * lifecycle functions from the owner, never from an endpoint callback. */
esp_err_t baji_ble_start(const baji_link_endpoint_t *endpoint);
esp_err_t baji_ble_stop(void);

#ifdef __cplusplus
}
#endif
