#pragma once

#include "baji_link_endpoint.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *sta_ssid;
    const char *sta_password;
    const char *ap_ssid;
    const char *ap_password;
} baji_wifi_config_t;

/* Requires esp_netif and the default ESP event loop. A nonempty sta_ssid uses
 * the router; otherwise ap_ssid and a WPA2 passphrase create a private AP.
 * Credentials are copied into the Wi-Fi driver's RAM during start. The
 * endpoint context must remain valid until stop succeeds. One lifecycle owner
 * calls start/stop, outside endpoint callbacks and the ESP event loop. */
esp_err_t baji_wifi_start(const baji_link_endpoint_t *endpoint, const baji_wifi_config_t *config);
esp_err_t baji_wifi_stop(void);

#ifdef __cplusplus
}
#endif
