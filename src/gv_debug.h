// gv_debug.h - the F1 overlay: path trails, predicted flight, hitboxes,
// formation slots and a counters panel. F3 cycles a path-catalogue view for
// tuning the turn tables in gv_paths.c.
#ifndef GV_DEBUG_H
#define GV_DEBUG_H

#include "gv_game.h"

void gv_debug_draw(const gv_game *g, SDL_Renderer *ren);

#endif // GV_DEBUG_H
