#include "pet_interactions.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int64_t now;
    baji_agent_cue_t cue;
    baji_input_t input;
    pet_interactions_t s;
    pet_interaction_view_t v;
} replay_t;
typedef struct {int x,y;} point_t;
static pet_clips_t *art;
static int checks;
static void tick(replay_t *r,int ms)
{
    r->now+=(int64_t)ms*1000;
    pet_interactions_tick(&r->s,&r->input,r->cue,r->now,&r->v);
    assert(r->v.clip && r->v.frame<pet_clip_frames(r->v.clip) && r->v.next_us>r->now);
}
static void init(replay_t *r,unsigned bond)
{
    *r=(replay_t){.now=1000000,.cue=BAJI_CUE_LOCAL};
    pet_interactions_init(&r->s,art,bond,r->now);
    tick(r,0);
}
static void action(const replay_t *r,const char *name)
{
    if(strcmp(r->v.action,name)) {
        fprintf(stderr,"expected %s, observed %s (intent %s, frame %u)\n",name,r->v.action,r->s.action,r->v.frame);
        abort();
    }
    checks++;
}
static void cmd(replay_t *r,pet_command_t command)
{
    pet_interactions_command(&r->s,command,r->now);
    tick(r,0);
}
/* Choose the centre of the requested painted target, separately for each pose. */
static point_t point(const replay_t *r,pet_hit_t part,int which)
{
    int64_t sx=0,sy=0;int n=0;
    for(int y=7;y<460;y+=4) for(int x=7;x<460;x+=4) {
        if((x-233)*(x-233)+(y-233)*(y-233)>=232*232) continue;
        int side=0;
        if(pet_clip_hit(art,r->v.clip,r->v.frame,x,y,&side)==part && (!which || side==which)) {
            sx+=x;sy+=y;n++;
        }
    }
    if(!n) {fprintf(stderr,"no target %d in %s %u\n",part,r->v.action,r->v.frame);abort();}
    point_t best={0,0};int score=1000000;
    int cx=(int)(sx/n),cy=(int)(sy/n);
    for(int y=7;y<460;y+=4) for(int x=7;x<460;x+=4) {
        if((x-233)*(x-233)+(y-233)*(y-233)>=232*232) continue;
        int side=0,d=(x-cx)*(x-cx)+(y-cy)*(y-cy);
        if(d<score && pet_clip_hit(art,r->v.clip,r->v.frame,x,y,&side)==part && (!which || side==which)) {
            best=(point_t){x,y};score=d;
        }
    }
    return best;
}
static void press(replay_t *r,point_t p)
{
    r->input.presses++;r->input.press_x=p.x;r->input.press_y=p.y;
    r->input.press_us=r->input.touch_us=r->now;
    r->input.travel=r->input.travel_y=0;
    r->input.touch=(baji_touch_sample_t){true,p.x,p.y};
    tick(r,0);
}
static void finger(replay_t *r,point_t p,int ms,bool pressed)
{
    int dx=abs(p.x-r->input.press_x),dy=abs(p.y-r->input.press_y);
    if(dx>r->input.travel) r->input.travel=dx;
    if(dy>r->input.travel) r->input.travel=dy;
    if(dy>r->input.travel_y) r->input.travel_y=dy;
    r->input.touch=(baji_touch_sample_t){pressed,p.x,p.y};
    r->input.touch_us=r->now+(int64_t)ms*1000;tick(r,ms);
}
static void tap(replay_t *r,pet_hit_t part,int side)
{
    point_t p=point(r,part,side);
    press(r,p);finger(r,p,30,false);
}
static void job(replay_t *r,baji_agent_cue_t cue,const char *name)
{
    r->cue=cue;tick(r,0);tick(r,1500);action(r,name);
}
static void naps(void)
{
    replay_t r;init(&r,0);cmd(&r,PET_CMD_NAP);action(&r,"doze");
    tick(&r,2100);assert(r.v.frame==21);
    point_t cloth=point(&r,PET_HIT_PILLOW,0);
    press(&r,cloth);tick(&r,150);assert(r.v.pillow_depth==3);
    finger(&r,cloth,5,false);assert(r.v.event && !strcmp(r.v.event,"pillow") && r.v.bond_delta==0);
    tick(&r,200);assert(r.v.pillow_depth==0);action(&r,"doze");
    tap(&r,PET_HIT_HEAD,-1);tick(&r,900);action(&r,"peekLeft");
    tick(&r,3200);action(&r,"doze");
    tap(&r,PET_HIT_HEAD,1);tick(&r,880);action(&r,"peekRight");
    tap(&r,PET_HIT_HEAD,1);tick(&r,900);action(&r,"wakeRight");
    tick(&r,2800);action(&r,"idle");
    cmd(&r,PET_CMD_NAP);tick(&r,2200);job(&r,BAJI_CUE_RUN,"running");
    assert(!r.s.pillow_held);
    init(&r,0);tick(&r,28001);action(&r,"doze");tick(&r,3500);
    tick(&r,900001);tap(&r,PET_HIT_HEAD,-1);assert(r.s.reunion_until>r.now);tick(&r,900);action(&r,"peekLeft");
}
static void cares(void)
{
    replay_t r;init(&r,0);tap(&r,PET_HIT_NOSE,0);action(&r,"nose");
    tick(&r,90);tap(&r,PET_HIT_NOSE,0);action(&r,"sneeze");
    init(&r,0);tap(&r,PET_HIT_HEAD,-1);action(&r,"patLeft");
    init(&r,0);tap(&r,PET_HIT_HEAD,1);action(&r,"pat");
    init(&r,0);tap(&r,PET_HIT_WING_LEFT,0);action(&r,"wingLeft");
    tap(&r,PET_HIT_WING_RIGHT,0);action(&r,"wing");
    init(&r,0);
    for(int i=0;i<3;i++) {tap(&r,PET_HIT_TAIL,0);if(i<2) {action(&r,"tail");tick(&r,65);}}
    assert(r.v.sulking && r.v.event && !strcmp(r.v.event,"sulk"));tick(&r,2700);
    if(pet_clips_lookup(art,"idleSulkPeek",NULL)) action(&r,"idleSulkPeek");
    cmd(&r,PET_CMD_FEED);action(&r,"feed");assert(!r.v.sulking && !strcmp(r.v.event,"reconcile") && r.v.bond_delta==4);
    cmd(&r,PET_CMD_FEED);action(&r,"feed");tick(&r,4500);tap(&r,PET_HIT_BUBBLE,0);action(&r,"bubblePop");assert(r.v.fx.burst);
    init(&r,0);point_t cheek=point(&r,PET_HIT_CHEEK,-1);press(&r,cheek);
    tick(&r,1900);action(&r,"cheekLeft");assert(r.v.frame==3);
    finger(&r,cheek,10,false);tick(&r,1200);action(&r,"idle");
    for(int i=0;i<3;i++) tap(&r,PET_HIT_CHEEK,i%2?1:-1);
    action(&r,"tickle");
    init(&r,0);cheek=point(&r,PET_HIT_CHEEK,1);press(&r,cheek);
    finger(&r,(point_t){cheek.x+18,cheek.y},80,true);finger(&r,(point_t){cheek.x-18,cheek.y},80,true);
    finger(&r,(point_t){cheek.x+18,cheek.y},80,true);action(&r,"tickle");
    init(&r,0);point_t nose=point(&r,PET_HIT_NOSE,0);
    r.input=(baji_input_t){.presses=1,.press_x=nose.x,.press_y=nose.y,.press_us=r.now,.touch_us=r.now+30000,
        .touch={false,nose.x,nose.y}};
    tick(&r,40);action(&r,"nose");assert(r.v.bond_delta==1);tick(&r,40);assert(r.v.bond_delta==0);
    init(&r,0);point_t head=point(&r,PET_HIT_HEAD,0);press(&r,head);tick(&r,250);action(&r,"brush");
    const int seq[]={26,-26,26};
    for(unsigned i=0;i<3;i++) finger(&r,(point_t){head.x+seq[i],head.y},80,true);
    assert(r.v.fx.hair_count>0);
    finger(&r,(point_t){head.x+26,head.y},20,false);tick(&r,430);action(&r,"heartHug");
    init(&r,0);cmd(&r,PET_CMD_WING_LEFT);action(&r,"wingLeft");cmd(&r,PET_CMD_WING_RIGHT);action(&r,"wing");
    cmd(&r,PET_CMD_NOSE);action(&r,"nose");tick(&r,250);cmd(&r,PET_CMD_NOSE);action(&r,"sneeze");
    cmd(&r,PET_CMD_TAIL);action(&r,"tail");tick(&r,180);cmd(&r,PET_CMD_TAIL);tick(&r,180);
    cmd(&r,PET_CMD_TAIL);assert(r.v.sulking && !strcmp(r.v.event,"sulk"));
    init(&r,0);cmd(&r,PET_CMD_INVITE);action(&r,"browInviteLeft");tick(&r,1550);
    head=point(&r,PET_HIT_HEAD,1);press(&r,head);tick(&r,170);assert(r.v.frame>=65 && r.v.frame<=71);
    finger(&r,head,50,false);tick(&r,700);tick(&r,500);action(&r,"idle");
    cmd(&r,PET_CMD_INVITE);cmd(&r,PET_CMD_INVITE);tick(&r,1550);tick(&r,200);tick(&r,250);
    assert(r.v.frame>=24 && r.v.frame<=36);tick(&r,1100);assert(!r.s.brow_touch);
}
static void work(void)
{
    replay_t r;init(&r,0);
    static const struct {baji_agent_cue_t cue;const char *name;} cases[]={
        {BAJI_CUE_THINK,"working"},{BAJI_CUE_READ,"reading"},{BAJI_CUE_WRITE,"writing"},
        {BAJI_CUE_SEARCH,"searching"},{BAJI_CUE_WAIT,"waiting"},{BAJI_CUE_SUCCESS,"succeeded"},
        {BAJI_CUE_FAILED,"error"},{BAJI_CUE_LIMIT,"limited"},{BAJI_CUE_COMPACT,"compact"},
        {BAJI_CUE_DONE,"ended"},{BAJI_CUE_CANCELLED,"cancelled"},{BAJI_CUE_OFFLINE,"idle"}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++) job(&r,cases[i].cue,cases[i].name);
    job(&r,BAJI_CUE_READ,"reading");tap(&r,PET_HIT_BOOK,0);action(&r,"turnStar");
    tick(&r,1700);action(&r,"readingMoon");assert(!strcmp(r.v.event,"page"));
    cmd(&r,PET_CMD_SCENE);action(&r,"turnMoon");tick(&r,1700);action(&r,"reading");
    point_t book=point(&r,PET_HIT_BOOK,0);press(&r,book);finger(&r,(point_t){book.x-18,book.y},80,true);
    finger(&r,(point_t){book.x-18,book.y},50,false);tick(&r,800);action(&r,"glanceStar");
    tick(&r,1100);action(&r,"reading");
    point_t cheek=point(&r,PET_HIT_CHEEK,1);press(&r,cheek);action(&r,"cheekRight");
    assert(r.v.clip==pet_clips_lookup(art,"cheekRight","reading") && r.v.clip!=pet_clips_lookup(art,"cheekRight",NULL));
    job(&r,BAJI_CUE_RUN,"running");assert(!r.s.held_cheek);
    finger(&r,cheek,0,false);tap(&r,PET_HIT_KEYS,0);action(&r,"keyChime");
    job(&r,BAJI_CUE_SEARCH,"searching");tap(&r,PET_HIT_LENS,0);action(&r,"lensFind");
    job(&r,BAJI_CUE_WAIT,"waiting");tap(&r,PET_HIT_PALM,0);action(&r,"palmTap");
    job(&r,BAJI_CUE_THINK,"working");cmd(&r,PET_CMD_SCENE);action(&r,"brush");
    tick(&r,800);tick(&r,450);action(&r,"working");cmd(&r,PET_CMD_NAP);action(&r,"working");
    job(&r,BAJI_CUE_WRITE,"writing");tick(&r,4100);unsigned pen_frame=r.v.frame;
    tap(&r,PET_HIT_CHEEK,1);action(&r,"cheekRight");tick(&r,1500);action(&r,"writing");
    assert(r.v.frame==pen_frame);
    job(&r,BAJI_CUE_READ,"reading");
    cmd(&r,PET_CMD_SCENE);
    tick(&r,2200);
    assert(r.s.moon);
    pet_interactions_new_session(&r.s);
    tick(&r,0);
    assert(!r.s.moon && !strcmp(r.v.action,"reading") && r.v.frame==0);
    init(&r,0);cmd(&r,PET_CMD_NAP);tick(&r,2850);
    r.cue=BAJI_CUE_READ;tick(&r,0);
    action(&r,"doze");unsigned folded=r.v.frame;tick(&r,25);assert(r.v.frame==folded+1);
    tick(&r,940);action(&r,"reading");
}
static void fireflies(void)
{
    replay_t r;init(&r,0);cmd(&r,PET_CMD_STAR);action(&r,"starWatch");
    assert(r.v.star_phase==PET_TOY_INVITE);
    point_t star={r.v.star_x,r.v.star_y};press(&r,star);
    assert(r.v.star_phase==PET_TOY_HELD);
    finger(&r,(point_t){233,12},100,true);assert(r.v.star_y<30);
    finger(&r,(point_t){455,233},100,true);assert(r.v.star_x>430);
    finger(&r,(point_t){233,455},100,true);assert(r.v.star_y>430);
    finger(&r,(point_t){12,233},100,true);assert(r.v.star_x<30);
    finger(&r,(point_t){400,400},100,true);
    int dx=r.v.star_x-233,dy=r.v.star_y-233;
    assert(dx==dy && dx*dx+dy*dy<=213*213);
    finger(&r,(point_t){233,115},120,true);finger(&r,(point_t){233,115},200,false);
    assert(r.v.star_phase==PET_TOY_RETURN);tick(&r,650);assert(r.v.star_phase==PET_TOY_INVITE);
    int palms[4];assert(pet_clip_palms(r.v.clip,r.v.frame,palms));point_t left={palms[0],palms[1]};
    press(&r,left);finger(&r,left,10,false);assert(r.v.star_phase==PET_TOY_LANDING);
    tick(&r,470);action(&r,"starCatchLeft");assert(r.v.fx.star==PET_STAR_CAUGHT);
    tick(&r,3000);action(&r,"idle");tick(&r,11501);action(&r,"starWatch");
    tick(&r,14001);assert(r.v.star_phase==PET_TOY_DEPART);tick(&r,600);action(&r,"idle");
    init(&r,0);cmd(&r,PET_CMD_STAR);star=(point_t){r.v.star_x,r.v.star_y};press(&r,star);
    assert(pet_clip_palms(r.v.clip,r.v.frame,palms));point_t right={palms[2],palms[3]};
    finger(&r,right,220,true);finger(&r,right,20,false);tick(&r,480);action(&r,"starCatchRight");
}
static void autonomy(void)
{
    static const char *const clips[]={"idleLookLeft","idleLookRight","idleGroomLeft","idleGroomRight",
        "idleStretch","idleHum","idleYawn","tail","wing","wingLeft","nose","sneeze","heartHug"};
    unsigned observed=0;
    for(unsigned run=0;run<160;run++) {
        unsigned bond=run%2?50:0;
        replay_t r;init(&r,bond);
        r.s.micro_seed=run*607u+671u;
        bool offered=false;
        for(unsigned step=0;step<285;step++) {
            tick(&r,100);
            if(!strncmp(r.v.action,"browInvite",10)) offered=true;
            if(r.s.micro) for(unsigned i=0;i<sizeof(clips)/sizeof(*clips);i++) if(!strcmp(r.v.action,clips[i])) {
                if(i==5 || i==12) assert(bond>=10);
                observed|=1u<<i;
            }
        }
        assert(offered && !strcmp(r.v.action,"doze") && r.s.bond==bond);
    }
    assert(observed==(1u<<(sizeof(clips)/sizeof(*clips)))-1);
    printf("All 13 autonomous actions, affection unlocks and 28s nap passed across 160 sessions\n");
}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fseek(f,0,SEEK_END)==0);long size=ftell(f);rewind(f);void *data=malloc(size);
    assert(data && fread(data,1,size,f)==(size_t)size);fclose(f);
    assert(pet_clips_open_data(&art,data,size)==ESP_OK);
    naps();cares();work();fireflies();autonomy();
    printf("%d observed GPT action transitions and physical hit masks passed\n",checks);
    pet_clips_close(art);free(data);return 0;
}
