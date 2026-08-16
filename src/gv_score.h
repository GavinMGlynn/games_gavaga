// gv_score.h - persistent high score. Kept out of gv_game.c so the
// simulation stays free of the filesystem.
#ifndef GV_SCORE_H
#define GV_SCORE_H

#include "gv_common.h"

uint32_t gv_score_load(void);
void     gv_score_save(uint32_t high);

#endif // GV_SCORE_H
