#include "baji_pet_private.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include <limits.h>
#include <string.h>

enum { GRID = 184, CELL_COUNT = GRID*GRID, IDLE_SAMPLES = IDLE_FRAMES*4,
       SAMPLES = IDLE_SAMPLES + PAT_FRAMES*3 };

typedef struct { uint16_t cell, quad; } cell_patch_t;
typedef struct { uint16_t start; uint8_t length, first; } axis_t;
typedef struct { uint16_t pixel[2][2]; } quad_t;

enum { CHEEK_SAMPLES=22*3, TICKLE_SAMPLES=30*3, REACTIONS=2*CHEEK_SAMPLES+TICKLE_SAMPLES,
       KEY_INTERVAL=12, CHEEK_KEYS=(CHEEK_SAMPLES+KEY_INTERVAL-1)/KEY_INTERVAL,
       REACTION_KEYS=2*CHEEK_KEYS+(TICKLE_SAMPLES+KEY_INTERVAL-1)/KEY_INTERVAL };
typedef struct { uint32_t first; uint16_t count, windows; uint32_t window; } reaction_pose_t;
typedef struct {
    const int16_t (*faces)[12];
    const reaction_pose_t *poses;
    const quad_t *palette;
    const uint16_t *keys;
    const cell_patch_t *changes;
    const baji_rect_t *windows;
} reactions_t;
_Static_assert(sizeof(reaction_pose_t)==12, "Reaction pose layout");

struct motion_art {
    const int16_t (*heads)[4];
    const uint16_t *key_ids, *keys;
    const range_t *deltas, *gazes;
    const range_t (*plans)[3];
    const baji_rect_t (*eyes)[2], *rectangles;
    const cell_patch_t *body, *eye;
    const range_t (*transitions)[2];
    const uint8_t *masks;
    reactions_t reactions;
    unsigned shown_reaction;
    const quad_t *active_palette;
    quad_t *palette;
    uint16_t *neutral, *scratch, *screen, *visible;
    uint16_t physical_row[SIDE];
    uint32_t pending[GRID][(GRID+31)/32];
    axis_t axis[GRID];
    unsigned shown, gaze;
};

static bool ranges_fit(const range_t *ranges,unsigned size,unsigned limit)
{
    for (unsigned i=0;i<size;i++)
        if (ranges[i].first>limit || ranges[i].count>limit-ranges[i].first) return false;
    return true;
}

static bool checked_rect(baji_rect_t r)
{
    return !((r.x|r.y|r.width|r.height)&1) && r.x+r.width<=SIDE && r.y+r.height<=SIDE;
}

static void *external(size_t size)
{
    return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}

esp_err_t pet_motion_open(baji_pet_t *p,size_t length)
{
    uint32_t n[5];
    if (length<sizeof(n)) return ESP_ERR_INVALID_RESPONSE;
    memcpy(n,p->decoded,sizeof(n));
    unsigned quads=n[0], changes=n[1], eyes=n[2], rects=n[3], keys=n[4];
    uint64_t expected=sizeof(n)+(uint64_t)SAMPLES*(8+2+8+GAZES*8+3*8+16)+
                      (uint64_t)quads*8+(uint64_t)keys*CELL_COUNT*2+
                      (uint64_t)changes*4+(uint64_t)eyes*4+(uint64_t)rects*8;
    expected+=(uint64_t)SAMPLES*2*sizeof(range_t);
    if (expected>length || quads==0 || quads>65536 || keys==0 || keys>SAMPLES)
        return ESP_ERR_INVALID_RESPONSE;
    motion_art_t *m=heap_caps_calloc(1,sizeof(*m),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if (!m) return ESP_ERR_NO_MEM;
    p->motion=m;
    m->heads=(const int16_t (*)[4])(p->decoded+sizeof(n));
    m->key_ids=(const uint16_t *)(m->heads+SAMPLES);
    m->deltas=(const range_t *)(m->key_ids+SAMPLES);
    m->gazes=m->deltas+SAMPLES;
    m->plans=(const range_t (*)[3])(m->gazes+SAMPLES*GAZES);
    m->eyes=(const baji_rect_t (*)[2])(m->plans+SAMPLES);
    const quad_t *palette=(const quad_t *)(m->eyes+SAMPLES);
    m->keys=(const uint16_t *)(palette+quads);
    m->body=(const cell_patch_t *)(m->keys+keys*CELL_COUNT);
    m->eye=m->body+changes;
    m->rectangles=(const baji_rect_t *)(m->eye+eyes);
    m->transitions=(const range_t (*)[2])(m->rectangles+rects);
    m->masks=(const uint8_t *)(m->transitions+SAMPLES);
    unsigned bytes=length-(size_t)expected;
    if (!ranges_fit(&m->transitions[0][0],SAMPLES*2,bytes)) return ESP_ERR_INVALID_RESPONSE;
    for (unsigned i=0;i<SAMPLES;i++) {
        for (unsigned target=0;target<2;target++) {
            range_t range=m->transitions[i][target];
            const uint8_t *src=m->masks+range.first;
            unsigned previous=0;
            while (range.count) {
                if (range.count<4) return ESP_ERR_INVALID_RESPONSE;
                unsigned start=src[0]|((unsigned)src[1]<<8), n=src[2]|((unsigned)src[3]<<8);
                if (!n || n+4>range.count || start<previous || start+n>CELL_COUNT/8)
                    return ESP_ERR_INVALID_RESPONSE;
                previous=start+n;
                src+=4+n;
                range.count-=4+n;
            }
        }
    }
    if (!ranges_fit(m->deltas,SAMPLES,changes) || !ranges_fit(m->gazes,SAMPLES*GAZES,eyes) ||
        !ranges_fit(&m->plans[0][0],SAMPLES*3,rects)) return ESP_ERR_INVALID_RESPONSE;
    for (unsigned i=0;i<SAMPLES;i++) {
        if ((m->key_ids[i]!=UINT16_MAX && m->key_ids[i]>=keys) ||
            ((i==0 || i==IDLE_SAMPLES) && m->key_ids[i]==UINT16_MAX) ||
            m->heads[i][2]<=0 || m->heads[i][3]<=0 ||
            !checked_rect(m->eyes[i][0]) || !checked_rect(m->eyes[i][1])) return ESP_ERR_INVALID_RESPONSE;
    }
    for (unsigned i=0;i<keys*CELL_COUNT;i++) if (m->keys[i]>=quads) return ESP_ERR_INVALID_RESPONSE;
    for (unsigned i=0;i<changes+eyes;i++)
        if (m->body[i].cell>=CELL_COUNT || m->body[i].quad>=quads) return ESP_ERR_INVALID_RESPONSE;
    for (unsigned i=0;i<rects;i++) if (!checked_rect(m->rectangles[i])) return ESP_ERR_INVALID_RESPONSE;
    m->palette=heap_caps_malloc(quads*sizeof(*palette),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    m->screen=external(SIDE*SIDE*2);
    m->neutral=external(CELL_COUNT*2);
    m->scratch=external(CELL_COUNT*2);
    m->visible=external(CELL_COUNT*2);
    if (!m->palette || !m->screen || !m->neutral || !m->scratch || !m->visible) return ESP_ERR_NO_MEM;
    memcpy(m->palette,palette,quads*sizeof(*palette));
    for (unsigned i=0;i<SIDE;i++) {
        unsigned src=((2*i+1)*GRID*2-1)/(2*SIDE);
        m->physical_row[i]=src/2;
        axis_t *a=&m->axis[src/2];
        if (!a->length) a->start=i;
        a->length++;
        if (!(src&1)) a->first++;
    }
    m->shown=UINT_MAX;
    m->shown_reaction=UINT_MAX;
    m->active_palette=m->palette;
    return ESP_OK;
}

esp_err_t baji_pet_attach_reactions(baji_pet_t *p,const void *data,size_t size)
{
    if (!p || !p->motion || !data || ((uintptr_t)data&3) || size<20) return ESP_ERR_INVALID_ARG;
    if (p->motion->reactions.faces) return ESP_ERR_INVALID_STATE;
    const uint8_t *bytes=data;
    uint32_t n[3], crc;
    memcpy(n,bytes+4,sizeof(n));
    memcpy(&crc,bytes+size-4,4);
    unsigned quads=n[0],changes=n[1],windows=n[2];
    uint64_t expected=20u+(SAMPLES+REACTIONS)*24u+REACTIONS*12u+(uint64_t)quads*8+
                      REACTION_KEYS*CELL_COUNT*2u+(uint64_t)changes*4+(uint64_t)windows*8;
    if (memcmp(bytes,p->full_body_panel ? "BPR2" : "BPR1",4) || !quads || quads>65536 || expected!=size ||
        esp_rom_crc32_le(0,bytes,size-4)!=crc) return ESP_ERR_INVALID_RESPONSE;
    reactions_t r={.faces=(const int16_t (*)[12])(bytes+16)};
    r.poses=(const reaction_pose_t *)(r.faces+SAMPLES+REACTIONS);
    r.palette=(const quad_t *)(r.poses+REACTIONS);
    r.keys=(const uint16_t *)(r.palette+quads);
    r.changes=(const cell_patch_t *)(r.keys+REACTION_KEYS*CELL_COUNT);
    r.windows=(const baji_rect_t *)(r.changes+changes);
    for (unsigned i=0;i<SAMPLES+REACTIONS;i++)
        for (unsigned j=8;j<12;j++) if (r.faces[i][j]<=0 || r.faces[i][j]>SIDE) return ESP_ERR_INVALID_RESPONSE;
    for (unsigned i=0;i<REACTIONS;i++) {
        reaction_pose_t pose=r.poses[i];
        if (pose.first>changes || pose.count>changes-pose.first ||
            pose.window>windows || pose.windows>windows-pose.window) return ESP_ERR_INVALID_RESPONSE;
    }
    for (unsigned i=0;i<REACTION_KEYS*CELL_COUNT;i++) if (r.keys[i]>=quads) return ESP_ERR_INVALID_RESPONSE;
    for (unsigned i=0;i<changes;i++)
        if (r.changes[i].cell>=CELL_COUNT || r.changes[i].quad>=quads) return ESP_ERR_INVALID_RESPONSE;
    for (unsigned i=0;i<windows;i++) if (!checked_rect(r.windows[i])) return ESP_ERR_INVALID_RESPONSE;
    p->motion->reactions=r;
    return ESP_OK;
}

void pet_motion_close(motion_art_t *m)
{
    if (!m) return;
    heap_caps_free(m->screen);
    heap_caps_free(m->visible);
    heap_caps_free(m->scratch);
    heap_caps_free(m->neutral);
    heap_caps_free(m->palette);
    heap_caps_free(m);
}

static unsigned sample(baji_pet_state_t s)
{
    return s.clip==BAJI_PET_PAT ? IDLE_SAMPLES+s.frame%PAT_FRAMES*3+s.phase%3 :
                                  s.frame%IDLE_FRAMES*4+s.phase%4;
}

static unsigned reaction_sample(const motion_art_t *m,baji_pet_state_t s)
{
    if (!m->reactions.faces || s.clip<BAJI_PET_CHEEK_LEFT || s.clip>BAJI_PET_TICKLE) return UINT_MAX;
    return (s.clip-BAJI_PET_CHEEK_LEFT)*CHEEK_SAMPLES+
           s.frame%baji_pet_frame_count(s.clip)*3+s.phase%3;
}

const int16_t *pet_motion_landmarks(const motion_art_t *m,baji_pet_state_t s)
{
    if (!m->reactions.faces) return NULL;
    unsigned id=reaction_sample(m,s);
    return m->reactions.faces[id==UINT_MAX ? sample(s) : SAMPLES+id];
}

const int16_t *pet_motion_head(const motion_art_t *m,baji_pet_state_t s)
{
    return m->heads[sample(s)];
}

/* Stage little coordinate/quad records on selection. Each display stripe
 * expands its native rows while the previous stripe is being transmitted. */
static inline __attribute__((always_inline)) void stage_cell(motion_art_t *m,unsigned cell,unsigned quad)
{
    unsigned x=cell%GRID, y=cell/GRID;
    m->visible[cell]=quad;
    m->pending[y][x/32] |= 1u<<(x%32);
}

static void expand_row(motion_art_t *m,unsigned y)
{
    axis_t dest_y=m->axis[y];
    uint16_t *base=m->screen+dest_y.start*SIDE;
    const uint16_t *visible=m->visible+y*GRID;
    for (unsigned group=0;group<(GRID+31)/32;group++) {
        uint32_t bits=m->pending[y][group];
        m->pending[y][group]=0;
        while (bits) {
            unsigned col=group*32+(unsigned)__builtin_ctz(bits);
            bits&=bits-1;
            axis_t x=m->axis[col];
            const quad_t *q=m->active_palette+visible[col];
            uint16_t *line=base+x.start;
            for (unsigned dy=0;dy<dest_y.length;dy++,line+=SIDE) {
                const uint16_t *v=q->pixel[dy>=dest_y.first];
                line[0]=v[0];
                line[1]=v[x.first==1];
                if (x.length==3) line[2]=v[1];
            }
        }
    }
}

static void apply(motion_art_t *m,const cell_patch_t *patch,range_t range,uint16_t *neutral,bool paint)
{
    patch+=range.first;
    while (range.count--) {
        unsigned cell=patch->cell, quad=patch->quad;
        ++patch;
        if (neutral) neutral[cell]=quad;
        if (paint) stage_cell(m,cell,quad);
    }
}

static void enter_clip(motion_art_t *m,unsigned next)
{
    range_t range=m->transitions[m->shown][next==IDLE_SAMPLES];
    const uint8_t *src=m->masks+range.first, *end=src+range.count;
    const uint16_t *target=m->keys+m->key_ids[next]*CELL_COUNT;
    while (src<end) {
        unsigned cell=(src[0]|((unsigned)src[1]<<8))*8;
        unsigned count=src[2]|((unsigned)src[3]<<8);
        src+=4;
        for (unsigned i=0;i<count;i++,cell+=8) {
            unsigned bits=*src++;
            while (bits) {
                unsigned pos=cell+(unsigned)__builtin_ctz(bits);
                bits&=bits-1;
                unsigned quad=target[pos];
                m->neutral[pos]=quad;
                stage_cell(m,pos,quad);
            }
        }
    }
}

static unsigned reaction_start(unsigned id)
{
    return id>=2*CHEEK_SAMPLES ? 2*CHEEK_SAMPLES : id>=CHEEK_SAMPLES ? CHEEK_SAMPLES : 0;
}

static void select_reaction(motion_art_t *m,unsigned id)
{
    if (id==m->shown_reaction) return;
    const reactions_t *r=&m->reactions;
    unsigned start=reaction_start(id), from=m->shown_reaction;
    bool same_clip=from!=UINT_MAX && reaction_start(from)==start;
    if (!same_clip || id<from || id-from>KEY_INTERVAL) {
        unsigned distance=id-start;
        unsigned key=(start/CHEEK_SAMPLES)*CHEEK_KEYS+distance/KEY_INTERVAL;
        from=id-distance%KEY_INTERVAL;
        memcpy(m->scratch,r->keys+key*CELL_COUNT,CELL_COUNT*2);
        for (unsigned i=from+1;i<=id;i++) {
            reaction_pose_t pose=r->poses[i];
            apply(m,r->changes,(range_t){pose.first,pose.count,0},m->scratch,false);
        }
        for (unsigned i=0;i<CELL_COUNT;i++)
            if (m->shown_reaction==UINT_MAX || m->visible[i]!=m->scratch[i])
                stage_cell(m,i,m->scratch[i]);
    } else {
        for (unsigned i=from+1;i<=id;i++) {
            reaction_pose_t pose=r->poses[i];
            apply(m,r->changes,(range_t){pose.first,pose.count,0},NULL,true);
        }
    }
    m->shown=UINT_MAX;
    m->shown_reaction=id;
    m->active_palette=r->palette;
}

void pet_motion_select(motion_art_t *m,baji_pet_state_t s,unsigned gaze)
{
    unsigned reaction=reaction_sample(m,s);
    if (reaction!=UINT_MAX) { select_reaction(m,reaction); return; }
    m->shown_reaction=UINT_MAX;
    m->active_palette=m->palette;
    unsigned next=sample(s);
    if (next==m->shown && gaze==m->gaze) return;
    if (m->shown!=UINT_MAX) {
        range_t erase=m->gazes[m->shown*GAZES+m->gaze];
        for (unsigned i=0;i<erase.count;i++) {
            unsigned cell=m->eye[erase.first+i].cell;
            stage_cell(m,cell,m->neutral[cell]);
        }
    }
    if (next!=m->shown) {
        bool short_jump=m->shown!=UINT_MAX && next>m->shown && next-m->shown<=4 &&
                        (next<IDLE_SAMPLES || m->shown>=IDLE_SAMPLES);
        bool sequential=m->shown!=UINT_MAX &&
                        ((next==m->shown+1 && next!=IDLE_SAMPLES) ||
                         (next==0 && m->shown==IDLE_SAMPLES-1));
        if (sequential) {
            apply(m,m->body,m->deltas[next],m->neutral,true);
        } else if (m->shown!=UINT_MAX && (next==0 || next==IDLE_SAMPLES)) {
            enter_clip(m,next);
        } else if (short_jump) {
            for (unsigned i=m->shown+1;i<=next;i++) apply(m,m->body,m->deltas[i],m->neutral,true);
        } else {
            unsigned key=next;
            while (m->key_ids[key]==UINT16_MAX) --key;
            memcpy(m->scratch,m->keys+m->key_ids[key]*CELL_COUNT,CELL_COUNT*2);
            for (unsigned i=key+1;i<=next;i++) apply(m,m->body,m->deltas[i],m->scratch,false);
            for (unsigned i=0;i<CELL_COUNT;i++)
                if (m->shown==UINT_MAX || m->neutral[i]!=m->scratch[i]) stage_cell(m,i,m->scratch[i]);
            uint16_t *previous=m->neutral;
            m->neutral=m->scratch;
            m->scratch=previous;
        }
    }
    apply(m,m->eye,m->gazes[next*GAZES+gaze],NULL,true);
    m->shown=next;
    m->gaze=gaze;
}

size_t pet_motion_damage(const motion_art_t *m,baji_pet_state_t before,unsigned g0,
                         baji_pet_state_t after,unsigned g1,baji_rect_t *areas,size_t capacity)
{
    unsigned a=reaction_sample(m,before), b=reaction_sample(m,after);
    if (a!=UINT_MAX || b!=UINT_MAX) {
        if (a==b) return 0;
        if (a!=UINT_MAX && b!=UINT_MAX && b>a && b-a<=4 && reaction_start(a)==reaction_start(b)) {
            size_t count=0;
            for (unsigned i=a+1;i<=b;i++) {
                reaction_pose_t pose=m->reactions.poses[i];
                for (unsigned j=0;j<pose.windows;j++)
                    pet_add_rect(areas,&count,capacity,m->reactions.windows[pose.window+j]);
            }
            return count;
        }
        areas[0]=(baji_rect_t){0,0,SIDE,SIDE};
        return 1;
    }
    unsigned old=sample(before),next=sample(after);
    size_t count=0;
    if (old==next && g0==g1) return 0;
    if (old!=next) {
        const range_t *plan=NULL;
        if (next==0 || next==IDLE_SAMPLES) plan=&m->plans[old][next==0 ? 1 : 2];
        else if (next==old+1) plan=&m->plans[next][0];
        if (plan) {
            for (unsigned i=0;i<plan->count;i++)
                pet_add_rect(areas,&count,capacity,m->rectangles[plan->first+i]);
        } else if (next>old && next-old<=4 && (next<IDLE_SAMPLES || old>=IDLE_SAMPLES)) {
            for (unsigned j=old+1;j<=next;j++) {
                range_t r=m->plans[j][0];
                for (unsigned i=0;i<r.count;i++)
                    pet_add_rect(areas,&count,capacity,m->rectangles[r.first+i]);
            }
        } else {
            areas[0]=(baji_rect_t){0,0,SIDE,SIDE};
            return 1;
        }
    }
    if (m->gazes[old*GAZES+g0].count)
        for (unsigned i=0;i<2;i++) pet_add_rect(areas,&count,capacity,m->eyes[old][i]);
    if (m->gazes[next*GAZES+g1].count)
        for (unsigned i=0;i<2;i++) pet_add_rect(areas,&count,capacity,m->eyes[next][i]);
    return count;
}

void pet_motion_paint(motion_art_t *m,const baji_rect_t *band,uint8_t *out)
{
    if (!band->width || !band->height) return;
    unsigned end=m->physical_row[band->y+band->height-1];
    for (unsigned y=m->physical_row[band->y];y<=end;y++) expand_row(m,y);
    const uint16_t *source=m->screen+band->y*SIDE+band->x;
    for (unsigned i=0;i<band->height;i++,source+=SIDE,out+=2*band->width)
        memcpy(out,source,band->width*2);
}
