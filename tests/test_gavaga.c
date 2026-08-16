// test_gavaga.c - unit tests for the parts of the game that can be checked
// without a window: fixed-point maths, the trig tables, the path interpreter,
// the LFSR starfield, formation geometry, and run-to-run determinism.
//
// No framework: a CHECK macro, a counter, and a non-zero exit on failure, so
// it runs anywhere the game builds.
#include "gv_common.h"
#include "gv_path.h"
#include "gv_star.h"
#include "gv_game.h"

#include <stdio.h>

static int g_checks, g_fails;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_fails++;                                                        \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            printf("       ");                                                \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

#define SECTION(name) printf("== %s\n", name)

static int iabs_(int v) { return v < 0 ? -v : v; }

// --- fixed point ----------------------------------------------------------
static void test_fixed(void) {
    SECTION("fixed point");

    CHECK(gv_unfix(gv_fix(123)) == 123, "round trip broke");
    CHECK(gv_unfix(gv_fix(-77)) == -77, "negative round trip broke");
    CHECK(gv_fix(1) == GV_FIX_ONE, "one is not one");

    // 2.5 * 4 == 10
    const fix_t a = gv_fix_from_f(2.5f), b = gv_fix(4);
    CHECK(gv_unfix(gv_fmul(a, b)) == 10, "2.5*4 = %d", gv_unfix(gv_fmul(a, b)));

    CHECK(gv_fmul(a, GV_FIX_ONE) == a, "multiplying by one changed the value");
    CHECK(gv_fdiv(gv_fix(10), gv_fix(4)) == gv_fix_from_f(2.5f), "10/4 != 2.5");
    CHECK(gv_fdiv(gv_fix(1), 0) == 0, "divide by zero must saturate to 0");

    CHECK(gv_fabs(gv_fix(-5)) == gv_fix(5), "fabs");
    CHECK(gv_clampf(gv_fix(20), gv_fix(0), gv_fix(10)) == gv_fix(10), "clamp high");
    CHECK(gv_clampi(-5, 0, 10) == 0, "clamp low");

    // The multiply must not overflow at the magnitudes the game actually uses
    // (positions up to ~300px, speeds a few px/tick).
    const fix_t big = gv_fix(300);
    CHECK(gv_unfix(gv_fmul(big, gv_fix_from_f(1.5f))) == 450, "300*1.5 = %d",
          gv_unfix(gv_fmul(big, gv_fix_from_f(1.5f))));
}

// --- trig -----------------------------------------------------------------
static void test_trig(void) {
    SECTION("trig tables");
    gv_math_init();

    CHECK(iabs_(gv_sin(0)) < 200, "sin(0) = %d", gv_sin(0));
    CHECK(iabs_(gv_cos(0) - GV_FIX_ONE) < 200, "cos(0) = %d", gv_cos(0));
    CHECK(iabs_(gv_sin(GV_ANG_90) - GV_FIX_ONE) < 200, "sin(90) = %d", gv_sin(GV_ANG_90));
    CHECK(iabs_(gv_cos(GV_ANG_180) + GV_FIX_ONE) < 200, "cos(180) = %d", gv_cos(GV_ANG_180));

    // sin^2 + cos^2 == 1 all the way round. Compare in fixed point rather than
    // unfixing first: 0.99998 is a fine answer, but gv_unfix truncates it to 0.
    for (int i = 0; i < 64; i++) {
        const ang_t a = (ang_t)(i * (65536 / 64));
        const fix_t s = gv_sin(a), c = gv_cos(a);
        const fix_t mag = gv_fmul(s, s) + gv_fmul(c, c);
        CHECK(iabs_(mag - GV_FIX_ONE) < 300,
              "unit magnitude at angle %u is off by %d/65536", a, mag - GV_FIX_ONE);
    }

    // Heading 0 is up the screen, and angles run clockwise from there.
    CHECK(gv_vy(0, gv_fix(10)) < 0, "heading 0 must move up (-y)");
    CHECK(gv_vx(GV_ANG_90, gv_fix(10)) > 0, "heading 90 must move right (+x)");
    CHECK(gv_vy(GV_ANG_180, gv_fix(10)) > 0, "heading 180 must move down (+y)");
    CHECK(gv_vx(GV_ANG_270, gv_fix(10)) < 0, "heading 270 must move left (-x)");

    // gv_dir is the inverse: feed it a vector, get the heading back.
    struct { int dx, dy; ang_t want; } cases[] = {
        {  0, -10, 0          },   // up
        { 10,   0, GV_ANG_90  },   // right
        {  0,  10, GV_ANG_180 },   // down
        {-10,   0, GV_ANG_270 },   // left
    };
    for (int i = 0; i < GV_COUNTOF(cases); i++) {
        const ang_t got = gv_dir(gv_fix(cases[i].dx), gv_fix(cases[i].dy));
        const int32_t err = iabs_(gv_angdiff(cases[i].want, got));
        CHECK(err < 400, "dir(%d,%d) = %u, wanted %u",
              cases[i].dx, cases[i].dy, got, cases[i].want);
    }
}

// --- path interpreter -----------------------------------------------------
static void test_paths(void) {
    SECTION("path interpreter");
    gv_math_init();

    CHECK(gv_path_def(GV_PATH_NONE) == nullptr, "path 0 must be the null path");
    CHECK(gv_path_def(GV_PATH_COUNT) == nullptr, "out of range must be null");

    for (uint16_t id = 1; id < GV_PATH_COUNT; id++) {
        const gv_pathdef *d = gv_path_def(id);
        CHECK(d != nullptr, "path %u missing from the table", id);
        if (!d) continue;
        CHECK(d->nsteps > 0, "path %u (%s) has no steps", id, d->name);
        CHECK(d->name != nullptr && d->name[0] != '\0', "path %u has no name", id);
        CHECK(gv_path_ticks(id) > 0, "path %u lasts no time", id);
    }

    // A runner reports exhaustion exactly when the table runs out.
    for (uint16_t id = 1; id < GV_PATH_COUNT; id++) {
        const int total = gv_path_ticks(id);
        gv_path_runner r;
        gv_path_start(&r, id, +1);

        ang_t heading = 0;
        int steps = 0;
        while (gv_path_step(&r, &heading)) {
            if (++steps > total + 8) break;   // guard against a runaway
        }
        CHECK(steps == total, "path %u ran %d ticks, table says %d",
              id, steps, total);
        CHECK(r.finished, "path %u did not mark itself finished", id);
    }

    // Mirroring must be an exact reflection: fly both mirrors from the same
    // origin heading straight up, and the x offsets should be equal and
    // opposite while the y track matches.
    const fix_t ox = gv_fix(112), oy = gv_fix(200);
    const fix_t speed = gv_fix_from_f(1.75f);
    static SDL_FPoint pa[512], pb[512];

    for (uint16_t id = 1; id < GV_PATH_COUNT; id++) {
        const int na = gv_path_trace(id, +1, ox, oy, 0, speed, pa, 512, 1);
        const int nb = gv_path_trace(id, -1, ox, oy, 0, speed, pb, 512, 1);
        CHECK(na == nb, "path %u traced %d vs %d points", id, na, nb);

        int worst_x = 0, worst_y = 0;
        for (int i = 0; i < na && i < nb; i++) {
            const int dx = (int)((pa[i].x - 112.0f) + (pb[i].x - 112.0f));
            const int dy = (int)(pa[i].y - pb[i].y);
            if (iabs_(dx) > worst_x) worst_x = iabs_(dx);
            if (iabs_(dy) > worst_y) worst_y = iabs_(dy);
        }
        // A pixel of slack: the trig table is not infinitely precise.
        CHECK(worst_x <= 1, "path %u mirror is not symmetric in x (off by %d)",
              id, worst_x);
        CHECK(worst_y <= 1, "path %u mirror diverges in y (off by %d)",
              id, worst_y);
    }

    // Tracing from a part-way runner must continue, not restart.
    gv_path_runner r;
    gv_path_start(&r, GV_PATH_ENTRY_LOOP, +1);
    ang_t heading = 0;
    for (int i = 0; i < 20; i++) gv_path_step(&r, &heading);
    const int rest = gv_path_trace_from(&r, ox, oy, heading, speed, pa, 512, 1);
    CHECK(rest == gv_path_ticks(GV_PATH_ENTRY_LOOP) - 20,
          "resumed trace gave %d points, wanted %d",
          rest, gv_path_ticks(GV_PATH_ENTRY_LOOP) - 20);
}

// --- starfield ------------------------------------------------------------
static void test_stars(void) {
    SECTION("LFSR starfield");

    static gv_starfield a, b, c;
    gv_star_init(&a, 0xACE1u);
    gv_star_init(&b, 0xACE1u);
    gv_star_init(&c, 0x1234u);

    CHECK(a.count > 0, "no stars generated");
    CHECK(a.count <= GV_MAX_STARS, "star count %d exceeds the pool", a.count);

    CHECK(SDL_memcmp(a.stars, b.stars, sizeof a.stars) == 0,
          "same seed produced a different field");

    bool differs = false;
    for (int i = 0; i < a.count && i < c.count; i++)
        if (a.stars[i].x != c.stars[i].x || a.stars[i].y != c.stars[i].y) differs = true;
    CHECK(differs, "different seeds produced the same field");

    // A zero seed is the LFSR's dead state and must be substituted, not used.
    static gv_starfield z;
    gv_star_init(&z, 0);
    CHECK(z.count > 0, "seed 0 produced an empty field - dead LFSR state");

    for (int i = 0; i < a.count; i++) {
        CHECK(a.stars[i].x >= 0 && a.stars[i].x < GV_SCREEN_W,
              "star %d off screen in x: %d", i, a.stars[i].x);
        CHECK(a.stars[i].y >= 0 && a.stars[i].y < GV_SCREEN_H,
              "star %d off screen in y: %d", i, a.stars[i].y);
        CHECK(a.stars[i].set < GV_STAR_SETS, "star %d has a bad blink set", i);
    }

    // Scrolling must wrap rather than run away.
    gv_star_set_speed(&a, gv_fix_from_f(0.5f));
    for (int i = 0; i < 5000; i++) gv_star_tick(&a);
    CHECK(a.scroll >= 0 && a.scroll < gv_fix(GV_SCREEN_H),
          "scroll escaped its range: %d", gv_unfix(a.scroll));
}

// --- formation geometry ---------------------------------------------------
static void test_formation(void) {
    SECTION("formation geometry");

    gv_game *g = SDL_calloc(1, sizeof *g);
    if (!g) { printf("  FAIL out of memory\n"); g_fails++; return; }
    gv_game_init(g, 12345u, 0);

    int kinds[GV_EK_COUNT] = { 0 };
    for (int s = 0; s < GV_FORM_SLOTS; s++) {
        CHECK(g->slot_kind[s] < GV_EK_COUNT, "slot %d has a bad kind", s);
        kinds[g->slot_kind[s]]++;
    }
    CHECK(kinds[GV_EK_FLAGSHIP] == 4,  "flagship slots: %d", kinds[GV_EK_FLAGSHIP]);
    CHECK(kinds[GV_EK_GUARD]    == 16, "guard slots: %d",    kinds[GV_EK_GUARD]);
    CHECK(kinds[GV_EK_GRUNT]    == 20, "grunt slots: %d",    kinds[GV_EK_GRUNT]);

    // Every slot must stay on screen through the whole sway/breathe cycle,
    // sprite width included - this is what catches a spacing change that
    // pushes the outer columns off the edge.
    int worst_left = GV_SCREEN_W, worst_right = 0;
    for (int t = 0; t < 4096; t++) {
        gv_game_tick(g);
        for (int s = 0; s < GV_FORM_SLOTS; s++) {
            fix_t x, y;
            gv_form_slot_pos(g, s, &x, &y);
            const int px = gv_unfix(x);
            if (px - 8 < worst_left)  worst_left  = px - 8;
            if (px + 8 > worst_right) worst_right = px + 8;
            CHECK(gv_unfix(y) >= 0 && gv_unfix(y) < GV_SCREEN_H,
                  "slot %d y off screen: %d", s, gv_unfix(y));
        }
    }
    CHECK(worst_left >= 0, "formation reaches x=%d, off the left edge", worst_left);
    CHECK(worst_right <= GV_SCREEN_W,
          "formation reaches x=%d, off the right edge", worst_right);

    SDL_free(g);
}

// --- determinism ----------------------------------------------------------
// The whole simulation is integer-only for this reason: the same seed and the
// same inputs must give the same game, on any machine.
static uint32_t hash_state(const gv_game *g) {
    uint32_t h = 2166136261u;
    const uint8_t *p = (const uint8_t *)&g->en;
    for (size_t i = 0; i < sizeof g->en; i++) { h ^= p[i]; h *= 16777619u; }
    h ^= g->score;      h *= 16777619u;
    h ^= g->tick;       h *= 16777619u;
    h ^= (uint32_t)g->stage;
    return h;
}

static void test_determinism(void) {
    SECTION("determinism");

    gv_game *a = SDL_calloc(1, sizeof *a);
    gv_game *b = SDL_calloc(1, sizeof *b);
    if (!a || !b) { printf("  FAIL out of memory\n"); g_fails++; SDL_free(a); SDL_free(b); return; }

    gv_game_init(a, 777u, 0);
    gv_game_init(b, 777u, 0);
    gv_game_start(a, 2);
    gv_game_start(b, 2);
    a->godmode = b->godmode = true;

    for (int t = 0; t < 6000; t++) {
        gv_game_demo(a);
        gv_game_tick(a);
        gv_game_demo(b);
        gv_game_tick(b);
    }
    CHECK(hash_state(a) == hash_state(b),
          "same seed diverged after 6000 ticks (%08x vs %08x)",
          hash_state(a), hash_state(b));
    CHECK(a->score == b->score, "scores differ: %u vs %u", a->score, b->score);

    // A different seed should not land on the same state.
    gv_game *c = SDL_calloc(1, sizeof *c);
    if (c) {
        gv_game_init(c, 778u, 0);
        gv_game_start(c, 2);
        c->godmode = true;
        for (int t = 0; t < 6000; t++) { gv_game_demo(c); gv_game_tick(c); }
        CHECK(hash_state(c) != hash_state(a), "different seeds produced the same game");
        SDL_free(c);
    }

    SDL_free(a);
    SDL_free(b);
}

// --- a long unattended run must stay inside its pools ----------------------
static void test_soak_invariants(void) {
    SECTION("pool invariants over a long run");

    gv_game *g = SDL_calloc(1, sizeof *g);
    if (!g) { printf("  FAIL out of memory\n"); g_fails++; return; }
    gv_game_init(g, 4242u, 0);
    gv_game_start(g, 1);
    g->godmode = true;

    int bad = 0;
    for (int t = 0; t < 40000; t++) {
        gv_game_demo(g);
        gv_game_tick(g);

        if (g->en.hi < 0 || g->en.hi > GV_MAX_ENEMIES) bad++;
        if (g->en.live < 0 || g->en.live > GV_MAX_ENEMIES) bad++;
        if (g->ps.hi < 0 || g->ps.hi > GV_MAX_PSHOTS) bad++;
        if (g->ps.live < 0 || g->ps.live > GV_MAX_PSHOTS) bad++;
        if (g->es.hi < 0 || g->es.hi > GV_MAX_ESHOTS) bad++;
        if (g->fx.hi < 0 || g->fx.hi > GV_MAX_FX) bad++;
        if (g->divers < 0 || g->divers > GV_MAX_ENEMIES) bad++;
        if (g->beamer < -1 || g->beamer >= GV_MAX_ENEMIES) bad++;
        if (g->captor < -1 || g->captor >= GV_MAX_ENEMIES) bad++;
        if (bad) break;
    }
    CHECK(bad == 0, "pool counters went out of range at tick %u", g->tick);
    CHECK(g->stage > 1, "40000 ticks and still on stage 1 - the demo is stuck");

    SDL_free(g);
}

// --- enemy shots must always travel downward ------------------------------
// A diver used to keep firing after it drew level with the ship, and gv_dir()
// would hand back a sideways or upward heading: bullets raked across the
// bottom of the screen, and an enemy underneath the player shot straight up.
static void test_enemy_shots_aim_down(void) {
    SECTION("enemy shots travel downward");

    gv_game *g = SDL_calloc(1, sizeof *g);
    if (!g) { printf("  FAIL out of memory\n"); g_fails++; return; }
    gv_game_init(g, 4242u, 0);
    gv_game_start(g, 1);
    g->godmode = true;

    uint8_t prev[GV_MAX_ESHOTS] = {0};
    long spawned = 0, not_down = 0;
    int32_t worst_yaw = 0;

    for (int t = 0; t < 200000; t++) {
        gv_game_demo(g);
        gv_game_tick(g);

        for (int s = 0; s < GV_MAX_ESHOTS; s++) {
            if (g->es.used[s] && !prev[s]) {       // spawned on this tick
                spawned++;
                if (g->es.vy[s] <= 0) not_down++;  // level or climbing
                int32_t yaw = gv_angdiff(GV_ANG_180, gv_dir(g->es.vx[s], g->es.vy[s]));
                if (yaw < 0) yaw = -yaw;
                if (yaw > worst_yaw) worst_yaw = yaw;
            }
            prev[s] = g->es.used[s];
        }
    }

    // A sample this long with no shots at all would make the test vacuous.
    CHECK(spawned > 500, "only %ld enemy shots in 200k ticks - test is not exercising anything", spawned);
    CHECK(not_down == 0, "%ld of %ld enemy shots were level or upward", not_down, spawned);

    // One degree of slack for the atan table's quantisation.
    const int32_t limit = (int32_t)GV_ANG_DEG(51);
    CHECK(worst_yaw <= limit, "shot aimed %.1f deg off vertical, cone is 50",
          worst_yaw * 360.0 / 65536.0);

    SDL_free(g);
}

int main(void) {
    printf("gavaga tests\n");
    test_fixed();
    test_trig();
    test_paths();
    test_stars();
    test_formation();
    test_determinism();
    test_soak_invariants();
    test_enemy_shots_aim_down();

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
