#include "baji_pet_clips.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "miniz.h"
#else
#include <zlib.h>
#endif

enum { GRID = 184, SIDE = 466, CELLS = GRID * GRID };
typedef struct {
    char magic[4];
    uint32_t size, crc, clips, frames, colors, body, reserved;
} header_t;
typedef struct {
    char name[24], scene[16];
    uint32_t first;
    uint16_t count;
    uint8_t fps, reserved;
} clip_rec_t;
typedef struct {
    uint32_t offset, size, mask, crc;
    uint8_t box[4];
    int16_t anchor[10], palms[4], prop[4];
    uint8_t pillow[4], kind, flags;
    uint16_t scale;
} frame_t;
typedef struct { uint16_t mesh, flat; } ink_t;
_Static_assert(sizeof(header_t)==32 && sizeof(clip_rec_t)==48 && sizeof(frame_t)==64, "BPC1 layout");

struct pet_clip { struct pet_clips *bank; const clip_rec_t *rec; };
struct pet_clips {
    header_t h;
    const clip_rec_t *records;
    const frame_t *frames;
    const ink_t *inks;
    const uint8_t *payload;
    pet_clip_t *clips;
    uint16_t *grid;
    uint8_t *decoded;
    unsigned current;
    uint16_t sample[SIDE];
    uint16_t edge[GRID+1];
#ifdef ESP_PLATFORM
    const esp_partition_t *partition;
    void *tables;
    uint8_t *compressed;
    uint8_t *mask_cache;
    const frame_t *cached_pillow;
    tinfl_decompressor *inflater;
#endif
};

static void *allocate(size_t n)
{
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1,n,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(1,n);
#endif
}

static uint32_t checksum(uint32_t crc,const void *data,size_t length)
{
#ifdef ESP_PLATFORM
    return esp_rom_crc32_le(crc,data,length);
#else
    return crc32(crc,data,length);
#endif
}

static bool valid_header(const header_t *h,size_t limit)
{
    return !memcmp(h->magic,"BPC1",4) && h->size<=limit && h->size>=h->body &&
        h->clips && h->clips<=512 && h->frames && h->frames<=32768 && h->colors && h->colors<=65536 &&
        h->body==sizeof(*h)+h->clips*sizeof(clip_rec_t)+h->frames*sizeof(frame_t)+h->colors*sizeof(ink_t);
}

static esp_err_t bind_tables(pet_clips_t *b,const void *tables)
{
    b->records=tables;
    b->frames=(const frame_t *)(b->records+b->h.clips);
    b->inks=(const ink_t *)(b->frames+b->h.frames);
    b->clips=allocate(b->h.clips*sizeof(*b->clips));
    b->grid=allocate(CELLS*sizeof(*b->grid));
    b->decoded=allocate(CELLS*2);
    if (!b->clips || !b->grid || !b->decoded) return ESP_ERR_NO_MEM;
    for (unsigned i=0;i<b->h.clips;i++) {
        const clip_rec_t *r=b->records+i;
        if (!memchr(r->name,0,sizeof(r->name)) || !r->name[0] || !memchr(r->scene,0,sizeof(r->scene)) ||
            !r->fps || !r->count || r->first>=b->h.frames || r->count>b->h.frames-r->first ||
            !(b->frames[r->first].flags&1)) return ESP_ERR_INVALID_RESPONSE;
        b->clips[i]=(pet_clip_t){b,r};
    }
    unsigned body=b->h.size-b->h.body, largest=1, max_mask=1;
    for (unsigned i=0;i<b->h.frames;i++) {
        const frame_t *f=b->frames+i;
        unsigned masks=((unsigned)f->pillow[2]*f->pillow[3]+7)/8;
        if (f->offset>body || body-f->offset<f->size || f->mask>body || body-f->mask<masks ||
            f->box[0]+f->box[2]>GRID || f->box[1]+f->box[3]>GRID ||
            f->pillow[0]+f->pillow[2]>GRID || f->pillow[1]+f->pillow[3]>GRID ||
            (f->size!=0)!=((unsigned)f->box[2]*f->box[3]!=0) ||
            f->kind>PET_HIT_PALM || f->flags>3 || !f->scale || f->scale>4096)
            return ESP_ERR_INVALID_RESPONSE;
        if (f->size>largest) largest=f->size;
        if (masks>max_mask) max_mask=masks;
    }
#ifdef ESP_PLATFORM
    b->inflater=allocate(sizeof(*b->inflater));
    if (!b->payload) {
        b->compressed=allocate(largest);
        b->mask_cache=allocate(max_mask);
    }
    if (!b->inflater || (!b->payload && (!b->compressed || !b->mask_cache))) return ESP_ERR_NO_MEM;
#else
    (void)largest; (void)max_mask;
#endif
    unsigned native=0;
    for (unsigned i=0;i<SIDE;i++) {
        b->sample[i]=((2*i+1)*2*GRID-1)/(2*SIDE);
        if ((b->sample[i]>>1)==native) b->edge[native++]=i;
    }
    b->edge[GRID]=SIDE;
    b->current=UINT_MAX;
    return ESP_OK;
}

esp_err_t pet_clips_open_data(pet_clips_t **out,const void *data,size_t size)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out=NULL;
    if (!data || ((uintptr_t)data&3) || size<sizeof(header_t)) return ESP_ERR_INVALID_ARG;
    const header_t *h=data;
    const uint8_t *tables=(const uint8_t*)data+sizeof(*h);
    if (!valid_header(h,size) || h->size!=size || checksum(0,tables,size-sizeof(*h))!=h->crc)
        return ESP_ERR_INVALID_RESPONSE;
    pet_clips_t *b=allocate(sizeof(*b));
    if (!b) return ESP_ERR_NO_MEM;
    b->h=*h;
    b->payload=(const uint8_t*)data+h->body;
    esp_err_t err=bind_tables(b,tables);
    if (err!=ESP_OK) pet_clips_close(b); else *out=b;
    return err;
}

esp_err_t pet_clips_open(pet_clips_t **out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out=NULL;
#ifdef ESP_PLATFORM
    const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"hdmotion");
    if (!p) return ESP_ERR_NOT_FOUND;
    header_t h;
    esp_err_t err=esp_partition_read(p,0,&h,sizeof(h));
    if (err!=ESP_OK) return err;
    if (!valid_header(&h,p->size)) return ESP_ERR_INVALID_RESPONSE;
    pet_clips_t *b=allocate(sizeof(*b));
    if (!b) return ESP_ERR_NO_MEM;
    b->h=h;
    b->partition=p;
    /* The 16 MiB pack need not occupy a contiguous DROM/MMU mapping. Keep
     * only the directory, hit maps and one drawing's compressed stream in RAM. */
    b->tables=allocate(h.body-sizeof(h));
    uint8_t *chunk=allocate(16384);
    if (!b->tables || !chunk) err=ESP_ERR_NO_MEM;
    if (err==ESP_OK) err=esp_partition_read(p,sizeof(h),b->tables,h.body-sizeof(h));
    uint32_t crc=err==ESP_OK ? checksum(0,b->tables,h.body-sizeof(h)) : 0;
    for (size_t at=h.body;err==ESP_OK && at<h.size;) {
        size_t n=h.size-at<16384 ? h.size-at : 16384;
        err=esp_partition_read(p,at,chunk,n);
        if (err==ESP_OK) crc=checksum(crc,chunk,n);
        at+=n;
    }
    free(chunk);
    if (err==ESP_OK && h.crc!=crc) err=ESP_ERR_INVALID_CRC;
    if (err==ESP_OK) err=bind_tables(b,b->tables);
    if (err==ESP_OK) *out=b; else pet_clips_close(b);
    return err;
#else
    return ESP_ERR_NOT_FOUND;
#endif
}

void pet_clips_close(pet_clips_t *b)
{
    if (!b) return;
#ifdef ESP_PLATFORM
    free(b->tables); free(b->compressed); free(b->mask_cache); free(b->inflater);
#endif
    free(b->clips); free(b->grid); free(b->decoded); free(b);
}

const pet_clip_t *pet_clips_lookup(const pet_clips_t *b,const char *name,const char *scene)
{
    if (!b || !name) return NULL;
    const pet_clip_t *base=NULL;
    for (unsigned i=0;i<b->h.clips;i++) {
        const clip_rec_t *r=b->records+i;
        if (strcmp(name,r->name)) continue;
        if (!r->scene[0]) base=b->clips+i;
        if (scene && !strcmp(scene,r->scene)) return b->clips+i;
        if ((!scene || !scene[0]) && base) return base;
    }
    return base;
}

unsigned pet_clip_frames(const pet_clip_t *c) { return c ? c->rec->count : 0; }
unsigned pet_clip_fps(const pet_clip_t *c) { return c ? c->rec->fps : 0; }
static const frame_t *get_frame(const pet_clip_t *c,unsigned n)
{
    return c && n<c->rec->count ? c->bank->frames+c->rec->first+n : NULL;
}

static esp_err_t decode(pet_clips_t *b,const frame_t *f)
{
    size_t cells=(size_t)f->box[2]*f->box[3], size=2*cells;
    if (!cells) return ESP_OK;
    const uint8_t *src=b->payload ? b->payload+f->offset : NULL;
#ifdef ESP_PLATFORM
    if (!src) {
        esp_err_t err=esp_partition_read(b->partition,b->h.body+f->offset,b->compressed,f->size);
        if (err!=ESP_OK) return err;
        src=b->compressed;
    }
    size_t input=f->size, output=size;
    tinfl_init(b->inflater);
    if (tinfl_decompress(b->inflater,src,&input,b->decoded,b->decoded,&output,
            TINFL_FLAG_PARSE_ZLIB_HEADER|TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF)!=TINFL_STATUS_DONE ||
            output!=size || input!=f->size) return ESP_ERR_INVALID_RESPONSE;
#else
    uLongf output=size;
    if (uncompress(b->decoded,&output,src,f->size)!=Z_OK || output!=size) return ESP_ERR_INVALID_RESPONSE;
#endif
    unsigned i=0;
    for (unsigned y=0;y<f->box[3];y++) {
        uint16_t *row=b->grid+(f->box[1]+y)*GRID+f->box[0];
        for (unsigned x=0;x<f->box[2];x++,i++) {
            uint16_t v=row[x]^b->decoded[i]^((uint16_t)b->decoded[cells+i]<<8);
            if (v>=b->h.colors) return ESP_ERR_INVALID_RESPONSE;
            row[x]=v;
        }
    }
    return ESP_OK;
}

esp_err_t pet_clips_select(pet_clips_t *b,const pet_clip_t *c,unsigned frame,baji_rect_t *damage)
{
    if (!b || !c || b!=c->bank || frame>=c->rec->count || !damage) return ESP_ERR_INVALID_ARG;
    *damage=(baji_rect_t){0};
    unsigned target=c->rec->first+frame, from=target;
    if (target==b->current) return ESP_OK;
    while (!(b->frames[from].flags&1) && (b->current==UINT_MAX || from!=b->current+1)) from--;
    bool full=from!=b->current+1 || b->current==UINT_MAX || (b->frames[from].flags&1);
    unsigned left=GRID,top=GRID,right=0,bottom=0;
    for (unsigned i=from;i<=target;i++) {
        const frame_t *f=b->frames+i;
        if (f->flags&1) memset(b->grid,0,CELLS*sizeof(*b->grid));
        esp_err_t err=decode(b,f);
        if (err!=ESP_OK) { b->current=UINT_MAX; return err; }
        if (!f->size) continue;
        if (f->box[0]<left) left=f->box[0];
        if (f->box[1]<top) top=f->box[1];
        if (f->box[0]+f->box[2]>right) right=f->box[0]+f->box[2];
        if (f->box[1]+f->box[3]>bottom) bottom=f->box[1]+f->box[3];
    }
    b->current=target;
    if (full) *damage=(baji_rect_t){0,0,SIDE,SIDE};
    else if (right>left && bottom>top) *damage=(baji_rect_t){b->edge[left],b->edge[top],
            b->edge[right]-b->edge[left],b->edge[bottom]-b->edge[top]};
    return ESP_OK;
}

void pet_clips_paint(const baji_rect_t *band,uint8_t *pixels,void *context)
{
    const pet_clips_t *b=context;
    for (unsigned y=band->y;y<band->y+band->height;y++) {
        unsigned sy=b->sample[y];
        const uint16_t *row=b->grid+(sy>>1)*GRID;
        for (unsigned x=band->x;x<band->x+band->width;x++) {
            unsigned sx=b->sample[x];
            const ink_t *ink=b->inks+row[sx>>1];
            uint16_t color=(sx&sy&1) ? ink->flat : ink->mesh;
            *pixels++=color>>8; *pixels++=(uint8_t)color;
        }
    }
}

void pet_clip_anchors(const pet_clip_t *c,unsigned n,int xy[10])
{
    const frame_t *f=get_frame(c,n);
    for (unsigned i=0;i<10;i++) xy[i]=f ? f->anchor[i] : 0;
}

bool pet_clip_palms(const pet_clip_t *c,unsigned n,int xy[4])
{
    const frame_t *f=get_frame(c,n);
    if (!f || !(f->flags&2)) return false;
    for (unsigned i=0;i<4;i++) xy[i]=f->palms[i];
    return true;
}

bool pet_clip_prop(const pet_clip_t *c,unsigned n,int xy[2],int *radius)
{
    const frame_t *f=get_frame(c,n);
    if (!f || !f->kind) return false;
    xy[0]=f->prop[0]; xy[1]=f->prop[1];
    if (radius) *radius=f->prop[2];
    return true;
}

bool pet_clip_pillow(const pet_clip_t *c,unsigned n,baji_rect_t *bounds)
{
    const frame_t *f=get_frame(c,n);
    if (!f || !f->pillow[2]) return false;
    const uint8_t *r=f->pillow;
    const uint16_t *edge=c->bank->edge;
    *bounds=(baji_rect_t){edge[r[0]],edge[r[1]],edge[r[0]+r[2]]-edge[r[0]],edge[r[1]+r[3]]-edge[r[1]]};
    return true;
}

bool pet_clip_pillow_contains(const pet_clips_t *b,const pet_clip_t *c,unsigned n,int x,int y)
{
    const frame_t *f=get_frame(c,n);
    if (!f || b!=c->bank || (unsigned)x>=SIDE || (unsigned)y>=SIDE || !f->pillow[2]) return false;
    const uint8_t *r=f->pillow;
    int col=(b->sample[x]>>1)-r[0], row=(b->sample[y]>>1)-r[1];
    if ((unsigned)col>=r[2] || (unsigned)row>=r[3]) return false;
    unsigned bit=(unsigned)row*r[2]+col;
    const uint8_t *mask=b->payload ? b->payload+f->mask : NULL;
#ifdef ESP_PLATFORM
    if (!mask) {
        pet_clips_t *cache=c->bank;
        if (cache->cached_pillow!=f) {
            cache->cached_pillow=NULL;
            if (esp_partition_read(b->partition,b->h.body+f->mask,cache->mask_cache,
                                   ((size_t)r[2]*r[3]+7)/8)!=ESP_OK) return false;
            cache->cached_pillow=f;
        }
        mask=cache->mask_cache;
    }
#endif
    return (mask[bit>>3] & (1u<<(bit%8)))!=0;
}

static bool oval(int x,int y,int cx,int cy,int rx,int ry)
{
    int dx=x-cx, dy=y-cy;
    if (rx<=0 || ry<=0 || abs(dx)>rx || abs(dy)>ry) return false;
    int64_t a=(int64_t)dx*ry, b=(int64_t)dy*rx, r=(int64_t)rx*ry;
    return a*a+b*b<=r*r;
}

pet_hit_t pet_clip_hit(const pet_clips_t *b,const pet_clip_t *c,unsigned n,int x,int y,int *side)
{
    const frame_t *f=get_frame(c,n);
    if (side) *side=0;
    if (!f || b!=c->bank || (unsigned)x>=SIDE || (unsigned)y>=SIDE) return PET_HIT_NONE;
    const int16_t *a=f->anchor;
    int s=x<a[2] ? -1 : 1;
    if (side) *side=s;
    if (pet_clip_pillow_contains(b,c,n,x,y)) return PET_HIT_PILLOW;
    if (f->kind && oval(x,y,f->prop[0],f->prop[1],f->prop[2],f->prop[3])) return (pet_hit_t)f->kind;
    int dx=a[2]-a[0], dy=a[3]-a[1];
    float face=sqrtf((float)dx*dx+(float)dy*dy)/132.f;
    float scale=f->scale/256.f;
    if (oval(x,y,a[2],a[3],lroundf(19*scale),lroundf(17*scale))) return PET_HIT_NOSE;
    if (oval(x,y,a[2]+s*lroundf(dy*.52f),a[3]-s*lroundf(dx*.52f),
             lroundf(39*face),lroundf(32*face))) return PET_HIT_CHEEK;
    if (oval(x,y,a[4],a[5],lroundf(40*scale),lroundf(30*scale))) return PET_HIT_WING_LEFT;
    if (oval(x,y,a[6],a[7],lroundf(40*scale),lroundf(30*scale))) return PET_HIT_WING_RIGHT;
    if (oval(x,y,a[8],a[9],lroundf(38*scale),lroundf(49*scale))) return PET_HIT_TAIL;
    if (oval(x,y,a[0],a[1],lroundf(91*face/.75f),lroundf(75*face/.75f)) ||
        oval(x,y,a[2],a[3],lroundf(63*scale),lroundf(40*scale))) {
        if (side) *side=x<a[0] ? -1 : 1;
        return PET_HIT_HEAD;
    }
    const char *name=c->rec->name;
    if ((!strcmp(name,"doze") || !strncmp(name,"peek",4) || !strncmp(name,"wake",4)) &&
        oval(x,y,(a[4]+a[6])/2,(a[5]+a[7])/2+lroundf(11*scale),lroundf(54*scale),lroundf(61*scale)))
        return PET_HIT_BODY;
    return PET_HIT_NONE;
}
