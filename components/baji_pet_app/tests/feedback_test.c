#include "pet_feedback.h"
#include "baji_speaker.h"
#include "feedback_shim.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct queue {
    pthread_mutex_t mutex;
    pthread_cond_t ready;
    size_t size;
    bool pending;
    char item[];
};
QueueHandle_t xQueueCreate(int length, size_t bytes)
{
    assert(length == 1);
    struct queue *q=calloc(1,sizeof(*q)+bytes);
    pthread_mutex_init(&q->mutex,NULL); pthread_cond_init(&q->ready,NULL);
    q->size=bytes; return q;
}
void vQueueDelete(QueueHandle_t q)
{
    pthread_cond_destroy(&q->ready); pthread_mutex_destroy(&q->mutex); free(q);
}
void xQueueOverwrite(QueueHandle_t q,const void *item)
{
    pthread_mutex_lock(&q->mutex);
    memcpy(q->item,item,q->size); q->pending=true;
    pthread_cond_signal(&q->ready); pthread_mutex_unlock(&q->mutex);
}
int xQueueReceive(QueueHandle_t q,void *item,uint32_t wait)
{
    pthread_mutex_lock(&q->mutex);
    while (!q->pending && wait) pthread_cond_wait(&q->ready,&q->mutex);
    bool ok=q->pending;
    if (ok) {memcpy(item,q->item,q->size); q->pending=false;}
    pthread_mutex_unlock(&q->mutex);
    return ok;
}

static pthread_t thread;
static void (*entry)(void *);
static void *run(void *arg) { entry(arg); return NULL; }
int xTaskCreate(void (*fn)(void *),const char *n,unsigned sz,void *arg,int pri,TaskHandle_t *task)
{
    (void)n; (void)sz; (void)pri; (void)task; entry=fn;
    return !pthread_create(&thread,NULL,run,arg);
}
void vTaskDelete(void *nothing) {(void)nothing;}

static uint16_t committed=UINT16_MAX,staged;
static unsigned commits, starts, natural_stops, frames;
static SemaphoreHandle_t drained, entered, proceed;
static atomic_bool suspend_write;
static bool nvs_available=true, fail_start;
esp_err_t nvs_flash_init(void) {return nvs_available?ESP_OK:ESP_FAIL;}
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *out)
{assert(!strcmp(ns,"baji_pet") && mode==NVS_READWRITE); *out=1; return ESP_OK;}
esp_err_t nvs_get_u16(nvs_handle_t h,const char *key,uint16_t *out)
{assert(h==1 && !strcmp(key,"state")); if (committed==UINT16_MAX) return ESP_ERR_NVS_NOT_FOUND; *out=committed; return ESP_OK;}
esp_err_t nvs_set_u16(nvs_handle_t h,const char *key,uint16_t val)
{assert(h==1 && !strcmp(key,"state")); staged=val; return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h) {assert(h==1); committed=staged; commits++; return ESP_OK;}
void nvs_close(nvs_handle_t h) {assert(h==1);}
baji_peripherals_t *baji_board_peripherals(baji_board_t *b) {return (void *)b;}
esp_err_t baji_speaker_open(baji_peripherals_t *p,baji_speaker_t **s)
{(void)p; *s=(void *)1; return ESP_OK;}
esp_err_t baji_speaker_start(baji_speaker_t *s)
{assert(s); starts++; if (fail_start) xSemaphoreGive(entered); return fail_start ? ESP_FAIL : ESP_OK;}
esp_err_t baji_speaker_write(baji_speaker_t *s,const uint8_t *pcm,size_t n)
{
    assert(s && pcm && n>0 && n<=128); frames+=n;
    if (atomic_exchange(&suspend_write,false)) {
        xSemaphoreGive(entered); xSemaphoreTake(proceed,portMAX_DELAY);
    }
    return ESP_OK;
}
void baji_speaker_stop(baji_speaker_t *s,bool drain)
{if (s && drain) {natural_stops++; xSemaphoreGive(drained);}}
void baji_speaker_close(baji_speaker_t *s) {(void)s;}
static void finish(pet_feedback_t *fb) {pet_feedback_close(fb); pthread_join(thread,NULL);}

int main(void)
{
    drained=xSemaphoreCreateBinary(); entered=xSemaphoreCreateBinary(); proceed=xSemaphoreCreateBinary();
    unsigned bond; bool sound;
    pet_feedback_t *fb=pet_feedback_open(NULL,&bond,&sound);
    assert(fb && sound && !bond);
    pet_feedback_update(fb,"unknown-animation",0,true);
    pet_feedback_update(fb,"pillow",4,true);
    xSemaphoreTake(drained,portMAX_DELAY);
    assert(starts==1 && natural_stops==1 && frames==1280);
    atomic_store(&suspend_write,true);
    pet_feedback_update(fb,"doze",5,true);
    xSemaphoreTake(entered,portMAX_DELAY);
    pet_feedback_update(fb,NULL,5,false);
    pet_feedback_update(fb,"heartHug",1000,false);
    xSemaphoreGive(proceed);
    finish(fb);
    assert(committed==100 && starts==2 && natural_stops==1 && frames==1280+128);
    unsigned old_commits=commits;
    fb=pet_feedback_open(NULL,&bond,&sound); assert(fb && !sound && bond==100);
    pet_feedback_update(fb,"reunion",bond,sound);
    finish(fb); assert(commits==old_commits);
    /* The NVS and speaker paths fail independently; user state still round-trips. */
    fail_start=true;
    fb=pet_feedback_open(NULL,&bond,&sound); pet_feedback_update(fb,"feed",77,true); xSemaphoreTake(entered,portMAX_DELAY);
    finish(fb); assert(committed==(77|256));
    nvs_available=false;
    fb=pet_feedback_open(NULL,&bond,&sound); assert(!bond && sound); finish(fb);
    vSemaphoreDelete(proceed); vSemaphoreDelete(entered); vSemaphoreDelete(drained);
    puts("S16 playback, preemption, mute, clean close, NVS round-trip and fallback passed");
}
