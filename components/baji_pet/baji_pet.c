#include "baji_pet_private.h"

#include <stdlib.h>
#include <string.h>

unsigned baji_pet_frame_count(baji_pet_clip_t clip)
{
    return clip == BAJI_PET_PAT ? PAT_FRAMES : IDLE_FRAMES;
}

unsigned baji_pet_fps(baji_pet_clip_t clip)
{
    return clip == BAJI_PET_PAT ? 16 : 12;
}

unsigned baji_pet_substeps(const baji_pet_t *p, baji_pet_clip_t clip)
{
    return p && p->motion ? (clip == BAJI_PET_PAT ? 3 : 4) : 1;
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

static range_t gaze_patch(const baji_pet_t *p,unsigned pose,unsigned gaze)
{
    return p->gazes[pose*GAZES+gaze];
}

void baji_pet_select(baji_pet_t *p, baji_pet_state_t state)
{
    unsigned pose = pose_id(state), gaze = gaze_id(state);
    if (p->motion) {
        pet_motion_select(p->motion,state,gaze);
        return;
    }
    const uint16_t *neutral = p->maps+pose*CELLS;
    if (pose != p->pose) {
        memcpy(p->selected,neutral,CELLS*sizeof(*p->selected));
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
    const int16_t *h = p->motion ? pet_motion_head(p->motion,state) : p->poses[pose_id(state)].forehead;
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

void pet_add_rect(baji_rect_t *areas,size_t *count,size_t capacity,baji_rect_t r)
{
    damage_t d={.areas=areas,.count=*count,.capacity=capacity};
    add(&d,r);
    *count=d.count;
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
    if (p->motion)
        return pet_motion_damage(p->motion,*before,old_gaze,*after,new_gaze,regions,capacity);
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

/* Specialize once per band: both hot loops do one index read per pixel row
 * and the same 16-byte copy. The high tile bit selects its BPT2 row bank. */
static inline __attribute__((always_inline)) void paint_band(const baji_rect_t *band, uint8_t *out,
                                                             const baji_pet_t *p, bool compact)
{
    unsigned end_x=band->x+band->width,end_y=band->y+band->height;
    for (unsigned y=band->y;y<end_y;y++) {
        const uint16_t *map=p->selected+(y/TILE)*COLS;
        for (unsigned x=band->x;x<end_x;) {
            unsigned offset=x%TILE,n=TILE-offset;
            if (n>end_x-x) n=end_x-x;
            unsigned tile = map[x/TILE];
            unsigned row_index = tile*TILE + y%TILE;
            const uint8_t *src;
            if (compact)
                src = p->pixel_bank[tile>>15] + p->rows16[row_index]*(2*TILE);
            else
                src = p->pixel_bank[0] + p->rows32[row_index]*(2*TILE);
            if (n==TILE) memcpy(out,src,2*TILE);
            else memcpy(out,src+2*offset,n*2);
            out+=2*n;
            x+=n;
        }
    }
}

void baji_pet_paint(const baji_rect_t *band,uint8_t *out,void *context)
{
    const baji_pet_t *p = context;
    if (p->motion) pet_motion_paint(p->motion,band,out);
    else if (p->rows16) paint_band(band, out, p, true);
    else paint_band(band, out, p, false);
}
