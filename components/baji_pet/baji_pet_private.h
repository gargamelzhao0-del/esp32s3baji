#pragma once

#include "baji_pet.h"

enum { SIDE = 466, TILE = 8, COLS = (SIDE+TILE-1)/TILE, CELLS = COLS*COLS,
       IDLE_FRAMES = 43, PAT_FRAMES = 30, POSES = IDLE_FRAMES+PAT_FRAMES,
       GAZES = 15, CENTER = 7, MAX_RAW = 6*1024*1024 };
typedef struct { uint32_t first; uint16_t count, reserved; } range_t;
typedef struct { uint16_t cell, tile; } patch_t;
typedef struct {
    int16_t forehead[4];
    baji_rect_t eyes[2];
    range_t changes[3]; /* from previous pose, to idle zero, to pat zero */
} pose_t;
typedef struct {
    uint32_t tiles, rows, patches, rectangles;
    uint16_t frames[2], fps[2];
} header_t;
_Static_assert(sizeof(header_t)==24 && sizeof(pose_t)==48 && sizeof(range_t)==8 &&
               sizeof(baji_rect_t)==8 && sizeof(patch_t)==4,"Asset layout");

typedef struct motion_art motion_art_t;
struct baji_pet {
    motion_art_t *motion;
    uint8_t *decoded;
    const pose_t *poses;
    const uint16_t *maps;
    const range_t *gazes;
    const patch_t *patches;
    const baji_rect_t *rectangles;
    const uint32_t *rows32;
    const uint16_t *rows16;
    const uint8_t *pixel_bank[2];
    unsigned pose, gaze;
    uint16_t *selected; /* Legacy BPT1/BPT2 physical tile map. */
};

esp_err_t pet_motion_open(baji_pet_t *p,size_t length);
void pet_motion_close(motion_art_t *m);
const int16_t *pet_motion_landmarks(const motion_art_t *m,baji_pet_state_t s);
const int16_t *pet_motion_head(const motion_art_t *m,baji_pet_state_t s);
void pet_motion_select(motion_art_t *m,baji_pet_state_t s,unsigned gaze);
size_t pet_motion_damage(const motion_art_t *m,baji_pet_state_t before,unsigned g0,
                         baji_pet_state_t after,unsigned g1,baji_rect_t *areas,size_t capacity);
void pet_motion_paint(motion_art_t *m,const baji_rect_t *band,uint8_t *out);

void pet_add_rect(baji_rect_t *areas,size_t *count,size_t capacity,baji_rect_t r);
