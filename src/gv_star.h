// gv_star.h - LFSR-driven scrolling starfield.
#ifndef GV_STAR_H
#define GV_STAR_H

#include "gv_common.h"

#define GV_MAX_STARS  256
#define GV_STAR_SETS  4     // blink groups, cycled to make the field twinkle

typedef struct {
    int16_t x, y;      // position in the virtual field (pixels)
    uint8_t color;     // index into the star palette
    uint8_t set;       // which blink group this star belongs to
} gv_star;

typedef struct {
    gv_star  stars[GV_MAX_STARS];
    int      count;
    fix_t    scroll;        // vertical offset, wraps at GV_SCREEN_H
    fix_t    speed;         // pixels per tick; negative scrolls the other way
    uint8_t  set_mask;      // which sets are currently lit
    uint16_t blink_timer;
    uint16_t blink_phase;
} gv_starfield;

// Runs the LFSR over the whole virtual field once and keeps whatever stars
// fall out. No allocation happens after this.
void gv_star_init(gv_starfield *sf, uint16_t seed);
void gv_star_tick(gv_starfield *sf);
void gv_star_set_speed(gv_starfield *sf, fix_t px_per_tick);
void gv_star_draw(const gv_starfield *sf, SDL_Renderer *ren);

#endif // GV_STAR_H
