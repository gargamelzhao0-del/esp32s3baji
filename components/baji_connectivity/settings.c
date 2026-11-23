#include "settings.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t key[32], ap_secret[12]; } pairing_t;
typedef struct { char ssid[33], pass[65]; } wifi_settings_t;

static bool valid_station(const char *ssid, const char *password)
{
    if (!ssid || !password) return false;
    size_t length = strlen(password);
    return strlen(ssid) <= 32 && (!length || (ssid[0] && length >= 8 && length <= 63));
}

esp_err_t badge_settings_load(badge_settings_t *s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("baji_radio", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    pairing_t pairing;
    size_t length = sizeof(pairing);
    err = nvs_get_blob(h, "pair_v1", &pairing, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        esp_fill_random(&pairing, sizeof(pairing));
        err = nvs_set_blob(h, "pair_v1", &pairing, sizeof(pairing));
        if (err == ESP_OK) err = nvs_commit(h);
    } else if (err == ESP_OK && length != sizeof(pairing)) err = ESP_ERR_INVALID_SIZE;
    if (err == ESP_OK) {
        uint8_t mac[6];
        err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        if (err == ESP_OK) {
            snprintf(s->ap_ssid,sizeof(s->ap_ssid),"BAJI-%02X%02X%02X",mac[3],mac[4],mac[5]);
            for (unsigned i=0; i<sizeof(pairing.ap_secret); i++)
                snprintf(s->ap_pass+2*i,3,"%02x",pairing.ap_secret[i]);
            memcpy(s->key,pairing.key,sizeof(s->key));
        }
    }
    if (err == ESP_OK) {
        wifi_settings_t w = {0};
        length = sizeof(w);
        esp_err_t wifi_err = nvs_get_blob(h,"wifi_v1",&w,&length);
        if (wifi_err == ESP_OK && (length != sizeof(w) || w.ssid[32] || w.pass[64] ||
                                   !valid_station(w.ssid,w.pass)))
            wifi_err = ESP_ERR_INVALID_SIZE;
        /* A bad optional station record must not strand USB setup, BLE, or
         * the paired AP. Leave NVS intact until the user saves new settings. */
        if (wifi_err != ESP_OK) {
            if (wifi_err != ESP_ERR_NVS_NOT_FOUND)
                ESP_LOGW("baji_radio", "station settings unavailable: %s", esp_err_to_name(wifi_err));
            w = (wifi_settings_t){0};
        }
        memcpy(s->ssid,w.ssid,sizeof(w.ssid));
        memcpy(s->pass,w.pass,sizeof(w.pass));
    }
    nvs_close(h);
    return err;
}

esp_err_t badge_settings_wifi(const char *ssid, const char *password)
{
    if (!valid_station(ssid,password)) return ESP_ERR_INVALID_ARG;
    wifi_settings_t w = {0};
    memcpy(w.ssid,ssid,strlen(ssid));
    memcpy(w.pass,password,strlen(password));
    nvs_handle_t h;
    esp_err_t err = nvs_open("baji_radio",NVS_READWRITE,&h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h,"wifi_v1",&w,sizeof(w));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
