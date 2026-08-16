// gv_game.c - simulation. Runs at exactly GV_TICK_HZ; nothing here touches
// the renderer, the wall clock, the filesystem or the heap. Sound and
// save-the-high-score leave as events for main.c to act on.
#include "gv_game.h"
#include "gv_sprite.h"

// --- tuning ---------------------------------------------------------------
#define PLAYER_Y         252
#define PLAYER_XMIN      12
#define PLAYER_XMAX      (GV_SCREEN_W - 12)
#define PLAYER_SPEED     gv_fix_from_f(1.40f)
#define PLAYER_FIRE_CD   10
#define PSHOT_LIMIT      2                      // classic two-shots-on-screen rule
#define PSHOT_LIMIT_DUAL 4
#define PSHOT_SPEED      gv_fix_from_f(4.50f)
#define ESHOT_SPEED      gv_fix_from_f(2.00f)

#define ENTRY_SPEED      gv_fix_from_f(1.75f)
#define DIVE_SPEED       gv_fix_from_f(1.55f)
#define TUCK_SPEED       gv_fix_from_f(1.60f)
#define TUCK_MAX_TURN    1200                   // BAM per tick
#define TUCK_TIMEOUT     360

#define FORM_SWAY_RATE   96
#define FORM_BREATH_RATE 150
#define FORM_SWAY_PX     8
#define FORM_BREATH_AMP  gv_fix_from_f(0.08f)

// Tractor beam timings, in ticks.
#define BEAM_OPEN_T      36
#define BEAM_HOLD_T      150
#define BEAM_CLOSE_T     28
#define BEAM_SPEED       gv_fix_from_f(1.30f)
#define CAPTURE_CD       540                    // between capture attempts
#define CAPTURE_ANIM     110                    // ship being drawn up
#define RESCUE_SPEED     gv_fix_from_f(1.25f)
#define RESCUE_DRIFT     gv_fix_from_f(0.70f)

#define CLEAN_STAGE_BONUS 1000u                 // cleared without losing a ship
#define EXTRA_FIRST      20000u
#define EXTRA_EVERY      60000u
#define MAX_LIVES        9

static const int      KIND_HALF[GV_EK_COUNT]  = { 6, 6, 7 };
static const uint8_t  KIND_HP[GV_EK_COUNT]    = { 1, 1, 2 };
static const uint32_t SCORE_FORM[GV_EK_COUNT] = {  50,  80, 150 };
static const uint32_t SCORE_DIVE[GV_EK_COUNT] = { 100, 160, 400 };

#define PLAYER_HALF_X 6
#define PLAYER_HALF_Y 6

// --- events out of the simulation ----------------------------------------
static void snd(gv_game *g, int id) {
    // The attract loop plays silently.
    if (g->mode == GV_MODE_ATTRACT) return;
    if (g->sfx_n < GV_SFX_QUEUE) g->sfx[g->sfx_n++] = (uint8_t)id;
}

// --- difficulty -----------------------------------------------------------
// Later stages dive faster, shoot more often and keep more ships in the air.
static fix_t stage_dive_speed(const gv_game *g) {
    const fix_t s = DIVE_SPEED + gv_fix_from_f(0.030f) * (g->stage - 1);
    return gv_clampf(s, DIVE_SPEED, gv_fix_from_f(2.45f));
}
static fix_t stage_entry_speed(const gv_game *g) {
    const fix_t s = ENTRY_SPEED + gv_fix_from_f(0.020f) * (g->stage - 1);
    return gv_clampf(s, ENTRY_SPEED, gv_fix_from_f(2.35f));
}
static uint32_t stage_fire_chance(const gv_game *g) {
    return (uint32_t)gv_clampi(3 + g->stage, 3, 16);   // out of 128 per tick
}
static uint16_t stage_fire_cd(const gv_game *g) {
    return (uint16_t)gv_clampi(50 - g->stage * 2, 18, 50);
}

// --- wave tables ----------------------------------------------------------
typedef struct {
    uint16_t delay;      // ticks after stage start before this group launches
    uint8_t  count;      // how many ships
    uint8_t  interval;   // ticks between launches
    uint8_t  path;       // GV_PATH_*
    int8_t   mirror;
    int16_t  sx, sy;     // spawn position, pixels
    int16_t  sdeg;       // spawn heading, degrees (0 = up, clockwise)
    uint8_t  slot0;      // first formation slot; the group fills upward
    uint8_t  kind;       // only used on challenging stages (no slots there)
} gv_group;

typedef struct {
    const gv_group *groups;
    int             ngroups;
    bool            challenge;
} gv_stage;

// Slot blocks: 0-3 flagships, 4-11 and 12-19 guards, 20-29 and 30-39 grunts.
static const gv_group STAGE_A[] = {
    {  30, 10, 10, GV_PATH_ENTRY_LOOP,   +1,  -20, 300,  14, 20, 0 },
    {  40, 10, 10, GV_PATH_ENTRY_LOOP,   -1,  244, 300, 346, 30, 0 },
    // Bottom entries launch from the flanks, never up the middle: the player
    // starts centred and watches the top of the screen, so a ship rising
    // through x=112 kills them from behind with nothing to react to.
    { 300,  8, 11, GV_PATH_ENTRY_S,      +1,   40, 312,   0,  4, 0 },
    { 310,  8, 11, GV_PATH_ENTRY_S,      -1,  184, 312,   0, 12, 0 },
    { 560,  4, 14, GV_PATH_ENTRY_SPIRAL, +1,  -20, 150,  90,  0, 0 },
};

static const gv_group STAGE_B[] = {
    {  30, 10, 10, GV_PATH_ENTRY_CROSS,  +1,  -20, 260,  60, 20, 0 },
    {  40, 10, 10, GV_PATH_ENTRY_CROSS,  -1,  244, 260, 300, 30, 0 },
    { 300,  8, 11, GV_PATH_ENTRY_ARC,    +1,   40, -20, 170,  4, 0 },
    { 310,  8, 11, GV_PATH_ENTRY_ARC,    -1,  184, -20, 190, 12, 0 },
    { 560,  4, 14, GV_PATH_ENTRY_LOOP,   -1,  244, 300, 346,  0, 0 },
};

// Spirals up the flanks, loops in from the bottom corners, bosses over the top.
static const gv_group STAGE_C[] = {
    {  30, 10, 10, GV_PATH_ENTRY_SPIRAL, +1,  -20, 200,  80, 20, 0 },
    {  40, 10, 10, GV_PATH_ENTRY_SPIRAL, -1,  244, 200, 280, 30, 0 },
    { 300,  8, 11, GV_PATH_ENTRY_LOOP,   +1,  -20, 300,  14,  4, 0 },
    { 310,  8, 11, GV_PATH_ENTRY_LOOP,   -1,  244, 300, 346, 12, 0 },
    { 560,  4, 14, GV_PATH_ENTRY_ARC,    +1,   40, -20, 170,  0, 0 },
};

// S-curves up the middle, then shallow crossing runs, bosses last.
static const gv_group STAGE_D[] = {
    {  30, 10, 10, GV_PATH_ENTRY_S,      +1,   40, 312,   0, 20, 0 },
    {  40, 10, 10, GV_PATH_ENTRY_S,      -1,  184, 312,   0, 30, 0 },
    { 300,  8, 11, GV_PATH_ENTRY_CROSS,  +1,  -20, 240,  55,  4, 0 },
    { 310,  8, 11, GV_PATH_ENTRY_CROSS,  -1,  244, 240, 305, 12, 0 },
    { 560,  4, 14, GV_PATH_ENTRY_SPIRAL, -1,  244, 150, 270,  0, 0 },
};

// Challenging stage: nobody joins the formation, nobody shoots. Clear the lot
// for a bonus.
static const gv_group STAGE_CHALLENGE[] = {
    {  20, 8,  9, GV_PATH_FLYBY_WAVE,  +1,  -20,  70,  90, 0, GV_EK_GRUNT    },
    { 140, 8,  9, GV_PATH_FLYBY_WAVE,  -1,  244, 110, 270, 0, GV_EK_GRUNT    },
    { 280, 8, 10, GV_PATH_FLYBY_LOOP,  +1,  -20, 300,  20, 0, GV_EK_GUARD    },
    { 420, 8, 10, GV_PATH_FLYBY_LOOP,  -1,  244, 300, 340, 0, GV_EK_GUARD    },
    { 560, 8, 12, GV_PATH_FLYBY_CROSS, +1,   60, 312,   0, 0, GV_EK_FLAGSHIP },
};

#define GV_STAGE(t, chal) { t, GV_COUNTOF(t), chal }
static const gv_stage STAGES[] = {
    GV_STAGE(STAGE_A, false),
    GV_STAGE(STAGE_B, false),
    GV_STAGE(STAGE_C, false),
    GV_STAGE(STAGE_D, false),
    GV_STAGE(STAGE_CHALLENGE, true),
};
#define GV_NORMAL_LAYOUTS 4
#define GV_CHALLENGE_IDX  4

static const gv_stage *stage_for(int stage) {
    // Every third stage is a challenging stage.
    if (stage % 3 == 0) return &STAGES[GV_CHALLENGE_IDX];

    // Rotate the normal layouts by their position among *normal* stages, so
    // they do not alias with the every-third-stage challenge cadence and end
    // up only ever showing two of the four.
    const int normal = stage - stage / 3;    // 1-based
    return &STAGES[(normal - 1) % GV_NORMAL_LAYOUTS];
}

// --- formation ------------------------------------------------------------
static void init_slots(gv_game *g) {
    int s = 0;
    for (int c = 3; c <= 6; c++) {
        g->slot_col[s] = (uint8_t)c; g->slot_row[s] = 0;
        g->slot_kind[s] = GV_EK_FLAGSHIP; s++;
    }
    for (int r = 1; r <= 2; r++)
        for (int c = 1; c <= 8; c++) {
            g->slot_col[s] = (uint8_t)c; g->slot_row[s] = (uint8_t)r;
            g->slot_kind[s] = GV_EK_GUARD; s++;
        }
    for (int r = 3; r <= 4; r++)
        for (int c = 0; c <= 9; c++) {
            g->slot_col[s] = (uint8_t)c; g->slot_row[s] = (uint8_t)r;
            g->slot_kind[s] = GV_EK_GRUNT; s++;
        }
    SDL_assert(s == GV_FORM_SLOTS);
}

void gv_form_slot_pos(const gv_game *g, int slot, fix_t *out_x, fix_t *out_y) {
    slot = gv_clampi(slot, 0, GV_FORM_SLOTS - 1);
    const int col = g->slot_col[slot];
    const int row = g->slot_row[slot];
    const fix_t off = gv_fix(col * GV_FORM_HGAP - (GV_FORM_COLS - 1) * GV_FORM_HGAP / 2);
    *out_x = g->form.cx + gv_fmul(off, g->form.breathe);
    *out_y = gv_fix(GV_FORM_TOP + row * GV_FORM_VGAP);
}

static void form_tick(gv_game *g) {
    g->form.sway_phase   = (ang_t)(g->form.sway_phase + FORM_SWAY_RATE);
    g->form.breath_phase = (ang_t)(g->form.breath_phase + FORM_BREATH_RATE);
    g->form.cx      = gv_fix(GV_SCREEN_W / 2)
                    + gv_fmul(gv_fix(FORM_SWAY_PX), gv_sin(g->form.sway_phase));
    g->form.breathe = GV_FIX_ONE
                    + gv_fmul(FORM_BREATH_AMP, gv_sin(g->form.breath_phase));
}

// --- pools ----------------------------------------------------------------
static int enemy_alloc(gv_enemies *en) {
    for (int i = 0; i < GV_MAX_ENEMIES; i++) {
        if (en->state[i] != GV_ES_FREE) continue;
        if (i >= en->hi) en->hi = i + 1;
        en->live++;
        SDL_memset(&en->path[i], 0, sizeof en->path[i]);
        en->trail_head[i] = 0;
        en->trail_n[i]    = 0;
        en->trail_tick[i] = 0;
        en->timer[i]      = 0;
        en->fire_cd[i]    = 0;
        en->phase[i]      = 0;
        en->captive[i]    = 0;
        return i;
    }
    return -1;
}

static void enemy_free(gv_game *g, int i) {
    if (g->en.state[i] == GV_ES_FREE) return;
    if (g->en.state[i] == GV_ES_DIVE && g->divers > 0) g->divers--;
    if (g->en.state[i] != GV_ES_FLYBY) g->form.occupied[g->en.slot[i]] = 0;
    if (g->beamer == i) g->beamer = -1;
    if (g->captor == i) g->captor = -1;
    if (g->en.captive[i]) { g->en.captive[i] = 0; g->any_captive = false; }
    g->en.state[i] = GV_ES_FREE;
    g->en.live--;
    while (g->en.hi > 0 && g->en.state[g->en.hi - 1] == GV_ES_FREE) g->en.hi--;
}

static int pshot_alloc(gv_pshots *p) {
    for (int i = 0; i < GV_MAX_PSHOTS; i++)
        if (!p->used[i]) {
            p->used[i] = 1;
            if (i >= p->hi) p->hi = i + 1;
            p->live++;
            return i;
        }
    return -1;
}

static int eshot_alloc(gv_eshots *e) {
    for (int i = 0; i < GV_MAX_ESHOTS; i++)
        if (!e->used[i]) {
            e->used[i] = 1;
            if (i >= e->hi) e->hi = i + 1;
            return i;
        }
    return -1;
}

void gv_spawn_fx(gv_game *g, fix_t x, fix_t y, bool big) {
    for (int i = 0; i < GV_MAX_FX; i++) {
        if (g->fx.used[i]) continue;
        g->fx.used[i]  = 1;
        g->fx.x[i]     = x;
        g->fx.y[i]     = y;
        g->fx.frame[i] = 0;
        g->fx.timer[i] = 0;
        g->fx.big[i]   = big ? 1u : 0u;
        if (i >= g->fx.hi) g->fx.hi = i + 1;
        return;
    }
}

// --- scoring --------------------------------------------------------------
// Kills of attacking ships chain: every fourth one steps the multiplier up,
// and a shot that sails off the top of the screen breaks it. Rewards picking
// targets over holding the trigger down.
static uint32_t dive_multiplier(int combo) {
    if (combo >= 12) return 4;
    if (combo >= 8)  return 3;
    if (combo >= 4)  return 2;
    return 1;
}

static void combo_break(gv_game *g) {
    if (g->combo > g->best_combo) g->best_combo = g->combo;
    g->combo = 0;
}

static void add_score(gv_game *g, uint32_t pts) {
    g->score += pts;

    // The attract demo plays for real but must not touch the record or earn
    // spare ships.
    if (g->demo) return;

    if (g->score > g->high) {
        g->high = g->score;
        g->want_save_high = true;
    }
    while (g->score >= g->next_extra) {
        if (g->player.lives < MAX_LIVES) g->player.lives++;
        g->next_extra += EXTRA_EVERY;
        snd(g, GV_SFX_EXTRA_LIFE);
    }
}

// --- spawning -------------------------------------------------------------
static void launch(gv_game *g, const gv_group *grp, int nth) {
    const int i = enemy_alloc(&g->en);
    if (i < 0) return;

    const bool chal = g->challenge;
    const int  slot = chal ? 0 : gv_clampi(grp->slot0 + nth, 0, GV_FORM_SLOTS - 1);

    g->en.x[i]     = gv_fix(grp->sx);
    g->en.y[i]     = gv_fix(grp->sy);
    g->en.ang[i]   = GV_ANG_DEG(grp->sdeg);
    g->en.spd[i]   = stage_entry_speed(g);
    g->en.state[i] = chal ? GV_ES_FLYBY : GV_ES_ENTER;
    g->en.kind[i]  = chal ? grp->kind : g->slot_kind[slot];
    g->en.slot[i]  = (uint8_t)slot;
    g->en.hp[i]    = KIND_HP[g->en.kind[i]];

    gv_path_start(&g->en.path[i], grp->path, grp->mirror);
    if (!chal) g->form.occupied[slot] = 1;
    else       g->chal_spawned++;
}

static void spawn_tick(gv_game *g) {
    const gv_stage *st = stage_for(g->stage);
    bool all_done = true;

    for (int gi = 0; gi < st->ngroups && gi < GV_MAX_GROUPS; gi++) {
        const gv_group *grp = &st->groups[gi];
        if (g->grp_done[gi] >= grp->count) continue;

        all_done = false;
        if (g->stage_timer < grp->delay) continue;

        if (g->grp_timer[gi] > 0) { g->grp_timer[gi]--; continue; }

        launch(g, grp, g->grp_done[gi]);
        g->grp_done[gi]++;
        g->grp_timer[gi] = grp->interval;
    }
    g->stage_spawned = all_done;
}

static void stage_begin(gv_game *g, int stage) {
    g->stage         = stage;
    g->challenge     = stage_for(stage)->challenge;
    g->stage_timer   = 0;
    g->stage_spawned = false;
    g->chal_spawned  = 0;
    g->chal_killed   = 0;
    g->deaths_this_stage = 0;
    combo_break(g);
    g->divers        = 0;
    g->dive_timer    = 240;
    g->beamer        = -1;
    g->captor        = -1;
    g->capture_cd    = CAPTURE_CD;
    SDL_zeroa(g->grp_done);
    SDL_zeroa(g->grp_timer);
    SDL_zeroa(g->form.occupied);
    gv_star_set_speed(&g->stars, gv_fix_from_f(g->challenge ? 0.85f : 0.35f));
}

// --- enemies --------------------------------------------------------------
static void trail_push(gv_enemies *en, int i) {
    if (++en->trail_tick[i] < GV_TRAIL_EVERY) return;
    en->trail_tick[i] = 0;

    const uint8_t h = en->trail_head[i];
    en->trail_x[i][h] = (int16_t)gv_unfix(en->x[i]);
    en->trail_y[i][h] = (int16_t)gv_unfix(en->y[i]);
    en->trail_head[i] = (uint8_t)((h + 1) % GV_TRAIL_LEN);
    if (en->trail_n[i] < GV_TRAIL_LEN) en->trail_n[i]++;
}

// Called whenever an entity's position jumps rather than moves, so the
// overlay does not draw a polyline straight across the screen.
static void trail_break(gv_enemies *en, int i) {
    en->trail_n[i]    = 0;
    en->trail_head[i] = 0;
    en->trail_tick[i] = 0;
}

static void enemy_to_tuck(gv_game *g, int i) {
    if (g->en.state[i] == GV_ES_DIVE && g->divers > 0) g->divers--;
    if (g->beamer == i) g->beamer = -1;
    g->en.state[i] = GV_ES_TUCK;
    g->en.spd[i]   = TUCK_SPEED;
    g->en.timer[i] = 0;
    gv_path_clear(&g->en.path[i]);
}

static void enemy_start_dive(gv_game *g, int i) {
    static const uint8_t DIVES[] = {
        GV_PATH_DIVE_SWOOP, GV_PATH_DIVE_LOOP,
        GV_PATH_DIVE_ZIGZAG, GV_PATH_DIVE_STRAFE
    };
    const uint8_t path = DIVES[gv_rng_below(&g->rng, (uint32_t)GV_COUNTOF(DIVES))];

    // Aim the swoop at the player. Dives launch heading straight down (180),
    // and angles run clockwise from up, so from "down" a positive turn steers
    // left and a negative turn steers right: an enemy sitting to the left of
    // the player wants the negative mirror.
    const int8_t mirror = (g->en.x[i] < g->player.x) ? (int8_t)-1 : (int8_t)+1;

    g->en.state[i]   = GV_ES_DIVE;
    g->en.ang[i]     = GV_ANG_180;
    g->en.spd[i]     = stage_dive_speed(g);
    g->en.fire_cd[i] = 30;
    gv_path_start(&g->en.path[i], path, mirror);
    g->divers++;
    snd(g, GV_SFX_DIVE);
}

static void enemy_fire(gv_game *g, int i) {
    if (!g->player.alive) return;
    const int s = eshot_alloc(&g->es);
    if (s < 0) return;

    const ang_t dir = gv_dir(g->player.x - g->en.x[i], g->player.y - g->en.y[i]);
    g->es.x[s]  = g->en.x[i];
    g->es.y[s]  = g->en.y[i];
    g->es.vx[s] = gv_vx(dir, ESHOT_SPEED);
    g->es.vy[s] = gv_vy(dir, ESHOT_SPEED);
}

// --- tractor beam ---------------------------------------------------------
bool gv_beam_shape(const gv_game *g, int i, fix_t *top, fix_t *bottom, fix_t *half_bot) {
    if (g->en.state[i] != GV_ES_BEAM) return false;

    // How far the cone has unfurled, 0..1 in 16.16.
    fix_t f;
    switch (g->en.phase[i]) {
    case GV_BEAM_OPEN:  f = gv_fdiv(gv_fix(g->en.timer[i]), gv_fix(BEAM_OPEN_T)); break;
    case GV_BEAM_HOLD:  f = GV_FIX_ONE; break;
    case GV_BEAM_CLOSE: f = GV_FIX_ONE - gv_fdiv(gv_fix(g->en.timer[i]), gv_fix(BEAM_CLOSE_T)); break;
    default: return false;
    }
    f = gv_clampf(f, 0, GV_FIX_ONE);
    if (f == 0) return false;

    *top      = g->en.y[i] + gv_fix(8);
    *bottom   = *top + gv_fmul(gv_fix(GV_BEAM_LEN), f);
    *half_bot = gv_fmul(gv_fix(GV_BEAM_HALF_BOT), f);
    return true;
}

// Half-width of the cone at a given depth.
static fix_t beam_half_at(fix_t top, fix_t bottom, fix_t half_bot, fix_t y) {
    const fix_t span = bottom - top;
    if (span <= 0) return 0;
    const fix_t t = gv_clampf(gv_fdiv(y - top, span), 0, GV_FIX_ONE);
    const fix_t h0 = gv_fix(GV_BEAM_HALF_TOP);
    return h0 + gv_fmul(half_bot - h0, t);
}

static void beam_try_capture(gv_game *g, int i) {
    if (!g->player.alive || g->player.invuln > 0 || g->player.dual) return;

    fix_t top, bottom, half_bot;
    if (!gv_beam_shape(g, i, &top, &bottom, &half_bot)) return;
    if (g->player.y < top || g->player.y > bottom) return;

    const fix_t half = beam_half_at(top, bottom, half_bot, g->player.y);
    if (gv_fabs(g->player.x - g->en.x[i]) > half) return;

    // Caught. The ship spirals up into the boss; the stage keeps running,
    // because you want the chance to shoot it back off him.
    g->mode        = GV_MODE_CAPTURED;
    g->mode_timer  = 0;
    g->captor      = i;
    g->cap_x       = g->player.x;
    g->cap_y       = g->player.y;
    g->cap_ang     = 0;
    g->player.alive = false;
    g->en.phase[i] = GV_BEAM_CLOSE;
    g->en.timer[i] = 0;
    snd(g, GV_SFX_CAPTURED);
}

static void maybe_capture(gv_game *g) {
    if (g->challenge) return;
    if (g->mode != GV_MODE_PLAY && g->mode != GV_MODE_ATTRACT) return;
    if (g->capture_cd > 0) { g->capture_cd--; return; }
    if (g->beamer >= 0 || g->captor >= 0 || g->any_captive) return;
    if (g->player.dual || !g->player.alive) return;
    if (g->stage < 2) return;    // one stage of peace before it starts

    int cand[GV_MAX_ENEMIES];
    int n = 0;
    for (int i = 0; i < g->en.hi; i++)
        if (g->en.state[i] == GV_ES_FORM && g->en.kind[i] == GV_EK_FLAGSHIP)
            cand[n++] = i;
    if (n == 0) { g->capture_cd = 120; return; }

    const int i = cand[gv_rng_below(&g->rng, (uint32_t)n)];
    g->en.state[i] = GV_ES_BEAM;
    g->en.phase[i] = GV_BEAM_APPROACH;
    g->en.timer[i] = 0;
    g->en.ang[i]   = GV_ANG_180;
    g->en.spd[i]   = BEAM_SPEED;
    // Lean toward the player's side of the screen on the way down.
    gv_path_start(&g->en.path[i], GV_PATH_BEAM_DIVE,
                  (g->en.x[i] < g->player.x) ? (int8_t)-1 : (int8_t)+1);
    g->beamer     = i;
    g->capture_cd = CAPTURE_CD;
}

static void rescue_tick(gv_game *g) {
    if (!g->rescue_active) return;

    g->rescue_y += RESCUE_SPEED;
    if (g->player.alive) {
        const fix_t dx = g->player.x - g->rescue_x;
        if (gv_fabs(dx) <= RESCUE_DRIFT) g->rescue_x = g->player.x;
        else g->rescue_x += dx > 0 ? RESCUE_DRIFT : -RESCUE_DRIFT;
    }

    if (g->rescue_y > gv_fix(GV_SCREEN_H + 16)) { g->rescue_active = false; return; }

    if (g->player.alive && !g->player.dual &&
        gv_fabs(g->rescue_x - g->player.x) < gv_fix(10) &&
        gv_fabs(g->rescue_y - g->player.y) < gv_fix(10)) {
        g->player.dual   = true;
        g->rescue_active = false;
        add_score(g, 1000);
        snd(g, GV_SFX_RESCUE);
    }
}

// --- demo brain -----------------------------------------------------------
// Drives attract mode, and --autoplay for soak runs. It walks *into* tractor
// beams on purpose: getting captured, shooting the boss and docking the freed
// fighter is the longest path in the game and the least likely to be reached
// by leaving the game running unattended.
void gv_game_demo(gv_game *g) {
    if (g->mode == GV_MODE_GAMEOVER) { g->in.start = true; return; }

    g->in.left = g->in.right = false;
    g->in.fire = true;
    if (!g->player.alive) return;

    fix_t target  = g->player.x;
    bool  have    = false;
    bool  to_beam = false;

    // 1. an open beam - stand in it
    for (int i = 0; i < g->en.hi && !have; i++) {
        fix_t top, bottom, half_bot;
        if (!gv_beam_shape(g, i, &top, &bottom, &half_bot)) continue;
        target = g->en.x[i];
        have = to_beam = true;
    }
    // 2. a freed fighter to catch
    if (!have && g->rescue_active) { target = g->rescue_x; have = true; }
    // 3. the boss holding our ship - shoot it off him
    if (!have && g->any_captive)
        for (int i = 0; i < g->en.hi && !have; i++)
            if (g->en.captive[i]) { target = g->en.x[i]; have = true; }
    // 4. otherwise line up under the nearest ship sitting in formation.
    // Chasing whatever is lowest instead just walks into the divers.
    if (!have) {
        fix_t best = gv_fix(9999);
        for (int i = 0; i < g->en.hi; i++) {
            if (g->en.state[i] != GV_ES_FORM) continue;
            const fix_t d = gv_fabs(g->en.x[i] - g->player.x);
            if (d < best) { best = d; target = g->en.x[i]; have = true; }
        }
    }
    if (!have) return;

    // Sidestep anything about to land on us, unless we want to be caught.
    if (!to_beam) {
        bool  danger = false;
        fix_t threat = 0;

        for (int t = 0; t < g->es.hi && !danger; t++) {
            if (!g->es.used[t]) continue;
            if (g->es.y[t] < g->player.y - gv_fix(80)) continue;
            if (gv_fabs(g->es.x[t] - g->player.x) < gv_fix(14)) {
                danger = true; threat = g->es.x[t];
            }
        }
        // Anything not parked in formation is on a trajectory, so treat it as
        // a threat once it is anywhere near our altitude.
        for (int i = 0; i < g->en.hi && !danger; i++) {
            const uint8_t st = g->en.state[i];
            if (st == GV_ES_FREE || st == GV_ES_FORM) continue;
            if (g->en.y[i] < g->player.y - gv_fix(110)) continue;
            if (gv_fabs(g->en.x[i] - g->player.x) < gv_fix(26)) {
                danger = true; threat = g->en.x[i];
            }
        }
        if (danger)
            target = threat < g->player.x ? g->player.x + gv_fix(36)
                                          : g->player.x - gv_fix(36);
    }

    // Hold fire while closing on a beam, otherwise it just shoots the hovering
    // boss down and never gets caught.
    g->in.fire = !to_beam;

    target = gv_clampf(target, gv_fix(16), gv_fix(GV_SCREEN_W - 16));
    if (target < g->player.x - gv_fix(2))      g->in.left  = true;
    else if (target > g->player.x + gv_fix(2)) g->in.right = true;
}

// --- enemy update ---------------------------------------------------------
static void enemies_tick(gv_game *g) {
    const fix_t off_bottom = gv_fix(GV_SCREEN_H + 24);
    const fix_t off_left   = gv_fix(-72);
    const fix_t off_right  = gv_fix(GV_SCREEN_W + 72);

    for (int i = 0; i < g->en.hi; i++) {
        const uint8_t st = g->en.state[i];
        if (st == GV_ES_FREE) continue;

        switch (st) {
        case GV_ES_ENTER:
        case GV_ES_DIVE:
        case GV_ES_FLYBY: {
            const bool more = gv_path_step(&g->en.path[i], &g->en.ang[i]);
            if (more) {
                g->en.x[i] += gv_vx(g->en.ang[i], g->en.spd[i]);
                g->en.y[i] += gv_vy(g->en.ang[i], g->en.spd[i]);
            }

            const bool gone = g->en.y[i] > off_bottom
                           || g->en.x[i] < off_left
                           || g->en.x[i] > off_right;

            if (st == GV_ES_FLYBY) {
                if (!more || gone) enemy_free(g, i);
                break;
            }
            if (gone) {
                // Come back down over the slot rather than retracing the path.
                fix_t sx, sy;
                gv_form_slot_pos(g, g->en.slot[i], &sx, &sy);
                g->en.x[i]   = sx;
                g->en.y[i]   = gv_fix(-20);
                g->en.ang[i] = GV_ANG_180;
                trail_break(&g->en, i);
                enemy_to_tuck(g, i);
                break;
            }
            if (!more) enemy_to_tuck(g, i);

            if (st == GV_ES_DIVE) {
                if (g->en.fire_cd[i] > 0) g->en.fire_cd[i]--;
                else if (gv_rng_below(&g->rng, 128) < stage_fire_chance(g)) {
                    enemy_fire(g, i);
                    g->en.fire_cd[i] = stage_fire_cd(g);
                }
            }
            break;
        }

        case GV_ES_BEAM: {
            switch (g->en.phase[i]) {
            case GV_BEAM_APPROACH: {
                const bool more = gv_path_step(&g->en.path[i], &g->en.ang[i]);
                if (more) {
                    g->en.x[i] += gv_vx(g->en.ang[i], g->en.spd[i]);
                    g->en.y[i] += gv_vy(g->en.ang[i], g->en.spd[i]);
                }
                if (!more) {
                    g->en.phase[i] = GV_BEAM_OPEN;
                    g->en.timer[i] = 0;
                    g->en.ang[i]   = GV_ANG_180;
                    snd(g, GV_SFX_BEAM);
                }
                break;
            }
            case GV_BEAM_OPEN:
                if (++g->en.timer[i] >= BEAM_OPEN_T) {
                    g->en.phase[i] = GV_BEAM_HOLD;
                    g->en.timer[i] = 0;
                }
                break;
            case GV_BEAM_HOLD:
                beam_try_capture(g, i);
                if (g->en.state[i] == GV_ES_BEAM && ++g->en.timer[i] >= BEAM_HOLD_T) {
                    g->en.phase[i] = GV_BEAM_CLOSE;
                    g->en.timer[i] = 0;
                }
                break;
            case GV_BEAM_CLOSE:
                if (++g->en.timer[i] >= BEAM_CLOSE_T) enemy_to_tuck(g, i);
                break;
            default:
                enemy_to_tuck(g, i);
                break;
            }
            break;
        }

        case GV_ES_TUCK: {
            fix_t sx, sy;
            gv_form_slot_pos(g, g->en.slot[i], &sx, &sy);

            const fix_t dx = sx - g->en.x[i];
            const fix_t dy = sy - g->en.y[i];

            int32_t turn = gv_angdiff(g->en.ang[i], gv_dir(dx, dy));
            turn = gv_clampi(turn, -TUCK_MAX_TURN, TUCK_MAX_TURN);
            g->en.ang[i] = (ang_t)(g->en.ang[i] + turn);

            g->en.x[i] += gv_vx(g->en.ang[i], g->en.spd[i]);
            g->en.y[i] += gv_vy(g->en.ang[i], g->en.spd[i]);

            const bool close = gv_fabs(sx - g->en.x[i]) < gv_fix(3)
                            && gv_fabs(sy - g->en.y[i]) < gv_fix(3);
            if (close || ++g->en.timer[i] > TUCK_TIMEOUT) {
                g->en.state[i] = GV_ES_FORM;
                g->en.ang[i]   = GV_ANG_180;
            }
            break;
        }

        case GV_ES_FORM: {
            fix_t sx, sy;
            gv_form_slot_pos(g, g->en.slot[i], &sx, &sy);
            g->en.x[i]   = sx;
            g->en.y[i]   = sy;
            g->en.ang[i] = GV_ANG_180;
            break;
        }

        default: break;
        }

        if (g->en.state[i] != GV_ES_FREE) trail_push(&g->en, i);
    }
}

static void dive_tick(gv_game *g) {
    // Attract mode dives too, so the demo screen shows the dive tables off.
    // Nothing shoots there: enemy_fire needs a live player.
    if (g->challenge) return;
    if (g->mode != GV_MODE_PLAY && g->mode != GV_MODE_ATTRACT) return;
    if (g->dive_timer > 0) { g->dive_timer--; return; }

    const int max_divers = gv_clampi(2 + g->stage / 2, 2, 6);
    if (g->divers >= max_divers) { g->dive_timer = 30; return; }

    int cand[GV_MAX_ENEMIES];
    int n = 0;
    for (int i = 0; i < g->en.hi; i++)
        if (g->en.state[i] == GV_ES_FORM) cand[n++] = i;

    if (n == 0) { g->dive_timer = 60; return; }

    enemy_start_dive(g, cand[gv_rng_below(&g->rng, (uint32_t)n)]);

    const int base = gv_clampi(90 - g->stage * 4, 30, 90);
    g->dive_timer = (uint16_t)(base + (int)gv_rng_below(&g->rng, 70));
}

// --- player ---------------------------------------------------------------
static void player_reset(gv_game *g) {
    g->player.x        = gv_fix(GV_SCREEN_W / 2);
    g->player.y        = gv_fix(PLAYER_Y);
    g->player.alive    = true;
    g->player.cooldown = 0;
    g->player.invuln   = 120;
}

static void player_fire(gv_game *g) {
    const int limit = g->player.dual ? PSHOT_LIMIT_DUAL : PSHOT_LIMIT;
    if (g->ps.live >= limit) return;

    // The dual fighter puts one bolt up from each hull.
    const int barrels = g->player.dual ? 2 : 1;
    if (g->ps.live + barrels > limit) return;

    for (int b = 0; b < barrels; b++) {
        const int s = pshot_alloc(&g->ps);
        if (s < 0) break;
        const fix_t off = g->player.dual
                        ? (b == 0 ? -gv_fix(GV_DUAL_OFFSET) : gv_fix(GV_DUAL_OFFSET))
                        : 0;
        g->ps.x[s]  = g->player.x + off;
        g->ps.y[s]  = g->player.y - gv_fix(8);
        g->ps.vy[s] = -PSHOT_SPEED;
    }
    g->player.cooldown = PLAYER_FIRE_CD;
    snd(g, GV_SFX_SHOT);
}

static void player_tick(gv_game *g) {
    if (!g->player.alive) return;

    if (g->player.invuln > 0) g->player.invuln--;

    if (g->in.left)  g->player.x -= PLAYER_SPEED;
    if (g->in.right) g->player.x += PLAYER_SPEED;

    // The dual fighter is wider, so it stops further from the edges.
    const fix_t margin = g->player.dual ? gv_fix(GV_DUAL_OFFSET) : 0;
    g->player.x = gv_clampf(g->player.x,
                            gv_fix(PLAYER_XMIN) + margin,
                            gv_fix(PLAYER_XMAX) - margin);

    if (g->player.cooldown > 0) g->player.cooldown--;
    if (g->in.fire && g->player.cooldown == 0) player_fire(g);
}

static void player_die(gv_game *g) {
    if (!g->player.alive || g->player.invuln > 0 || g->godmode) return;
    g->player.alive = false;
    gv_spawn_fx(g, g->player.x, g->player.y, true);
    if (g->player.dual) {
        gv_spawn_fx(g, g->player.x - gv_fix(GV_DUAL_OFFSET), g->player.y, false);
        gv_spawn_fx(g, g->player.x + gv_fix(GV_DUAL_OFFSET), g->player.y, false);
        g->player.dual = false;
    }
    snd(g, GV_SFX_PLAYER_BOOM);
    g->deaths_this_stage++;
    combo_break(g);
    g->mode       = GV_MODE_DYING;
    g->mode_timer = 0;
}

// --- projectiles and effects ---------------------------------------------
static void shots_tick(gv_game *g) {
    for (int i = 0; i < g->ps.hi; i++) {
        if (!g->ps.used[i]) continue;
        g->ps.y[i] += g->ps.vy[i];
        if (g->ps.y[i] < gv_fix(-8)) {
            g->ps.used[i] = 0;
            g->ps.live--;
            combo_break(g);
        }
    }
    while (g->ps.hi > 0 && !g->ps.used[g->ps.hi - 1]) g->ps.hi--;

    for (int i = 0; i < g->es.hi; i++) {
        if (!g->es.used[i]) continue;
        g->es.x[i] += g->es.vx[i];
        g->es.y[i] += g->es.vy[i];
        if (g->es.y[i] > gv_fix(GV_SCREEN_H + 8) || g->es.y[i] < gv_fix(-8) ||
            g->es.x[i] < gv_fix(-8) || g->es.x[i] > gv_fix(GV_SCREEN_W + 8))
            g->es.used[i] = 0;
    }
    while (g->es.hi > 0 && !g->es.used[g->es.hi - 1]) g->es.hi--;
}

static void fx_tick(gv_game *g) {
    for (int i = 0; i < g->fx.hi; i++) {
        if (!g->fx.used[i]) continue;
        if (++g->fx.timer[i] >= 6) {
            g->fx.timer[i] = 0;
            if (++g->fx.frame[i] >= 4) g->fx.used[i] = 0;
        }
    }
    while (g->fx.hi > 0 && !g->fx.used[g->fx.hi - 1]) g->fx.hi--;
}

// --- collisions -----------------------------------------------------------
static bool overlap(fix_t ax, fix_t ay, int ah, fix_t bx, fix_t by, int bhx, int bhy) {
    return gv_fabs(ax - bx) < gv_fix(ah + bhx) && gv_fabs(ay - by) < gv_fix(ah + bhy);
}

static void kill_enemy(gv_game *g, int i) {
    const int kind = g->en.kind[i];
    const bool diving = g->en.state[i] == GV_ES_DIVE
                     || g->en.state[i] == GV_ES_BEAM
                     || g->en.state[i] == GV_ES_FLYBY;

    // Shooting a boss that is holding your ship sets it free.
    if (g->en.captive[i]) {
        g->rescue_active = true;
        g->rescue_x      = g->en.x[i];
        g->rescue_y      = g->en.y[i] + gv_fix(14);
        g->en.captive[i] = 0;
        g->any_captive   = false;
    }

    if (diving) {
        g->combo++;
        add_score(g, SCORE_DIVE[kind] * dive_multiplier(g->combo));
    } else {
        add_score(g, SCORE_FORM[kind]);
    }
    if (g->challenge) g->chal_killed++;
    gv_spawn_fx(g, g->en.x[i], g->en.y[i], kind == GV_EK_FLAGSHIP);
    snd(g, kind == GV_EK_FLAGSHIP ? GV_SFX_FLAGSHIP_BOOM : GV_SFX_ENEMY_BOOM);
    enemy_free(g, i);
}

static void collide(gv_game *g) {
    // player shots -> enemies
    for (int s = 0; s < g->ps.hi; s++) {
        if (!g->ps.used[s]) continue;
        for (int i = 0; i < g->en.hi; i++) {
            if (g->en.state[i] == GV_ES_FREE) continue;
            const int half = KIND_HALF[g->en.kind[i]];
            if (!overlap(g->en.x[i], g->en.y[i], half, g->ps.x[s], g->ps.y[s], 1, 4))
                continue;

            g->ps.used[s] = 0;
            g->ps.live--;
            if (g->en.hp[i] > 1) {
                g->en.hp[i]--;
                gv_spawn_fx(g, g->ps.x[s], g->ps.y[s], false);
                snd(g, GV_SFX_FLAGSHIP_HIT);
            } else {
                kill_enemy(g, i);
            }
            break;
        }
    }

    if (!g->player.alive || g->player.invuln > 0) return;

    const int php = g->player.dual ? PLAYER_HALF_X + GV_DUAL_OFFSET : PLAYER_HALF_X;

    // enemy shots -> player
    for (int s = 0; s < g->es.hi; s++) {
        if (!g->es.used[s]) continue;
        if (overlap(g->player.x, g->player.y, php, g->es.x[s], g->es.y[s], 2, 2)) {
            g->es.used[s] = 0;
            player_die(g);
            return;
        }
    }

    // enemy bodies -> player
    for (int i = 0; i < g->en.hi; i++) {
        if (g->en.state[i] == GV_ES_FREE) continue;

        // A ship still flying its entry cannot kill you from underneath. You
        // are watching the top of the screen; something climbing into you from
        // behind is a death you had no way to react to. Once it is level with
        // you or above, it is fair game again - and divers always are.
        if (g->en.state[i] == GV_ES_ENTER && g->en.y[i] > g->player.y) continue;

        const int half = KIND_HALF[g->en.kind[i]];
        if (overlap(g->en.x[i], g->en.y[i], half, g->player.x, g->player.y,
                    php, PLAYER_HALF_Y)) {
            kill_enemy(g, i);
            player_die(g);
            return;
        }
    }
}

// --- modes ----------------------------------------------------------------
static void clear_world(gv_game *g) {
    SDL_zero(g->en);
    SDL_zero(g->ps);
    SDL_zero(g->es);
    SDL_zero(g->fx);
    SDL_zeroa(g->form.occupied);
    g->divers        = 0;
    g->beamer        = -1;
    g->captor        = -1;
    g->any_captive   = false;
    g->rescue_active = false;
}

void gv_game_start(gv_game *g, int stage) {
    clear_world(g);
    g->demo         = false;
    g->score        = 0;
    g->next_extra   = EXTRA_FIRST;
    g->player.lives = 3;
    g->player.dual  = false;
    g->mode         = GV_MODE_READY;
    g->mode_timer   = 0;
    stage_begin(g, stage < 1 ? 1 : stage);
    player_reset(g);
    g->player.alive = false;    // appears when READY finishes
}

static void start_game(gv_game *g) { gv_game_start(g, 1); }

// Losing a ship wipes the stage and starts it over.
static void lose_life_restart(gv_game *g) {
    if (--g->player.lives > 0) {
        clear_world(g);
        stage_begin(g, g->stage);
        g->mode = GV_MODE_READY;
    } else {
        g->mode = GV_MODE_GAMEOVER;
        snd(g, GV_SFX_GAMEOVER);
    }
    g->mode_timer = 0;
}

// Attract mode plays the game for real, with a free ship every time.
static void attract_begin(gv_game *g) {
    g->demo         = true;
    g->score        = 0;
    g->player.lives = 3;
    g->player.dual  = false;
    player_reset(g);
}

static void mode_tick(gv_game *g) {
    g->mode_timer++;

    switch (g->mode) {
    case GV_MODE_ATTRACT:
        // The attract loop runs the real spawner with no player, so the entry
        // paths are always on show.
        if (g->stage_spawned && g->en.live == 0) stage_begin(g, g->stage + 1);
        if (g->in.start) start_game(g);
        break;

    case GV_MODE_READY:
        if (g->mode_timer == 1) snd(g, GV_SFX_STAGE);
        if (g->mode_timer > 100) {
            g->mode = GV_MODE_PLAY;
            g->mode_timer = 0;
            player_reset(g);
        }
        break;

    case GV_MODE_PLAY:
        if (g->stage_spawned && g->en.live == 0) {
            if (g->challenge && g->chal_killed >= g->chal_spawned && g->chal_spawned > 0) {
                add_score(g, 1000);   // perfect clear
                snd(g, GV_SFX_PERFECT);
            } else if (!g->challenge && g->deaths_this_stage == 0) {
                add_score(g, CLEAN_STAGE_BONUS + (uint32_t)g->stage * 200u);
                snd(g, GV_SFX_PERFECT);
            }
            g->mode = GV_MODE_STAGE_CLEAR;
            g->mode_timer = 0;
        }
        break;

    case GV_MODE_STAGE_CLEAR:
        if (g->mode_timer > 120) {
            stage_begin(g, g->stage + 1);
            g->mode = GV_MODE_READY;
            g->mode_timer = 0;
        }
        break;

    case GV_MODE_CAPTURED:
        // Being caught costs a ship but deliberately does NOT restart the
        // stage: the boss has to stay alive and on screen for you to have any
        // chance of shooting your fighter back off him.
        if (g->mode_timer > CAPTURE_ANIM) {
            SDL_zero(g->ps);
            SDL_zero(g->es);
            if (g->captor >= 0 && g->en.state[g->captor] != GV_ES_FREE) {
                g->en.captive[g->captor] = 1;
                g->any_captive = true;
            }
            g->captor = -1;
            if (g->demo) {
                player_reset(g);
                g->mode = GV_MODE_ATTRACT;
            } else if (--g->player.lives > 0) {
                g->mode = GV_MODE_READY;
            } else {
                g->mode = GV_MODE_GAMEOVER;
                snd(g, GV_SFX_GAMEOVER);
            }
            g->mode_timer = 0;
        }
        break;

    case GV_MODE_DYING:
        if (g->mode_timer > 90) {
            if (g->demo) {
                clear_world(g);
                stage_begin(g, g->stage);
                attract_begin(g);
                g->mode = GV_MODE_ATTRACT;
                g->mode_timer = 0;
            } else {
                lose_life_restart(g);
            }
        }
        break;

    case GV_MODE_GAMEOVER:
        if (g->mode_timer > 240 || g->in.start) {
            const bool start = g->in.start;
            clear_world(g);
            g->mode = GV_MODE_ATTRACT;
            g->mode_timer = 0;
            stage_begin(g, 1);
            attract_begin(g);
            if (start) gv_game_start(g, 1);
        }
        break;

    default: break;
    }
}

// The captured ship being drawn up into the boss.
static void capture_anim_tick(gv_game *g) {
    if (g->mode != GV_MODE_CAPTURED) return;
    g->cap_ang = (ang_t)(g->cap_ang + 1800);

    if (g->captor < 0 || g->en.state[g->captor] == GV_ES_FREE) return;
    const fix_t tx = g->en.x[g->captor];
    const fix_t ty = g->en.y[g->captor] + gv_fix(14);

    // Ease toward the boss rather than tracking it exactly - it reads as being
    // hauled in.
    g->cap_x += gv_fmul(tx - g->cap_x, gv_fix_from_f(0.045f));
    g->cap_y += gv_fmul(ty - g->cap_y, gv_fix_from_f(0.045f));
}

// --- public ---------------------------------------------------------------
void gv_game_init(gv_game *g, uint32_t seed, uint32_t high) {
    SDL_zerop(g);
    gv_math_init();
    init_slots(g);
    gv_rng_seed(&g->rng, seed);
    gv_star_init(&g->stars, (uint16_t)(seed & 0xFFFFu));

    g->high       = high;
    g->next_extra = EXTRA_FIRST;
    g->mode       = GV_MODE_ATTRACT;
    g->beamer     = -1;
    g->captor     = -1;

    stage_begin(g, 1);
    form_tick(g);
    attract_begin(g);
}

void gv_game_tick(gv_game *g) {
    g->tick++;

    if (g->demo && (g->mode == GV_MODE_ATTRACT || g->mode == GV_MODE_PLAY))
        gv_game_demo(g);

    gv_star_tick(&g->stars);
    form_tick(g);

    const bool world_runs = g->mode == GV_MODE_ATTRACT || g->mode == GV_MODE_READY
                         || g->mode == GV_MODE_PLAY    || g->mode == GV_MODE_DYING
                         || g->mode == GV_MODE_CAPTURED
                         || g->mode == GV_MODE_STAGE_CLEAR;

    if (world_runs) {
        g->stage_timer++;
        spawn_tick(g);
        enemies_tick(g);
        dive_tick(g);
        maybe_capture(g);
        rescue_tick(g);
        shots_tick(g);
        if (g->mode == GV_MODE_PLAY || (g->demo && g->mode == GV_MODE_ATTRACT)) {
            player_tick(g);
            collide(g);
        }
    }
    capture_anim_tick(g);
    fx_tick(g);
    mode_tick(g);

    g->in.start = false;   // edge-triggered
}

int gv_enemy_sprite(const gv_game *g, int i) {
    const int frame = (int)(((g->tick + (uint32_t)i * 7u) / 14u) & 1u);
    switch (g->en.kind[i]) {
    case GV_EK_GUARD:
        return frame ? GV_SPR_GUARD_B : GV_SPR_GUARD_A;
    case GV_EK_FLAGSHIP:
        // Two-hit boss: once damaged it repaints, so you can see which ones
        // are one shot from dead.
        if (g->en.hp[i] <= 1)
            return frame ? GV_SPR_FLAGSHIP_HURT_B : GV_SPR_FLAGSHIP_HURT_A;
        return frame ? GV_SPR_FLAGSHIP_B : GV_SPR_FLAGSHIP_A;
    default:
        return frame ? GV_SPR_GRUNT_B : GV_SPR_GRUNT_A;
    }
}

void gv_game_action(gv_game *g, int action, bool down) {
    switch (action) {
    // Held state. The caller composes keyboard and gamepad and sets these
    // every frame, so neither source can stamp on the other.
    case GV_ACT_LEFT:  g->in.left  = down; break;
    case GV_ACT_RIGHT: g->in.right = down; break;
    case GV_ACT_FIRE:  g->in.fire  = down; break;

    // Edges.
    case GV_ACT_START:   if (down) g->in.start   = true;         break;
    case GV_ACT_PAUSE:   if (down) g->paused     = !g->paused;   break;
    case GV_ACT_STEP:    if (down) g->step_once  = true;         break;
    case GV_ACT_RESTART: if (down) gv_game_start(g, 1);          break;
    case GV_ACT_DEBUG:   if (down) g->debug      = !g->debug;    break;

    case GV_ACT_PATH:
        // Cycle the path catalogue: off -> path 1 .. N -> off.
        if (down) {
            g->debug = true;
            g->debug_path = (uint16_t)((g->debug_path + 1) % GV_PATH_COUNT);
        }
        break;

    default: break;
    }
}
