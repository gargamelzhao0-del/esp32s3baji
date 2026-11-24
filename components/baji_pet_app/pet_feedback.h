#pragma once
#include "baji_board.h"
#include <stdbool.h>

typedef struct pet_feedback pet_feedback_t;
/* Called by the scene owner. A missing speaker or NVS does not prevent play. */
pet_feedback_t *pet_feedback_open(baji_board_t *board, unsigned *bond, bool *sound);
void pet_feedback_update(pet_feedback_t *fb, const char *event, unsigned bond, bool sound);
void pet_feedback_close(pet_feedback_t *fb);
