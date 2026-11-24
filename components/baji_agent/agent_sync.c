#include "baji_agent_sync.h"
#include "agent_wire.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/md.h"
#include "mbedtls/constant_time.h"
#include <stdlib.h>
#include <string.h>

struct baji_agent_sync {
    SemaphoreHandle_t lock, changed;
    baji_state_t state;
    uint8_t key[32], nonce[16], stream[8];
    uint64_t lease_until_ms;
    bool bound, selected;
    char model[BAJI_MODEL_MAX + 1];
};

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time()/1000; }

static void hello(void *context, uint8_t out[16])
{
    baji_agent_sync_t *s = context;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    uint64_t now = now_ms();
    if (now >= s->lease_until_ms) {
        /* Start the first-packet window when a radio client asks for it.
         * Reading the same challenge again cannot prolong that window. */
        esp_fill_random(s->nonce, sizeof(s->nonce));
        s->lease_until_ms = now + BAJI_LINK_TIMEOUT_MS;
        s->bound = false;
    }
    memcpy(out, s->nonce, 16);
    xSemaphoreGive(s->lock);
}

static esp_err_t receive(void *context, const uint8_t *packet, size_t length)
{
    baji_agent_sync_t *s = context;
    if (!packet || length < BAJI_BODY_FIXED + BAJI_TAG_BYTES ||
        length > BAJI_BODY_FIXED + BAJI_MODEL_MAX + BAJI_TAG_BYTES) return ESP_ERR_INVALID_SIZE;
    size_t body = length - BAJI_TAG_BYTES;
    uint8_t tag[BAJI_TAG_BYTES];
    const mbedtls_md_info_t *sha = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!sha || mbedtls_md_hmac(sha,s->key,sizeof(s->key),packet,body,tag)) return ESP_FAIL;
    if (mbedtls_ct_memcmp(tag,packet+body,sizeof(tag))) return ESP_ERR_INVALID_CRC;
    agent_message_t msg;
    if (!agent_wire_decode(packet, body, &msg)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    uint64_t now = now_ms();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (now < s->lease_until_ms && !memcmp(packet+4,s->nonce,16) &&
        (!s->bound || !memcmp(packet+20,s->stream,8))) {
        if (!s->bound) baji_state_begin_connection(&s->state);
        if (baji_state_apply(&s->state,&msg.snapshot,now) == BAJI_APPLIED) {
            memcpy(s->stream,packet+20,8);
            memcpy(s->model,msg.model,sizeof(s->model));
            s->bound = true;
            s->selected = msg.selected;
            s->lease_until_ms = now + BAJI_LINK_TIMEOUT_MS;
            err = ESP_OK;
        }
    }
    xSemaphoreGive(s->lock);
    if (err == ESP_OK) xSemaphoreGive(s->changed);
    return err;
}

static bool read_view(void *context, uint64_t now, baji_agent_display_t *out)
{
    baji_agent_sync_t *s = context;
    xSemaphoreTake(s->lock,portMAX_DELAY);
    bool available = s->state.has_snapshot;
    out->has_session = s->selected;
    out->view = baji_state_view(&s->state,now);
    memcpy(out->model,s->model,sizeof(s->model));
    xSemaphoreGive(s->lock);
    return available;
}

esp_err_t baji_agent_sync_open(baji_agent_sync_t **out, const uint8_t key[32])
{
    if (!out || !key) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    baji_agent_sync_t *s = calloc(1,sizeof(*s));
    if (!s) return ESP_ERR_NO_MEM;
    s->lock = xSemaphoreCreateMutex();
    s->changed = xSemaphoreCreateBinary();
    if (!s->lock || !s->changed) { baji_agent_sync_close(s); return ESP_ERR_NO_MEM; }
    memcpy(s->key,key,32);
    baji_state_init(&s->state);
    *out = s;
    return ESP_OK;
}

baji_link_endpoint_t baji_agent_endpoint(baji_agent_sync_t *s)
{ return (baji_link_endpoint_t){.context=s,.hello=hello,.receive=receive}; }
baji_agent_feed_t baji_agent_feed(baji_agent_sync_t *s)
{ return (baji_agent_feed_t){.context=s,.read=read_view,.changed=s->changed}; }
void baji_agent_sync_close(baji_agent_sync_t *s)
{
    if (!s) return;
    if (s->lock) vSemaphoreDelete(s->lock);
    if (s->changed) vSemaphoreDelete(s->changed);
    free(s);
}
