#include "pet_effects.h"
#include <stdlib.h>
#include <string.h>

/* The web face is 184 logical pixels wide. Round both edges on that grid so
 * adjacent coloured runs share the same boundary on the physical AMOLED. */
enum { SIDE = 466, GRID = 184, PRESS_RX = 22, PRESS_RY = 15, PRESS_SHIFT = 8 };
#define RGB(r,g,b) (((r)>>3)<<11 | ((g)>>2)<<5 | ((b)>>3))
enum { GOLD = RGB(255,226,150), MINT = RGB(199,239,207), CREAM = RGB(255,248,223),
       EDGE = RGB(130,91,109), AMBER = RGB(225,164,104), LILAC = RGB(214,168,195) };

typedef struct {
    const baji_rect_t *band;
    uint8_t *pixels;
    int left, top, right, bottom;
} ink_t;

static int min_i(int a, int b) { return a < b ? a : b; }
static int max_i(int a, int b) { return a > b ? a : b; }
static int unit(int p) { return (p * GRID + SIDE/2) / SIDE; }
static int edge(int g) { return (g * SIDE + GRID/2) / GRID; }

static void rect(ink_t *ink, int x, int y, int w, int h, unsigned color)
{
    int x0 = edge(x), y0 = edge(y), x1 = edge(x+w), y1 = edge(y+h);
    if (!ink->band) {
        ink->left = min_i(ink->left, x0); ink->top = min_i(ink->top, y0);
        ink->right = max_i(ink->right, x1); ink->bottom = max_i(ink->bottom, y1);
        return;
    }
    const baji_rect_t *b = ink->band;
    x0 = max_i(x0,b->x); y0 = max_i(y0,b->y);
    x1 = min_i(x1,b->x+b->width); y1 = min_i(y1,b->y+b->height);
    if (x0 >= x1 || y0 >= y1) return;
    for (int row=y0; row<y1; ++row) {
        uint8_t *dst = ink->pixels + 2*((row-b->y)*b->width+x0-b->x);
        for (int col=x0; col<x1; ++col) { *dst++=(uint8_t)(color>>8); *dst++=(uint8_t)color; }
    }
}

static void shine(ink_t *ink, int x, int y, int rays, unsigned color)
{
    rect(ink,x-rays,y,2*rays+1,1,color);
    rect(ink,x,y-rays,1,2*rays+1,color);
}

static void star_layer(const pet_effects_t *fx, ink_t *ink)
{
    if (!fx->star) return;
    int x = unit(fx->star_at.x), y = unit(fx->star_at.y);
    if (fx->star == PET_STAR_HELD) {
        for (int i=0; i<min_i(fx->trail_count,4); i++)
            rect(ink,unit(fx->trail[i].x),unit(fx->trail[i].y),1,1,MINT);
        for (int i=0; i<2; i++) {
            int px=unit(fx->palms[i].x), py=unit(fx->palms[i].y);
            bool near = fx->glow == (i ? 1 : -1);
            rect(ink,px-4,py+2,2,1,near ? GOLD : MINT);
            rect(ink,px+3,py+2,2,1,near ? GOLD : MINT);
            if (near) rect(ink,px-2,py+4,5,1,GOLD);
        }
    }
    if (fx->star == PET_STAR_DEPART) { rect(ink,x,y,2,2,GOLD); return; }
    if (fx->star == PET_STAR_CAUGHT && fx->catch_frame>=17) {
        int f=fx->catch_frame;
        if (f<20) {
            int gy=y+f-16;
            if (f==17) {
                rect(ink,x-2,gy-1,5,2,EDGE); rect(ink,x-1,gy-2,2,4,GOLD);
                rect(ink,x-1,gy,3,1,CREAM);
            } else if (f==18) {
                rect(ink,x-1,gy-1,3,2,GOLD); rect(ink,x,gy-1,1,1,CREAM);
            } else rect(ink,x,gy-1,1,1,GOLD);
        } else if (f<26) {
            rect(ink,x-1-(f-20),y+2-(f-20),1,2,GOLD);
            rect(ink,x+1+(f-20),y+1-(f-20),1,1,MINT);
        }
        return;
    }
    if (fx->star != PET_STAR_CAUGHT) {
        int wing=fx->wing & 1;
        rect(ink,x-5,y-2-wing,3,2,EDGE); rect(ink,x+3,y-2-wing,3,2,EDGE);
        rect(ink,x-5,y-3-wing,2,2,MINT); rect(ink,x+4,y-3-wing,2,2,MINT);
    }
    rect(ink,x-1,y-4,2,8,EDGE); rect(ink,x-4,y-1,8,3,EDGE);
    rect(ink,x-2,y-2,5,5,AMBER); rect(ink,x-1,y-3,2,6,GOLD);
    rect(ink,x-3,y,6,1,GOLD); rect(ink,x-1,y-1,2,2,CREAM);
}

static void mark_layer(const pet_effects_t *fx, ink_t *ink)
{
    if (!fx->mark) return;
    int x=unit(fx->mark_at.x), y=unit(fx->mark_at.y), step=fx->mark_step;
    if (fx->mark!=PET_FX_BLUSH)
        y-=min_i(step,4);
    switch (fx->mark) {
    case PET_FX_HEART: {
        static const char heart[]=".o.o." "owogo" "ogggg" ".ogo.";
        for (int i=0; i<20; i++) if (heart[i]!='.')
            rect(ink,x+i%5-2,y+i/5-2,1,1,heart[i]=='o' ? RGB(111,74,98) :
                 heart[i]=='w' ? CREAM : RGB(232,132,155));
        break;
    }
    case PET_FX_BLUSH:
        rect(ink,x-2,y,4,1,RGB(248,192,206)); rect(ink,x-1,y+1,3,1,RGB(239,154,176)); break;
    case PET_FX_NOTE:
        rect(ink,x,y,1,5,RGB(206,195,232)); rect(ink,x-2,y+4,3,2,RGB(206,195,232));
        rect(ink,x+3,y+1,1,4,RGB(160,149,172)); rect(ink,x+2,y+4,2,1,RGB(160,149,172)); break;
    case PET_FX_QUESTION:
        rect(ink,x,y-2,3,1,GOLD); rect(ink,x+2,y-1,1,2,GOLD);
        rect(ink,x+1,y+1,1,1,GOLD); rect(ink,x+1,y+3,1,1,GOLD); break;
    case PET_FX_SWEAT:
        rect(ink,x,y,2,4,RGB(155,203,189)); rect(ink,x+1,y-1,1,2,MINT);
        rect(ink,x,y+3,2,1,RGB(88,123,144)); break;
    case PET_FX_POUT:
        for (int side=-1; side<=1; side+=2) {
            rect(ink,x+2*side,y-1,1,1,RGB(255,150,168)); rect(ink,x+side,y-2,1,2,RGB(255,150,168));
            rect(ink,x+2*side,y+1,1,1,RGB(255,150,168)); rect(ink,x+side,y+1,1,2,RGB(255,150,168));
        }
        break;
    case PET_FX_ELLIPSIS:
        for (int i=0;i<=6;i+=3) {
            rect(ink,x+i+1,y+1,1,1,RGB(53,47,65)); rect(ink,x+i,y,1,1,RGB(225,209,242));
        }
        break;
    default:
        shine(ink,x,y,2,GOLD); shine(ink,x+5,y-3,1,LILAC); break;
    }
}

static void contact_layer(const pet_effects_t *fx, ink_t *ink)
{
    if (!fx->contact || fx->pillow_depth) return;
    int x=unit(fx->finger.x), y=unit(fx->finger.y), a=min_i(fx->contact_step,3);
    unsigned light=fx->tool ? RGB(217,239,213) : RGB(255,224,207);
    unsigned dark=fx->tool ? RGB(83,110,112) : RGB(138,87,109);
    if (fx->contact==1) {
        int r=a ? 3 : 4;
        rect(ink,x-r-1,y,2,1,dark); rect(ink,x-r,y-1,1,1,light);
        rect(ink,x+r,y,2,1,dark); rect(ink,x+r,y-1,1,1,light);
    } else {
        for (int side=-1; side<=1; side+=2) {
            rect(ink,x+side*(3+a),y-1-a,1,1,light);
            if (a<3) rect(ink,x+side*(4+a),y-a,1,1,dark);
        }
    }
}

static void hair_layer(const pet_effects_t *fx, ink_t *ink)
{
    for (int i=0; i<min_i(fx->hair_count,3); i++) {
        unsigned age = fx->hair_age_ms[i];
        if (age > 380) continue;
        int x = unit(fx->hair[i].x), y = unit(fx->hair[i].y)-2-(int)(age/90);
        shine(ink, x, y, age<210, age<210 ? RGB(255,242,190) : LILAC);
    }
}

static void burst_layer(const pet_effects_t *fx, ink_t *ink)
{
    int f=fx->burst_frame;
    if (!fx->burst || f>19) return;
    int cx=unit(fx->bubble.x), cy=unit(fx->bubble.y);
    if (f<7) {
        static const int8_t dir[6][2]={{-80,-60},{30,-95},{96,-28},{72,69},{-20,98},{-96,28}};
        static const unsigned colors[]={RGB(255,230,180),RGB(188,233,224),RGB(255,209,223)};
        /* The hit radius includes 12 source pixels of finger tolerance,
         * corresponding to 11 physical pixels in the baked close view. */
        int reach=unit(max_i(0,fx->bubble_radius-11))+(5*f+2)/4;
        for (int i=0;i<6;i++) if (f<=4+i%3) {
            int x=cx+dir[i][0]*reach/100, y=cy+dir[i][1]*reach/100+f*f/12;
            int w=abs(dir[i][0])<80 && f<4 ? 3 : 1, h=w==1 && f<4 ? 2 : 1;
            rect(ink,x,y+1,w,h,RGB(131,103,127)); rect(ink,x,y,w,h,colors[i%3]);
        }
    }
    int t=max_i(0,min_i(10,f-1)), u=10-t;
    int nx=unit(fx->nose.x)+1, ny=unit(fx->nose.y)-1;
    int sx=(u*u*u*cx+3*u*u*t*(cx+6)+3*u*t*t*(cx-4)+t*t*t*nx+500)/1000;
    int sy=(u*u*u*cy+3*u*u*t*(cy-6)+3*u*t*t*(ny+12)+t*t*t*ny+500)/1000;
    int rays=f<11 ? 3 : f<17 ? 2 : 1;
    shine(ink,sx,sy,rays,RGB(137,100,119));
    if (rays==3) rect(ink,sx-1,sy-1,3,3,RGB(231,170,112));
    shine(ink,sx,sy,rays-1,RGB(245,202,120)); rect(ink,sx,sy,1,1,RGB(255,245,203));
}

static void pillow(const pet_effects_t *fx, const baji_rect_t *band, uint8_t *pixels,
                   baji_paint_fn base, pet_fx_fabric_fn fabric, void *context)
{
    if (!fx->pillow_depth || !fabric || !base) return;
    int cx=fx->pillow_at.x, cy=fx->pillow_at.y;
    int x0=max_i(cx-PRESS_RX,band->x), x1=min_i(cx+PRESS_RX+1,band->x+band->width);
    int y0=max_i(cy-PRESS_RY,band->y), y1=min_i(cy+PRESS_RY+1,band->y+band->height);
    if (x0>=x1 || y0>=y1) return;
    /* Rows inside this band still contain original fabric as we work upwards.
     * A split DMA band may need a narrow strip from the preceding band. */
    uint8_t extra[2*(2*PRESS_RX+1)*PRESS_SHIFT];
    int ey=max_i(0,y0-PRESS_SHIFT), eh=max_i(0,band->y-ey);
    if (eh) {
        baji_rect_t source={x0,ey,x1-x0,eh};
        base(&source,extra,context);
    }
    for (int y=y1-1; y>=y0; --y) for (int x=x0; x<x1; ++x) {
        int weight=(PRESS_RX*PRESS_RX-(x-cx)*(x-cx))*(PRESS_RY*PRESS_RY-(y-cy)*(y-cy));
        int shift=(int)min_i(fx->pillow_depth,3)*PRESS_SHIFT*weight/(3*PRESS_RX*PRESS_RX*PRESS_RY*PRESS_RY);
        if (!shift || y<shift || !fabric(context,x,y) || !fabric(context,x,y-shift)) continue;
        const uint8_t *src= y-shift<band->y ? extra+2*((y-shift-ey)*(x1-x0)+x-x0) :
                              pixels+2*((y-shift-band->y)*band->width+x-band->x);
        uint16_t c=(uint16_t)(src[0]<<8 | src[1]);
        /* A softly shaded contact point remains legible on a flat blue lobe. */
        if (shift>=2) c-=((c&0xf800) ? 0x0800 : 0)+((c&0x07e0) ? 0x20 : 0)+(c&0x1f ? 1 : 0);
        uint8_t *dst=pixels+2*((y-band->y)*band->width+x-band->x);
        dst[0]=(uint8_t)(c>>8); dst[1]=(uint8_t)c;
    }
}

void pet_effects_paint(const pet_effects_t *fx, const baji_rect_t *band, uint8_t *pixels,
                       baji_paint_fn base, pet_fx_fabric_fn fabric, void *context)
{
    ink_t ink={.band=band,.pixels=pixels};
    pillow(fx,band,pixels,base,fabric,context);
    burst_layer(fx,&ink); hair_layer(fx,&ink); mark_layer(fx,&ink);
    star_layer(fx,&ink); contact_layer(fx,&ink);
}

typedef void (*fx_layer_fn)(const pet_effects_t *,ink_t *);
static void mark_layer_bounds(const pet_effects_t *fx, fx_layer_fn layer, baji_damage_t *damage)
{
    ink_t ink={.left=SIDE,.top=SIDE};
    layer(fx,&ink);
    if (ink.right>ink.left && ink.bottom>ink.top)
        baji_damage_mark(damage,ink.left,ink.top,ink.right-ink.left,ink.bottom-ink.top);
}

static bool same_point(pet_fx_point_t a, pet_fx_point_t b) { return a.x==b.x && a.y==b.y; }

void pet_effects_damage(const pet_effects_t *a, const pet_effects_t *b, baji_damage_t *damage)
{
    bool hair_changed = a->hair_count != b->hair_count;
    for (int i=0; !hair_changed && i<min_i(a->hair_count,3); i++) {
        unsigned t=a->hair_age_ms[i], u=b->hair_age_ms[i];
        hair_changed = !same_point(a->hair[i],b->hair[i]) || t/90!=u/90 ||
                       (t<210)!=(u<210) || (t>380)!=(u>380);
    }
    if (hair_changed) {
        mark_layer_bounds(a,hair_layer,damage); mark_layer_bounds(b,hair_layer,damage);
    }
    if ((a->star || b->star) && (a->star!=b->star || a->wing!=b->wing || a->catch_frame!=b->catch_frame ||
        a->glow!=b->glow || !same_point(a->star_at,b->star_at) || a->trail_count!=b->trail_count ||
        memcmp(a->trail,b->trail,sizeof(a->trail)) || memcmp(a->palms,b->palms,sizeof(a->palms)))) {
        mark_layer_bounds(a,star_layer,damage); mark_layer_bounds(b,star_layer,damage);
    }
    if ((a->mark || b->mark) && (a->mark!=b->mark || a->mark_step!=b->mark_step ||
        !same_point(a->mark_at,b->mark_at))) {
        mark_layer_bounds(a,mark_layer,damage); mark_layer_bounds(b,mark_layer,damage);
    }
    if ((a->contact || b->contact) && (a->contact!=b->contact || a->tool!=b->tool ||
        a->contact_step!=b->contact_step || !same_point(a->finger,b->finger) ||
        (!!a->pillow_depth)!=(!!b->pillow_depth))) {
        mark_layer_bounds(a,contact_layer,damage); mark_layer_bounds(b,contact_layer,damage);
    }
    if ((a->burst || b->burst) && (a->burst!=b->burst || a->burst_frame!=b->burst_frame ||
        !same_point(a->bubble,b->bubble) || a->bubble_radius!=b->bubble_radius || !same_point(a->nose,b->nose))) {
        mark_layer_bounds(a,burst_layer,damage); mark_layer_bounds(b,burst_layer,damage);
    }
    if (a->pillow_depth!=b->pillow_depth || (b->pillow_depth && !same_point(a->pillow_at,b->pillow_at))) {
        const pet_effects_t *both[]={a,b};
        for (int i=0;i<2;i++) if (both[i]->pillow_depth)
            baji_damage_mark(damage,both[i]->pillow_at.x-PRESS_RX,both[i]->pillow_at.y-PRESS_RY,
                             2*PRESS_RX+1,2*PRESS_RY+1);
    }
}
