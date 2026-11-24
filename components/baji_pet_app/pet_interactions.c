#include "pet_interactions.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MS(x) ((int64_t)(x) * 1000)
static bool is(const char *a, const char *b) { return a && b && strcmp(a,b)==0; }
static bool prefix(const char *s, const char *p) { return s && !strncmp(s,p,strlen(p)); }
static int clamp(int n, int lo, int hi) { return n < lo ? lo : n > hi ? hi : n; }
static int sign(int x) { return (x > 0) - (x < 0); }
static bool asleep(const char *name) { return is(name,"doze") || prefix(name,"peek"); }
static const char *side_name(int side, const char *left, const char *right) { return side < 0 ? left : right; }
static const char *base(const pet_interactions_t *s) { return is(s->base,"reading") && s->moon ? "readingMoon" : s->base; }
static const char *cue_name(baji_agent_cue_t cue)
{
    switch (cue) {
        case BAJI_CUE_THINK: return "working";
        case BAJI_CUE_READ: return "reading";
        case BAJI_CUE_WRITE: return "writing";
        case BAJI_CUE_RUN: return "running";
        case BAJI_CUE_SEARCH: return "searching";
        case BAJI_CUE_WAIT: return "waiting";
        case BAJI_CUE_SUCCESS: return "succeeded";
        case BAJI_CUE_ERROR: case BAJI_CUE_FAILED: return "error";
        case BAJI_CUE_LIMIT: return "limited";
        case BAJI_CUE_COMPACT: return "compact";
        case BAJI_CUE_DONE: return "ended";
        case BAJI_CUE_CANCELLED: return "cancelled";
        default: return "idle";
    }
}
static const char *stow(const char *name)
{
    static const char *const names[]={"stow-reading","stow-readingMoon","stow-running","stow-searching",
        "stow-working","stow-writing","stow-compact","stow-waiting","stow-error"};
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++) if(is(name,names[i]+5)) return names[i];
    return NULL;
}
static int count(const pet_clip_t *clip) { return clip ? (int)pet_clip_frames(clip) : 1; }
static int rate(const pet_clip_t *clip) { return clip ? (int)pet_clip_fps(clip) : 12; }
static int64_t span_us(pet_transition_t m) { return (int64_t)(1+abs(m.last-m.first))*m.step_us; }
static void add_move(pet_interactions_t *s,const char *name,int first,int last,int step_us)
{
    if(name && s->move_count < sizeof(s->moves)/sizeof(*s->moves))
        s->moves[s->move_count++] = (pet_transition_t){name,first,last,step_us};
}
static void stamp_start(pet_interactions_t *s,int64_t now)
{
    s->move_since=now;
    s->since=now;
    for(unsigned i=0;i<s->move_count;i++) s->since+=span_us(s->moves[i]);
}
/* Change costume through the source hand-off strip. A superseding event can
 * reverse a strip from the frame actually on the screen. */
static void select_action(pet_interactions_t *s,const char *action,int64_t now)
{
    const char *scene=base(s);
    const char *previous=s->action;
    const pet_clip_t *context=pet_clips_lookup(s->art,action,scene);
    bool paired=context && context!=pet_clips_lookup(s->art,action,"idle");
    const char *next_gear=(paired || is(action,scene) || prefix(action,"turn") || prefix(action,"glance") ||
        is(action,"keyChime") || is(action,"lensFind") || is(action,"palmTap")) ? stow(scene):NULL;
    const char *old=s->gear;
    const char *shown=s->shown.action;
    int frame=(int)s->shown.frame;
    if(is(previous,scene) && is(shown,scene) && !is(action,scene)) {
        s->saved_scene=scene;
        s->saved_us=s->shown.elapsed_us;
    }
    s->pose_offset=0;
    if(is(action,scene) && is(s->saved_scene,scene) && !is(scene,"idle") &&
            !(prefix(scene,"reading") && (prefix(previous,"turn") || prefix(previous,"glance"))) &&
            !(is(scene,"searching") && is(previous,"lensFind"))) s->pose_offset=s->saved_us;
    if(is(action,"lensFind") && is(shown,"searching")) {
        int origin[2],r;
        if(pet_clip_prop(s->shown.clip,s->shown.frame,origin,&r)) {
            int closest=INT_MAX;
            for(int i=0;i<8;i++) {
                int p[2];
                if(!pet_clip_prop(context,i,p,&r)) continue;
                int dx=p[0]-origin[0],dy=p[1]-origin[1],d=dx*dx+dy*dy;
                if(d<closest) {s->pose_offset=((int64_t)i*1000000+rate(context)-1)/rate(context);closest=d;}
            }
        }
    }
    s->move_count=0;
    bool same=(!old && !next_gear) || is(old,next_gear) || (prefix(old,"stow-reading") && prefix(next_gear,"stow-reading"));
    if(prefix(shown,"stow-")) {
        bool reopen=is(shown,next_gear) || (prefix(shown,"stow-reading") && prefix(next_gear,"stow-reading"));
        add_move(s,shown,frame,reopen ? 0 : 7,41667);
        if(!reopen && next_gear) add_move(s,next_gear,7,0,41667);
    } else {
        if(!asleep(action) && !prefix(action,"wake") && (asleep(shown) || prefix(shown,"wake"))) {
            if(is(shown,"doze") && frame>0) {
                if(frame>25) { add_move(s,shown,frame,34,22000); frame=15; }
                add_move(s,shown,frame,0,22000);
            } else if(prefix(shown,"peek")) {
                add_move(s,shown,frame,8,22000);
                add_move(s,is(shown,"peekLeft")?"wakeLeft":"wakeRight",0,20,22000);
            } else if(prefix(shown,"wake") && frame<35) add_move(s,shown,frame,frame<=20?20:35,22000);
        }
        if(!same && old) add_move(s,old,0,7,41667);
        if(!same && next_gear) add_move(s,next_gear,7,0,41667);
    }
    s->gear=next_gear;
    s->action=action;
    s->micro=false;
    s->brush_auto=false;
    stamp_start(s,now);
}
static void restore(pet_interactions_t *s,int64_t now)
{
    select_action(s,base(s),now);
}
static void reset_contact(pet_interactions_t *s)
{
    s->part=PET_HIT_NONE;
    s->down=s->held_cheek=s->pillow_held=s->page_drag=false;
    s->brush_release=-1;
    s->release_at=-1;
    s->consumed=true;
    s->brow_touch=0;
}
static int bond_for(const char *name)
{
    if(is(name,"heartHug") || is(name,"reconcile")) return 4;
    if(is(name,"brush") || is(name,"tickle") || is(name,"forehead") || prefix(name,"starCatch")) return 3;
    if(prefix(name,"pat") || is(name,"palmTap") || is(name,"feed")) return 2;
    if(is(name,"pillow") || is(name,"sulk") || is(name,"reunion") || prefix(name,"browInvite")) return 0;
    return 1;
}
typedef struct {const char *name; int8_t energy, affection, curiosity;} mood_change_t;
static void mood(pet_interactions_t *s,const char *name)
{
    static const mood_change_t deltas[]={
        {"pat",2,8,0},{"patLeft",2,8,0},{"brush",0,10,0},{"cheekLeft",0,0,4},{"cheekRight",0,0,4},
        {"tickle",8,6,0},{"nose",0,0,4},{"sneeze",3,0,0},{"feed",6,5,0},{"bubblePop",3,0,0},
        {"play",0,0,6},{"starCatchLeft",6,10,0},{"starCatchRight",6,10,0},{"heartHug",0,14,0},
        {"palmTap",0,6,0},{"keyChime",3,0,0},{"lensFind",0,0,5},{"page",0,0,3},{"forehead",0,12,0},
        {"reconcile",6,14,0},{"sulk",4,-8,-5},{"idle",2,0,2},{"working",-4,0,5},{"reading",-3,0,8},
        {"running",-7,0,0},{"writing",-5,0,3},{"searching",-2,0,8},{"waiting",-2,0,5},{"succeeded",16,8,0},
        {"ended",-5,0,0},{"error",-16,-2,0},{"limited",-20,0,0},{"compact",-3,0,0},{"cancelled",-4,0,0}
    };
    for(unsigned i=0;i<sizeof(deltas)/sizeof(*deltas);i++) if(is(name,deltas[i].name)) {
        s->energy=clamp((int)s->energy+deltas[i].energy,0,100);
        s->affection=clamp((int)s->affection+deltas[i].affection,0,100);
        s->curiosity=clamp((int)s->curiosity+deltas[i].curiosity,0,100);
        break;
    }
}
static void event(pet_interactions_t *s,const char *name,int64_t now)
{
    if(now<s->sulk_until && (is(name,"feed") || is(name,"brush") || prefix(name,"pat") ||
            is(name,"forehead") || is(name,"heartHug"))) {
        name="reconcile";
        s->sulk_until=0;
    }
    s->event=name;
    s->mark_action=name;
    mood(s,name);
    int gain=clamp(bond_for(name),0,100-(int)s->bond);
    s->event_bond+=gain;
    s->bond+=(unsigned)gain;
    s->mark_since=now;
    if(is(name,"sulk")) s->mark=PET_FX_POUT;
    else if(prefix(name,"cheek")) s->mark=PET_FX_BLUSH;
    else if(is(name,"tickle") || is(name,"keyChime")) s->mark=PET_FX_NOTE;
    else if(is(name,"nose")) s->mark=PET_FX_QUESTION;
    else if(prefix(name,"pat") || is(name,"heartHug") || is(name,"forehead") || prefix(name,"starCatch") ||
            is(name,"reconcile") || is(name,"reunion")) s->mark=PET_FX_HEART;
    else if(prefix(name,"wing") || is(name,"tail") || is(name,"lensFind")) s->mark=PET_FX_SPARKLE;
    else s->mark=PET_FX_NONE;
}
static bool third(int64_t now,int64_t interval,int64_t stamps[2],unsigned *n)
{
    if(*n==2 && now-stamps[0]<interval) { *n=0; return true; }
    if(*n==2) { stamps[0]=stamps[1]; *n=1; }
    stamps[(*n)++]=now;
    return false;
}
static void interacted(pet_interactions_t *s,int64_t now)
{
    if(now-s->last_visit>=MS(900000)) {
        s->reunion_until=now+MS(4200);
        event(s,"reunion",now);
    }
    s->last_visit=now;
    s->quiet_since=now;
    s->next_micro=now+MS(4000);
}
static void start_nap(pet_interactions_t *s,int64_t now)
{
    if(!is(s->base,"idle")) return;
    s->star_phase=PET_TOY_OFF;
    s->sleep_offset=0;
    s->brow_touch=0;
    select_action(s,"doze",now);
    s->brow_offered=false;
}
static void wake(pet_interactions_t *s,int side,int64_t now)
{
    const char *shown=s->shown.action;
    int f=s->shown.frame;
    if(prefix(s->action,"peek")) {
        const char *peek=s->action;
        select_action(s,is(peek,"peekLeft")?"wakeLeft":"wakeRight",now);
        if(is(shown,"doze")) add_move(s,shown,f,f>25?34:15,22000);
        add_move(s,peek,is(shown,peek)?f:0,8,22000);
    } else if(is(s->action,"doze")) {
        if(f<=6) { restore(s,now); return; }
        select_action(s,side_name(side,"peekLeft","peekRight"),now);
        add_move(s,"doze",f,f>25?34:15,22000);
    } else return;
    stamp_start(s,now);
}
static void care(pet_interactions_t *s,const char *name,int64_t now)
{
    interacted(s,now);
    if(asleep(s->action)) { wake(s,s->side,now); return; }
    static const char *const cool[]={"pat","patLeft","wing","wingLeft"};
    for(unsigned i=0;i<sizeof(cool)/sizeof(*cool);i++) if(is(name,cool[i])) {
        if(now<s->part_cooldown[i]) return;
        s->part_cooldown[i]=now+MS(350);
        break;
    }
    if(is(name,"nose")) {
        if(is(s->action,"nose") && now-s->last_nose<MS(900)) name="sneeze";
        s->last_nose=now;
    }
    if(is(name,"tail") && is(s->base,"idle") && third(now,MS(5500),s->tail_times,&s->tail_count)) {
        s->sulk_until=now+MS(6500);
        event(s,"sulk",now);
        restore(s,now);
        return;
    }
    s->star_phase=PET_TOY_OFF;
    select_action(s,name,now);
    event(s,name,now);
}

static int palms(const pet_interactions_t *s,int xy[4])
{
    if(s->shown.clip && pet_clip_palms(s->shown.clip,s->shown.frame,xy)) return 1;
    const pet_clip_t *watch=pet_clips_lookup(s->art,"starWatch","idle");
    return watch && pet_clip_palms(watch,13,xy);
}
static int inside_palm(const int xy[4],int x,int y,int radius)
{
    int best=radius*radius+1, side=0;
    for(int i=0;i<2;i++) {
        int dx=x-xy[2*i],dy=y-xy[2*i+1],d=dx*dx+dy*dy;
        if(d<best) { best=d; side=i ? 1 : -1; }
    }
    return side;
}
static void toy_phase(pet_interactions_t *s,pet_star_phase_t phase,int64_t now)
{
    s->star_from_x=s->star_x;
    s->star_from_y=s->star_y;
    s->star_since=now;
    s->star_phase=phase;
}
static int home_x(const pet_interactions_t *s) { return 233+s->next_side*129; }
static void toy_start(pet_interactions_t *s,int64_t now)
{
    s->next_side=-s->hand;
    s->hand=s->next_side;
    s->glow=0;
    s->star_x=home_x(s);
    s->star_y=175;
    s->trail_count=0;
    select_action(s,"starWatch",now);
    toy_phase(s,PET_TOY_INVITE,s->since);
    s->rematch=false;
}
static void toy_finish(pet_interactions_t *s,int side,int64_t now)
{
    s->hand=s->glow=side;
    s->star_thrown=false;
    toy_phase(s,PET_TOY_LANDING,now);
}
static bool toy_down(pet_interactions_t *s,int x,int y,int64_t now)
{
    int hands[4];
    if(!palms(s,hands)) return false;
    int side=inside_palm(hands,x,y,23);
    int dx=s->star_x-x,dy=s->star_y-y;
    if(dx*dx+dy*dy<=30*30) {
        s->star_dx=dx; s->star_dy=dy;
        s->star_vx=s->star_vy=0;
        s->star_vtime=0;
        s->star_sample=now;
        s->star_prev_x=s->star_x;
        s->star_prev_y=s->star_y;
        toy_phase(s,PET_TOY_HELD,now);
    } else if(side) {
        s->hand=s->glow=side;
        toy_phase(s,PET_TOY_HAND,now);
    } else return false;
    return true;
}
static void toy_touch(pet_interactions_t *s,int x,int y,bool down,int64_t now)
{
    int p[4];
    if(!palms(s,p)) return;
    if(s->star_phase==PET_TOY_HAND) {
        s->glow=inside_palm(p,x,y,30)==s->hand ? s->hand : 0;
    } else {
        s->star_y=clamp(y+s->star_dy,79,365);
        int dy=s->star_y-233, half=(int)sqrtf(185*185-dy*dy);
        s->star_x=clamp(x+s->star_dx,233-half,233+half);
        s->glow=inside_palm(p,s->star_x,s->star_y,30);
        if(!s->trail_count || abs(s->star_x-s->trail[s->trail_count-1].x)+abs(s->star_y-s->trail[s->trail_count-1].y)>=8) {
            if(s->trail_count==4) {
                memmove(s->trail,s->trail+1,3*sizeof(*s->trail));
                memmove(s->trail_at,s->trail_at+1,3*sizeof(*s->trail_at));
                s->trail_count--;
            }
            s->trail_at[s->trail_count]=now;
            s->trail[s->trail_count++]=(pet_fx_point_t){s->star_x,s->star_y};
        }
        if(now-s->star_sample>=MS(12) && (s->star_x!=s->star_prev_x || s->star_y!=s->star_prev_y)) {
            s->star_vx=s->star_x-s->star_prev_x;
            s->star_vy=s->star_y-s->star_prev_y;
            s->star_vtime=now-s->star_sample;
            s->star_sample=now;
            s->star_prev_x=s->star_x;
            s->star_prev_y=s->star_y;
        }
    }
    if(down) return;
    int side=s->glow;
    bool thrown=false;
    if(!side && s->star_phase==PET_TOY_HELD && now-s->star_sample<=MS(120) && s->star_vtime>0) {
        float length=hypotf(s->star_vx,s->star_vy), best=142;
        if(length>=20 && length*1000/s->star_vtime>=0.405f) for(int i=0;i<2;i++) {
            int dx=p[2*i]-s->star_x,dy=p[2*i+1]-s->star_y;
            float ahead=(dx*s->star_vx+dy*s->star_vy)/length;
            if(ahead>0 && ahead<best && abs(dx*s->star_vy-dy*s->star_vx)/length<28) {
                side=i?1:-1; best=ahead;thrown=true;
            }
        }
    }
    if(side) {toy_finish(s,side,now);s->star_thrown=thrown;}
    else { s->glow=0; toy_phase(s,PET_TOY_RETURN,now); }
}
static int toy_frame(pet_interactions_t *s,int64_t now)
{
    int p[4]={}, x=home_x(s), y=175, duration=1;
    bool have_hands=palms(s,p);
    int64_t age=now-s->star_since;
    switch(s->star_phase) {
        case PET_TOY_INVITE:
            s->star_x=x+(int)(7*sinf(age/520000.f));
            s->star_y=y+(int)(7*sinf(age/340000.f));
            if(age>=MS(14000)) toy_phase(s,PET_TOY_DEPART,now);
            break;
        case PET_TOY_RETURN: case PET_TOY_LANDING: case PET_TOY_DEPART: {
            int lift=10, lane=0, mid=(p[0]+p[2])/2;
            bool skirt=false;
            if(s->star_phase==PET_TOY_LANDING && have_hands) {
                int i=s->hand>0?2:0; x=p[i]; y=p[i+1];
                int gap=(int)hypotf(x-s->star_from_x,y-s->star_from_y);
                duration=gap<=3?1:clamp(50+gap*2,50,s->star_thrown?410:290);
                lift=s->star_thrown?clamp(gap/3,0,33):clamp((gap-30)/6,0,13);
                skirt=!s->star_thrown && s->star_from_y<y-45 && (s->star_from_x-mid)*(x-mid)<0;
            } else if(s->star_phase==PET_TOY_DEPART) { x=233+s->next_side*109;y=91;duration=580; }
            else {
                int dist=(int)hypotf(x-s->star_from_x,y-s->star_from_y);
                if(have_hands && (s->star_from_x-mid)*(x-mid)<0) {
                    int palm_y=(p[1]+p[3])/2;
                    lane=s->star_from_y>(palm_y+y)/2?palm_y+10:clamp((y<s->star_from_y?y:s->star_from_y)-25,91,465);
                }
                duration=lane?clamp(220+dist*6/5,220,620):clamp(70+dist*2,70,360);
                lift=clamp(dist/5,0,15);
            }
            float u=fminf(1.f,fmaxf(0.f,(float)age/MS(duration)));
            float uu=skirt?u*u:u, vv=skirt?1-(1-u)*(1-u):u;
            s->star_x=s->star_from_x+(int)((x-s->star_from_x)*uu*uu*(3-2*uu));
            s->star_y=lane?(int)((1-u)*(1-u)*(1-u)*s->star_from_y+3*u*(1-u)*lane+u*u*u*y):
                s->star_from_y+(int)((y-s->star_from_y)*vv*vv*(3-2*vv)-(skirt?-5:lift)*4*u*(1-u));
            if(u>=1.f) {
                if(s->star_phase==PET_TOY_LANDING) {
                    const char *clip=side_name(s->hand,"starCatchLeft","starCatchRight");
                    select_action(s,clip,now); event(s,clip,now);
                    toy_phase(s,PET_TOY_CAUGHT,now); s->rematch=true; s->quiet_since=now;
                } else if(s->star_phase==PET_TOY_RETURN) toy_phase(s,PET_TOY_INVITE,now);
                else { s->star_phase=PET_TOY_OFF; restore(s,now); }
            }
            break;
        }
        default: break;
    }
    int a[10]={233,178,232,278};
    const pet_clip_t *watch=pet_clips_lookup(s->art,"starWatch",NULL);
    if(watch) {pet_clip_anchors(watch,13,a);pet_clip_palms(watch,13,p);}
    int eye_x=(a[0]+a[2])/2,eye_y=a[1]+(a[3]-a[1])*72/100;
    int i=s->star_x<eye_x?0:2, step=clamp(abs(p[i]-eye_x)/3,1,256);
    return (s->star_y<eye_y-20 ? 0 : s->star_y>eye_y+20 ? 18 : 9)+clamp(4+(int)lroundf((float)(s->star_x-eye_x)/step),0,8);
}
static void brush_begin(pet_interactions_t *s,int x,int64_t now)
{
    bool invitation=prefix(s->action,"browInvite");
    int from=s->shown.frame;
    const char *brow=s->action;
    select_action(s,"brush",now);
    if(invitation) {
        int end=from<12?0:from>=65?76:from<24?23:from<=30?24:36;
        add_move(s,brow,from,end,22000);
        if(from>=12 && from<65) add_move(s,brow,from>30?84:78,from>30?88:82,22000);
        stamp_start(s,now);
    }
    int a[10]={233,180};
    if(s->shown.clip) pet_clip_anchors(s->shown.clip,s->shown.frame,a);
    s->brush_origin=s->brush_pos=clamp((x-a[0])*4/67,-4,4);
    s->brush_depth=1; s->brush_stroked=false; s->brush_reward=false;
    s->brush_release=-1;
    s->hair_count=0;
    s->last_hair=(pet_fx_point_t){s->contact_x,s->contact_y};
    s->direction=s->turns=0; s->extreme_x=x;
}
static void brush_end(pet_interactions_t *s,int64_t now,bool voluntary)
{
    if(s->brush_release>=0) return;
    s->brush_release=now;
    s->brush_reward=voluntary && s->turns>=2 && is(s->base,"idle");
}
static void stroke(pet_interactions_t *s,int x,int span)
{
    int delta=x-s->extreme_x, dir=sign(delta);
    if(!dir || (dir!=s->direction && abs(delta)<span)) return;
    if(s->direction && dir!=s->direction) s->turns++;
    s->direction=dir; s->extreme_x=x;
}
static void stroke_spark(pet_interactions_t *s,int x,int y,int64_t now)
{
    int dx=x-s->last_hair.x,dy=y-s->last_hair.y;
    if(dx*dx+dy*dy<20*20) return;
    if(s->hair_count==3) {
        memmove(s->hair,s->hair+1,2*sizeof(*s->hair));
        memmove(s->hair_at,s->hair_at+1,2*sizeof(*s->hair_at));s->hair_count--;
    }
    s->last_hair=(pet_fx_point_t){x,y};
    s->hair[s->hair_count]=s->last_hair;s->hair_at[s->hair_count++]=now;
}
static void start_book(pet_interactions_t *s,int64_t now)
{
    select_action(s,s->moon?"turnMoon":"turnStar",now);
    s->page_drag=true; s->page_frame=0;
    int pt[2]={},radius;
    const pet_clip_t *clip=pet_clips_lookup(s->art,s->action,base(s));
    pet_clip_prop(clip,0,pt,&radius);
    s->page_start_x=pt[0];
}
static void settle_page(pet_interactions_t *s,bool commit,bool glance,int64_t now)
{
    s->page_drag=false;
    s->page_commit=commit; s->page_glance=glance;
    s->page_from=s->page_frame;
    s->since=now;
}
enum { BROW_OFFER,BROW_WAIT,BROW_TOUCH,BROW_NUZZLE,BROW_REST,BROW_TIDY,BROW_AWAY,
       BROW_LOOK,BROW_LOOK_HOLD,BROW_LOOK_BACK,BROW_RETRACT,BROW_UNWIND };
static void brow_branch(pet_interactions_t *s,int branch,int64_t now)
{
    s->brow_branch=branch; s->brow_since=now;
}
static void invitation(pet_interactions_t *s,int64_t now)
{
    s->brow_offered=true; s->next_side=-s->next_side; s->brow_touch=0;
    select_action(s,side_name(s->next_side,"browInviteLeft","browInviteRight"),now);
    brow_branch(s,BROW_OFFER,s->since);
    event(s,s->action,now);
}
static void brow_press(pet_interactions_t *s,int side,int64_t now)
{
    if(s->brow_branch!=BROW_OFFER && s->brow_branch!=BROW_WAIT) return;
    s->brow_touch=side;
    if(s->brow_branch==BROW_WAIT) brow_branch(s,s->next_side==side?BROW_TOUCH:BROW_LOOK,now);
    event(s,"forehead",now);
}
static void brow_release(pet_interactions_t *s,int64_t now)
{
    if(!s->brow_touch) return;
    s->brow_touch=0;
    switch(s->brow_branch) {
        case BROW_OFFER: s->brow_from=s->shown.frame; brow_branch(s,BROW_RETRACT,now); break;
        case BROW_NUZZLE: case BROW_REST: s->brow_from=clamp((int)s->shown.frame-24,0,12); brow_branch(s,BROW_UNWIND,now); break;
        case BROW_TOUCH: brow_branch(s,BROW_AWAY,now); break;
        case BROW_LOOK_HOLD: brow_branch(s,BROW_LOOK_BACK,now); break;
        default: break;
    }
}
static int brow_frame(pet_interactions_t *s,int64_t now)
{
    int64_t age=now-s->brow_since; int step=(int)(age/62500);
    switch(s->brow_branch) {
        case BROW_OFFER:
            if(step<24) return step;
            brow_branch(s,!s->brow_touch ? BROW_WAIT : s->brow_touch==s->next_side ? BROW_TOUCH : BROW_LOOK,now); return 23;
        case BROW_WAIT: if(age>=MS(3700)) brow_branch(s,BROW_TIDY,now); return 23;
        case BROW_TOUCH: if(age>=MS(150)) brow_branch(s,BROW_NUZZLE,now); return 24;
        case BROW_NUZZLE: if(step>=13) brow_branch(s,BROW_REST,now); return 24+clamp(step,0,12);
        case BROW_REST: if(age>=MS(2400)) {s->brow_from=12; brow_branch(s,BROW_UNWIND,now);} return 36;
        case BROW_LOOK: if(step>=7) brow_branch(s,s->brow_touch?BROW_LOOK_HOLD:BROW_LOOK_BACK,now); return 65+clamp(step,0,6);
        case BROW_LOOK_HOLD: if(age>=MS(2400)) brow_branch(s,BROW_LOOK_BACK,now); return 71;
        case BROW_LOOK_BACK: if(step<5) return 72+step; break;
        case BROW_TIDY: if(step<18) return 37+step; break;
        case BROW_AWAY: if(step<10) return 55+step; break;
        case BROW_RETRACT: if(step<=s->brow_from) return s->brow_from-step; break;
        case BROW_UNWIND: if(step<=s->brow_from) return 24+s->brow_from-step; brow_branch(s,BROW_AWAY,now); return 55;
    }
    restore(s,now); s->brow_touch=0; return 0;
}

static const char *tool_action(const char *scene)
{
    if(is(scene,"running")) return "keyChime";
    if(is(scene,"searching")) return "lensFind";
    if(is(scene,"waiting")) return "palmTap";
    return NULL;
}
void pet_interactions_command(pet_interactions_t *s,pet_command_t cmd,int64_t now)
{
    interacted(s,now);
    reset_contact(s);
    if(asleep(s->action)) { wake(s,s->next_side,now); return; }
    switch(cmd) {
        case PET_CMD_TAIL: care(s,"tail",now);break;
        case PET_CMD_WING_LEFT: care(s,"wingLeft",now);break;
        case PET_CMD_WING_RIGHT: care(s,"wing",now);break;
        case PET_CMD_NOSE: care(s,"nose",now);break;
        case PET_CMD_FEED:
            if(is(s->action,"feed")) {
                int pt[2],radius;
                if(s->shown.clip && s->shown.frame>=33 && pet_clip_prop(s->shown.clip,s->shown.frame,pt,&radius) && radius>0) {
                    s->bubble_x=pt[0]; s->bubble_y=pt[1]; s->bubble_radius=radius;
                    care(s,"bubblePop",now);
                }
            } else if(!is(s->action,"bubblePop")) care(s,"feed",now);
            break;
        case PET_CMD_STAR:
            if(s->star_phase==PET_TOY_INVITE || s->star_phase==PET_TOY_RETURN) toy_finish(s,s->hand,now);
            else if(s->star_phase==PET_TOY_OFF || s->star_phase==PET_TOY_CAUGHT) {
                event(s,"play",now); toy_start(s,now);
            }
            break;
        case PET_CMD_INVITE:
            if(prefix(s->action,"browInvite")) {
                brow_press(s,s->next_side,now);
                s->assisted_until=(now>s->since+MS(1500)?now:s->since+MS(1500))+MS(1250);
            }
            else if(is(s->action,"idle") && now>=s->sulk_until) invitation(s,now);
            else care(s,"pat",now);
            break;
        case PET_CMD_NAP:
            if(prefix(s->action,"wake")) s->nap_stretch=true;
            else start_nap(s,now);
            break;
        case PET_CMD_SCENE:
            if(prefix(s->action,"wake") && is(s->base,"idle")) s->nap_stretch=true;
            else if(s->star_phase!=PET_TOY_OFF) { s->star_phase=PET_TOY_OFF; restore(s,now); }
            else if(tool_action(s->base)) care(s,tool_action(s->base),now);
            else if(is(s->base,"reading")) { start_book(s,now); settle_page(s,true,false,now); }
            else if(is(s->base,"idle")) start_nap(s,now);
            else { brush_begin(s,233,now); s->brush_auto=true; s->brush_stroked=true; event(s,"brush",now); }
            break;
    }
}
void pet_interactions_init(pet_interactions_t *s,const pet_clips_t *art,unsigned bond,int64_t now)
{
    *s=(pet_interactions_t){.art=art,.base="idle",.action="idle",.since=now,.last_tick=now,.last_visit=now,
        .quiet_since=now,.next_micro=now+MS(4200),.micro_seed=(unsigned)now+73,.bond=bond>100?100:bond,
        .last_nose=-MS(1000),.release_at=-1,.brush_release=-1,.next_side=1,.hand=1,
        .periodic_at=now+MS(2600),.energy=60,.curiosity=60,.affection=50};
    s->shown.action=s->shown.scene="idle";
    s->shown.clip=pet_clips_lookup(art,"idle","idle");
}
static bool transition_active(const pet_interactions_t *s,int64_t now)
{
    return s->move_count && now<s->since;
}
static pet_hit_t hit(pet_interactions_t *s,int x,int y,int *side)
{
    if(!s->shown.clip || prefix(s->shown.action,"stow-")) return PET_HIT_NONE;
    return pet_clip_hit(s->art,s->shown.clip,s->shown.frame,x,y,side);
}
static void down(pet_interactions_t *s,const baji_input_t *in,int64_t at)
{
    interacted(s,at);
    s->down=true; s->consumed=s->moved=false;
    s->contact_since=at;
    s->contact_x=in->press_x; s->contact_y=in->press_y;
    s->release_at=-1; s->pillow_release_depth=0;
    s->turns=s->direction=0; s->extreme_x=in->press_x;
    s->part=hit(s,in->press_x,in->press_y,&s->side);
    if(!s->side) s->side=in->press_x<233?-1:1;
    s->tool_contact=s->part==PET_HIT_KEYS || s->part==PET_HIT_LENS || s->part==PET_HIT_PALM || s->part==PET_HIT_BOOK;
    if(asleep(s->action)) {
        if(s->part==PET_HIT_PILLOW) s->pillow_held=true;
        else if(s->part!=PET_HIT_NONE) s->part=PET_HIT_BODY;
        /* A quick second call during the first peek transition is valid. */
        return;
    }
    if(transition_active(s,at)) { s->consumed=true; return; }
    if(s->star_phase==PET_TOY_INVITE || s->star_phase==PET_TOY_RETURN) {
        if(toy_down(s,in->press_x,in->press_y,at)) return;
        if(s->part==PET_HIT_NONE) return;
        s->star_phase=PET_TOY_OFF;
    }
    if(s->part==PET_HIT_BUBBLE && (!is(s->action,"feed") || s->shown.frame<33)) s->part=PET_HIT_NONE;
    switch(s->part) {
        case PET_HIT_CHEEK:
            s->held_cheek=true;
            select_action(s,side_name(s->side,"cheekLeft","cheekRight"),at); event(s,s->action,at);
            break;
        case PET_HIT_BOOK: if(is(s->base,"reading") && prefix(s->action,"reading")) start_book(s,at); break;
        case PET_HIT_HEAD:
            if(prefix(s->action,"browInvite")) {
                int a[10];pet_clip_anchors(s->shown.clip,s->shown.frame,a);
                if(abs((int)in->press_x-a[0])<=hypotf(a[0]-a[2],a[1]-a[3])/6) s->side=s->next_side;
                s->assisted_until=0;brow_press(s,s->side,at);
            }
            break;
        default: break;
    }
}
static void release_cheek(pet_interactions_t *s,int64_t now)
{
    if(!s->held_cheek) return;
    if(now-s->since>187500) s->since=now-187500;
    s->held_cheek=false;
}
static void drag_page(pet_interactions_t *s,int dx)
{
    const pet_clip_t *clip=pet_clips_lookup(s->art,s->action,base(s));
    int best=INT_MAX,desired=s->page_start_x+dx;
    for(int i=0;i<count(clip);i++) {
        int p[2],radius;
        if(!pet_clip_prop(clip,i,p,&radius)) continue;
        int d=abs(p[0]-desired);
        if(d<best) { best=d; s->page_frame=i; }
    }
}
static void input_tick(pet_interactions_t *s,const baji_input_t *in,int64_t now)
{
    if(in->presses!=s->presses) {
        if(s->down) { release_cheek(s,now); s->pillow_held=false; }
        s->presses=in->presses;
        down(s,in,in->press_us>0 && in->press_us<=now ? in->press_us : now);
    }
    if(!s->down) return;
    bool pressed=in->touch.pressed;
    int64_t at=!pressed && in->touch_us>=s->contact_since && in->touch_us<=now ? in->touch_us : now;
    int dx=(int)in->touch.x-in->press_x,dy=(int)in->touch.y-in->press_y;
    if(in->travel>7 || dx*dx+dy*dy>54) s->moved=true;
    if(!s->consumed) {
        if(s->star_phase==PET_TOY_HELD || s->star_phase==PET_TOY_HAND) {
            toy_touch(s,in->touch.x,in->touch.y,pressed,at);
            s->consumed=!pressed;
        } else if(s->pillow_held) {
            if(s->moved || !pressed) {
                s->pillow_release_depth=clamp((int)((at-s->contact_since)/MS(50))+1,1,3);
                s->pillow_held=false;
                if(!s->moved) event(s,"pillow",at);
                s->release_at=at; s->consumed=true;
            }
        } else if(s->held_cheek) {
            if(abs(dx)>40 || in->travel>40 || abs(dy)>26 || in->travel_y>26) {
                release_cheek(s,at); s->consumed=true;
            } else {
                stroke(s,in->touch.x,16);
                bool tickle=s->turns>=2;
                if(!pressed) {
                    bool tap=!s->moved && at-s->contact_since<MS(420);
                    release_cheek(s,at);
                    if(tap && third(at,MS(1100),s->tap_times,&s->tap_count)) tickle=true;
                }
                if(tickle) {
                    s->tap_count=0; s->held_cheek=false;
                    care(s,"tickle",at); s->consumed=true;
                }
            }
        } else if(s->part==PET_HIT_HEAD) {
            int side=0;
            bool left=s->moved && (abs(dx)>133 || abs(dy)>81 || in->travel>133 || in->travel_y>81 ||
                hit(s,in->touch.x,in->touch.y,&side)!=PET_HIT_HEAD);
            if(left) {
                if(is(s->action,"brush")) brush_end(s,at,false);
                if(s->brow_touch) brow_release(s,at);
                s->consumed=true;
            } else if(prefix(s->action,"browInvite") && !s->moved) {
                if(!pressed) brow_release(s,at);
            } else {
                if(!is(s->action,"brush") && (s->moved || at-s->contact_since>=MS(230)))
                    brush_begin(s,in->press_x,at);
                if(is(s->action,"brush")) {
                    stroke(s,in->touch.x,23);
                    if(s->moved) stroke_spark(s,in->touch.x,in->touch.y,at);
                    s->brush_pos=clamp(s->brush_origin+dx*4/54,-4,4);
                    if(s->moved && !s->brush_stroked) { s->brush_stroked=true;event(s,"brush",at); }
                    if(!pressed) brush_end(s,at,true);
                } else if(!pressed) care(s,side_name(s->side,"patLeft","pat"),at);
            }
        } else if(s->page_drag) {
            if(dx>24 || abs(dy)>32 || in->travel_y>32) {
                settle_page(s,false,false,at); s->consumed=true;
            } else {
                drag_page(s,dx);
                if(!pressed) {
                    const pet_clip_t *clip=pet_clips_lookup(s->art,s->action,base(s));
                    settle_page(s,!s->moved || s->page_frame*100>=58*(count(clip)-1),true,at);
                }
            }
        } else if(!pressed && !s->moved) {
            if(asleep(s->action) && s->part==PET_HIT_BODY) wake(s,s->side,at);
            else {
                static const char *const acts[]={
                    [PET_HIT_NOSE]="nose",[PET_HIT_WING_LEFT]="wingLeft",[PET_HIT_WING_RIGHT]="wing",
                    [PET_HIT_TAIL]="tail",[PET_HIT_BUBBLE]="bubblePop",[PET_HIT_KEYS]="keyChime",
                    [PET_HIT_LENS]="lensFind",[PET_HIT_PALM]="palmTap"};
                int side=0;
                if((unsigned)s->part<sizeof(acts)/sizeof(*acts) && acts[s->part] && hit(s,in->touch.x,in->touch.y,&side)==s->part) {
                    const char *name=acts[s->part];
                    if(s->part==PET_HIT_BUBBLE) {
                        int pt[2],r=0;
                        if(pet_clip_prop(s->shown.clip,s->shown.frame,pt,&r)) {
                            s->bubble_x=pt[0];s->bubble_y=pt[1];s->bubble_radius=r;
                        }
                    }
                    if(!(is(name,tool_action(s->base)) && is(s->action,name))) care(s,name,at);
                }
            }
        }
    }
    if(!s->pillow_held && !s->pillow_release_depth) { s->contact_x=in->touch.x; s->contact_y=in->touch.y; }
    if(!pressed) {
        s->down=false; s->quiet_since=at;
        if(s->release_at<0) s->release_at=at;
    }
}

static unsigned random_value(pet_interactions_t *s,unsigned n)
{
    s->micro_seed=s->micro_seed*1664525u+1013904223u;
    return (s->micro_seed>>8)%n;
}
static void micro_action(pet_interactions_t *s,int64_t now,int64_t quiet,int64_t available)
{
    static const char *const names[]={"idleLookLeft","idleLookRight","idleGroomLeft","idleGroomRight",
        "idleStretch","idleHum","idleYawn","tail","wing","wingLeft","nose","sneeze","heartHug"};
    unsigned level=(s->bond>=10)+(s->bond>=25)+(s->bond>=50)+(s->bond>=75);
    unsigned look=260+s->curiosity*14/10+level*25,groom=130+s->curiosity*7/10;
    unsigned weights[]={look,look,groom,groom,quiet>MS(9000)?100+(100-s->energy)*11/10:0,
        s->bond>=10?80+s->affection*12/10+level*30:0,quiet>MS(14000)?60+(100-s->energy)*13/10:0,
        150+s->energy,150+s->energy*9/10,150+s->energy*9/10,90,30,
        s->bond>=25?25+(level-1)*40+s->affection*6/10:0};
    unsigned total=0;
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++) {
        const pet_clip_t *c=pet_clips_lookup(s->art,names[i],"idle");
        if(!c || is(s->last_micro,names[i]) || (int64_t)count(c)*1000000/rate(c)+MS(180)>available) weights[i]=0;
        total+=weights[i];
    }
    if(!total) { s->next_micro=now+available; return; }
    unsigned roll=random_value(s,total);
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++) {
        if(roll>=weights[i]) { roll-=weights[i]; continue; }
        select_action(s,names[i],now);
        s->micro=true; s->last_micro=names[i];
        const pet_clip_t *clip=pet_clips_lookup(s->art,names[i],"idle");
        s->next_micro=now+(int64_t)count(clip)*1000000/rate(clip)+MS(2400+random_value(s,1800));
        s->mark_since=now; s->mark_action=names[i];
        s->mark=is(names[i],"idleHum")?PET_FX_NOTE : is(names[i],"idleYawn")?PET_FX_ELLIPSIS : is(names[i],"nose")?PET_FX_QUESTION :
            (is(names[i],"tail") || prefix(names[i],"wing"))?PET_FX_SPARKLE : PET_FX_NONE;
        break;
    }
}
static void autonomous(pet_interactions_t *s,int64_t now)
{
    if(s->down || !is(s->action,base(s)) || transition_active(s,now)) return;
    if(now>=s->periodic_at) {
        unsigned chance=is(s->base,"error")?28:is(s->base,"reading")?22:is(s->base,"searching")?30:
            is(s->base,"succeeded")?35:is(s->base,"working")?45:0;
        if(chance && random_value(s,100)<chance) {
            s->mark=is(s->base,"error")?PET_FX_SWEAT:is(s->base,"working")?PET_FX_QUESTION:PET_FX_SPARKLE;
            s->mark_since=now; s->mark_action=s->base;
        }
        s->periodic_at=now+MS(2600);
    }
    if(!is(s->base,"idle") || now<s->sulk_until || now<s->reunion_until) return;
    int64_t quiet=now-s->quiet_since,next=s->rematch?MS(11500):!s->brow_offered?MS(7000):MS(28000);
    if(quiet>=MS(28000)) start_nap(s,now);
    else if(quiet>=next) {
        if(s->rematch) toy_start(s,now);
        else invitation(s,now);
    } else if(now>=s->next_micro && quiet>=MS(2600)) micro_action(s,now,quiet,next-quiet);
}
static int hold_from(const char *action)
{
    if(is(action,"writing")) return 62;
    if(is(action,"compact")) return 32;
    if(is(action,"error")) return 31;
    if(is(action,"limited")) return 23;
    if(is(action,"succeeded")) return 35;
    if(is(action,"ended")) return 28;
    if(is(action,"cancelled")) return 26;
    return 0;
}
static int frame_for(pet_interactions_t *s,const pet_clip_t *clip,int64_t now)
{
    int64_t age=now-s->since+s->pose_offset;
    int frames=count(clip),fps=rate(clip);
    int64_t step=age*fps/1000000;
    if(is(s->action,base(s))) {
        int last=frames-1;
        if(!last) return 0;
        if(is(s->action,"idle")) {int f=step%(2*last);return f<=last?f:2*last-f;}
        /* The last drawing of these web loops duplicates their seam. */
        int hold=clamp(hold_from(s->action),0,last-1);
        return step<hold?(int)step:hold+(int)((step-hold)%(last-hold));
    }
    if(is(s->action,"doze")) {
        int64_t f=s->sleep_offset+age/MS(100);
        return f<15?(int)f:15+(int)((f-15)%20);
    }
    if(prefix(s->action,"peek")) {
        if(age>=MS(3100)) { s->sleep_offset=15;select_action(s,"doze",now);return 0; }
        return (int)lroundf((frames-1)*fminf(1,fminf((float)age/MS(460),(float)(MS(3100)-age)/MS(480))));
    }
    if(is(s->action,"starWatch")) return toy_frame(s,now);
    if(prefix(s->action,"browInvite")) {
        if(s->assisted_until && now>=s->assisted_until) { brow_release(s,now);s->assisted_until=0; }
        return brow_frame(s,now);
    }
    if(is(s->action,"brush")) {
        if(s->brush_auto && age>=MS(720)) brush_end(s,now,false);
        bool released=s->brush_release>=0;
        int64_t relax=released?now-s->brush_release:0;
        if(released && relax>=MS(420)) {
            if(s->brush_reward) care(s,"heartHug",now);
            else restore(s,now);
            return 0;
        }
        int target=s->brush_stroked?4:2;
        s->brush_depth=released && relax>=MS(160)?clamp(target-(int)((relax-MS(160))/MS(55))-1,0,target):
            clamp(1+(int)(age/MS(55)),1,target);
        return s->brush_depth*9+4+(released && relax>=MS(160)?s->brush_pos*(4-clamp((int)((relax-MS(160))/MS(55)),0,4))/4:s->brush_pos);
    }
    if(prefix(s->action,"turn")) {
        if(s->page_drag) return s->page_frame;
        int from=s->page_from;
        int f=from+(s->page_commit?(int)step:-(int)(age*fps*3/2000000));
        if((s->page_commit && f>=frames-1) || (!s->page_commit && f<=0)) {
            if(s->page_commit) {s->moon=!s->moon;event(s,"page",now);}
            if(!s->page_commit && s->page_glance && from>0) select_action(s,s->moon?"glanceMoon":"glanceStar",now);
            else restore(s,now);
        }
        return clamp(f,0,frames-1);
    }
    if(prefix(s->action,"cheek") && s->held_cheek && age>=187500) return 3;
    if(step<frames) return (int)step;
    if(prefix(s->action,"starCatch")) {
        s->star_phase=PET_TOY_OFF;
        s->quiet_since=now;
    }
    /* A timer may wake late. Finishing the full wake strip has already put
     * the pillow away, regardless of the last submitted frame. */
    if(prefix(s->action,"wake")) s->shown.frame=frames-1;
    if(prefix(s->action,"wake") && s->nap_stretch) {s->nap_stretch=false;start_nap(s,now);}
    else {
        if(!s->micro) {s->quiet_since=now;s->next_micro=now+MS(3000);}
        restore(s,now);
    }
    return 0;
}
static void sample(pet_interactions_t *s,int64_t now,pet_interaction_view_t *v)
{
    for(unsigned attempt=0;attempt<3;attempt++) {
        if(transition_active(s,now)) {
            int64_t age=now-s->move_since;
            for(unsigned i=0;i<s->move_count;i++) {
                pet_transition_t m=s->moves[i];
                if(age>=span_us(m)) {age-=span_us(m);continue;}
                v->action=m.name;v->scene="idle";v->clip=pet_clips_lookup(s->art,m.name,"idle");
                v->frame=clamp(m.first+sign(m.last-m.first)*(int)(age/m.step_us),0,count(v->clip)-1);
                v->elapsed_us=age;v->next_us=now+(m.step_us-age%m.step_us);return;
            }
        }
        s->move_count=0;
        const char *action=s->action;
        v->action=action; v->scene=base(s);
        if(is(action,"idle")) {
            if(now<s->sulk_until) {
                int64_t t=MS(6500)-(s->sulk_until-now);
                v->action=((t>MS(2600) && t<MS(3600)) || (t>MS(4800) && t<MS(5700)))?"idleSulkPeek":"idleSulky";
            } else if(now<s->reunion_until) v->action="idleReunion";
            else if(s->bond>=50) v->action="idleBond";
        }
        v->clip=is(v->action,s->shown.action) && is(v->scene,s->shown.scene)?s->shown.clip:
            pet_clips_lookup(s->art,v->action,v->scene);
        if(!v->clip && !is(v->action,action)) {v->action=action;v->clip=pet_clips_lookup(s->art,action,v->scene);}
        int f=frame_for(s,v->clip,now);
        if(!is(s->action,action)) continue;
        v->frame=clamp(f,0,count(v->clip)-1);v->elapsed_us=now-s->since+s->pose_offset;
        int64_t hz=rate(v->clip);
        if(is(v->scene,"idle") && (is(v->action,"idle") || is(v->action,"pat") || prefix(v->action,"cheek") || is(v->action,"tickle"))) hz=48;
        if(is(action,"brush") || prefix(action,"peek") || is(action,"starWatch") || prefix(action,"turn")) hz=40;
        int64_t step=v->elapsed_us*hz/1000000;
        v->next_us=s->since-s->pose_offset+((step+1)*1000000+hz-1)/hz;
        if(s->held_cheek && v->elapsed_us>=187500) {v->elapsed_us=187500;v->next_us=INT64_MAX;}
        return;
    }
}
static void due(pet_interaction_view_t *v,int64_t deadline)
{
    if(deadline<v->next_us) v->next_us=deadline;
}
static void effects(pet_interactions_t *s,int64_t now,pet_interaction_view_t *v)
{
    int a[10]={233,155,233,260,95,253,365,253,340,356},p[4]={};
    if(v->clip) pet_clip_anchors(v->clip,v->frame,a);
    bool hands=v->clip && pet_clip_palms(v->clip,v->frame,p);
    pet_effects_t *fx=&v->fx;
    if(is(v->action,"brush")) for(unsigned i=0;i<s->hair_count;i++) {
        int64_t age=now-s->hair_at[i];
        if(age>=0 && age<MS(380)) {
            unsigned slot=fx->hair_count++;
            fx->hair[slot]=s->hair[i];fx->hair_age_ms[slot]=age/1000;
        }
    }
    v->star_phase=s->star_phase;v->hand=s->hand;v->glow=s->glow;
    v->sulking=now<s->sulk_until;
    if(s->star_phase!=PET_TOY_OFF && (is(v->action,"starWatch") || prefix(v->action,"starCatch"))) {
        fx->star=s->star_phase==PET_TOY_CAUGHT?PET_STAR_CAUGHT:
            s->star_phase==PET_TOY_DEPART && now-s->star_since>MS(380)?PET_STAR_DEPART:
            (s->star_phase==PET_TOY_HELD || s->star_phase==PET_TOY_HAND)?PET_STAR_HELD:PET_STAR_FLY;
        if(hands) {
            fx->palms[0]=(pet_fx_point_t){p[0],p[1]};fx->palms[1]=(pet_fx_point_t){p[2],p[3]};
            if(fx->star==PET_STAR_CAUGHT) {
                int i=s->hand<0?0:2;
                if(v->frame>=20) pet_clip_palms(v->clip,20,p);
                s->star_x=p[i];s->star_y=p[i+1];
            }
        }
        fx->wing=(v->elapsed_us/MS(95))&1;fx->glow=s->glow;
        fx->catch_frame=fx->star==PET_STAR_CAUGHT?v->frame:0;
        fx->star_at=(pet_fx_point_t){s->star_x,s->star_y};
        for(unsigned i=0;i<s->trail_count;i++) if(s->star_phase==PET_TOY_HELD && now-s->trail_at[i]<MS(160))
            fx->trail[fx->trail_count++]=s->trail[i];
    }
    v->star_x=s->star_x;v->star_y=s->star_y;
    if(s->mark!=PET_FX_NONE && now-s->mark_since<MS(900)) {
        fx->mark=s->mark;fx->mark_step=clamp((int)((now-s->mark_since)/MS(80)),0,11);
        fx->mark_at=(pet_fx_point_t){a[0],a[1]-13};
        if(prefix(s->mark_action,"wing")) {
            int i=is(s->mark_action,"wingLeft")?4:6;fx->mark_at=(pet_fx_point_t){a[i],a[i+1]};
        } else if(is(s->mark_action,"tail")) fx->mark_at=(pet_fx_point_t){a[8],a[9]};
        else if(is(s->mark_action,"nose")) fx->mark_at=(pet_fx_point_t){a[2],a[3]-9};
        else if(prefix(s->mark_action,"cheek")) fx->mark_at=(pet_fx_point_t){a[2]+s->side*40,a[3]};
        else if(prefix(s->mark_action,"starCatch") && hands) fx->mark_at=fx->palms[s->hand>0];
        else if(is(s->mark_action,"reading") || is(s->mark_action,"searching")) {
            int pt[2],r;
            if(v->clip && pet_clip_prop(v->clip,v->frame,pt,&r)) fx->mark_at=(pet_fx_point_t){pt[0],pt[1]};
        }
        due(v,s->mark_since+(fx->mark_step+1)*MS(80));
    }
    if(is(v->action,"bubblePop") && s->bubble_radius>0 && v->frame<=19) {
        fx->burst=true;fx->burst_frame=v->frame;fx->bubble=(pet_fx_point_t){s->bubble_x,s->bubble_y};
        fx->nose=(pet_fx_point_t){a[2],a[3]};fx->bubble_radius=s->bubble_radius;
    }
    if(s->pillow_held || s->pillow_release_depth) {
        int depth=0;
        if(s->pillow_held) depth=clamp((int)((now-s->contact_since)/MS(50))+1,1,3);
        else {
            int64_t age=now-s->release_at;
            depth=clamp(s->pillow_release_depth-(int)(age/MS(50)),0,3);
            if(!depth) s->pillow_release_depth=0;
        }
        if(asleep(v->action)) {
            fx->pillow_at=(pet_fx_point_t){s->contact_x,s->contact_y};v->pillow_depth=fx->pillow_depth=depth;
            if((s->pillow_held && depth<3) || (!s->pillow_held && depth)) due(v,now+MS(30));
        }
    }
    if(s->part!=PET_HIT_NONE && !s->pillow_held && (s->down || (s->release_at>=0 && now-s->release_at<MS(180)))) {
        fx->contact=s->down?1:2;
        fx->contact_step=clamp((int)((now-(s->down?s->contact_since:s->release_at))/MS(55)),0,3);
        fx->finger=(pet_fx_point_t){s->contact_x,s->contact_y};fx->tool=s->tool_contact;
        if(fx->contact_step<3 || !s->down) due(v,now+MS(55));
    }
    v->contact_x=s->contact_x;v->contact_y=s->contact_y;
}
void pet_interactions_new_session(pet_interactions_t *s)
{
    s->base=NULL;
}

void pet_interactions_tick(pet_interactions_t *s,const baji_input_t *input,baji_agent_cue_t cue,
                           int64_t now,pet_interaction_view_t *view)
{
    const char *scene=cue_name(cue);
    if(!is(s->base,scene)) {
        s->saved_scene=NULL;s->base=scene; s->moon=false; s->star_phase=PET_TOY_OFF; s->brow_offered=false;
        s->rematch=s->nap_stretch=false; s->sulk_until=0;s->tail_count=s->tap_count=0;
        s->assisted_until=0;s->pillow_release_depth=0;s->mark=PET_FX_NONE;
        s->quiet_since=now;s->next_micro=now+MS(4200);s->periodic_at=now+MS(2600);
        reset_contact(s); mood(s,scene);select_action(s,scene,now);
    }
    s->cue=cue;
    if(input) input_tick(s,input,now);
    autonomous(s,now);
    pet_interaction_view_t v={.next_us=INT64_MAX};
    sample(s,now,&v);
    effects(s,now,&v);
    if(s->down && s->part==PET_HIT_HEAD && !s->consumed && now<s->contact_since+MS(230))
        due(&v,s->contact_since+MS(230));
    v.event=s->event;v.bond_delta=s->event_bond;s->event=NULL;s->event_bond=0;
    s->shown=v;s->last_tick=now;*view=v;
}
