#include "lifecycle_shim.h"
#include "baji_agent_sync.h"
#include "baji_ble.h"
#include "baji_wifi.h"
#include "baji_connectivity.h"
#include "settings.h"
#include <assert.h>
#include <pthread.h>
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
static pthread_t usb_thread;
static pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t io_ready = PTHREAD_COND_INITIALIZER;
static usj_select_notif_callback_t usb_irq;
static uint32_t notifications;

static void *run_reader(void *arg) {
    usb_reader(arg);
    return NULL;
}

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
    assert(!sync_live && !entropy_on && !memcmp(key,pair_record,32));
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
    assert(usb_installed && !usb_irq && !console_uses_driver); usb_installed = false; return ESP_OK;
}
void usb_serial_jtag_vfs_use_nonblocking(void) { assert(usb_installed); console_uses_driver = false; }
void usb_serial_jtag_vfs_use_driver(void) { assert(usb_installed); console_uses_driver = true; }
int xTaskCreate(void (*entry)(void *),const char *name,unsigned stack,void *arg,int priority,TaskHandle_t *task) {
    (void)name; (void)stack; (void)priority;
    if (usb_task_failure) return 0;
    usb_reader = entry; notifications = 0; *task = &usb_thread;
    assert(!pthread_create(&usb_thread, NULL, run_reader, arg));
    return pdPASS;
}
void usb_serial_jtag_set_select_notif_callback(usj_select_notif_callback_t fn) { usb_irq = fn; }
void xTaskNotify(TaskHandle_t task, uint32_t bits, int action) {
    assert(task == &usb_thread && action == eSetBits);
    pthread_mutex_lock(&io_lock);
    notifications |= bits;
    pthread_cond_broadcast(&io_ready);
    pthread_mutex_unlock(&io_lock);
}
void xTaskNotifyFromISR(TaskHandle_t task, uint32_t bits, int action, BaseType_t *woken) {
    *woken = pdTRUE;
    xTaskNotify(task, bits, action);
}
void xTaskNotifyWait(uint32_t before, uint32_t after, uint32_t *out, unsigned wait) {
    assert(!before && after == UINT32_MAX && (wait == 0 || wait == portMAX_DELAY));
    pthread_mutex_lock(&io_lock);
    while (wait && !notifications) pthread_cond_wait(&io_ready, &io_lock);
    *out = notifications;
    notifications = 0;
    pthread_mutex_unlock(&io_lock);
}
void vTaskSuspend(void *task) { assert(!task); pthread_exit(NULL); }
void vTaskDelete(void *task) { assert(task == &usb_thread); assert(!pthread_join(usb_thread, NULL)); }
int usb_serial_jtag_read_bytes(void *dest,uint32_t limit,unsigned wait) {
    assert(wait == 0);
    pthread_mutex_lock(&io_lock);
    size_t n = usb_rx ? strlen(usb_rx) : 0;
    if (n > limit) n = limit;
    if (n) { memcpy(dest,usb_rx,n); usb_rx += n; }
    pthread_mutex_unlock(&io_lock);
    return (int)n;
}
int usb_serial_jtag_write_bytes(const void *data,size_t n,unsigned wait) {
    (void)wait; assert(console_uses_driver && n < sizeof(usb_tx));
    pthread_mutex_lock(&io_lock);
    memcpy(usb_tx,data,n); usb_tx[n] = 0;
    pthread_cond_broadcast(&io_ready);
    pthread_mutex_unlock(&io_lock);
    return (int)n;
}
static void setup_command(const char *command) {
    assert(usb_irq);
    BaseType_t woken = 0;
    usb_irq(USJ_SELECT_WRITE_NOTIF, &woken);
    assert(!woken); /* Console output cannot wake the pairing task. */
    pthread_mutex_lock(&io_lock);
    usb_tx[0] = 0;
    usb_rx = command;
    pthread_mutex_unlock(&io_lock);
    usb_irq(USJ_SELECT_READ_NOTIF, &woken);
    assert(woken);
    pthread_mutex_lock(&io_lock);
    while (!usb_tx[0]) pthread_cond_wait(&io_ready, &io_lock);
    pthread_mutex_unlock(&io_lock);
}

int main(void) {
    wifi_fail_start = wifi_fail_stop = true;
    assert(baji_connectivity_start() == ESP_OK && baji_connectivity_feed());
    assert(!*wifi_name && pair_len == 44);
    assert(baji_connectivity_start() == ESP_ERR_INVALID_STATE);
    setup_command("BAJI_WIFI 736369656e6365:3132333435363738\n");
    assert(baji_connectivity_stop() == ESP_FAIL);
    assert(!strcmp(usb_tx,"BAJI_WIFI_SAVED reboot\n") && station_len == 98);
    assert(sync_live && loop_live && wifi_live && !ble_live && !usb_installed && !closed);
    assert(baji_connectivity_stop() == ESP_OK && !sync_live && !loop_live && closed == 1);
    assert(wifi_stops == 2 && ble_stops == 1);

    wifi_fail_start = false;
    external_loop = ble_fail_stop = true;
    assert(baji_connectivity_start() == ESP_OK);
    assert(!strcmp(wifi_name,"science"));
    setup_command("BAJI_PAIR?\n");
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
    setup_command("BAJI_WIFI :\n");
    assert(baji_connectivity_stop() == ESP_OK && !strcmp(usb_tx,"BAJI_WIFI_SAVED reboot\n"));
    assert(baji_connectivity_stop() == ESP_OK && closed == 4);
    pair_len = 1;
    assert(baji_connectivity_start() == ESP_ERR_INVALID_SIZE && !entropy_on && !sync_live && !usb_installed);
    assert(!baji_connectivity_feed());
    puts("Connectivity fault injection: partial starts, independent stops, retry, external loop, NVS and USB passed.");
}
