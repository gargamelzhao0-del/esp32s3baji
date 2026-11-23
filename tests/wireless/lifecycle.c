#include "lifecycle_shim.h"
#include "baji_agent_sync.h"
#include "baji_ble.h"
#include "baji_wifi.h"
#include "baji_connectivity.h"
#include "settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool wifi_live, ble_live, sync_live, loop_live, external_loop;
static bool wifi_fail_start, wifi_fail_stop, ble_fail_stop, usb_task_failure;
static bool usb_installed, console_uses_driver, entropy_on;
static int closed, wifi_stops, ble_stops;
static char wifi_name[33], usb_tx[192];
static const char *usb_rx;
static uint8_t pair_record[44], station_record[98];
static size_t pair_len, station_len;
static void (*usb_reader)(void *);
static void *usb_context;

esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_open(const char *s,int mode,nvs_handle_t *h) {
    assert(!strcmp(s,"baji_radio") && mode == NVS_READWRITE); *h = 1; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *name,void *out,size_t *n) {
    bool pair = !strcmp(name,"pair_v1");
    assert(h && (pair || !strcmp(name,"wifi_v1")));
    size_t size = pair ? pair_len : station_len;
    if (!size) return ESP_ERR_NVS_NOT_FOUND;
    if (size > *n) return ESP_ERR_INVALID_SIZE;
    memcpy(out, pair ? pair_record : station_record, size);
    *n = size;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *name,const void *p,size_t n) {
    bool pair = !strcmp(name,"pair_v1");
    assert(h && n == (pair ? sizeof(pair_record) : sizeof(station_record)));
    memcpy(pair ? pair_record : station_record,p,n);
    if (pair) pair_len = n;
    else station_len = n;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { assert(h); return ESP_OK; }
void nvs_close(nvs_handle_t h) { assert(h); }
void bootloader_random_enable(void) { assert(!entropy_on); entropy_on = true; }
void bootloader_random_disable(void) { assert(entropy_on); entropy_on = false; }
esp_err_t esp_read_mac(uint8_t *mac,int type) {
    assert(type == ESP_MAC_WIFI_SOFTAP); memset(mac,0xa5,6); return ESP_OK;
}
esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_err_t esp_event_loop_create_default(void) {
    if (external_loop) return ESP_ERR_INVALID_STATE;
    assert(!loop_live); loop_live = true; return ESP_OK;
}
esp_err_t esp_event_loop_delete_default(void) {
    assert(!external_loop && loop_live && !wifi_live); loop_live = false; return ESP_OK;
}

esp_err_t baji_agent_sync_open(baji_agent_sync_t **s,const uint8_t key[32]) {
    assert(!sync_live && entropy_on && !memcmp(key,pair_record,32));
    sync_live = true; *s = (void*)&sync_live; return ESP_OK;
}
baji_link_endpoint_t baji_agent_endpoint(baji_agent_sync_t *s) {
    assert(sync_live && s); return (baji_link_endpoint_t){.context=s};
}
baji_agent_feed_t baji_agent_feed(baji_agent_sync_t *s) {
    assert(sync_live && s); return (baji_agent_feed_t){.context=s};
}
void baji_agent_sync_close(baji_agent_sync_t *s) {
    if (!s) return;
    assert(sync_live && !wifi_live && !ble_live && !usb_installed); sync_live = false; closed++;
}
esp_err_t baji_ble_start(const baji_link_endpoint_t *ep) {
    assert(sync_live && ep->context && !ble_live); ble_live = true; return ESP_OK;
}
esp_err_t baji_ble_stop(void) {
    ble_stops++;
    if (ble_fail_stop) { ble_fail_stop = false; return ESP_FAIL; }
    ble_live = false; return ESP_OK;
}
esp_err_t baji_wifi_start(const baji_link_endpoint_t *ep,const baji_wifi_config_t *cfg) {
    assert(sync_live && ep->context && !wifi_live && (external_loop || loop_live));
    assert(!strcmp(cfg->ap_ssid,"BAJI-A5A5A5") && strlen(cfg->ap_password) == 24);
    snprintf(wifi_name,sizeof(wifi_name),"%s",cfg->sta_ssid);
    wifi_live = true;
    return wifi_fail_start ? ESP_FAIL : ESP_OK; /* simulate an incomplete startup rollback */
}
esp_err_t baji_wifi_stop(void) {
    wifi_stops++;
    if (wifi_fail_stop) { wifi_fail_stop = false; return ESP_FAIL; }
    wifi_live = false; return ESP_OK;
}

esp_err_t usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *cfg) {
    assert(!usb_installed && cfg->rx_buffer_size > 64); usb_installed = true; return ESP_OK;
}
esp_err_t usb_serial_jtag_driver_uninstall(void) {
    assert(usb_installed && !console_uses_driver); usb_installed = false; return ESP_OK;
}
void usb_serial_jtag_vfs_use_nonblocking(void) { assert(usb_installed); console_uses_driver = false; }
void usb_serial_jtag_vfs_use_driver(void) { assert(usb_installed); console_uses_driver = true; }
int xTaskCreate(void (*entry)(void *),const char *name,unsigned stack,void *arg,int priority,TaskHandle_t *task) {
    (void)name; (void)stack; (void)priority;
    if (usb_task_failure) return 0;
    usb_reader = entry; usb_context = arg; *task = &usb_installed; return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) { assert(task); usb_reader(usb_context); }
int ulTaskNotifyTake(int clear,unsigned wait) { (void)clear; (void)wait; return !usb_rx || !*usb_rx; }
void vTaskDelete(void *task) { assert(!task); }
int usb_serial_jtag_read_bytes(void *dest,uint32_t limit,unsigned wait) {
    (void)wait;
    size_t n = strlen(usb_rx);
    if (n > limit) n = limit;
    memcpy(dest,usb_rx,n); usb_rx += n; return (int)n;
}
int usb_serial_jtag_write_bytes(const void *data,size_t n,unsigned wait) {
    (void)wait; assert(console_uses_driver && n < sizeof(usb_tx));
    memcpy(usb_tx,data,n); usb_tx[n] = 0; return (int)n;
}

int main(void) {
    wifi_fail_start = wifi_fail_stop = true;
    assert(baji_connectivity_start() == ESP_OK && baji_connectivity_feed());
    assert(!*wifi_name && pair_len == 44);
    assert(baji_connectivity_start() == ESP_ERR_INVALID_STATE);
    usb_rx = "BAJI_WIFI 736369656e6365:3132333435363738\n";
    assert(baji_connectivity_stop() == ESP_FAIL);
    assert(!strcmp(usb_tx,"BAJI_WIFI_SAVED reboot\n") && station_len == 98);
    assert(sync_live && loop_live && wifi_live && !ble_live && !usb_installed && !closed);
    assert(baji_connectivity_stop() == ESP_OK && !sync_live && !loop_live && closed == 1);
    assert(wifi_stops == 2 && ble_stops == 1);

    wifi_fail_start = false;
    external_loop = ble_fail_stop = true;
    assert(baji_connectivity_start() == ESP_OK);
    assert(!strcmp(wifi_name,"science"));
    usb_rx = "BAJI_PAIR?\n";
    assert(baji_connectivity_stop() == ESP_FAIL && !wifi_live && ble_live && sync_live);
    assert(!strncmp(usb_tx,"BAJI_PAIR1 ",11) && strstr(usb_tx," BAJI-A5A5A5 "));
    assert(baji_connectivity_stop() == ESP_OK && closed == 2);
    assert(wifi_stops == 3 && ble_stops == 3);

    /* Only station NVS is bad. Both radios and subsequent USB repair stay usable. */
    station_len = 1;
    usb_task_failure = true;
    assert(baji_connectivity_start() == ESP_OK && !*wifi_name && !usb_installed);
    assert(baji_connectivity_stop() == ESP_OK && closed == 3);
    usb_task_failure = false;
    station_len = sizeof(station_record);
    memset(station_record,'x',station_len);
    assert(baji_connectivity_start() == ESP_OK && !*wifi_name && usb_installed);
    usb_rx = "BAJI_WIFI :\n";
    assert(baji_connectivity_stop() == ESP_OK && !strcmp(usb_tx,"BAJI_WIFI_SAVED reboot\n"));
    assert(baji_connectivity_stop() == ESP_OK && closed == 4);
    pair_len = 1;
    assert(baji_connectivity_start() == ESP_ERR_INVALID_SIZE && !entropy_on && !sync_live && !usb_installed);
    assert(!baji_connectivity_feed());
    puts("Connectivity fault injection: partial starts, independent stops, retry, external loop, NVS and USB passed.");
}
