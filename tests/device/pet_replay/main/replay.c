#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "baji_board.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "miniz.h"

enum { W = 138, H = 132, N = 8, X = 94, Y = 100, FRAMES = 48, PACED = 300 };
static const baji_rect_t viewport = {X, Y, W*2, H*2};
typedef struct { baji_rect_t box; unsigned first, count; } step_t;
typedef struct {
    const char *name;
    const uint8_t *compressed;
    unsigned bytes;
    uint32_t crc;
    const uint16_t *palette;
    unsigned colors;
    const baji_rect_t *rects;
    const step_t *steps;
} clip_t;
#include "pet_replay_asset.h"

typedef struct { const uint16_t *source; unsigned pixels; } image_t;
static uint32_t duration[PACED], wake_delay[PACED];
static uint16_t *frames;

static esp_err_t unpack(const clip_t *clip)
{
    size_t input = clip->bytes, output = N*W*H;
    uint8_t *indices = heap_caps_malloc(output, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    tinfl_decompressor *decoder = heap_caps_malloc(sizeof(*decoder), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_err_t err = ESP_ERR_NO_MEM;
    if (decoder && indices) {
        tinfl_init(decoder);
        tinfl_status status = tinfl_decompress(decoder, clip->compressed, &input, indices, indices,
                                                &output, TINFL_FLAG_PARSE_ZLIB_HEADER |
                                                TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        err = ESP_ERR_INVALID_RESPONSE;
        if (status == TINFL_STATUS_DONE && output == N*W*H && input == clip->bytes) {
            size_t k = 0;
            for (; k < output && indices[k] < clip->colors; ++k) frames[k] = clip->palette[indices[k]];
            if (k == output && esp_rom_crc32_le(0, (uint8_t *)frames, output*2) == clip->crc)
                err = ESP_OK;
        }
    }
    heap_caps_free(decoder);
    heap_caps_free(indices);
    return err;
}

static void sprite(const baji_rect_t *area, uint8_t *dst, void *arg)
{
    image_t *image = arg;
    image->pixels += (unsigned)area->width * area->height;
    size_t stride = area->width * 2;
    for (unsigned y = 0; y < area->height; y++, dst += stride) {
        if (y & 1) { memcpy(dst, dst - stride, stride); continue; }
        const uint16_t *row = image->source + ((area->y + y - Y)/2)*W + (area->x-X)/2;
        for (unsigned x = 0; x < area->width/2; ++x) {
            uint32_t v = __builtin_bswap16(row[x]);
            v |= v << 16;
            memcpy(dst + x*4, &v, sizeof(v));
        }
    }
}

static void background(const baji_rect_t *area, uint8_t *dst, void *arg)
{
    (void)arg;
    const uint8_t color[2] = {0x21, 0x07};
    size_t count = (size_t)area->width * area->height;
    for (size_t i = 0; i < count; ++i) memcpy(dst + 2*i, color, 2);
}

/* Reconstruct every submitted update into an independent physical-pixel
 * framebuffer. Compare to nearest-neighbour RGB565, including loop wrap. */
static bool validate(const clip_t *clip)
{
    const size_t pixels = 4*W*H;
    uint16_t *screen = heap_caps_malloc(2*pixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *band = heap_caps_malloc(4*W*2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!screen || !band) { heap_caps_free(screen); heap_caps_free(band); return false; }
    bool ok = true;
    for (int i = -1; i < N && ok; ++i) {
        unsigned f = (i+N) % N;
        image_t image = {.source=frames + f*W*H};
        const step_t *s = &clip->steps[f];
        const baji_rect_t *plan = i < 0 ? &viewport : clip->rects + s->first;
        unsigned count = i < 0 ? 1 : s->count;
        for (unsigned j = 0; j < count && ok; ++j) {
            baji_rect_t r = plan[j];
            ok = r.x >= X && r.y >= Y && r.x + r.width <= X+W*2 && r.y+r.height <= Y+H*2 &&
                 !((r.x | r.y | r.width | r.height) & 1) && r.width && r.height;
            unsigned end = r.y+r.height;
            r.height = 2;
            for (;ok && r.y < end; r.y+=2) {
                sprite(&r,band,&image);
                for (unsigned row=0; row<2; row++) {
                    uint16_t *dest = screen+(r.y-Y+row)*2*W+r.x-X;
                    for(unsigned x=0; x<r.width; x++) {
                        unsigned p=(row*r.width+x)*2;
                        dest[x]=((uint16_t)band[p]<<8) | band[p+1];
                    }
                }
            }
        }
        for(unsigned y=0; y<H*2 && ok; y++)
            for(unsigned x=0; x<W*2 && ok; x++)
                ok = screen[y*2*W+x] == image.source[(y/2)*W+x/2];
    }
    heap_caps_free(screen);
    heap_caps_free(band);
    return ok;
}

enum method { FULL, BOX, BANDS };
static esp_err_t draw(baji_board_t *board, const clip_t *clip, unsigned f, enum method mode,
                      unsigned *pixels)
{
    image_t image={.source=frames+f*W*H};
    const step_t *step=&clip->steps[f];
    esp_err_t err=ESP_OK;
    if (mode == FULL) err=baji_board_paint_round(board,viewport,sprite,&image);
    else if (step->count && mode == BOX) err=baji_board_paint_round(board,step->box,sprite,&image);
    else if (step->count)
        err=baji_board_paint_round_regions(board,clip->rects+step->first,step->count,sprite,&image);
    *pixels += image.pixels;
    return err;
}

static int compare(const void *a,const void *b)
{
    uint32_t x=*(const uint32_t *)a, y=*(const uint32_t *)b;
    return (x>y)-(x<y);
}

static esp_err_t measure(baji_board_t *board,const clip_t *clip,enum method mode)
{
    static const char *names[]={"full","box","bands"};
    unsigned pixels=0;
    esp_err_t err=draw(board,clip,N-1,FULL,&pixels);
    for(unsigned i=0; i<N+FRAMES && err==ESP_OK; i++) {
        if(i==N) pixels=0;
        int64_t start=esp_timer_get_time();
        err=draw(board,clip,i%N,mode,&pixels);
        if (i>=N) duration[i-N]=esp_timer_get_time()-start;
        if(i%N==N-1) vTaskDelay(1);
    }
    if(err!=ESP_OK) return err;
    qsort(duration,FRAMES,sizeof(duration[0]),compare);
    printf("REPLAY_COST clip=%s mode=%s n=%u bytes=%u p50=%u p95=%u max=%u\n",
           clip->name,names[mode],FRAMES,pixels*2,(unsigned)(duration[23]+duration[24])/2,
           (unsigned)duration[45],(unsigned)duration[47]);
    return ESP_OK;
}

typedef struct { uint32_t seq, stamp; } edge_t;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static edge_t latest;
static void IRAM_ATTR te_irq(void *arg)
{
    portENTER_CRITICAL_ISR(&lock);
    latest.seq++;
    latest.stamp=(uint32_t)esp_timer_get_time();
    portEXIT_CRITICAL_ISR(&lock);
    BaseType_t woke=pdFALSE;
    vTaskNotifyGiveFromISR((TaskHandle_t)arg,&woke);
    if (woke) portYIELD_FROM_ISR();
}

static edge_t snapshot(void)
{
    portENTER_CRITICAL(&lock);
    edge_t e=latest;
    portEXIT_CRITICAL(&lock);
    return e;
}

static esp_err_t play_paced(baji_board_t *board,const clip_t *clip)
{
    unsigned pixels=0;
    esp_err_t err=draw(board,clip,N-1,FULL,&pixels);
    if(err!=ESP_OK) return err;
    unsigned late=0, skipped=0;
    ulTaskNotifyTake(pdTRUE,0);
    edge_t first={0}, prev=snapshot();
    for(unsigned i=0; i<PACED; i++) {
        edge_t edge;
        for (;;) {
            edge=snapshot();
            if(edge.seq!=prev.seq) break;
            if (!ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(150))) return ESP_ERR_TIMEOUT;
        }
        if(i==0) first=edge;
        else skipped+=edge.seq-prev.seq-1;
        wake_delay[i]=(uint32_t)esp_timer_get_time()-edge.stamp;
        err=draw(board,clip,i%N,BANDS,&pixels);
        if(err!=ESP_OK) return err;
        /* The same critical section orders the two observations against the
         * ISR. They cannot straddle a scheduler stall or a second edge. */
        portENTER_CRITICAL(&lock);
        uint32_t done=(uint32_t)esp_timer_get_time();
        uint32_t completed_edge=latest.seq;
        portEXIT_CRITICAL(&lock);
        if(completed_edge!=edge.seq) late++;
        duration[i]=done-edge.stamp;
        prev=edge;
    }
    qsort(duration,PACED,sizeof(duration[0]),compare);
    qsort(wake_delay,PACED,sizeof(wake_delay[0]),compare);
    printf("REPLAY_TE clip=%s frames=%u edges=%u span_us=%u late=%u skipped=%u "
           "te_dma_p50=%u te_dma_p95=%u te_dma_max=%u wake_p95=%u wake_max=%u\n",
           clip->name,PACED,(unsigned)(prev.seq-first.seq+1),(unsigned)(prev.stamp-first.stamp),
           late,skipped,(unsigned)(duration[149]+duration[150])/2,(unsigned)duration[284],
           (unsigned)duration[299],(unsigned)wake_delay[284],(unsigned)wake_delay[299]);
    return ESP_OK;
}

void app_main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    vTaskDelay(pdMS_TO_TICKS(2500));
    puts("REPLAY_BEGIN rgb565 138x132-to-276x264");
    baji_board_t *board=NULL;
    baji_board_info_t info;
    esp_err_t err=baji_board_open(&board,&info);
    frames=heap_caps_malloc(N*W*H*2,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(!frames && err==ESP_OK) err=ESP_ERR_NO_MEM;
    const gpio_config_t te={.pin_bit_mask=1ULL<<13,.mode=GPIO_MODE_INPUT,.intr_type=GPIO_INTR_POSEDGE};
    if(err==ESP_OK) err=gpio_config(&te);
    bool handler=false;
    if(err==ESP_OK) {
        err=gpio_isr_handler_add(GPIO_NUM_13,te_irq,xTaskGetCurrentTaskHandle());
        handler=err==ESP_OK;
    }
    for(unsigned i=0; i<sizeof(clips)/sizeof(clips[0]) && err==ESP_OK; i++) {
        const clip_t *clip=&clips[i];
        err=unpack(clip);
        if(err==ESP_OK && !validate(clip)) err=ESP_ERR_INVALID_RESPONSE;
        printf("REPLAY_VERIFY clip=%s compressed_bytes=%u verified=%u\n",clip->name,clip->bytes,err==ESP_OK);
        if(err==ESP_OK && i==0) {
            unsigned pixels=0;
            err=baji_board_paint_round(board,(baji_rect_t){0,0,466,466},background,NULL);
            if(err==ESP_OK) err=draw(board,clip,N-1,FULL,&pixels);
            if(err==ESP_OK) err=baji_board_set_brightness(board,40);
        }
        for(enum method mode=FULL; mode<=BANDS && err==ESP_OK; mode++) err=measure(board,clip,mode);
        if(err==ESP_OK) err=play_paced(board,clip);
    }
    if(handler) gpio_isr_handler_remove(GPIO_NUM_13);
    gpio_reset_pin(GPIO_NUM_13);
    if(board) while(baji_board_close(board)!=ESP_OK) vTaskDelay(pdMS_TO_TICKS(100));
    heap_caps_free(frames);
    printf("REPLAY_END err=%d\n",err);
}
