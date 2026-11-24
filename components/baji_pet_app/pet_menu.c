#include "pet_menu.h"
#include "baji_ui_button.h"
#include <string.h>

enum { BUTTONS = 8 };
static const baji_rect_t toggle = {190, 432, 86, 28};
static const baji_rect_t popup = {90, 115, 286, 293};
static const char *const care[] = {"伸手", "喂星糖", "追星", "打个盹", "工作预览", "声 开", "碰一碰", "小动作"};
static const struct { const char *label; pet_command_t command; } parts[] = {
    {"左翼",PET_CMD_WING_LEFT},{"右翼",PET_CMD_WING_RIGHT},{"摸尾尖",PET_CMD_TAIL},{"碰鼻尖",PET_CMD_NOSE}
};
static const struct { const char *label; baji_agent_cue_t cue; } jobs[] = {
    {"待机", BAJI_CUE_LOCAL}, {"思考", BAJI_CUE_THINK}, {"阅读", BAJI_CUE_READ},
    {"写入", BAJI_CUE_WRITE}, {"敲键", BAJI_CUE_RUN}, {"搜索", BAJI_CUE_SEARCH},
    {"等确认", BAJI_CUE_WAIT}, {"完成", BAJI_CUE_SUCCESS}, {"异常", BAJI_CUE_ERROR},
    {"受限", BAJI_CUE_LIMIT}, {"整理", BAJI_CUE_COMPACT}, {"结束", BAJI_CUE_DONE},
    {"取消", BAJI_CUE_CANCELLED}, {"失败", BAJI_CUE_FAILED}
};
enum { JOB_COUNT = sizeof(jobs)/sizeof(jobs[0]), GROUPS = (JOB_COUNT+5)/6 };

static baji_rect_t bounds(int index)
{
    return (baji_rect_t){90+(index%2)*146, 115+(index/2)*60, 140, 51};
}

static bool inside(baji_rect_t rect, int x, int y)
{
    return x >= rect.x && y >= rect.y && x < rect.x+rect.width && y < rect.y+rect.height;
}

static baji_rect_t part_bounds(int index)
{
    return (baji_rect_t){95+(index%2)*143,300+(index/2)*56,133,48};
}

bool pet_menu_input(pet_menu_t *m, const baji_input_t *input, bool live,
                    int *command, bool *sound_changed, baji_damage_t *damage)
{
    if (m->live != live && m->page != PET_MENU_CLOSED) baji_damage_add(damage,popup);
    m->live = live;
    *command = -1;
    *sound_changed = false;
    if (m->seen_press != input->presses) {
        m->seen_press = input->presses;
        m->captured = m->page != PET_MENU_CLOSED || inside(toggle, input->press_x, input->press_y);
        m->selected = -1;
        if (m->page == PET_MENU_PARTS) {
            for (int i=0; i<4; i++) if (inside(part_bounds(i),input->press_x,input->press_y)) m->selected=i;
        } else if (m->page != PET_MENU_CLOSED)
            for (int i = 0; i < BUTTONS; i++)
                if (inside(bounds(i), input->press_x, input->press_y)) m->selected = i;
    }
    if (!m->captured) return m->page != PET_MENU_CLOSED;
    if (input->touch.pressed) return true;
    m->captured = false;
    if (input->travel > 14) return true;
    int key = m->selected;
    if (m->page == PET_MENU_CLOSED) m->page = PET_MENU_CARE;
    else if (m->page == PET_MENU_CARE && key >= 0 && (m->disabled & (1u<<key))) return true;
    else if (m->page == PET_MENU_CARE && key == 7) m->page = PET_MENU_PARTS;
    else if (m->page == PET_MENU_PARTS && key >= 0 && key < 4) {
        *command = parts[key].command;
        return true;
    } else if (m->page == PET_MENU_PARTS) m->page = PET_MENU_CARE;
    else if (m->page == PET_MENU_CARE && key == 4 && !live) {
        m->page = PET_MENU_WORK;
        m->group = 0;
    } else if (m->page == PET_MENU_CARE && key == 5) {
        m->sound = !m->sound;
        *sound_changed = true;
    } else if (m->page == PET_MENU_WORK && key == 6) {
        m->group = (m->group+1) % GROUPS;
    } else {
        if (m->page == PET_MENU_CARE) {
            static const pet_command_t actions[] = {PET_CMD_INVITE,PET_CMD_FEED,PET_CMD_STAR,PET_CMD_NAP};
            if (key >= 0 && key < 4) *command = actions[key];
            if (key == 6) *command = PET_CMD_SCENE;
        } else if (!live && key >= 0 && key < 6) {
            unsigned index = m->group * 6 + key;
            if (index < JOB_COUNT) m->preview = jobs[index].cue;
        }
        m->page = PET_MENU_CLOSED;
    }
    baji_damage_add(damage, popup);
    baji_damage_add(damage, toggle);
    return true;
}

void pet_menu_paint(const pet_menu_t *m, const baji_rect_t *band, uint8_t *pixels)
{
    baji_ui_button_paint(band, pixels, toggle, m->page == PET_MENU_CLOSED ? "陪玩" : "返回", 2, true);
    if (m->page == PET_MENU_CLOSED) return;
    if (m->page == PET_MENU_PARTS) {
        for (int i=0; i<4; i++) baji_ui_button_paint(band,pixels,part_bounds(i),parts[i].label,2,false);
        return;
    }
    for (int i = 0; i < BUTTONS; i++) {
        if (m->page == PET_MENU_CARE && (m->disabled & (1u<<i))) continue;
        const char *label;
        if (m->page == PET_MENU_CARE) label = m->labels[i] ? m->labels[i] : care[i];
        else if (i >= 6) label = i == 6 ? "下一页" : "返回";
        else {
            unsigned idx = m->group*6 + i;
            if (idx >= JOB_COUNT) continue;
            label = jobs[idx].label;
        }
        baji_ui_button_paint(band, pixels, bounds(i), label, 2, false);
    }
}

void pet_menu_update(pet_menu_t *m, const pet_interaction_view_t *v, baji_damage_t *damage)
{
    if (m->page != PET_MENU_CARE || !v->action || !v->scene) return;
    const char *labels[BUTTONS];
    memcpy(labels,care,sizeof(labels));
    uint8_t disabled=0;
    labels[4]=m->live ? "工作中" : care[4];
    labels[5]=m->sound ? "声 开" : "声 关";
    bool peek=!strncmp(v->action,"peek",4);
    if (peek || !strcmp(v->action,"doze")) {
        labels[3]=peek ? "再叫一声" : "轻轻叫她";
        disabled=(1u<<0)|(1u<<1)|(1u<<2)|(1u<<6)|(1u<<7);
    } else {
        if (!strncmp(v->action,"browInvite",9)) labels[0]="摸额头";
        if (!strcmp(v->action,"feed")) {
            int point[2], radius;
            labels[1]=v->frame>=33 && pet_clip_prop(v->clip,v->frame,point,&radius) ? "戳泡泡" : "等糖泡";
        }
        if (v->star_phase!=PET_TOY_OFF) {
            labels[2]=v->star_phase==PET_TOY_CAUGHT ? "再追星" : "放手心";
            labels[6]="收起星星";
        } else if (!strncmp(v->scene,"reading",7)) labels[6]="翻一页";
        else if (!strcmp(v->scene,"running")) labels[6]="敲亮键";
        else if (!strcmp(v->scene,"searching")) labels[6]="找星屑";
        else if (!strcmp(v->scene,"waiting")) labels[6]="碰一碰手";
        else if (strcmp(v->scene,"idle")) labels[6]="顺顺毛";
        if (strcmp(v->scene,"idle")) disabled |= (1u<<3);
        else if (!strncmp(v->action,"wake",4)) labels[3]="回去睡";
    }
    for (int i=0;i<BUTTONS;i++) {
        if (m->labels[i]!=labels[i] || ((m->disabled^disabled)&(1u<<i))) {
            m->labels[i]=labels[i];
            baji_damage_add(damage,bounds(i));
        }
    }
    m->disabled=disabled;
}
