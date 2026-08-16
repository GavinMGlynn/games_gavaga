// gv_render.h - all drawing. The simulation never calls into here.
#ifndef GV_RENDER_H
#define GV_RENDER_H

#include "gv_game.h"

bool gv_render_init(SDL_Renderer *ren);
void gv_render_quit(void);
void gv_render_frame(const gv_game *g, SDL_Renderer *ren);

// Draws a sprite centred on a 16.16 position, snapped to the pixel grid.
void gv_draw_sprite(SDL_Renderer *ren, int spr, fix_t x, fix_t y);
void gv_draw_sprite_px(SDL_Renderer *ren, int spr, int x, int y);
void gv_draw_sprite_rot(SDL_Renderer *ren, int spr, fix_t x, fix_t y, ang_t a);

#endif // GV_RENDER_H
