#include "sdk_shim.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

struct host_semaphore {
    pthread_mutex_t lock;
    pthread_cond_t available;
    int token;
};

static SemaphoreHandle_t make_semaphore(int token)
{
    SemaphoreHandle_t s = calloc(1, sizeof(*s));
    assert(s);
    assert(!pthread_mutex_init(&s->lock, NULL));
    assert(!pthread_cond_init(&s->available, NULL));
    s->token = token;
    return s;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return make_semaphore(1); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return make_semaphore(0); }

int xSemaphoreTake(SemaphoreHandle_t s, uint32_t wait)
{
    assert(wait == 0 || wait == portMAX_DELAY);
    pthread_mutex_lock(&s->lock);
    while (wait && !s->token) pthread_cond_wait(&s->available, &s->lock);
    int taken = s->token;
    s->token = 0;
    pthread_mutex_unlock(&s->lock);
    return taken;
}

int xSemaphoreGive(SemaphoreHandle_t s)
{
    pthread_mutex_lock(&s->lock);
    int changed = !s->token;
    s->token = 1;
    pthread_cond_signal(&s->available);
    pthread_mutex_unlock(&s->lock);
    return changed;
}

void vSemaphoreDelete(SemaphoreHandle_t s)
{
    pthread_cond_destroy(&s->available);
    pthread_mutex_destroy(&s->lock);
    free(s);
}

static _Atomic int64_t now_us;
void test_set_ms(uint64_t ms) { atomic_store(&now_us, (int64_t)ms * 1000); }
int64_t esp_timer_get_time(void) { return atomic_load(&now_us); }

/* Deterministic, distinct challenges for the handful of leases in each run. */
void esp_fill_random(void *out, size_t size)
{
    static atomic_uint batch;
    unsigned serial = atomic_fetch_add(&batch, 1);
    uint8_t *bytes = out;
    for (size_t i = 0; i < size; i++) bytes[i] = (uint8_t)(serial + 17*i);
}

typedef int (*hmac_fn)(const uint8_t *, size_t, const uint8_t *, size_t, uint8_t *);
static hmac_fn test_hmac;
void test_set_hmac(hmac_fn fn) { test_hmac = fn; }
const mbedtls_md_info_t *mbedtls_md_info_from_type(int type)
{
    static const mbedtls_md_info_t sha256 = {MBEDTLS_MD_SHA256};
    return type == MBEDTLS_MD_SHA256 ? &sha256 : NULL;
}

int mbedtls_md_hmac(const mbedtls_md_info_t *info, const uint8_t *key, size_t key_size,
                    const uint8_t *data, size_t data_size, uint8_t *digest)
{
    assert(info && info->type == MBEDTLS_MD_SHA256 && test_hmac);
    return test_hmac(key, key_size, data, data_size, digest);
}

int mbedtls_ct_memcmp(const void *left, const void *right, size_t size)
{
    const uint8_t *a = left, *b = right;
    uint8_t diff = 0;
    for (size_t i = 0; i < size; i++) diff |= a[i] ^ b[i];
    return diff;
}
