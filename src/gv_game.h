// gv_game.h - game state. Every pool is struct-of-arrays with a fixed
// capacity; the whole thing is allocated once and never grows.
#ifndef GV_GAME_H
#define GV_GAME_H

#include "gv_common.h"
#include "gv_path.h"
#include "gv_star.h"

// --- pool capacities ------------------------------------------------------
#define GV_MAX_ENEMIES 64
#define GV_MAX_PSHOTS   8
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

// --- tractor beam ---------------------------------------------------------
// The cone has to reach past the player's row to be able to catch anything.
// A flagship finishes its approach around y=142 and the player sits at y=252,
// so anything under ~110 here is a beam that can never work.
#define GV_BEAM_LEN      128   // how far the cone reaches below the flagship
#define GV_BEAM_HALF_TOP 4
#define GV_BEAM_HALF_BOT 34

#define GV_DUAL_OFFSET   8     // px from centre to each hull of a dual fighter

enum { GV_EK_GRUNT = 0, GV_EK_GUARD, GV_EK_FLAGSHIP, GV_EK_COUNT };

enum {
    GV_ES_FREE = 0,  // slot unused
    GV_ES_ENTER,     // flying an entry path
    GV_ES_TUCK,      // steering into its formation slot
    GV_ES_FORM,      // parked in formation
    GV_ES_DIVE,      // flying a dive path
    GV_ES_BEAM,      // flagship running a tractor-beam capture
    GV_ES_FLYBY,     // challenging-stage pass; despawns at the end
};

// Phases of GV_ES_BEAM, held in gv_enemies.phase.
enum {
    GV_BEAM_APPROACH = 0,   // flying the approach path
    GV_BEAM_OPEN,           // cone growing
    GV_BEAM_HOLD,           // cone full - this is when the player can be caught
    GV_BEAM_CLOSE,          // cone shrinking
    GV_BEAM_LEAVE,          // heading back to formation
};

enum {
    GV_MODE_ATTRACT = 0,
    GV_MODE_READY,
    GV_MODE_PLAY,
    GV_MODE_DYING,
    GV_MODE_CAPTURED,       // the beam got you: ship spirals up into the boss
    GV_MODE_STAGE_CLEAR,
    GV_MODE_GAMEOVER,
};

// Game events the simulation emits for something outside to act on. Keeping
// them as data is what lets gv_game.c stay free of the audio device.
#define GV_SFX_QUEUE 24
enum {
    GV_SFX_NONE = 0,
    GV_SFX_SHOT,
    GV_SFX_ENEMY_BOOM,
    GV_SFX_FLAGSHIP_HIT,
    GV_SFX_FLAGSHIP_BOOM,
    GV_SFX_PLAYER_BOOM,
    GV_SFX_DIVE,
    GV_SFX_BEAM,
    GV_SFX_CAPTURED,
    GV_SFX_RESCUE,
    GV_SFX_EXTRA_LIFE,
    GV_SFX_STAGE,
    GV_SFX_GAMEOVER,
    GV_SFX_PERFECT,
    GV_SFX_COUNT
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
    uint8_t        phase[GV_MAX_ENEMIES];    // GV_BEAM_* while in GV_ES_BEAM
    uint8_t        captive[GV_MAX_ENEMIES];  // holding a captured fighter
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
    bool     dual;      // rescued fighter docked: two ships, twin shots
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
    uint32_t     next_extra;    // score at which the next spare ship is awarded

    // Wave launcher progress: one counter pair per group, so groups can
    // overlap in time rather than running strictly one after another.
    uint32_t     stage_timer;   // ticks since the stage began
    uint8_t      grp_done[GV_MAX_GROUPS];
    uint16_t     grp_timer[GV_MAX_GROUPS];
    bool         stage_spawned; // every group has finished launching
    uint16_t     chal_spawned, chal_killed;

    uint16_t     dive_timer;
    int          divers;

    // Tractor beam. Two separate roles, because they do not end together:
    // `beamer` is whoever is projecting a cone right now and is cleared when
    // the cone closes, while `captor` is whoever actually caught the ship and
    // must survive until the pull-up animation finishes. Both are -1 when idle.
    int          beamer;
    int          captor;
    bool         any_captive;   // some flagship is holding a fighter
    uint16_t     capture_cd;    // ticks before another capture may be tried
    fix_t        cap_x, cap_y;  // the ship being drawn up during the capture
    ang_t        cap_ang;

    // A freed fighter flying down to dock with the player.
    bool         rescue_active;
    fix_t        rescue_x, rescue_y;

    // Effects leaving the simulation as data.
    uint8_t      sfx[GV_SFX_QUEUE];
    int          sfx_n;
    bool         want_save_high;

    bool         debug;         // overlay on
    bool         godmode;       // debug: collisions cannot kill the player.
                                // Distinct from invuln, which also blocks the
                                // tractor beam - god mode still lets you be
                                // captured, so soak runs exercise that path.
    bool         paused;
    bool         step_once;
    uint16_t     debug_path;    // GV_PATH_NONE, or a path to show on its own

    // Debug counters, refreshed by main.c.
    double       fps;
    int          ticks_last_frame;
} gv_game;

// --- API ------------------------------------------------------------------
void gv_game_init(gv_game *g, uint32_t seed, uint32_t high);
void gv_game_tick(gv_game *g);
void gv_game_key(gv_game *g, SDL_Scancode sc, bool down);

// Begin a fresh game at the given stage. Stage 1 in normal play; the higher
// stages are reachable from the command line for testing late-game content.
void gv_game_start(gv_game *g, int stage);

// Formation slot position, in 16.16. Used by the game and the debug overlay.
void gv_form_slot_pos(const gv_game *g, int slot, fix_t *out_x, fix_t *out_y);

int  gv_enemy_sprite(const gv_game *g, int i);
void gv_spawn_fx(gv_game *g, fix_t x, fix_t y, bool big);

// Vertical extent and half-width of an active tractor beam, for drawing and
// for the catch test. Returns false when the enemy is not projecting one.
bool gv_beam_shape(const gv_game *g, int i, fix_t *top, fix_t *bottom, fix_t *half_bot);

#endif // GV_GAME_H
