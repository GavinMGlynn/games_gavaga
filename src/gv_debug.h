// gv_debug.h - the developer view: path trails, predicted flight, hitboxes,
// formation slots and a counters panel, plus a path catalogue for tuning the
// turn tables in gv_paths.c.
//
// This lives in its own window so it never draws over the game. The canvas is
// the playfield with a panel bolted underneath it, and the top region uses
// playfield coordinates one-to-one.
#ifndef GV_DEBUG_H
#define GV_DEBUG_H

#include "gv_game.h"

#define GV_DBG_PANEL_H 68
#define GV_DBG_W       GV_SCREEN_W
#define GV_DBG_H       (GV_SCREEN_H + GV_DBG_PANEL_H)

void gv_debug_draw(const gv_game *g, SDL_Renderer *ren);

#endif // GV_DEBUG_H
