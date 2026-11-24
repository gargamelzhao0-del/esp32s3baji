#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "baji_link_endpoint.h"

typedef struct {
    uint8_t key[32];
    char ap_ssid[16], ap_pass[25];
    char ssid[33], pass[65];
} badge_settings_t;
esp_err_t badge_settings_load(badge_settings_t *settings);
esp_err_t badge_settings_wifi(const char *ssid, const char *password);
esp_err_t badge_setup_start(const badge_settings_t *settings, const baji_link_endpoint_t *endpoint);
void badge_setup_stop(void);
