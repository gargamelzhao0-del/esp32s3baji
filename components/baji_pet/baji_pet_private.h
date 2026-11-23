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

struct baji_pet {
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
    uint16_t selected[CELLS];
};
