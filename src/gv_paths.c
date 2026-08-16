// gv_paths.c - the turn tables themselves.
//
// Each table is a sequence of (turn, ticks). GV_TURN(deg, ticks) spreads an
// arc of `deg` degrees evenly over `ticks`; GV_HOLD(ticks) flies straight.
// Positive degrees turn clockwise on screen.
//
// Paths carry no position: the spawn point and initial heading come from the
// wave table (gv_game.c), and `mirror` flips every turn so one table serves
// both sides of the screen. Turn on the debug overlay (F1) to see them drawn.
//
// Radius, if you want to reason about the shapes:
//     r_px = (speed_px_per_tick * ticks) / radians_turned
#include "gv_path.h"

// --- entry flights --------------------------------------------------------

// Up the side of the screen, one full loop, then bank in toward formation.
static const gv_pathstep P_ENTRY_LOOP[] = {
    GV_HOLD(30),
    GV_TURN(360, 66),
    GV_HOLD(30),
    GV_TURN(150, 44),
};

// In from a top corner on a long shallow arc that crosses the playfield.
static const gv_pathstep P_ENTRY_ARC[] = {
    GV_HOLD(20),
    GV_TURN(210, 70),
    GV_HOLD(24),
    GV_TURN(-120, 44),
    GV_HOLD(18),
};

// Lazy S up the middle.
static const gv_pathstep P_ENTRY_S[] = {
    GV_HOLD(24),
    GV_TURN(-90, 34),
    GV_TURN(180, 60),
    GV_TURN(-90, 34),
    GV_HOLD(20),
};

// Two tight loops stacked, then peel toward formation.
static const gv_pathstep P_ENTRY_SPIRAL[] = {
    GV_HOLD(30),
    GV_TURN(720, 90),
    GV_HOLD(16),
    GV_TURN(-100, 36),
    GV_HOLD(20),
};

// Shallow diagonal crossing run - cheap to fly, good for filling rows fast.
static const gv_pathstep P_ENTRY_CROSS[] = {
    GV_HOLD(40),
    GV_TURN(70, 40),
    GV_HOLD(30),
    GV_TURN(-70, 40),
    GV_HOLD(24),
};

// --- dives ----------------------------------------------------------------

// Peel out of formation, swoop down past the player, curl away.
static const gv_pathstep P_DIVE_SWOOP[] = {
    GV_TURN(60, 20),
    GV_HOLD(40),
    GV_TURN(-120, 44),
    GV_HOLD(50),
    GV_TURN(60, 24),
    GV_HOLD(60),
};

// Half loop out, half loop back, then straight down the screen.
static const gv_pathstep P_DIVE_LOOP[] = {
    GV_TURN(180, 40),
    GV_HOLD(20),
    GV_TURN(-180, 40),
    GV_HOLD(80),
};

// Hard to lead - alternating banks all the way down.
static const gv_pathstep P_DIVE_ZIGZAG[] = {
    GV_TURN(50, 16),  GV_HOLD(18),
    GV_TURN(-100, 30), GV_HOLD(18),
    GV_TURN(100, 30),  GV_HOLD(18),
    GV_TURN(-100, 30), GV_HOLD(40),
};

// Dive to one side, then run across low.
static const gv_pathstep P_DIVE_STRAFE[] = {
    GV_TURN(80, 26),
    GV_HOLD(50),
    GV_TURN(-80, 26),
    GV_HOLD(70),
};

// --- challenging-stage flybys --------------------------------------------

static const gv_pathstep P_FLYBY_WAVE[] = {
    GV_TURN(40, 14),
    GV_TURN(-80, 28),
    GV_TURN(80, 28),
    GV_TURN(-80, 28),
    GV_TURN(80, 28),
    GV_TURN(-40, 14),
    GV_HOLD(40),
};

static const gv_pathstep P_FLYBY_LOOP[] = {
    GV_HOLD(50),
    GV_TURN(360, 64),
    GV_HOLD(70),
};

static const gv_pathstep P_FLYBY_CROSS[] = {
    GV_HOLD(40),
    GV_TURN(150, 50),
    GV_HOLD(40),
    GV_TURN(-150, 50),
    GV_HOLD(50),
};

#define GV_PATH_ENTRY(sym, label) { sym, (uint16_t)GV_COUNTOF(sym), label }

const gv_pathdef gv_path_table[GV_PATH_COUNT] = {
    [GV_PATH_NONE]         = { NULL, 0, "none" },
    [GV_PATH_ENTRY_LOOP]   = GV_PATH_ENTRY(P_ENTRY_LOOP,   "entry.loop"),
    [GV_PATH_ENTRY_ARC]    = GV_PATH_ENTRY(P_ENTRY_ARC,    "entry.arc"),
    [GV_PATH_ENTRY_S]      = GV_PATH_ENTRY(P_ENTRY_S,      "entry.s"),
    [GV_PATH_ENTRY_SPIRAL] = GV_PATH_ENTRY(P_ENTRY_SPIRAL, "entry.spiral"),
    [GV_PATH_ENTRY_CROSS]  = GV_PATH_ENTRY(P_ENTRY_CROSS,  "entry.cross"),
    [GV_PATH_DIVE_SWOOP]   = GV_PATH_ENTRY(P_DIVE_SWOOP,   "dive.swoop"),
    [GV_PATH_DIVE_LOOP]    = GV_PATH_ENTRY(P_DIVE_LOOP,    "dive.loop"),
    [GV_PATH_DIVE_ZIGZAG]  = GV_PATH_ENTRY(P_DIVE_ZIGZAG,  "dive.zigzag"),
    [GV_PATH_DIVE_STRAFE]  = GV_PATH_ENTRY(P_DIVE_STRAFE,  "dive.strafe"),
    [GV_PATH_FLYBY_WAVE]   = GV_PATH_ENTRY(P_FLYBY_WAVE,   "flyby.wave"),
    [GV_PATH_FLYBY_LOOP]   = GV_PATH_ENTRY(P_FLYBY_LOOP,   "flyby.loop"),
    [GV_PATH_FLYBY_CROSS]  = GV_PATH_ENTRY(P_FLYBY_CROSS,  "flyby.cross"),
};
