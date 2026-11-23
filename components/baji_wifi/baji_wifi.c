#include "baji_wifi.h"
#include "wifi_http.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <ctype.h>
#include <stdatomic.h>
#include <string.h>

static const char *TAG = "baji_wifi";
static atomic_flag lifecycle = ATOMIC_FLAG_INIT;
static struct {
    baji_link_endpoint_t endpoint;
    esp_netif_t *netif;
    httpd_handle_t server;
    esp_event_handler_instance_t station_events, ip_events;
    TaskHandle_t retry_task;
    StaticSemaphore_t joined_storage;
    SemaphoreHandle_t joined;
    bool initialized, started;
} radio;

enum { STATION_START = 1, DISCONNECTED, HAS_ADDRESS, STOP_TASK };
enum { RETRY_MIN_MS = 500, RETRY_MAX_MS = 30000 };

static esp_err_t make_config(const baji_wifi_config_t *settings, wifi_mode_t *mode, wifi_config_t *out)
{
    bool station = settings->sta_ssid && settings->sta_ssid[0];
    const char *ssid = station ? settings->sta_ssid : settings->ap_ssid;
    const char *password = station ? settings->sta_password : settings->ap_password;
    size_t ssid_len = ssid ? strnlen(ssid, sizeof(out->sta.ssid) + 1) : 0;
    size_t pass_len = password ? strnlen(password, sizeof(out->sta.password) + 1) : 0;
    if (!ssid_len || ssid_len > sizeof(out->sta.ssid) || pass_len > sizeof(out->sta.password) ||
        (pass_len < 8 && (!station || pass_len != 0))) return ESP_ERR_INVALID_ARG;
    if (pass_len == 64) {
        for (size_t i = 0; i < pass_len; i++)
            if (!isxdigit((unsigned char)password[i])) return ESP_ERR_INVALID_ARG;
    }

    *mode = station ? WIFI_MODE_STA : WIFI_MODE_AP;
    if (station) {
        memcpy(out->sta.ssid, ssid, ssid_len);
        if (pass_len) memcpy(out->sta.password, password, pass_len);
        out->sta.threshold.authmode = pass_len ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        out->sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    } else {
        memcpy(out->ap.ssid, ssid, ssid_len);
        out->ap.ssid_len = ssid_len;
        memcpy(out->ap.password, password, pass_len);
        out->ap.channel = 1;
        out->ap.max_connection = 2;
        out->ap.authmode = WIFI_AUTH_WPA2_PSK;
        out->ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
    }
    return ESP_OK;
}

/* Only this worker initiates station connections. Driver callbacks publish
 * the latest link condition; the backoff sleeps without blocking the IP loop. */
static void reconnect_task(void *unused)
{
    (void)unused;
    unsigned backoff = RETRY_MIN_MS;
    TickType_t delay = portMAX_DELAY;
    for (;;) {
        uint32_t event = 0;
        bool notified = xTaskNotifyWait(0, UINT32_MAX, &event, delay) == pdTRUE;
        if (notified && event == STOP_TASK) break;
        if (notified && event == HAS_ADDRESS) {
            backoff = RETRY_MIN_MS;
            delay = portMAX_DELAY;
            continue;
        }
        if ((!notified || event == STATION_START) && esp_wifi_connect() == ESP_OK) {
            delay = portMAX_DELAY;
        } else {
            delay = pdMS_TO_TICKS(backoff);
            backoff = backoff < RETRY_MAX_MS / 2 ? backoff * 2 : RETRY_MAX_MS;
        }
    }
    xSemaphoreGive(radio.joined);
    vTaskDelete(NULL);
}

static void station_event(void *context, esp_event_base_t base, int32_t id, void *data)
{
    (void)context;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START)
            xTaskNotify(radio.retry_task, STATION_START, eSetValueWithOverwrite);
        else if (id == WIFI_EVENT_STA_DISCONNECTED)
            xTaskNotify(radio.retry_task, DISCONNECTED, eSetValueWithOverwrite);
    } else if (id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        if (event->esp_netif != radio.netif) return;
        ESP_LOGI(TAG, "Agent endpoint at http://" IPSTR, IP2STR(&event->ip_info.ip));
        xTaskNotify(radio.retry_task, HAS_ADDRESS, eSetValueWithOverwrite);
    }
}

static esp_err_t stop_locked(void)
{
    esp_err_t err;
    if (radio.server) {
        err = httpd_stop(radio.server);
        if (err != ESP_OK) return err;
        radio.server = NULL;
    }
    if (radio.station_events) {
        err = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, radio.station_events);
        if (err != ESP_OK) return err;
        radio.station_events = NULL;
    }
    if (radio.ip_events) {
        err = esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, radio.ip_events);
        if (err != ESP_OK) return err;
        radio.ip_events = NULL;
    }
    if (radio.retry_task) {
        xTaskNotify(radio.retry_task, STOP_TASK, eSetValueWithOverwrite);
        xSemaphoreTake(radio.joined, portMAX_DELAY);
        radio.retry_task = NULL;
    }
    if (radio.joined) {
        vSemaphoreDelete(radio.joined);
        radio.joined = NULL;
    }
    if (radio.started) {
        err = esp_wifi_stop();
        if (err != ESP_OK) return err;
        radio.started = false;
    }
    if (radio.initialized) {
        err = esp_wifi_deinit();
        if (err != ESP_OK) return err;
        radio.initialized = false;
    }
    if (radio.netif) {
        esp_netif_destroy_default_wifi(radio.netif);
        radio.netif = NULL;
    }
    radio.endpoint = (baji_link_endpoint_t){0};
    return ESP_OK;
}

static esp_err_t start_locked(const baji_link_endpoint_t *endpoint, const baji_wifi_config_t *settings)
{
    if (radio.endpoint.hello) return ESP_ERR_INVALID_STATE;
    if (!endpoint || !endpoint->hello || !endpoint->receive || !settings) return ESP_ERR_INVALID_ARG;
    wifi_config_t config = {0};
    wifi_mode_t mode;
    esp_err_t err = make_config(settings, &mode, &config);
    if (err != ESP_OK) return err;
    bool station = mode == WIFI_MODE_STA;
    wifi_interface_t iface = station ? WIFI_IF_STA : WIFI_IF_AP;
    /* IDF init also returns OK for a driver already owned by another
     * subsystem. Do not reconfigure or tear down that instance. */
    wifi_mode_t existing_mode;
    err = esp_wifi_get_mode(&existing_mode);
    if (err != ESP_ERR_WIFI_NOT_INIT) return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;

    radio.endpoint = *endpoint;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = false;
    err = esp_wifi_init(&init);
    if (err != ESP_OK) goto fail;
    radio.initialized = true;
    /* The IDF create_default_wifi helpers abort on allocation/attach errors.
     * Own those steps individually so the badge keeps rendering on failure. */
    esp_netif_config_t net = station ? (esp_netif_config_t)ESP_NETIF_DEFAULT_WIFI_STA()
                                     : (esp_netif_config_t)ESP_NETIF_DEFAULT_WIFI_AP();
    radio.netif = esp_netif_new(&net);
    if (!radio.netif) { err = ESP_ERR_NO_MEM; goto fail; }
    err = station ? esp_netif_attach_wifi_station(radio.netif) : esp_netif_attach_wifi_ap(radio.netif);
    if (err != ESP_OK) goto fail;
    err = station ? esp_wifi_set_default_wifi_sta_handlers() : esp_wifi_set_default_wifi_ap_handlers();
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(mode);
    if (err == ESP_OK) err = esp_wifi_set_config(iface, &config);
    if (err != ESP_OK) goto fail;

    if (station) {
        radio.joined = xSemaphoreCreateBinaryStatic(&radio.joined_storage);
        if (xTaskCreate(reconnect_task, "wifi_retry", 2560, NULL, 3, &radio.retry_task) != pdPASS) {
            err = ESP_ERR_NO_MEM;
            goto fail;
        }
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, station_event,
                                                  NULL, &radio.station_events);
        if (err == ESP_OK)
            err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, station_event,
                                                      NULL, &radio.ip_events);
        if (err != ESP_OK) goto fail;
    }
    err = baji_wifi_http_start(&radio.server, &radio.endpoint);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) goto fail;
    radio.started = true;
    if (!station) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(radio.netif, &ip) == ESP_OK)
            ESP_LOGI(TAG, "Private AP endpoint at http://" IPSTR, IP2STR(&ip.ip));
    }
    return ESP_OK;

fail:
    {
        esp_err_t cleanup = stop_locked();
        if (cleanup != ESP_OK) ESP_LOGE(TAG, "Wi-Fi cleanup: %s", esp_err_to_name(cleanup));
    }
    return err;
}

esp_err_t baji_wifi_start(const baji_link_endpoint_t *endpoint, const baji_wifi_config_t *config)
{
    if (atomic_flag_test_and_set(&lifecycle)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = start_locked(endpoint, config);
    atomic_flag_clear(&lifecycle);
    return err;
}

esp_err_t baji_wifi_stop(void)
{
    if (atomic_flag_test_and_set(&lifecycle)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = stop_locked();
    atomic_flag_clear(&lifecycle);
    return err;
}
