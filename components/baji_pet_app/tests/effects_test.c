#include "pet_effects.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { SIDE=466, BYTES=SIDE*SIDE*2 };
static uint8_t full[BYTES], partial[BYTES], band[BYTES], reference[BYTES];
static const baji_rect_t screen={0,0,SIDE,SIDE};

static void paint_base(const baji_rect_t *r,uint8_t *output,void *unused)
{
    (void)unused;
    for (int y=r->y;y<r->y+r->height;y++) for (int x=r->x;x<r->x+r->width;x++) {
        unsigned color=(x*61+y*2093)&0xffffu;
        *output++=(uint8_t)(color>>8); *output++=(uint8_t)color;
    }
}
static bool fabric(void *context,int x,int y)
{
    (void)context;
    /* Include both a foreground hand and a curved outer hem. */
    return (x-170)*(x-170)+(y-294)*(y-294)<29*29 && !(x>174 && x<186 && y<298);
}
static void paint(const pet_effects_t *fx,baji_rect_t r,uint8_t *dst)
{
    paint_base(&r,band,NULL);
    pet_effects_paint(fx,&r,band,paint_base,fabric,NULL);
    for (unsigned j=0;j<r.height;j++)
        memcpy(dst+2*((r.y+j)*SIDE+r.x),band+2*j*r.width,2*r.width);
}
int main(void)
{
    paint_base(&screen,reference,NULL);
    memcpy(partial,reference,BYTES);
    pet_effects_t prev={0};
    for (unsigned i=0;i<54;i++) {
        pet_effects_t fx={
            .star_at={145+(int)i*2,123}, .palms={{135,215},{279,226}},
            .trail={{124,135},{143,123}},.trail_count=2,.star=i%5,.glow=i%2?1:-1,
            .wing=i%2,.catch_frame=i%27,.mark=(pet_fx_mark_t)(i%11),
            .hair={{193,149},{203,153},{212,150}},.hair_count=i%4,.hair_age_ms={i*8,i*9,i*10},
            .mark_at={190+(int)i,113},.mark_step=i%5,.pillow_depth=i%4,.pillow_at={170,292},
            .finger={307,219},.contact=i%3,.contact_step=i%4,
            .burst=i%2,.bubble={277,114},.nose={227,215},.bubble_radius=19,.burst_frame=i%20
        };
        if (i==53) fx=(pet_effects_t){0};
        paint(&fx,screen,full);
        baji_damage_t d; baji_damage_init(&d,SIDE,SIDE);
        pet_effects_damage(&prev,&fx,&d);
        for (size_t n=0;n<d.count;n++) {
            baji_rect_t r=d.regions[n];
            /* All seams, including single scanlines, must reproduce a full paint. */
            for (unsigned j=0;j<r.height;j++)
                paint(&fx,(baji_rect_t){r.x,r.y+j,r.width,1},partial);
        }
        assert(memcmp(full,partial,BYTES)==0);
        for (int y=278;y<=306;y++) for (int x=148;x<=192;x++)
            if (!fabric(NULL,x,y)) assert(!memcmp(full+2*(y*SIDE+x),reference+2*(y*SIDE+x),2));
        baji_damage_clear(&d); pet_effects_damage(&fx,&fx,&d); assert(!d.count);
        prev=fx;
    }
    assert(!memcmp(partial,reference,BYTES));
    puts("54 physical overlays matched complete and dirty scanline paint; hand occlusion and erase verified");
}
