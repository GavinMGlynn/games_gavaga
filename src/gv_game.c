// gv_game.c - simulation. Runs at exactly GV_TICK_HZ; nothing here touches
// the renderer, the wall clock, or the heap.
#include "gv_game.h"
#include "gv_sprite.h"

// --- tuning ---------------------------------------------------------------
#define PLAYER_Y        252
#define PLAYER_XMIN     12
#define PLAYER_XMAX     (GV_SCREEN_W - 12)
#define PLAYER_SPEED    gv_fix_from_f(1.40f)
#define PLAYER_FIRE_CD  10
#define PSHOT_LIMIT     2                      // classic two-shots-on-screen rule
#define PSHOT_SPEED     gv_fix_from_f(4.50f)
#define ESHOT_SPEED     gv_fix_from_f(2.00f)

#define ENTRY_SPEED     gv_fix_from_f(1.75f)
#define DIVE_SPEED      gv_fix_from_f(1.55f)
#define TUCK_SPEED      gv_fix_from_f(1.60f)
#define TUCK_MAX_TURN   1200                   // BAM per tick
#define TUCK_TIMEOUT    360

#define FORM_SWAY_RATE   96
#define FORM_BREATH_RATE 150
#define FORM_SWAY_PX     10
#define FORM_BREATH_AMP  gv_fix_from_f(0.10f)

static const int      KIND_HALF[GV_EK_COUNT]  = { 6, 6, 7 };
static const uint8_t  KIND_HP[GV_EK_COUNT]    = { 1, 1, 2 };
static const uint32_t SCORE_FORM[GV_EK_COUNT] = {  50,  80, 150 };
static const uint32_t SCORE_DIVE[GV_EK_COUNT] = { 100, 160, 400 };

#define PLAYER_HALF_X 6
#define PLAYER_HALF_Y 6

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
    { 300,  8, 11, GV_PATH_ENTRY_S,      +1,   96, 312,   0,  4, 0 },
    { 310,  8, 11, GV_PATH_ENTRY_S,      -1,  128, 312,   0, 12, 0 },
    { 560,  4, 14, GV_PATH_ENTRY_SPIRAL, +1,  -20, 150,  90,  0, 0 },
};

static const gv_group STAGE_B[] = {
    {  30, 10, 10, GV_PATH_ENTRY_CROSS,  +1,  -20, 260,  60, 20, 0 },
    {  40, 10, 10, GV_PATH_ENTRY_CROSS,  -1,  244, 260, 300, 30, 0 },
    { 300,  8, 11, GV_PATH_ENTRY_ARC,    +1,   40, -20, 170,  4, 0 },
    { 310,  8, 11, GV_PATH_ENTRY_ARC,    -1,  184, -20, 190, 12, 0 },
    { 560,  4, 14, GV_PATH_ENTRY_LOOP,   -1,  244, 300, 346,  0, 0 },
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
    GV_STAGE(STAGE_CHALLENGE, true),
};

static const gv_stage *stage_for(int stage) {
    // Every third stage is a challenging stage; the rest alternate layouts.
    if (stage % 3 == 0) return &STAGES[2];
    return &STAGES[(stage % 2) ? 0 : 1];
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
        return i;
    }
    return -1;
}

static void enemy_free(gv_game *g, int i) {
    if (g->en.state[i] == GV_ES_FREE) return;
    if (g->en.state[i] == GV_ES_DIVE && g->divers > 0) g->divers--;
    if (g->en.state[i] != GV_ES_FLYBY) g->form.occupied[g->en.slot[i]] = 0;
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

// --- spawning -------------------------------------------------------------
static void launch(gv_game *g, const gv_group *grp, int nth) {
    const int i = enemy_alloc(&g->en);
    if (i < 0) return;

    const bool chal = g->challenge;
    const int  slot = chal ? 0 : gv_clampi(grp->slot0 + nth, 0, GV_FORM_SLOTS - 1);

    g->en.x[i]     = gv_fix(grp->sx);
    g->en.y[i]     = gv_fix(grp->sy);
    g->en.ang[i]   = GV_ANG_DEG(grp->sdeg);
    g->en.spd[i]   = ENTRY_SPEED;
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
    g->stage        = stage;
    g->challenge    = stage_for(stage)->challenge;
    g->stage_timer  = 0;
    g->stage_spawned = false;
    g->chal_spawned = 0;
    g->chal_killed  = 0;
    g->divers       = 0;
    g->dive_timer   = 240;
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
    g->en.spd[i]     = DIVE_SPEED;
    g->en.fire_cd[i] = 30;
    gv_path_start(&g->en.path[i], path, mirror);
    g->divers++;
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
            g->en.x[i] += gv_vx(g->en.ang[i], g->en.spd[i]);
            g->en.y[i] += gv_vy(g->en.ang[i], g->en.spd[i]);

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
                else if (gv_rng_below(&g->rng, 128) < 3) {
                    enemy_fire(g, i);
                    g->en.fire_cd[i] = 45;
                }
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

static void player_tick(gv_game *g) {
    if (!g->player.alive) return;

    if (g->player.invuln > 0) g->player.invuln--;

    if (g->in.left)  g->player.x -= PLAYER_SPEED;
    if (g->in.right) g->player.x += PLAYER_SPEED;
    g->player.x = gv_clampf(g->player.x, gv_fix(PLAYER_XMIN), gv_fix(PLAYER_XMAX));

    if (g->player.cooldown > 0) g->player.cooldown--;

    if (g->in.fire && g->player.cooldown == 0 && g->ps.live < PSHOT_LIMIT) {
        const int s = pshot_alloc(&g->ps);
        if (s >= 0) {
            g->ps.x[s]  = g->player.x;
            g->ps.y[s]  = g->player.y - gv_fix(8);
            g->ps.vy[s] = -PSHOT_SPEED;
            g->player.cooldown = PLAYER_FIRE_CD;
        }
    }
}

static void player_die(gv_game *g) {
    if (!g->player.alive || g->player.invuln > 0) return;
    g->player.alive = false;
    gv_spawn_fx(g, g->player.x, g->player.y, true);
    g->mode       = GV_MODE_DYING;
    g->mode_timer = 0;
}

// --- projectiles and effects ---------------------------------------------
static void shots_tick(gv_game *g) {
    for (int i = 0; i < g->ps.hi; i++) {
        if (!g->ps.used[i]) continue;
        g->ps.y[i] += g->ps.vy[i];
        if (g->ps.y[i] < gv_fix(-8)) { g->ps.used[i] = 0; g->ps.live--; }
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
    const bool diving = g->en.state[i] == GV_ES_DIVE || g->en.state[i] == GV_ES_FLYBY;
    g->score += diving ? SCORE_DIVE[kind] : SCORE_FORM[kind];
    if (g->challenge) g->chal_killed++;
    gv_spawn_fx(g, g->en.x[i], g->en.y[i], kind == GV_EK_FLAGSHIP);
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
            } else {
                kill_enemy(g, i);
            }
            break;
        }
    }

    if (!g->player.alive || g->player.invuln > 0) return;

    // enemy shots -> player
    for (int s = 0; s < g->es.hi; s++) {
        if (!g->es.used[s]) continue;
        if (overlap(g->player.x, g->player.y, PLAYER_HALF_X, g->es.x[s], g->es.y[s], 2, 2)) {
            g->es.used[s] = 0;
            player_die(g);
            return;
        }
    }

    // enemy bodies -> player
    for (int i = 0; i < g->en.hi; i++) {
        if (g->en.state[i] == GV_ES_FREE) continue;
        const int half = KIND_HALF[g->en.kind[i]];
        if (overlap(g->en.x[i], g->en.y[i], half, g->player.x, g->player.y,
                    PLAYER_HALF_X, PLAYER_HALF_Y)) {
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
    g->divers = 0;
}

static void start_game(gv_game *g) {
    clear_world(g);
    g->score         = 0;
    g->player.lives  = 3;
    g->mode          = GV_MODE_READY;
    g->mode_timer    = 0;
    stage_begin(g, 1);
    player_reset(g);
    g->player.alive = false;    // appears when READY finishes
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
        if (g->mode_timer > 100) {
            g->mode = GV_MODE_PLAY;
            g->mode_timer = 0;
            player_reset(g);
        }
        break;

    case GV_MODE_PLAY:
        if (g->stage_spawned && g->en.live == 0) {
            if (g->challenge && g->chal_killed >= g->chal_spawned && g->chal_spawned > 0)
                g->score += 1000;   // perfect clear
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

    case GV_MODE_DYING:
        if (g->mode_timer > 90) {
            if (--g->player.lives > 0) {
                // Keep the stage as it stands - only the shots are swept up,
                // and anything mid-dive is recalled to formation.
                SDL_zero(g->ps);
                SDL_zero(g->es);
                for (int i = 0; i < g->en.hi; i++)
                    if (g->en.state[i] == GV_ES_DIVE) enemy_to_tuck(g, i);
                g->dive_timer = 180;
                g->mode = GV_MODE_READY;
            } else {
                g->mode = GV_MODE_GAMEOVER;
            }
            g->mode_timer = 0;
        }
        break;

    case GV_MODE_GAMEOVER:
        if (g->mode_timer > 240 || g->in.start) {
            clear_world(g);
            g->mode = GV_MODE_ATTRACT;
            g->mode_timer = 0;
            stage_begin(g, 1);
        }
        break;

    default: break;
    }

    if (g->score > g->high) g->high = g->score;
}

// --- public ---------------------------------------------------------------
void gv_game_init(gv_game *g, uint32_t seed) {
    SDL_zerop(g);
    gv_math_init();
    init_slots(g);
    gv_rng_seed(&g->rng, seed);
    gv_star_init(&g->stars, (uint16_t)(seed & 0xFFFFu));

    g->high  = 20000;
    g->mode  = GV_MODE_ATTRACT;

    // Park the (absent) player mid-screen so attract-mode dives have something
    // sensible to aim at.
    g->player.x     = gv_fix(GV_SCREEN_W / 2);
    g->player.y     = gv_fix(PLAYER_Y);
    g->player.alive = false;

    stage_begin(g, 1);
    form_tick(g);
}

void gv_game_tick(gv_game *g) {
    g->tick++;

    gv_star_tick(&g->stars);
    form_tick(g);

    const bool world_runs = g->mode == GV_MODE_ATTRACT || g->mode == GV_MODE_READY
                         || g->mode == GV_MODE_PLAY    || g->mode == GV_MODE_DYING
                         || g->mode == GV_MODE_STAGE_CLEAR;

    if (world_runs) {
        g->stage_timer++;
        spawn_tick(g);
        enemies_tick(g);
        dive_tick(g);
        shots_tick(g);
        if (g->mode == GV_MODE_PLAY) {
            player_tick(g);
            collide(g);
        }
    }
    fx_tick(g);
    mode_tick(g);

    g->in.start = false;   // edge-triggered
}

int gv_enemy_sprite(const gv_game *g, int i) {
    const int frame = (int)(((g->tick + (uint32_t)i * 7u) / 14u) & 1u);
    switch (g->en.kind[i]) {
    case GV_EK_GUARD:    return frame ? GV_SPR_GUARD_B    : GV_SPR_GUARD_A;
    case GV_EK_FLAGSHIP: return frame ? GV_SPR_FLAGSHIP_B : GV_SPR_FLAGSHIP_A;
    default:             return frame ? GV_SPR_GRUNT_B    : GV_SPR_GRUNT_A;
    }
}

void gv_game_key(gv_game *g, SDL_Scancode sc, bool down) {
    switch (sc) {
    case SDL_SCANCODE_LEFT:  case SDL_SCANCODE_A: g->in.left  = down; break;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: g->in.right = down; break;
    case SDL_SCANCODE_SPACE: case SDL_SCANCODE_Z:
    case SDL_SCANCODE_LCTRL: g->in.fire = down; break;

    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        if (down) g->in.start = true;
        break;

    case SDL_SCANCODE_F1: if (down) g->debug  = !g->debug;  break;
    case SDL_SCANCODE_P:  if (down) g->paused = !g->paused; break;
    case SDL_SCANCODE_F2: if (down) g->step_once = true;    break;
    case SDL_SCANCODE_R:  if (down) start_game(g);          break;

    case SDL_SCANCODE_F3:
        // Cycle the path catalogue: off -> path 1 .. N -> off.
        if (down) {
            g->debug = true;
            g->debug_path = (uint16_t)((g->debug_path + 1) % GV_PATH_COUNT);
        }
        break;
    default: break;
    }
}
