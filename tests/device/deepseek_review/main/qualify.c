#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "baji_deepseek_app.h"
#include "live_probe.h"
#include "deepseek_review.h"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hd_check.h"

enum { WIDTH = 466, HEIGHT = 466, BAND_ROWS = 24, MAX_CASES = 66 * 8 * 7 };
typedef struct {
    uint8_t item, frame, phase, view_bubble;
    uint32_t reference_crc;
} sample_t;
_Static_assert(sizeof(sample_t) == 8, "USB fixture record layout");
typedef struct {
    void *pack;
    uint32_t size, count;
    sample_t *samples;
    void *hd;
    uint32_t hd_size, hd_count;
    hd_sample_t *hd_samples;
    bool motion, native_gallery, feed_only, all_clips, hd_live;
} transfer_t;

bool review_usb_read_exact(void *dst, size_t size)
{
    uint8_t *p = dst;
    while (size) {
        int n = usb_serial_jtag_read_bytes(p, size, pdMS_TO_TICKS(5000));
        if (n <= 0) return false;
        p += n;
        size -= n;
    }
    return true;
}

static esp_err_t receive(transfer_t *data)
{
    usb_serial_jtag_driver_config_t usb = {.tx_buffer_size=512, .rx_buffer_size=8192};
    esp_err_t err = usb_serial_jtag_driver_install(&usb);
    if (err != ESP_OK) return err;
    const uint32_t caps = MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM;
    unsigned limit = heap_caps_get_largest_free_block(caps);
    uint32_t hdr[5] = {0}, hd[4] = {0};
    int got = 0;
    for (unsigned i = 0; i < 100 && !got; ++i) {
        printf("DSK_READY max=%u\n", limit);
        got = usb_serial_jtag_read_bytes(hdr, sizeof hdr, pdMS_TO_TICKS(1000));
    }
    err = ESP_ERR_INVALID_RESPONSE;
    if (got <= 0 || !review_usb_read_exact((uint8_t *)hdr + got, sizeof(hdr) - got)) goto done;
    data->all_clips = (hdr[3] & (1u << 31)) != 0;
    data->hd_live = (hdr[3] & (1u << 30)) != 0;
    hdr[3] &= (1u << 30)-1;
    if (!hdr[1] || hdr[1] > limit || !hdr[3] || hdr[3] > MAX_CASES) {
        goto done;
    }
    data->feed_only = memcmp(hdr, "DSQB", 4) == 0;
    bool motion_alone = data->feed_only || memcmp(hdr, "DSQM", 4) == 0;
    data->motion = motion_alone || memcmp(hdr, "DSQ3", 4) == 0;
    bool pair = !motion_alone && (data->motion || memcmp(hdr, "DSQ2", 4) == 0);
    data->native_gallery = motion_alone || memcmp(hdr, "DSQN", 4) == 0;
    if ((!pair && !data->native_gallery && memcmp(hdr, "DSQ1", 4)) ||
        (pair && (!review_usb_read_exact(hd, sizeof hd) || !hd[0] || hd[0] > limit ||
                  !hd[2] || hd[2] > 2000))) goto done;
    data->size = hdr[1];
    data->count = hdr[3];
    size_t table_size = data->count * (data->native_gallery ? sizeof(hd_sample_t) : sizeof(sample_t));
    data->pack = heap_caps_malloc(data->size, caps);
    data->samples = heap_caps_malloc(table_size, caps);
    if (!data->pack || !data->samples) {
        err = ESP_ERR_NO_MEM;
        goto done;
    }
    printf("DSK_RECEIVE bytes=%u cases=%u\n", (unsigned)data->size, (unsigned)data->count);
    if (!review_usb_read_exact(data->pack, data->size) || !review_usb_read_exact(data->samples, table_size)) goto done;
    uint32_t crc = esp_rom_crc32_le(0, data->pack, data->size);
    if (crc != hdr[2] || esp_rom_crc32_le(0, (const uint8_t *)data->samples, table_size) != hdr[4]) {
        err = ESP_ERR_INVALID_CRC;
        goto done;
    }
    printf("DSK_ASSET bytes=%u crc=%08x\n", (unsigned)data->size, (unsigned)crc);
    if (pair) {
        data->hd_size = hd[0];
        data->hd_count = hd[2];
        size_t bytes = data->hd_count * sizeof(hd_sample_t);
        data->hd = heap_caps_malloc(data->hd_size, caps);
        data->hd_samples = heap_caps_malloc(bytes, caps);
        if (!data->hd || !data->hd_samples) {
            err = ESP_ERR_NO_MEM;
            goto done;
        }
        printf("DSK_HD_RECEIVE bytes=%u cases=%u\n", (unsigned)data->hd_size, (unsigned)data->hd_count);
        if (!review_usb_read_exact(data->hd, data->hd_size) || !review_usb_read_exact(data->hd_samples, bytes)) goto done;
        crc = esp_rom_crc32_le(0, data->hd, data->hd_size);
        if (crc != hd[1] || esp_rom_crc32_le(0, (const uint8_t *)data->hd_samples, bytes) != hd[3]) {
            err = ESP_ERR_INVALID_CRC;
            goto done;
        }
        printf("DSK_HD_ASSET bytes=%u crc=%08x\n", (unsigned)data->hd_size, (unsigned)crc);
    }
    err = ESP_OK;
done:
    usb_serial_jtag_driver_uninstall();
    return err;
}

static dsk_frame_t sample_frame(sample_t s)
{
    return (dsk_frame_t){.item=s.item, .frame=s.frame, .phase=s.phase,
                         .close=s.view_bubble / 8, .bubble=s.view_bubble % 8};
}

static esp_err_t pixels(dsk_canvas_t *canvas, const transfer_t *data)
{
    uint8_t *band = heap_caps_malloc(WIDTH * BAND_ROWS * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!band) return ESP_ERR_NO_MEM;
    unsigned mismatches = 0;
    int64_t start = esp_timer_get_time();
    for (uint32_t i = 0; i < data->count; ++i) {
        sample_t sample = data->samples[i];
        if (!dsk_canvas_select(canvas, sample_frame(sample))) {
            heap_caps_free(band);
            return ESP_ERR_INVALID_ARG;
        }
        uint32_t crc = 0;
        for (uint16_t y = 0; y < HEIGHT; y += BAND_ROWS) {
            const unsigned rows = (HEIGHT-y < BAND_ROWS) ? HEIGHT-y : BAND_ROWS;
            baji_rect_t area = {0, y, WIDTH, rows};
            dsk_paint(&area, band, canvas);
            crc = esp_rom_crc32_le(crc, band, WIDTH * rows * 2);
        }
        if (crc != sample.reference_crc && ++mismatches <= 5) {
            printf("DSK_DIFF i=%u item=%u frame=%u phase=%u view=%u bubble=%u actual=%08x expected=%08x\n",
                   (unsigned)i, sample.item, sample.frame, sample.phase, sample.view_bubble / 8,
                   sample.view_bubble % 8, (unsigned)crc, (unsigned)sample.reference_crc);
        }
        if ((i+1) % 128 == 0) printf("DSK_PROGRESS done=%u total=%u\n", (unsigned)(i+1), (unsigned)data->count);
        if (i % 8 == 0) vTaskDelay(1);
    }
    heap_caps_free(band);
    printf("DSK_PIXELS cases=%u mismatch=%u elapsed_ms=%u\n", (unsigned)data->count, mismatches,
           (unsigned)((esp_timer_get_time()-start)/1000));
    return mismatches ? ESP_ERR_INVALID_CRC : ESP_OK;
}

static esp_err_t panel(dsk_canvas_t *canvas, baji_board_t *board)
{
    unsigned minimum = UINT32_MAX, maximum = 0, total = 0;
    for (unsigned i = 0; i < 18; ++i) {
        dsk_frame_t frame = {.item=i, .phase=i%4, .close=i%2, .bubble=i%7};
        if (!dsk_canvas_select(canvas, frame)) return ESP_ERR_INVALID_ARG;
        int64_t start = esp_timer_get_time();
        esp_err_t err = baji_board_paint_round(board, (baji_rect_t){0,0,WIDTH,HEIGHT}, dsk_paint, canvas);
        if (err != ESP_OK) return err;
        unsigned elapsed = (unsigned)(esp_timer_get_time()-start);
        if (elapsed < minimum) minimum = elapsed;
        if (elapsed > maximum) maximum = elapsed;
        total += elapsed;
    }
    printf("DSK_PANEL frames=18 min_us=%u mean_us=%u max_us=%u\n", minimum, total/18, maximum);
    return ESP_OK;
}

void app_main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    vTaskDelay(pdMS_TO_TICKS(1250));
    transfer_t data = {0};
    baji_board_t *board = NULL;
    baji_board_info_t info = {0};
    dsk_art_t art = {0};
    dsk_canvas_t canvas;
    esp_err_t err = receive(&data);
    if (err == ESP_OK && !data.native_gallery && !dsk_art_open(&art, data.pack, data.size))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK) err = baji_board_open(&board, &info);
    if (err == ESP_OK) {
        printf("DSK_BOARD w=%u h=%u touch=%d power=%d free_psram=%u\n", info.width, info.height,
               info.touch_status, info.power_status, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        if (info.width != WIDTH || info.height != HEIGHT || info.touch_status != ESP_OK)
            err = ESP_ERR_INVALID_STATE;
    }
    if (data.native_gallery) {
        if (err == ESP_OK)
            err = hd_check_pixels(board, data.pack, data.size, (const hd_sample_t *)data.samples, data.count);
        if (err == ESP_OK && data.motion) err = hd_check_motion(board, &info, data.pack, data.size, true, data.feed_only);
        if (err == ESP_OK) puts("DSK_NATIVE_OK");
        else printf("DSK_FAIL err=%d\n", err);
        if (board) while (baji_board_close(board) != ESP_OK) vTaskDelay(pdMS_TO_TICKS(20));
        heap_caps_free(data.samples);
        heap_caps_free(data.pack);
        return;
    }
    if (err == ESP_OK) {
        dsk_canvas_init(&canvas, &art);
        err = pixels(&canvas, &data);
        if (err == ESP_OK) err = panel(&canvas, board);
        dsk_canvas_release_motion(&canvas);
    }
    heap_caps_free(data.samples);
    dsk_art_close(&art);
    if (err == ESP_OK && data.hd) {
        err = hd_check_pixels(NULL, data.hd, data.hd_size, data.hd_samples, data.hd_count);
        if (err == ESP_OK && data.motion) err = hd_check_motion(board, &info, data.hd, data.hd_size, false, false);
        if (err == ESP_OK) err = hd_check_switch(board, &info, data.pack, data.size, data.hd, data.hd_size);
    }
    heap_caps_free(data.hd_samples);
    if (err == ESP_OK) {
        puts("DSK_LIVE");
        if (data.hd_live) err = hd_check_live(board, &info, data.pack, data.size, data.hd, data.hd_size);
        else {
            live_probe_start(data.all_clips);
            err = data.hd ? deepseek_app_run_pair(board, &info, data.pack, data.size, data.hd, data.hd_size)
                          : deepseek_app_run_loaded(board, &info, data.pack, data.size);
        }
        printf("DSK_LIVE_END err=%d\n", err);
    } else {
        printf("DSK_FAIL err=%d\n", err);
    }
    if (board) while (baji_board_close(board) != ESP_OK) vTaskDelay(pdMS_TO_TICKS(20));
    heap_caps_free(data.pack);
    heap_caps_free(data.hd);
}
