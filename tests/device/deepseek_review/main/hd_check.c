#include "hd_check.h"
#include "breath_trace.h"
#include "baji_deepseek_app.h"
#include "baji_deepseek_hd.h"
#include "baji_runtime.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

esp_err_t hd_check_motion(baji_board_t *board, const void *pack, size_t bytes, bool trace)
{
    dsh_art_t art;
    if (!dsh_art_open(&art, pack, bytes)) return ESP_ERR_INVALID_RESPONSE;
    usb_serial_jtag_driver_config_t usb = {.tx_buffer_size=512, .rx_buffer_size=8192};
    esp_err_t err = usb_serial_jtag_driver_install(&usb);
    if (err != ESP_OK) return err;
    enum { ROWS = 8 };
    dsh_canvas_t *canvas = heap_caps_calloc(1, sizeof(*canvas), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t *band = heap_caps_malloc(DSH_STAGE * ROWS * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!canvas || !band || !dsh_canvas_init(canvas, &art)) err = ESP_ERR_NO_MEM;
    unsigned stages = 0, pages = 0, mismatch = 0, unpack_max = 0, dma_max = 0, warm_peak = 0;
    unsigned warm_select_max = 0, warm_dma_max = 0;
    dsh_motion_t held[16];
    uint8_t *owned[16] = {0};
    for (unsigned entry = 1; err == ESP_OK && entry <= 17; ++entry) {
        for (unsigned level = 1; err == ESP_OK && level <= 16; ++level) {
            printf("DSK_PAGE entry=%u level=%u\n", entry, level);
            uint32_t hdr[4];
            if (!review_usb_read_exact(hdr, sizeof hdr) || hdr[0] > 2 || !hdr[1] || hdr[1] > 256 * 1024) {
                err = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            uint8_t *page = heap_caps_malloc(hdr[1], MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!page) {
                err = ESP_ERR_NO_MEM;
                break;
            }
            dsh_motion_t motion;
            if (!review_usb_read_exact(page, hdr[1]) ||
                !dsh_motion_open_frame(&motion, entry, level, hdr[0], page, hdr[1])) {
                err = ESP_ERR_INVALID_RESPONSE;
            } else {
                ++pages;
                dsh_canvas_set_motion(canvas, &motion);
                for (unsigned guide = 0; err == ESP_OK && guide < 2; ++guide) {
                    dsh_view_t view = {.item=entry < 7 ? entry-1 : entry, .guides=guide, .breath=level};
                    int64_t start = esp_timer_get_time();
                    if (!dsh_canvas_select(canvas, view)) {
                        err = ESP_ERR_INVALID_STATE;
                        break;
                    }
                    unsigned us = (unsigned)(esp_timer_get_time()-start);
                    if (us > unpack_max) unpack_max = us;
                    if (!guide) {
                        /* The physical panel requires an even start and end;
                         * this includes the two border pixels around the stage. */
                        int64_t before_dma = esp_timer_get_time();
                        err = baji_board_paint_round(board, (baji_rect_t){DSH_INSET-1,DSH_INSET-1,
                                              DSH_STAGE+2,DSH_STAGE+2}, dsh_paint, canvas);
                        int64_t after_dma = esp_timer_get_time();
                        unsigned dma = (unsigned)(after_dma-before_dma);
                        if (dma > dma_max) dma_max = dma;
                        if (level > 1) {
                            if (us > warm_select_max) warm_select_max = us;
                            if (dma > warm_dma_max) warm_dma_max = dma;
                            unsigned pair = (unsigned)(after_dma-start);
                            if (pair > warm_peak) warm_peak = pair;
                        }
                    }
                    uint32_t crc = 0;
                    for (unsigned y = 0; y < DSH_STAGE; y += ROWS) {
                        unsigned rows = DSH_STAGE-y < ROWS ? DSH_STAGE-y : ROWS;
                        baji_rect_t clip = {DSH_INSET, DSH_INSET + y, DSH_STAGE, rows};
                        dsh_paint(&clip, band, canvas);
                        crc = esp_rom_crc32_le(crc, band, DSH_STAGE * rows * 2);
                    }
                    if (crc != hdr[2+guide] && ++mismatch <= 5)
                        printf("DSK_MOTION_DIFF entry=%u level=%u guide=%u actual=%08x expected=%08x\n",
                               entry, level, guide, (unsigned)crc, (unsigned)hdr[2+guide]);
                    ++stages;
                }
                dsh_canvas_set_motion(canvas, NULL);
                if (trace && entry == 2 && err == ESP_OK) {
                    held[level-1] = motion;
                    owned[level-1] = page;
                    page = NULL;
                }
            }
            heap_caps_free(page);
        }
    }
    usb_serial_jtag_driver_uninstall();
    printf("DSK_MOTION pages=%u stages=%u mismatch=%u max_select_us=%u max_dma_us=%u "
           "warm_select_max_us=%u warm_dma_max_us=%u warm_pair_max_us=%u err=%d\n",
           pages, stages, mismatch, unpack_max, dma_max, warm_select_max, warm_dma_max, warm_peak, err);
    if (trace && err == ESP_OK && !mismatch) err = breath_trace(board, canvas, held);
    for (unsigned i = 0; i < 16; ++i) heap_caps_free(owned[i]);
    dsh_canvas_close(canvas);
    heap_caps_free(canvas);
    heap_caps_free(band);
    return err != ESP_OK ? err : mismatch ? ESP_ERR_INVALID_CRC : ESP_OK;
}

esp_err_t hd_check_pixels(baji_board_t *gallery_board, const void *pack, size_t bytes,
                          const hd_sample_t *samples, size_t count)
{
    dsh_art_t art;
    if (!dsh_art_open(&art, pack, bytes)) return ESP_ERR_INVALID_RESPONSE;
    enum { ROWS = 16 };
    dsh_canvas_t *canvas = heap_caps_calloc(1, sizeof(*canvas), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t *band = heap_caps_malloc(DSH_PANEL * ROWS * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    esp_err_t err = ESP_OK;
    unsigned mismatches = 0, gallery_draws = 0;
    int64_t start = esp_timer_get_time();
    if (!canvas || !band || !dsh_canvas_init(canvas, &art)) err = ESP_ERR_NO_MEM;
    for (size_t i = 0; err == ESP_OK && i < count; ++i) {
        hd_sample_t s = samples[i];
        dsh_view_t view = {.gallery=s.kind != 0, .guides=s.flag != 0, .breath=s.breath};
        if (s.kind == 0) view.item = s.item;
        else {
            view.sheet = s.item;
            view.zoom = s.flag;
            view.pan_x = s.pan_x;
            view.pan_y = s.pan_y;
        }
        if (!dsh_canvas_select(canvas, view)) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        uint32_t crc = 0;
        for (unsigned y = 0; y < DSH_PANEL; y += ROWS) {
            unsigned rows = DSH_PANEL-y < ROWS ? DSH_PANEL-y : ROWS;
            baji_rect_t area = {0, y, DSH_PANEL, rows};
            dsh_paint(&area, band, canvas);
            crc = esp_rom_crc32_le(crc, band, DSH_PANEL * rows * 2);
        }
        if (crc != s.crc && ++mismatches <= 5) {
            printf("DSK_HD_DIFF i=%u kind=%u item=%u flag=%u breath=%u actual=%08x expected=%08x\n",
                   (unsigned)i, s.kind, s.item, s.flag, s.breath, (unsigned)crc, (unsigned)s.crc);
        }
        if (gallery_board && s.kind) {
            err = baji_board_paint_round(gallery_board, (baji_rect_t){0,0,DSH_PANEL,DSH_PANEL},
                                        dsh_paint, canvas);
            if (err == ESP_OK && !gallery_draws) err = baji_board_set_brightness(gallery_board, 45);
            if (err == ESP_OK) ++gallery_draws;
        }
        if (i % 8 == 0) vTaskDelay(1);
    }
    printf("DSK_HD_PIXELS cases=%u mismatch=%u elapsed_ms=%u gallery_dma=%u err=%d\n",
           (unsigned)count, mismatches, (unsigned)((esp_timer_get_time()-start)/1000), gallery_draws, err);
    dsh_canvas_close(canvas);
    heap_caps_free(canvas);
    heap_caps_free(band);
    return err != ESP_OK ? err : mismatches ? ESP_ERR_INVALID_CRC : ESP_OK;
}

esp_err_t hd_check_switch(baji_board_t *board, const baji_board_info_t *info, const void *pixels,
                          size_t pixel_bytes, const void *hd, size_t hd_bytes)
{
    deepseek_app_t *app = NULL;
    int64_t now = esp_timer_get_time(), due = now;
    esp_err_t err = deepseek_app_open(&app, board, info, pixels, pixel_bytes, hd, hd_bytes, now);
    if (err != ESP_OK) return err;
    baji_input_t input = {0};
    err = deepseek_app_tick(app, &input, now, &due);
    for (unsigned i = 0; err == ESP_OK && i < 2; ++i) {
        ++input.presses;
        input.press_x = input.press_y = 233;
        input.touch = (baji_touch_sample_t){true,233,233};
        now += 90000;
        err = deepseek_app_tick(app, &input, now, &due);
        now += 680000;
        if (err == ESP_OK) err = deepseek_app_tick(app, &input, now, &due);
        input.touch.pressed = false;
        now += 20000;
        if (err == ESP_OK) err = deepseek_app_tick(app, &input, now, &due);
        if (err == ESP_OK && deepseek_app_is_hd(app) != (i == 0)) err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) puts("DSK_SWITCH hd=1 pixel=1");
    deepseek_app_close(app);
    return err;
}
