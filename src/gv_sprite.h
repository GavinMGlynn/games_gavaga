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
    GV_SPR_FLAGSHIP_HURT_A, GV_SPR_FLAGSHIP_HURT_B,  // after the first hit
    GV_SPR_CAPTIVE,                                  // your ship, in their colours
    GV_SPR_PSHOT,
    GV_SPR_ESHOT,
    GV_SPR_BOOM0, GV_SPR_BOOM1, GV_SPR_BOOM2, GV_SPR_BOOM3,
    GV_SPR_LIFE,
    GV_SPR_BADGE,      // one stage
    GV_SPR_BADGE5,     // five stages
    GV_SPR_BADGE10,    // ten stages
    GV_SPR_SENTINEL_A, GV_SPR_SENTINEL_B,   // wide cruiser: fires a spread
    GV_SPR_DARTER_A,   GV_SPR_DARTER_B,     // slim and fast, dives hard
    GV_SPR_COUNT
};

// The baked art is 32 texels per cell and draws at 16 logical pixels, so the
// playfield geometry is unchanged and the sprites simply carry more detail
// than the screen resolution needs. The hand-drawn fallback is 1:1.
#define GV_ATLAS_CELL 16
#define GV_ATLAS_COLS 8

// Atlas texels per logical pixel. Divide a source rect by this to get the size
// a sprite should be drawn at.
int gv_sprite_oversample(void);

// True when the baked art loaded; false when the built-in art is being used.
bool gv_sprite_using_art(void);

bool gv_sprite_init(SDL_Renderer *ren);
void gv_sprite_quit(void);

// A window icon built from the player sprite. Needs no renderer, so it can be
// set as soon as the window exists. Caller owns the surface.
SDL_Surface     *gv_sprite_icon(int scale);

SDL_Texture     *gv_sprite_texture(void);
const SDL_FRect *gv_sprite_rect(int id);   // source rect within the atlas

#endif // GV_SPRITE_H
