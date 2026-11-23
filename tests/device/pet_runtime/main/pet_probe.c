#include "pet_probe.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include <string.h>

/* The probe samples the actual RGB565 payload handed to the panel painter. The
 * grid is inside the portrait; untouched samples keep their panel value. */
enum { CX=33, CY=31, LEFT=102, TOP=115, STEP=8 };
static uint16_t cells[CY][CX];
static bool recording;
static int64_t bus_start;
int64_t pet_probe_bus_start(void) { return bus_start; }

typedef struct { baji_paint_fn paint; void *context; } tap_t;
void pet_probe_enable(bool enabled) { recording=enabled; }
uint32_t pet_probe_signature(void)
{
    return esp_rom_crc32_le(0,(const uint8_t *)cells,sizeof(cells));
}

static void paint_tap(const baji_rect_t *band,uint8_t *pixels,void *ctx)
{
    tap_t *tap=ctx;
    tap->paint(band,pixels,tap->context);
    unsigned top=band->y, left=band->x, end=top+band->height, right=left+band->width;
    unsigned y0=top>TOP ? (top-TOP+STEP-1)/STEP : 0;
    unsigned x0=left>LEFT ? (left-LEFT+STEP-1)/STEP : 0;
    for (unsigned j=y0;j<CY && TOP+j*STEP<end;j++) {
        unsigned pos=(TOP+j*STEP-top)*band->width;
        for (unsigned i=x0;i<CX && LEFT+i*STEP<right;i++)
            memcpy(&cells[j][i],pixels+2*(pos+LEFT+i*STEP-left),2);
    }
}

esp_err_t __real_baji_board_paint_round_regions(baji_board_t *,const baji_rect_t *,size_t,baji_paint_fn,void *);
esp_err_t __wrap_baji_board_paint_round_regions(baji_board_t *board,const baji_rect_t *regions,size_t count,
                                                baji_paint_fn paint,void *context)
{
    if (!recording) return __real_baji_board_paint_round_regions(board,regions,count,paint,context);
    bus_start=esp_timer_get_time();
    tap_t tap={paint,context};
    return __real_baji_board_paint_round_regions(board,regions,count,paint_tap,&tap);
}
