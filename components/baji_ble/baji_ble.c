#include "baji_ble.h"
#include "sdkconfig.h"

#if CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_NIMBLE_ROLE_PERIPHERAL && CONFIG_BT_NIMBLE_GATT_SERVER
#include "ble_fragments.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdatomic.h>
#include <string.h>

#define BAJI_UUID(id) BLE_UUID128_INIT(0xa7,0x45,0xc4,0x8f,0x46,0xb1,0xb7,0x97, \
                                       0xc9,0x4b,0xbb,0x79,id,0x00,0xa1,0x95)
static const ble_uuid128_t service_uuid = BAJI_UUID(0);
static const ble_uuid128_t hello_uuid = BAJI_UUID(1);
static const ble_uuid128_t state_uuid = BAJI_UUID(2);
static const char name[] = "BAJI-S3";

/* Only the NimBLE host task accesses peer and the reassembly buffer. The
 * owner joins that task before dropping the borrowed endpoint context. */
static baji_link_endpoint_t endpoint;
static baji_ble_fragments_t rx;
static uint16_t peer = BLE_HS_CONN_HANDLE_NONE;
static struct ble_npl_callout retry;
static bool retry_ready;
static atomic_bool accepting;
static atomic_flag lifecycle = ATOMIC_FLAG_INIT;
static TaskHandle_t host;
static bool initialized;
static esp_err_t teardown_error;
static SemaphoreHandle_t joined;
static StaticSemaphore_t joined_storage;

static int read_hello(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg)
{
    (void)attr; (void)arg;
    if (!atomic_load(&accepting) || conn != peer) return BLE_ATT_ERR_UNLIKELY;
    uint8_t nonce[16];
    endpoint.hello(endpoint.context, nonce);
    return os_mbuf_append(ctx->om, nonce, sizeof nonce) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

static int write_state(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg)
{
    (void)attr; (void)arg;
    if (!atomic_load(&accepting) || conn != peer || conn == BLE_HS_CONN_HANDLE_NONE)
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    uint8_t chunk[BAJI_BLE_FRAME_MAX + BAJI_BLE_PREFIX];
    uint16_t length = OS_MBUF_PKTLEN(ctx->om);
    if (length > sizeof chunk || os_mbuf_copydata(ctx->om, 0, length, chunk)) {
        baji_ble_frag_reset(&rx);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    switch (baji_ble_frag_push(&rx, chunk, length)) {
    case BAJI_BLE_MORE:
        return 0;
    case BAJI_BLE_COMPLETE:
        /* The final Write Response acknowledges authentication and publication.
         * Intermediate responses only acknowledge a fragment. */
        return endpoint.receive(endpoint.context, rx.data, rx.size) == ESP_OK
             ? 0 : BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    default:
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
}

static const struct ble_gatt_chr_def characteristics[] = {
    {.uuid = &hello_uuid.u, .access_cb = read_hello, .flags = BLE_GATT_CHR_F_READ},
    {.uuid = &state_uuid.u, .access_cb = write_state, .flags = BLE_GATT_CHR_F_WRITE},
    {0}
};
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service_uuid.u, .characteristics = characteristics},
    {0}
};

static int gap_event(struct ble_gap_event *event, void *arg);

static void advertise(void)
{
    if (!atomic_load(&accepting) || peer != BLE_HS_CONN_HANDLE_NONE ||
        !ble_hs_synced() || ble_gap_adv_active()) return;
    struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .uuids128 = &service_uuid, .num_uuids128 = 1, .uuids128_is_complete = 1,
        .name = (const uint8_t *)name, .name_len = sizeof name - 1, .name_is_complete = 1,
    };
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = 160, .itvl_max = 240, /* 100 to 150 ms */
    };
    uint8_t address_type;
    int rc = ble_hs_util_ensure_addr(0);
    if (!rc) rc = ble_hs_id_infer_auto(0, &address_type);
    if (!rc) rc = ble_gap_adv_set_fields(&fields);
    if (!rc) rc = ble_gap_adv_start(address_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc && atomic_load(&accepting)) {
        ESP_LOGW("baji_ble", "advertising retry, host error %d", rc);
        ble_npl_callout_reset(&retry, ble_npl_time_ms_to_ticks32(1000));
    }
}

static void retry_advertising(struct ble_npl_event *event)
{
    (void)event;
    advertise();
}

static void reset_host(int reason)
{
    ESP_LOGW("baji_ble", "host reset, reason %d", reason);
    peer = BLE_HS_CONN_HANDLE_NONE;
    baji_ble_frag_reset(&rx);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            peer = event->connect.conn_handle;
            baji_ble_frag_reset(&rx);
        } else {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        if (peer == event->disconnect.conn.conn_handle) {
            peer = BLE_HS_CONN_HANDLE_NONE;
            baji_ble_frag_reset(&rx);
            advertise();
        }
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    default:
        break;
    }
    return 0;
}

static void run_host(void *arg)
{
    (void)arg;
    nimble_port_run();
    xSemaphoreGive(joined);
    vTaskDelete(NULL);
}

static esp_err_t close_stack(void)
{
    atomic_store(&accepting, false);
    if (teardown_error != ESP_OK) return teardown_error;
    if (host) {
        /* Start schedules host synchronization. A stop from the owner can
         * arrive before NimBLE has processed its first startup event. */
        int rc = nimble_port_stop();
        for (unsigned i = 0; rc == BLE_HS_EALREADY && i < 100; i++) {
            vTaskDelay(1);
            rc = nimble_port_stop();
        }
        if (rc) return ESP_ERR_INVALID_STATE;
        xSemaphoreTake(joined, portMAX_DELAY);
        host = NULL;
    }
    if (retry_ready) {
        ble_npl_callout_stop(&retry);
        ble_npl_callout_deinit(&retry);
        retry_ready = false;
    }
    if (initialized) {
        /* IDF deinitializes host, then controller. A hard error here can
         * leave only part of the SDK stack alive; do not deinit it twice. */
        teardown_error = nimble_port_deinit();
        if (teardown_error != ESP_OK) return teardown_error;
        initialized = false;
    }
    if (joined) {
        vSemaphoreDelete(joined);
        joined = NULL;
    }
    endpoint = (baji_link_endpoint_t){0};
    peer = BLE_HS_CONN_HANDLE_NONE;
    baji_ble_frag_reset(&rx);
    return ESP_OK;
}

esp_err_t baji_ble_start(const baji_link_endpoint_t *config)
{
    if (!config || !config->hello || !config->receive) return ESP_ERR_INVALID_ARG;
    if (atomic_flag_test_and_set(&lifecycle)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (initialized || teardown_error != ESP_OK) goto done;
    err = nimble_port_init();
    if (err != ESP_OK) goto done;
    initialized = true;
    endpoint = *config;
    joined = xSemaphoreCreateBinaryStatic(&joined_storage);
    ble_hs_cfg.reset_cb = reset_host;
    ble_hs_cfg.sync_cb = advertise;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set(name);
    if (!rc) rc = ble_gatts_count_cfg(services);
    if (!rc) rc = ble_gatts_add_svcs(services);
    if (rc) { err = ESP_FAIL; goto failed; }
    if (ble_npl_callout_init(&retry, nimble_port_get_dflt_eventq(), retry_advertising, NULL)) {
        err = ESP_ERR_NO_MEM;
        goto failed;
    }
    retry_ready = true;
    atomic_store(&accepting, true);
    /* IDF's nimble_port_freertos_init wrapper does not report task creation
     * failure. Own the host task so the display can carry on if DRAM is low. */
    if (xTaskCreatePinnedToCore(run_host, "baji_ble", NIMBLE_HS_STACK_SIZE, NULL,
                                configMAX_PRIORITIES - 4, &host, NIMBLE_CORE) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto failed;
    }
    goto done;
failed: {
        esp_err_t cleanup = close_stack();
        if (cleanup != ESP_OK) err = cleanup;
    }
done:
    atomic_flag_clear(&lifecycle);
    return err;
}

esp_err_t baji_ble_stop(void)
{
    if (atomic_flag_test_and_set(&lifecycle)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = host == xTaskGetCurrentTaskHandle() ? ESP_ERR_INVALID_STATE : close_stack();
    atomic_flag_clear(&lifecycle);
    return err;
}
#else
esp_err_t baji_ble_start(const baji_link_endpoint_t *endpoint)
{
    (void)endpoint;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t baji_ble_stop(void)
{
    return ESP_OK;
}
#endif
