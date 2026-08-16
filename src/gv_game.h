// gv_game.h - game state. Every pool is struct-of-arrays with a fixed
// capacity; the whole thing is allocated once and never grows.
#ifndef GV_GAME_H
#define GV_GAME_H

#include "gv_common.h"
#include "gv_path.h"
#include "gv_star.h"

// --- pool capacities ------------------------------------------------------
#define GV_MAX_ENEMIES 64
#define GV_MAX_PSHOTS   6
#define GV_MAX_ESHOTS  24
#define GV_MAX_FX      24

#define GV_TRAIL_LEN   40   // debug-overlay history, samples
#define GV_TRAIL_EVERY 2    // ticks between samples
#define GV_MAX_GROUPS  8    // launch groups per stage

// --- formation ------------------------------------------------------------
// 5 rows: 4 flagships, 2 rows of 8 guards, 2 rows of 10 grunts.
#define GV_FORM_SLOTS 40
#define GV_FORM_COLS  10
#define GV_FORM_ROWS  5
#define GV_FORM_TOP   42    // y of row 0
#define GV_FORM_VGAP  18
#define GV_FORM_HGAP  17

enum { GV_EK_GRUNT = 0, GV_EK_GUARD, GV_EK_FLAGSHIP, GV_EK_COUNT };

enum {
    GV_ES_FREE = 0,  // slot unused
    GV_ES_ENTER,     // flying an entry path
    GV_ES_TUCK,      // steering into its formation slot
    GV_ES_FORM,      // parked in formation
    GV_ES_DIVE,      // flying a dive path
    GV_ES_FLYBY,     // challenging-stage pass; despawns at the end
};

enum {
    GV_MODE_ATTRACT = 0,
    GV_MODE_READY,
    GV_MODE_PLAY,
    GV_MODE_DYING,
    GV_MODE_STAGE_CLEAR,
    GV_MODE_GAMEOVER,
};

// --- pools ----------------------------------------------------------------
typedef struct {
    fix_t          x[GV_MAX_ENEMIES];
    fix_t          y[GV_MAX_ENEMIES];
    ang_t          ang[GV_MAX_ENEMIES];
    fix_t          spd[GV_MAX_ENEMIES];
    gv_path_runner path[GV_MAX_ENEMIES];
    uint8_t        state[GV_MAX_ENEMIES];
    uint8_t        kind[GV_MAX_ENEMIES];
    uint8_t        slot[GV_MAX_ENEMIES];
    uint8_t        hp[GV_MAX_ENEMIES];
    uint16_t       timer[GV_MAX_ENEMIES];
    uint16_t       fire_cd[GV_MAX_ENEMIES];

    // Debug trail history (ring buffer per entity).
    int16_t        trail_x[GV_MAX_ENEMIES][GV_TRAIL_LEN];
    int16_t        trail_y[GV_MAX_ENEMIES][GV_TRAIL_LEN];
    uint8_t        trail_head[GV_MAX_ENEMIES];
    uint8_t        trail_n[GV_MAX_ENEMIES];
    uint8_t        trail_tick[GV_MAX_ENEMIES];

    int hi;    // one past the highest index in use - bounds iteration
    int live;
} gv_enemies;

typedef struct {
    fix_t   x[GV_MAX_ESHOTS];
    fix_t   y[GV_MAX_ESHOTS];
    fix_t   vx[GV_MAX_ESHOTS];
    fix_t   vy[GV_MAX_ESHOTS];
    uint8_t used[GV_MAX_ESHOTS];
    int     hi;
} gv_eshots;

typedef struct {
    fix_t   x[GV_MAX_PSHOTS];
    fix_t   y[GV_MAX_PSHOTS];
    fix_t   vy[GV_MAX_PSHOTS];
    uint8_t used[GV_MAX_PSHOTS];
    int     hi;
    int     live;
} gv_pshots;

typedef struct {
    fix_t   x[GV_MAX_FX];
    fix_t   y[GV_MAX_FX];
    uint8_t frame[GV_MAX_FX];
    uint8_t timer[GV_MAX_FX];
    uint8_t big[GV_MAX_FX];
    uint8_t used[GV_MAX_FX];
    int     hi;
} gv_fx;

typedef struct {
    fix_t    x, y;
    int8_t   lives;
    uint16_t cooldown;
    uint16_t invuln;
    bool     alive;
} gv_player;

typedef struct {
    fix_t cx;            // formation centre, sways side to side
    fix_t breathe;       // horizontal spread, 16.16 around 1.0
    ang_t sway_phase;
    ang_t breath_phase;
    uint8_t occupied[GV_FORM_SLOTS];   // 1 while a live enemy owns the slot
} gv_formation;

typedef struct {
    bool left, right, fire;
    bool start;
} gv_input;

// --- the whole game -------------------------------------------------------
typedef struct {
    uint8_t      mode;
    uint32_t     tick;          // total logic ticks since boot
    uint32_t     mode_timer;

    gv_starfield stars;
    gv_enemies   en;
    gv_pshots    ps;
    gv_eshots    es;
    gv_fx        fx;
    gv_player    player;
    gv_formation form;
    gv_input     in;
    gv_rng       rng;

    // Formation slot geometry, filled once at init.
    uint8_t      slot_col[GV_FORM_SLOTS];
    uint8_t      slot_row[GV_FORM_SLOTS];
    uint8_t      slot_kind[GV_FORM_SLOTS];

    int          stage;         // 1-based
    bool         challenge;     // challenging stage: flybys, no formation
    uint32_t     score;
    uint32_t     high;

    // Wave launcher progress: one counter pair per group, so groups can
    // overlap in time rather than running strictly one after another.
    uint32_t     stage_timer;   // ticks since the stage began
    uint8_t      grp_done[GV_MAX_GROUPS];
    uint16_t     grp_timer[GV_MAX_GROUPS];
    bool         stage_spawned; // every group has finished launching
    uint16_t     chal_spawned, chal_killed;

    uint16_t     dive_timer;
    int          divers;

    bool         debug;         // overlay on
    bool         paused;
    bool         step_once;
    uint16_t     debug_path;    // GV_PATH_NONE, or a path to show on its own

    // Debug counters, refreshed by main.c.
    double       fps;
    int          ticks_last_frame;
} gv_game;

// --- API ------------------------------------------------------------------
void gv_game_init(gv_game *g, uint32_t seed);
void gv_game_tick(gv_game *g);
void gv_game_key(gv_game *g, SDL_Scancode sc, bool down);

// Formation slot position, in 16.16. Used by the game and the debug overlay.
void gv_form_slot_pos(const gv_game *g, int slot, fix_t *out_x, fix_t *out_y);

int  gv_enemy_sprite(const gv_game *g, int i);
void gv_spawn_fx(gv_game *g, fix_t x, fix_t y, bool big);

#endif // GV_GAME_H
