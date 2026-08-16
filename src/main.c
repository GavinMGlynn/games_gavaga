// main.c - SDL3 callback entry points and the fixed-timestep loop.
//
// Logic runs at exactly GV_TICK_HZ (60.60606 Hz - 16.5 ms, the arcade rate)
// off an accumulator. Rendering is decoupled: SDL_AppIterate draws once per
// call, however many logic ticks happened to fall due.
//
// Note the SDL3 error convention - most calls return bool, true on success,
// the opposite of SDL2's "0 means OK".
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "gv_common.h"
#include "gv_game.h"
#include "gv_render.h"
#include "gv_audio.h"
#include "gv_score.h"

// The high score is written at most this often while playing, plus once on
// exit - otherwise leading the board would mean a file write per point.
#define SAVE_INTERVAL_NS 5000000000ULL

#define WINDOW_SCALE 3

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *ren;
    gv_game      *game;

    uint64_t last_ns;
    uint64_t accum_ns;

    uint64_t fps_mark_ns;
    int      fps_frames;

    // Headless capture: --shot <file.bmp> [--shot-at <tick>] fast-forwards the
    // simulation, writes one frame and exits. Handy for eyeballing changes
    // without opening a window, and for CI smoke tests.
    const char *shot_path;
    uint32_t    shot_at;
    bool        start_debug;
    bool        start_play;
    bool        start_mute;
    bool        autoplay;
    bool        godmode;
    int         start_stage;
    uint32_t    seed;
    uint16_t    start_path;

    bool     save_due;
    uint64_t last_save_ns;

    // --trace: log state transitions with their tick, so a soak run can be
    // pointed at the exact moment something interesting happened.
    bool     trace;
    uint8_t  tr_mode;
    int      tr_stage;
    bool     tr_captive, tr_dual, tr_rescue, tr_beam;
} gv_app;

static const char *mode_label(uint8_t m) {
    switch (m) {
    case GV_MODE_ATTRACT:     return "attract";
    case GV_MODE_READY:       return "ready";
    case GV_MODE_PLAY:        return "play";
    case GV_MODE_DYING:       return "dying";
    case GV_MODE_CAPTURED:    return "captured";
    case GV_MODE_STAGE_CLEAR: return "stage-clear";
    case GV_MODE_GAMEOVER:    return "game-over";
    default:                  return "?";
    }
}

static void trace_tick(gv_app *app) {
    const gv_game *g = app->game;
    if (g->mode != app->tr_mode) {
        SDL_Log("t%-6u mode %s -> %s", g->tick, mode_label(app->tr_mode), mode_label(g->mode));
        app->tr_mode = g->mode;
    }
    if (g->stage != app->tr_stage) {
        SDL_Log("t%-6u stage %d (%s)", g->tick, g->stage,
                g->challenge ? "challenging" : "normal");
        app->tr_stage = g->stage;
    }
    if ((g->beamer >= 0) != app->tr_beam) {
        app->tr_beam = g->beamer >= 0;
        SDL_Log("t%-6u tractor beam: %s", g->tick, app->tr_beam ? "deploying" : "done");
    }
    if (g->any_captive != app->tr_captive) {
        SDL_Log("t%-6u captive held: %s", g->tick, g->any_captive ? "yes" : "no");
        app->tr_captive = g->any_captive;
    }
    if (g->rescue_active != app->tr_rescue) {
        SDL_Log("t%-6u rescue ship: %s", g->tick, g->rescue_active ? "released" : "gone");
        app->tr_rescue = g->rescue_active;
    }
    if (g->player.dual != app->tr_dual) {
        SDL_Log("t%-6u dual fighter: %s", g->tick, g->player.dual ? "DOCKED" : "lost");
        app->tr_dual = g->player.dual;
    }
}

// A crude bot for soak testing (--autoplay). It walks *into* open tractor
// beams on purpose, because getting captured, shooting the boss and docking
// the freed fighter is the longest code path in the game and the one least
// likely to be reached by leaving the game running unattended.
static void autoplay_input(gv_game *g) {
    if (g->mode == GV_MODE_ATTRACT || g->mode == GV_MODE_GAMEOVER) {
        g->in.start = true;
        return;
    }
    g->in.left = g->in.right = false;
    g->in.fire = true;
    if (!g->player.alive) return;

    fix_t target   = g->player.x;
    bool  have     = false;
    bool  to_beam  = false;

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

    // Sidestep anything about to land on us - except when we are walking into
    // a beam on purpose.
    if (!to_beam) {
        bool  danger = false;
        fix_t threat = 0;

        for (int s = 0; s < g->es.hi && !danger; s++) {
            if (!g->es.used[s]) continue;
            if (g->es.y[s] < g->player.y - gv_fix(80)) continue;
            if (gv_fabs(g->es.x[s] - g->player.x) < gv_fix(14)) { danger = true; threat = g->es.x[s]; }
        }
        // Anything not parked in formation is on a trajectory, so treat it as
        // a threat once it is anywhere near our altitude.
        for (int i = 0; i < g->en.hi && !danger; i++) {
            const uint8_t st = g->en.state[i];
            if (st == GV_ES_FREE || st == GV_ES_FORM) continue;
            if (g->en.y[i] < g->player.y - gv_fix(110)) continue;
            if (gv_fabs(g->en.x[i] - g->player.x) < gv_fix(26)) { danger = true; threat = g->en.x[i]; }
        }
        if (danger)
            target = threat < g->player.x ? g->player.x + gv_fix(36)
                                          : g->player.x - gv_fix(36);
    }

    // Hold fire while closing on a beam, otherwise the bot simply shoots the
    // hovering boss down and never gets caught - which is good play, but not
    // what this run is trying to exercise.
    g->in.fire = !to_beam;

    target = gv_clampf(target, gv_fix(16), gv_fix(GV_SCREEN_W - 16));
    if (target < g->player.x - gv_fix(2))      g->in.left  = true;
    else if (target > g->player.x + gv_fix(2)) g->in.right = true;
}

// Effects leave the simulation as data; this is where they become sound and
// filesystem writes.
static void drain_events(gv_app *app) {
    gv_game *g = app->game;
    for (int i = 0; i < g->sfx_n; i++) gv_audio_play(g->sfx[i]);
    g->sfx_n = 0;

    if (g->want_save_high) {
        g->want_save_high = false;
        app->save_due = true;
    }
    if (app->trace) trace_tick(app);
}

// Reads back the current render target. Must run before SDL_RenderPresent.
static bool save_shot(SDL_Renderer *ren, const char *path) {
    SDL_Surface *surf = SDL_RenderReadPixels(ren, nullptr);
    if (!surf) {
        SDL_Log("gavaga: RenderReadPixels failed: %s", SDL_GetError());
        return false;
    }
    const bool ok = SDL_SaveBMP(surf, path);
    if (!ok) SDL_Log("gavaga: SaveBMP failed: %s", SDL_GetError());
    SDL_DestroySurface(surf);
    return ok;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    SDL_SetAppMetadata("Gavaga", "0.1.0", "dev.gavin.gavaga");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("gavaga: SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    gv_app *app = SDL_calloc(1, sizeof *app);
    if (!app) {
        SDL_Log("gavaga: out of memory");
        return SDL_APP_FAILURE;
    }
    *appstate = app;

    app->shot_at = 600;
    for (int i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--shot") == 0 && i + 1 < argc)
            app->shot_path = argv[++i];
        else if (SDL_strcmp(argv[i], "--shot-at") == 0 && i + 1 < argc)
            app->shot_at = (uint32_t)SDL_atoi(argv[++i]);
        else if (SDL_strcmp(argv[i], "--debug") == 0)
            app->start_debug = true;
        else if (SDL_strcmp(argv[i], "--path") == 0 && i + 1 < argc)
            app->start_path = (uint16_t)SDL_atoi(argv[++i]);
        else if (SDL_strcmp(argv[i], "--play") == 0)
            app->start_play = true;
        else if (SDL_strcmp(argv[i], "--mute") == 0)
            app->start_mute = true;
        else if (SDL_strcmp(argv[i], "--autoplay") == 0)
            app->autoplay = true;
        else if (SDL_strcmp(argv[i], "--trace") == 0)
            app->trace = true;
        else if (SDL_strcmp(argv[i], "--godmode") == 0)
            app->godmode = true;
        else if (SDL_strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            app->seed = (uint32_t)SDL_strtoul(argv[++i], nullptr, 10);
        else if (SDL_strcmp(argv[i], "--stage") == 0 && i + 1 < argc)
            app->start_stage = SDL_atoi(argv[++i]);
        else if (SDL_strcmp(argv[i], "--help") == 0) {
            SDL_Log("usage: gavaga [--debug] [--path N] [--play] [--stage N]"
                    " [--autoplay] [--godmode] [--seed N]\n"
                    "              [--mute] [--trace]"
                    "              [--shot FILE.bmp] [--shot-at TICK]");
            return SDL_APP_SUCCESS;
        }
    }

    if (!SDL_CreateWindowAndRenderer("Gavaga",
                                     GV_SCREEN_W * WINDOW_SCALE,
                                     GV_SCREEN_H * WINDOW_SCALE,
                                     SDL_WINDOW_RESIZABLE,
                                     &app->win, &app->ren)) {
        SDL_Log("gavaga: could not create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetRenderVSync(app->ren, 1);   // best effort; the accumulator copes either way

    // The whole game draws into a 224x288 playfield. INTEGER_SCALE keeps the
    // pixel grid square and sharp, letterboxing whatever is left over.
    if (!SDL_SetRenderLogicalPresentation(app->ren, GV_SCREEN_W, GV_SCREEN_H,
                                          SDL_LOGICAL_PRESENTATION_INTEGER_SCALE)) {
        SDL_Log("gavaga: logical presentation failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!gv_render_init(app->ren)) return SDL_APP_FAILURE;

    // Audio is optional: no device means a silent game, not a failed start.
    gv_audio_init();
    gv_audio_set_muted(app->start_mute);

    app->game = SDL_calloc(1, sizeof *app->game);
    if (!app->game) {
        SDL_Log("gavaga: out of memory");
        return SDL_APP_FAILURE;
    }
    // The only allocations in the program happen above this line.
    // A fixed seed makes a run byte-for-byte repeatable, which is the whole
    // point of the integer-only simulation - handy for reproducing a bug or a
    // screenshot.
    gv_game_init(app->game,
                 app->seed ? app->seed : (uint32_t)SDL_GetPerformanceCounter(),
                 gv_score_load());
    app->game->debug      = app->start_debug || app->start_path != GV_PATH_NONE;
    app->game->godmode    = app->godmode;
    app->game->debug_path = app->start_path;
    if (app->start_play || app->start_stage > 0 || app->autoplay)
        gv_game_start(app->game, app->start_stage > 0 ? app->start_stage : 1);

    app->last_ns     = SDL_GetTicksNS();
    app->fps_mark_ns = app->last_ns;

    SDL_Log("gavaga: %dx%d logical, logic at %.3f Hz (%llu ns/tick)",
            GV_SCREEN_W, GV_SCREEN_H, GV_TICK_HZ, (unsigned long long)GV_TICK_NS);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    gv_app *app = appstate;

    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_KEY_DOWN:
        if (event->key.scancode == SDL_SCANCODE_ESCAPE) return SDL_APP_SUCCESS;
        if (event->key.scancode == SDL_SCANCODE_F11 ||
            (event->key.scancode == SDL_SCANCODE_RETURN && (event->key.mod & SDL_KMOD_ALT))) {
            const bool fs = (SDL_GetWindowFlags(app->win) & SDL_WINDOW_FULLSCREEN) != 0;
            SDL_SetWindowFullscreen(app->win, !fs);
            return SDL_APP_CONTINUE;
        }
        if (event->key.repeat) return SDL_APP_CONTINUE;
        if (event->key.scancode == SDL_SCANCODE_M) {
            // Mute lives outside the simulation, with the rest of the audio.
            gv_audio_set_muted(!gv_audio_muted());
            return SDL_APP_CONTINUE;
        }
        gv_game_key(app->game, event->key.scancode, true);
        break;

    case SDL_EVENT_KEY_UP:
        gv_game_key(app->game, event->key.scancode, false);
        break;

    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    gv_app  *app = appstate;
    gv_game *g   = app->game;

    // Capture mode: fast-forward the simulation, draw once, write, exit.
    if (app->shot_path) {
        while (g->tick < app->shot_at) {
            if (app->autoplay) autoplay_input(g);
            gv_game_tick(g);
            drain_events(app);
        }
        gv_render_frame(g, app->ren);
        const bool ok = save_shot(app->ren, app->shot_path);
        SDL_Log("gavaga: wrote %s at tick %u", app->shot_path, g->tick);
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    const uint64_t now = SDL_GetTicksNS();
    uint64_t delta = now - app->last_ns;
    app->last_ns = now;

    // Never try to make up more than a few ticks at once: after a stall (a
    // window drag, a breakpoint) it is better to drop time than to fast
    // forward the game.
    const uint64_t max_delta = GV_TICK_NS * GV_MAX_CATCHUP_TICKS;
    if (delta > max_delta) delta = max_delta;
    app->accum_ns += delta;

    int ticks = 0;
    while (app->accum_ns >= GV_TICK_NS) {
        app->accum_ns -= GV_TICK_NS;
        if (app->autoplay) autoplay_input(g);
        if (!g->paused) {
            gv_game_tick(g);
            ticks++;
        } else if (g->step_once) {
            gv_game_tick(g);
            g->step_once = false;
            ticks++;
        }
        // While paused the accumulated time is still consumed, just not
        // simulated. Letting it pile up instead would burst-run every missed
        // tick the moment you unpause.
        drain_events(app);   // per tick, so nothing overflows the sound queue
    }
    g->ticks_last_frame = ticks;

    if (app->save_due && now - app->last_save_ns >= SAVE_INTERVAL_NS) {
        gv_score_save(g->high);
        app->save_due     = false;
        app->last_save_ns = now;
    }

    // Rolling FPS for the debug panel.
    app->fps_frames++;
    const uint64_t span = now - app->fps_mark_ns;
    if (span >= 500000000ULL) {
        g->fps = (double)app->fps_frames * 1e9 / (double)span;
        app->fps_frames  = 0;
        app->fps_mark_ns = now;
    }

    gv_render_frame(g, app->ren);
    SDL_RenderPresent(app->ren);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)result;
    gv_app *app = appstate;
    if (!app) return;

    if (app->game && (app->save_due || app->game->want_save_high))
        gv_score_save(app->game->high);

    gv_audio_quit();
    gv_render_quit();
    SDL_free(app->game);
    if (app->ren) SDL_DestroyRenderer(app->ren);
    if (app->win) SDL_DestroyWindow(app->win);
    SDL_free(app);
}
