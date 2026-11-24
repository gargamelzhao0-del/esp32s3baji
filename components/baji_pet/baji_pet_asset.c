#include "baji_pet_private.h"

#include <limits.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "miniz.h"
#ifndef BAJI_PET_EXTERNAL_ASSET
#include "esp_log.h"
#endif

static bool within(range_t range, unsigned count)
{
    return range.first <= count && range.count <= count-range.first;
}

static bool valid_rect(baji_rect_t r)
{
    return r.x+r.width <= SIDE && r.y+r.height <= SIDE &&
           !((r.x | r.y | r.width | r.height) & 1);
}

/* Parse all indices and lengths at open so painting is bounded and allocation-free. */
static bool bind_tables(baji_pet_t *p, size_t length, bool compact)
{
    const uint8_t *data = p->decoded;
    if (length < sizeof(header_t)) return false;
    const header_t *h = (const header_t *)data;
    if (!h->tiles || h->tiles > 65536 || !h->rows ||
        h->frames[0] != IDLE_FRAMES || h->frames[1] != PAT_FRAMES ||
        h->fps[0] != 12 || h->fps[1] != 16) return false;
    size_t offset = sizeof(*h);
    /* The range check precedes multiplication, including on 32-bit targets. */
#define TABLE(field, type, count) do { \
    size_t entries = (count); \
    if (offset > length || entries > (length-offset)/sizeof(type)) return false; \
    p->field = (const type *)(data+offset); offset += entries*sizeof(type); \
} while (0)
    TABLE(poses,pose_t,POSES);
    TABLE(maps,uint16_t,POSES*CELLS);
    offset = (offset+3) & ~(size_t)3;
    TABLE(gazes,range_t,POSES*GAZES);
    TABLE(patches,patch_t,h->patches);
    TABLE(rectangles,baji_rect_t,h->rectangles);
    uint32_t bank_start = 0;
    if (compact) {
        if (length-offset < sizeof(bank_start)) return false;
        memcpy(&bank_start, data+offset, sizeof(bank_start));
        if (bank_start > h->rows) return false;
        offset += sizeof(bank_start);
        TABLE(rows16,uint16_t,h->tiles*TILE);
    } else {
        TABLE(rows32,uint32_t,h->tiles*TILE);
    }
    if (h->rows > length/(TILE*2)) return false;
    TABLE(pixel_bank[0],uint8_t,h->rows*TILE*2);
    p->pixel_bank[1] = p->pixel_bank[0] + (size_t)bank_start*TILE*2;
#undef TABLE
    if (offset != length) return false;
    for (unsigned i=0; i<POSES; i++) {
        const pose_t *pose = p->poses+i;
        if (pose->forehead[2] <= 0 || pose->forehead[3] <= 0) return false;
        for (unsigned j=0;j<2;j++) if (!valid_rect(pose->eyes[j])) return false;
        for (unsigned j=0;j<3;j++)
            if (!within(pose->changes[j],h->rectangles)) return false;
    }
    for (unsigned i=0;i<POSES*CELLS;i++) if (p->maps[i] >= h->tiles) return false;
    for (unsigned i=0;i<POSES*GAZES;i++) if (!within(p->gazes[i],h->patches)) return false;
    for (unsigned i=0;i<h->patches;i++)
        if (p->patches[i].cell >= CELLS || p->patches[i].tile >= h->tiles) return false;
    for (unsigned i=0;i<h->rectangles;i++) if (!valid_rect(p->rectangles[i])) return false;
    for (unsigned i=0; i<h->tiles*TILE; ++i) {
        uint32_t row = compact ? p->rows16[i] + (i >= 32768u*TILE ? bank_start : 0) : p->rows32[i];
        if (row >= h->rows) return false;
    }
    return true;
}

esp_err_t baji_pet_open_data(baji_pet_t **out, const void *packed, size_t size)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!packed || size < 16) return ESP_ERR_INVALID_ARG;
    const uint8_t *bytes = packed;
    uint32_t meta[3];
    memcpy(meta,bytes+4,sizeof(meta));
    bool compact = memcmp(bytes,"BPT2",4) == 0;
    bool motion = memcmp(bytes,"BPH5",4) == 0;
    if ((!compact && !motion && memcmp(bytes,"BPT1",4)) || meta[0]<sizeof(header_t) || meta[0]>MAX_RAW ||
        meta[1] != size-16) return ESP_ERR_INVALID_RESPONSE;

    baji_pet_t *p = heap_caps_calloc(1,sizeof(*p),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if (!p) return ESP_ERR_NO_MEM;
    p->decoded = heap_caps_malloc(meta[0],MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    tinfl_decompressor *decoder = heap_caps_malloc(sizeof(*decoder),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    esp_err_t err = ESP_ERR_NO_MEM;
    if (p->decoded && decoder) {
        size_t input=meta[1], output=meta[0];
        tinfl_init(decoder);
        tinfl_status result = tinfl_decompress(decoder,bytes+16,&input,p->decoded,p->decoded,
                                               &output,TINFL_FLAG_PARSE_ZLIB_HEADER |
                                               TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        err = ESP_ERR_INVALID_RESPONSE;
        if (result == TINFL_STATUS_DONE && input == meta[1] && output == meta[0] &&
            esp_rom_crc32_le(0,p->decoded,output)==meta[2]) {
            err = motion ? pet_motion_open(p,output) :
                  bind_tables(p,output,compact) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
        }
        if (err == ESP_OK && !motion) {
            p->selected = heap_caps_malloc(CELLS*sizeof(*p->selected),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
            if (!p->selected) err = ESP_ERR_NO_MEM;
        }
        if (err == ESP_OK) {
            p->pose = UINT_MAX;
            baji_pet_select(p,(baji_pet_state_t){0});
            *out=p;
            err=ESP_OK;
        }
    }
    heap_caps_free(decoder);
    if (err != ESP_OK) baji_pet_close(p);
    return err;
}

esp_err_t baji_pet_open(baji_pet_t **out)
{
#ifdef BAJI_PET_EXTERNAL_ASSET
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    return ESP_ERR_INVALID_STATE;
#else
    extern const uint8_t start[] asm("_binary_motion_bpz_start");
    extern const uint8_t end[] asm("_binary_motion_bpz_end");
    extern const uint8_t reactions[] asm("_binary_reactions_bpr_start");
    extern const uint8_t reactions_end[] asm("_binary_reactions_bpr_end");
    esp_err_t err=baji_pet_open_data(out,start,(size_t)(end-start));
    if (err!=ESP_OK) return err;
    err=baji_pet_attach_reactions(*out,reactions,(size_t)(reactions_end-reactions));
    if (err==ESP_OK)
        ESP_LOGI("baji_pet","reactions.bpr active: left cheek, right cheek and tickle");
    else
        ESP_LOGW("baji_pet","reactions.bpr rejected (%d), using idle and pat only",(int)err);
    return ESP_OK;
#endif
}

void baji_pet_close(baji_pet_t *p)
{
    if (p) {
        heap_caps_free(p->selected);
        pet_motion_close(p->motion);
        heap_caps_free(p->decoded);
        heap_caps_free(p);
    }
}
