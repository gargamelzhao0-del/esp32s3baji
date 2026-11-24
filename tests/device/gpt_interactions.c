/* Opt-in board build. Deliver physical-screen samples through the normal menu,
 * renderer and panel. Preserve this badge's saved bond and sound preference. */
#include "baji_runtime.h"
#include "pet_interactions.h"
#include "pet_feedback.h"
#include "esp_log.h"
#include <string.h>

pet_feedback_t *__real_pet_feedback_open(baji_board_t *,unsigned *,bool *);
void __real_pet_feedback_update(pet_feedback_t *,const char *,unsigned,bool);
static unsigned saved_bond;
static bool saved_sound;
pet_feedback_t *__wrap_pet_feedback_open(baji_board_t *board,unsigned *bond,bool *sound)
{
    pet_feedback_t *out=__real_pet_feedback_open(board,bond,sound);
    saved_bond=*bond;
    saved_sound=*sound;
    return out;
}
void __wrap_pet_feedback_update(pet_feedback_t *f,const char *event,unsigned bond,bool sound)
{
    (void)bond; (void)sound;
    __real_pet_feedback_update(f,event,saved_bond,saved_sound);
}

static pet_interaction_view_t visible;
static const pet_clips_t *art;
static bool indented, released;
void __real_pet_interactions_tick(pet_interactions_t *,const baji_input_t *,baji_agent_cue_t,int64_t,pet_interaction_view_t *);
void __wrap_pet_interactions_tick(pet_interactions_t *s,const baji_input_t *i,baji_agent_cue_t c,int64_t t,pet_interaction_view_t *v)
{
    __real_pet_interactions_tick(s,i,c,t,v);
    visible=*v;
    art=s->art;
    if(v->pillow_depth>=2) indented=true;
    if(v->event && !strcmp(v->event,"pillow")) released=true;
}

enum target { MENU, NAP, CLOTH, FACE, FEED, BUBBLE, STAR, HAND, WORK, READ, BOOK,
              IDLE_JOB, PARTS, TAIL, NOSE, OBSERVE };
static const struct {enum target target; const char *action; unsigned frame;} steps[]={
    {MENU,NULL,0},{NAP,NULL,0},{CLOTH,"doze",16},{FACE,"doze",16},{FACE,"peek",1},
    {OBSERVE,"wake",6},{MENU,"idle",0},{FEED,NULL,0},{BUBBLE,"feed",33},{OBSERVE,"bubblePop",2},
    {MENU,NULL,0},{STAR,NULL,0},{HAND,"starWatch",0},{OBSERVE,"starCatch",2},
    {MENU,NULL,0},{WORK,NULL,0},{READ,NULL,0},{BOOK,"reading",2},{OBSERVE,"turn",2},
    {MENU,NULL,0},{WORK,NULL,0},{IDLE_JOB,NULL,0},{MENU,"idle",0},{PARTS,NULL,0},
    {TAIL,NULL,0},{TAIL,NULL,0},{TAIL,NULL,0},{OBSERVE,"idleSulky",0},
    {NOSE,NULL,0},{NOSE,NULL,0},{OBSERVE,"sneeze",1},
};
static bool target_point(enum target target,int *x,int *y)
{
    *x=233; *y=446;
    switch(target) {
    case MENU: return true;
    case NAP: *x=306;*y=201;return true;
    case FEED: *x=306;*y=141;return true;
    case STAR: case READ: *x=160;*y=201;return true;
    case WORK: *x=160;*y=261;return true;
    case IDLE_JOB: *x=160;*y=141;return true;
    case PARTS: *x=306;*y=320;return true;
    case TAIL: *x=160;*y=380;return true;
    case NOSE: *x=306;*y=380;return true;
    case FACE: {
        int axes[10];
        pet_clip_anchors(visible.clip,visible.frame,axes);
        *x=axes[2];*y=axes[3];return true;
    }
    case HAND: {
        int hands[4];
        if(!pet_clip_palms(visible.clip,visible.frame,hands)) return false;
        *x=hands[2];*y=hands[3];return true;
    }
    case BOOK: case BUBBLE: {
        int p[2],r;
        if(!pet_clip_prop(visible.clip,visible.frame,p,&r)) return false;
        *x=p[0];*y=p[1];return true;
    }
    case CLOTH: {
        baji_rect_t r;
        if(!pet_clip_pillow(visible.clip,visible.frame,&r)) return false;
        for(int j=r.y+r.height/2;j<r.y+r.height;j+=2)
            for(int i=r.x+r.width/2;i<r.x+r.width;i+=2)
                if(pet_clip_pillow_contains(art,visible.clip,visible.frame,i,j)) { *x=i;*y=j;return true; }
        return false;
    }
    default: return false;
    }
}
static baji_frame_fn render;
static void *owner;
static baji_input_t finger;
static unsigned step;
static int64_t started, earliest;
static bool finished;
static esp_err_t replay(void *ctx,const baji_input_t *real,int64_t now,int64_t *due)
{
    (void)ctx;
    if (!started) {started=now;earliest=now+1500000;}
    if (!finished && now>=earliest) {
        if (finger.touch.pressed) {
            finger.touch.pressed=false;finger.touch_us=now;
            step++;started=now;earliest=now+160000;
        } else if (step==sizeof(steps)/sizeof(steps[0])) {
            ESP_LOGI("pet_probe","complete steps=%u pillow_press=%d pillow_release=%d",step,indented,released);
            finished=true;
        } else if (now-started>12000000) {
            ESP_LOGE("pet_probe","timeout step=%u visible=%s frame=%u",step,visible.action?visible.action:"?",visible.frame);
            finished=true;
        } else if (visible.clip && (!steps[step].action ||
                   (!strncmp(visible.action,steps[step].action,strlen(steps[step].action)) && visible.frame>=steps[step].frame))) {
            int x,y;
            if (steps[step].target==OBSERVE) {
                ESP_LOGI("pet_probe","observed step=%u action=%s",step,visible.action);
                step++;started=now;earliest=now+80000;
            } else if (target_point(steps[step].target,&x,&y)) {
                ESP_LOGI("pet_probe","press step=%u action=%s at=%d,%d",step,visible.action,x,y);
                finger.presses++;finger.touch=(baji_touch_sample_t){true,x,y};
                finger.touch_us=finger.press_us=now;finger.press_x=x;finger.press_y=y;
                earliest=now+(steps[step].target==CLOTH?330000:85000);
            }
        }
    }
    finger.power=real->power;
    esp_err_t err=render(owner,&finger,now,due);
    if(!finished && *due>now+25000) *due=now+25000;
    return err;
}
esp_err_t __real_baji_runtime_run(baji_board_t *,const baji_board_info_t *,const baji_runtime_config_t *,baji_frame_fn,void *);
esp_err_t __wrap_baji_runtime_run(baji_board_t *b,const baji_board_info_t *info,const baji_runtime_config_t *cfg,baji_frame_fn fn,void *c)
{
    render=fn;owner=c;
    return __real_baji_runtime_run(b,info,cfg,replay,NULL);
}
