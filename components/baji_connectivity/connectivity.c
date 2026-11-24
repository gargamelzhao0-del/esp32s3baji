#include "baji_connectivity.h"
#include "baji_agent_sync.h"
#include "baji_ble.h"
#include "baji_wifi.h"
#include "bootloader_random.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "settings.h"

static struct {
    badge_settings_t settings;
    baji_agent_sync_t *sync;
    baji_agent_feed_t feed;
    bool ble, wifi, setup, event_owner;
    bool ble_stop_needed, wifi_stop_needed;
} service;

esp_err_t baji_connectivity_start(void)
{
    if (service.sync) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    // Generate the first pairing secret before starting either radio.
    // The live radio supplies entropy for subsequent handshake challenges.
    bootloader_random_enable();
    err = badge_settings_load(&service.settings);
    bootloader_random_disable();
    if (err != ESP_OK) return err;
    err = baji_agent_sync_open(&service.sync, service.settings.key);
    if (err != ESP_OK) return err;
    baji_link_endpoint_t endpoint = baji_agent_endpoint(service.sync);
    service.feed = baji_agent_feed(service.sync);
    err = badge_setup_start(&service.settings);
    service.setup = err == ESP_OK;
    if (!service.setup) ESP_LOGW("baji_radio","USB pairing unavailable: %s",esp_err_to_name(err));
    /* A failed start can own callbacks while its own rollback is incomplete.
     * Even this path must stop successfully before the shared endpoint dies. */
    service.ble_stop_needed = true;
    err = baji_ble_start(&endpoint);
    service.ble = err == ESP_OK;
    if (!service.ble) ESP_LOGW("baji_radio","BLE unavailable: %s",esp_err_to_name(err));
    err = esp_netif_init();
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
        service.event_owner = err == ESP_OK;
        if (err == ESP_ERR_INVALID_STATE) err = ESP_OK; // An existing default loop is usable.
    }
    if (err == ESP_OK) {
        baji_wifi_config_t cfg = {.sta_ssid=service.settings.ssid,.sta_password=service.settings.pass,
            .ap_ssid=service.settings.ap_ssid,.ap_password=service.settings.ap_pass};
        service.wifi_stop_needed = true;
        err = baji_wifi_start(&endpoint,&cfg);
        service.wifi = err == ESP_OK;
    }
    if (!service.wifi) ESP_LOGW("baji_radio","Wi-Fi unavailable: %s",esp_err_to_name(err));
    return service.wifi || service.ble ? ESP_OK : err;
}

const baji_agent_feed_t *baji_connectivity_feed(void)
{ return service.ble || service.wifi ? &service.feed : NULL; }

esp_err_t baji_connectivity_stop(void)
{
    esp_err_t result = ESP_OK;
    if (service.setup) { badge_setup_stop(); service.setup=false; }
    if (service.wifi_stop_needed) {
        esp_err_t err = baji_wifi_stop();
        if (err == ESP_OK) service.wifi_stop_needed = service.wifi = false;
        else result = err;
    }
    if (service.ble_stop_needed) {
        esp_err_t err = baji_ble_stop();
        if (err == ESP_OK) service.ble_stop_needed = service.ble = false;
        else if (result == ESP_OK) result = err;
    }
    if (service.event_owner && !service.wifi_stop_needed) {
        esp_err_t err = esp_event_loop_delete_default();
        if (err == ESP_OK) service.event_owner = false;
        else if (result == ESP_OK) result = err;
    }
    if (result != ESP_OK) return result;
    baji_agent_sync_close(service.sync);
    service.sync=NULL;
    return ESP_OK;
}
