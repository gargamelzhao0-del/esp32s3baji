#include "baji_pet.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "miniz.h"

enum { SIDE = 466, TILE = 8, COLS = (SIDE+TILE-1)/TILE, CELLS = COLS*COLS,
       IDLE_FRAMES = 43, PAT_FRAMES = 30, POSES = IDLE_FRAMES+PAT_FRAMES,
       GAZES = 15, CENTER = 7, MAX_RAW = 6*1024*1024 };
typedef struct { uint32_t first; uint16_t count, reserved; } range_t;
typedef struct { uint16_t cell, tile; } patch_t;
typedef struct {
    int16_t forehead[4];
    baji_rect_t eyes[2];
    range_t changes[3]; /* from previous pose, to idle zero, to pat zero */
} pose_t;
typedef struct {
    uint32_t tiles, rows, patches, rectangles;
    uint16_t frames[2], fps[2];
} header_t;
_Static_assert(sizeof(header_t)==24 && sizeof(pose_t)==48 && sizeof(range_t)==8 &&
               sizeof(baji_rect_t)==8 && sizeof(patch_t)==4,"Asset layout");

struct baji_pet {
    uint8_t *decoded;
    const header_t *header;
    const pose_t *poses;
    const uint16_t *maps;
    const range_t *gazes;
    const patch_t *patches;
    const baji_rect_t *rectangles;
    const uint32_t *tiles;
    const uint8_t *rows;
    unsigned pose, gaze;
    uint16_t selected[CELLS];
};

unsigned baji_pet_frame_count(baji_pet_clip_t clip)
{
    return clip == BAJI_PET_PAT ? PAT_FRAMES : IDLE_FRAMES;
}

unsigned baji_pet_fps(baji_pet_clip_t clip)
{
    return clip == BAJI_PET_PAT ? 16 : 12;
}

static unsigned pose_id(baji_pet_state_t state)
{
    return (state.clip == BAJI_PET_PAT ? IDLE_FRAMES : 0) +
           state.frame % baji_pet_frame_count(state.clip);
}

static unsigned gaze_id(baji_pet_state_t state)
{
    int x = state.gaze_x, y = state.gaze_y;
    if (x < -2) x = -2;
    if (x > 2) x = 2;
    if (y < -1) y = -1;
    if (y > 1) y = 1;
    return (unsigned)((y+1)*5 + x+2);
}

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
static bool bind_tables(baji_pet_t *p, size_t length)
{
    const uint8_t *data = p->decoded;
    if (length < sizeof(header_t)) return false;
    const header_t *h = p->header = (const header_t *)data;
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
    TABLE(tiles,uint32_t,h->tiles*TILE);
    if (h->rows > length/(TILE*2)) return false;
    TABLE(rows,uint8_t,h->rows*TILE*2);
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
    for (unsigned i=0;i<h->tiles*TILE;i++) if (p->tiles[i] >= h->rows) return false;
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
    if (memcmp(bytes,"BPT1",4) || meta[0]<sizeof(header_t) || meta[0]>MAX_RAW ||
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
            esp_rom_crc32_le(0,p->decoded,output)==meta[2] && bind_tables(p,output)) {
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
    extern const uint8_t start[] asm("_binary_scene_bpz_start");
    extern const uint8_t end[] asm("_binary_scene_bpz_end");
    return baji_pet_open_data(out,start,(size_t)(end-start));
#endif
}

void baji_pet_close(baji_pet_t *p)
{
    if (p) {
        heap_caps_free(p->decoded);
        heap_caps_free(p);
    }
}

static range_t gaze_patch(const baji_pet_t *p,unsigned pose,unsigned gaze)
{
    return p->gazes[pose*GAZES+gaze];
}

void baji_pet_select(baji_pet_t *p, baji_pet_state_t state)
{
    unsigned pose = pose_id(state), gaze = gaze_id(state);
    const uint16_t *neutral = p->maps+pose*CELLS;
    if (pose != p->pose) {
        memcpy(p->selected,neutral,sizeof(p->selected));
    } else {
        if (gaze == p->gaze) return;
        range_t erase = gaze_patch(p,pose,p->gaze);
        for (unsigned i=0;i<erase.count;i++) {
            unsigned cell = p->patches[erase.first+i].cell;
            p->selected[cell] = neutral[cell];
        }
    }
    range_t apply = gaze_patch(p,pose,gaze);
    for (unsigned i=0;i<apply.count;i++) {
        patch_t patch = p->patches[apply.first+i];
        p->selected[patch.cell] = patch.tile;
    }
    p->pose = pose;
    p->gaze = gaze;
}

bool baji_pet_forehead(const baji_pet_t *p,baji_pet_state_t state,uint16_t x,uint16_t y)
{
    if (!p || x >= SIDE || y >= SIDE) return false;
    const int16_t *h = p->poses[pose_id(state)].forehead;
    int dx=x-h[0],dy=y-h[1],rx=h[2],ry=h[3];
    if (abs(dx)>rx || abs(dy)>ry) return false;
    int64_t a=(int64_t)dx*ry,b=(int64_t)dy*rx,r=(int64_t)rx*ry;
    return a*a+b*b <= r*r;
}

static baji_rect_t joined(baji_rect_t a,baji_rect_t b)
{
    unsigned left = a.x < b.x ? a.x : b.x, top = a.y < b.y ? a.y : b.y;
    unsigned right = a.x+a.width > b.x+b.width ? a.x+a.width : b.x+b.width;
    unsigned end = a.y+a.height > b.y+b.height ? a.y+a.height : b.y+b.height;
    return (baji_rect_t){left,top,right-left,end-top};
}

typedef struct { baji_rect_t *areas; size_t count, capacity; } damage_t;
static void add(damage_t *d,baji_rect_t r)
{
    if (!r.width || !r.height) return;
    for (size_t i=0;i<d->count;i++) {
        baji_rect_t merged=joined(d->areas[i],r);
        if (merged.width*merged.height <= d->areas[i].width*d->areas[i].height + r.width*r.height) {
            d->areas[i]=merged;
            return;
        }
    }
    if (d->count < d->capacity) d->areas[d->count++]=r;
    else d->areas[d->count-1]=joined(d->areas[d->count-1],r);
}

/* A user can interrupt either clip at any pose. The usual entrance and return
 * have offline plans. For other jumps, compare tile IDs in 32-pixel strips. */
static void skipped(const baji_pet_t *p,unsigned old,unsigned next,damage_t *d)
{
    const uint16_t *a=p->maps+old*CELLS,*b=p->maps+next*CELLS;
    for (unsigned top=0;top<COLS;top+=4) {
        unsigned end=top+4<COLS?top+4:COLS,left=COLS,right=0;
        for (unsigned y=top;y<end;y++)
            for (unsigned x=0;x<COLS;x++) {
                if (a[y*COLS+x] == b[y*COLS+x]) continue;
                if (x<left) left=x;
                if (x+1>right) right=x+1;
            }
        if (left<right) {
            unsigned x1=right*TILE<SIDE?right*TILE:SIDE;
            unsigned y1=end*TILE<SIDE?end*TILE:SIDE;
            add(d,(baji_rect_t){left*TILE,top*TILE,x1-left*TILE,y1-top*TILE});
        }
    }
}

size_t baji_pet_damage(baji_pet_t *p,const baji_pet_state_t *before,const baji_pet_state_t *after,
                       baji_rect_t *regions,size_t capacity)
{
    if (!p || !after || !regions || !capacity) return 0;
    if (!before) {regions[0]=(baji_rect_t){0,0,SIDE,SIDE};return 1;}
    unsigned old=pose_id(*before), next=pose_id(*after);
    unsigned old_gaze=gaze_id(*before), new_gaze=gaze_id(*after);
    if (old==next && old_gaze==new_gaze) return 0;
    damage_t d={.areas=regions,.capacity=capacity};
    if (old!=next) {
        const range_t *plan=NULL;
        if (next==0 || next==IDLE_FRAMES) plan=&p->poses[old].changes[next==0?1:2];
        else if (next==old+1) plan=&p->poses[next].changes[0];
        if (plan) {
            for (unsigned i=0;i<plan->count;i++) add(&d,p->rectangles[plan->first+i]);
        } else skipped(p,old,next,&d);
    }
    if (gaze_patch(p,old,old_gaze).count)
        for (unsigned eye=0;eye<2;eye++) add(&d,p->poses[old].eyes[eye]);
    if (gaze_patch(p,next,new_gaze).count)
        for (unsigned eye=0;eye<2;eye++) add(&d,p->poses[next].eyes[eye]);
    return d.count;
}

void baji_pet_paint(const baji_rect_t *band,uint8_t *out,void *context)
{
    const baji_pet_t *p=context;
    unsigned end_x=band->x+band->width,end_y=band->y+band->height;
    for (unsigned y=band->y;y<end_y;y++) {
        const uint16_t *map=p->selected+(y/TILE)*COLS;
        const uint32_t *rows=p->tiles+(y%TILE);
        for (unsigned x=band->x;x<end_x;) {
            unsigned offset=x%TILE,n=TILE-offset;
            if (n>end_x-x) n=end_x-x;
            const uint8_t *src=p->rows+rows[map[x/TILE]*TILE]*(2*TILE);
            if (n==TILE) memcpy(out,src,2*TILE);
            else memcpy(out,src+2*offset,n*2);
            out+=2*n;
            x+=n;
        }
    }
}
