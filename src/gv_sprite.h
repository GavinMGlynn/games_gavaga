// gv_sprite.h - placeholder sprite atlas, built procedurally at init.
//
// Nothing here is ripped from anywhere: the art is defined as ASCII rows in
// gv_sprite.c, one character per pixel, indexing a 16-colour palette. Edit the
// strings to change the art, or replace gv_sprite_init() with a PNG loader
// once you have real assets - the rest of the game only ever asks for a
// sprite id and a source rect.
#ifndef GV_SPRITE_H
#define GV_SPRITE_H

#include "gv_common.h"

enum {
    GV_SPR_PLAYER = 0,
    GV_SPR_GRUNT_A, GV_SPR_GRUNT_B,
    GV_SPR_GUARD_A, GV_SPR_GUARD_B,
    GV_SPR_FLAGSHIP_A, GV_SPR_FLAGSHIP_B,
    GV_SPR_PSHOT,
    GV_SPR_ESHOT,
    GV_SPR_BOOM0, GV_SPR_BOOM1, GV_SPR_BOOM2, GV_SPR_BOOM3,
    GV_SPR_LIFE,
    GV_SPR_BADGE,
    GV_SPR_COUNT
};

#define GV_ATLAS_CELL 16
#define GV_ATLAS_COLS 8

bool gv_sprite_init(SDL_Renderer *ren);
void gv_sprite_quit(void);

SDL_Texture     *gv_sprite_texture(void);
const SDL_FRect *gv_sprite_rect(int id);   // source rect within the atlas

#endif // GV_SPRITE_H
