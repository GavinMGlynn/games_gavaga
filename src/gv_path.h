// gv_path.h - the turn-table path interpreter.
//
// Every scripted enemy motion in the game runs through this one system.
// A path is a sequence of (turn rate, duration) pairs. An entity walking a
// path moves at a constant speed along its current heading; each tick the
// heading is advanced by the current step's turn rate. Duration is in ticks.
//
// Because a path only ever describes *relative* turning, the same table can
// be started from any position/heading and mirrored left/right, which is how
// one entry table serves both sides of the screen.
//
//   heading (BAM, 0 = up, clockwise) += turn * mirror
//   pos     += (sin(heading), -cos(heading)) * speed      [16.16 fixed point]
#ifndef GV_PATH_H
#define GV_PATH_H

#include "gv_common.h"

typedef struct {
    int16_t  turn;   // BAM per tick; >0 turns clockwise, <0 counter-clockwise
    uint16_t ticks;  // how long to hold this turn rate
} gv_pathstep;

typedef struct {
    const gv_pathstep *steps;
    uint16_t           nsteps;
    const char        *name;   // for the debug overlay
} gv_pathdef;

// Build a step from degrees-of-arc over a duration, so the tables read the way
// you'd draw them: "sweep 140 degrees right over 48 ticks".
#define GV_TURN(deg, ticks) { (int16_t)(((deg) * (65536.0 / 360.0)) / (double)(ticks)), (uint16_t)(ticks) }
#define GV_HOLD(ticks)      { 0, (uint16_t)(ticks) }

// ---------------------------------------------------------------------------
// Path IDs. 0 is reserved for "no path".
// ---------------------------------------------------------------------------
enum {
    GV_PATH_NONE = 0,

    // Entry flights - flown from a spawn point off-screen into the formation.
    GV_PATH_ENTRY_LOOP,      // up the side, big loop over the top
    GV_PATH_ENTRY_ARC,       // in from a top corner, wide arc across
    GV_PATH_ENTRY_S,         // S-curve up the middle
    GV_PATH_ENTRY_SPIRAL,    // tight double loop
    GV_PATH_ENTRY_CROSS,     // shallow diagonal crossing run

    // Dives - flown out of the formation and back around.
    GV_PATH_DIVE_SWOOP,
    GV_PATH_DIVE_LOOP,
    GV_PATH_DIVE_ZIGZAG,
    GV_PATH_DIVE_STRAFE,

    // Challenging-stage flybys - never attack, just fly the pattern and leave.
    GV_PATH_FLYBY_WAVE,
    GV_PATH_FLYBY_LOOP,
    GV_PATH_FLYBY_CROSS,

    GV_PATH_COUNT
};

// ---------------------------------------------------------------------------
// Runner: the per-entity cursor into a path.
// ---------------------------------------------------------------------------
typedef struct {
    uint16_t def;      // GV_PATH_* id; GV_PATH_NONE when idle
    uint16_t step;     // index of the current step
    uint16_t timer;    // ticks remaining in the current step
    int8_t   mirror;   // +1 as authored, -1 to flip the whole path horizontally
    bool     finished;
} gv_path_runner;

const gv_pathdef *gv_path_def(uint16_t id);
const char       *gv_path_name(uint16_t id);
int               gv_path_ticks(uint16_t id);   // total duration of a path

void gv_path_start(gv_path_runner *r, uint16_t def, int8_t mirror);
void gv_path_clear(gv_path_runner *r);

// Advance one tick. Updates *heading. Returns false once the path is spent
// (the caller decides what the entity does next).
bool gv_path_step(gv_path_runner *r, ang_t *heading);

// Roll a path forward without owning an entity: used by the debug overlay to
// draw where a path goes. Writes up to `max` points, one every `stride` ticks,
// and returns how many it wrote.
int gv_path_trace(uint16_t def, int8_t mirror,
                  fix_t x, fix_t y, ang_t heading, fix_t speed,
                  SDL_FPoint *out, int max, int stride);

// Same, but continues from a runner already part-way through its path, so the
// overlay can draw the remainder of a flight in progress. The runner is
// copied, not advanced.
int gv_path_trace_from(const gv_path_runner *r,
                       fix_t x, fix_t y, ang_t heading, fix_t speed,
                       SDL_FPoint *out, int max, int stride);

#endif // GV_PATH_H
