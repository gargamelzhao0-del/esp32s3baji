#pragma once
#include "pet_interactions.h"

typedef enum { PET_MENU_CLOSED, PET_MENU_CARE, PET_MENU_WORK, PET_MENU_PARTS } pet_menu_page_t;
typedef struct {
    pet_menu_page_t page;
    uint32_t seen_press;
    unsigned group;
    bool captured, sound, live;
    int selected;
    baji_agent_cue_t preview;
    const char *labels[8];
    uint8_t disabled;
} pet_menu_t;

/* Consume a full contact when it starts on a control, including its release. */
bool pet_menu_input(pet_menu_t *menu, const baji_input_t *input, bool live,
                    int *command, bool *sound_changed, baji_damage_t *damage);
void pet_menu_update(pet_menu_t *menu, const pet_interaction_view_t *view, baji_damage_t *damage);
void pet_menu_paint(const pet_menu_t *menu, const baji_rect_t *band, uint8_t *pixels);
