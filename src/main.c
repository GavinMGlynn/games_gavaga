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
    uint16_t    start_path;
} gv_app;

// Reads back the current render target. Must run before SDL_RenderPresent.
static bool save_shot(SDL_Renderer *ren, const char *path) {
    SDL_Surface *surf = SDL_RenderReadPixels(ren, NULL);
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
        else if (SDL_strcmp(argv[i], "--help") == 0) {
            SDL_Log("usage: gavaga [--debug] [--path N] [--play]"
                    " [--shot FILE.bmp] [--shot-at TICK]");
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

    app->game = SDL_calloc(1, sizeof *app->game);
    if (!app->game) {
        SDL_Log("gavaga: out of memory");
        return SDL_APP_FAILURE;
    }
    // The only allocations in the program happen above this line.
    gv_game_init(app->game, (uint32_t)SDL_GetPerformanceCounter());
    app->game->debug      = app->start_debug || app->start_path != GV_PATH_NONE;
    app->game->debug_path = app->start_path;
    if (app->start_play) gv_game_key(app->game, SDL_SCANCODE_R, true);

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
        while (g->tick < app->shot_at) gv_game_tick(g);
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
        if (!g->paused) {
            gv_game_tick(g);
            ticks++;
        } else if (g->step_once) {
            gv_game_tick(g);
            g->step_once = false;
            ticks++;
        }
    }
    g->ticks_last_frame = ticks;

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

    gv_render_quit();
    SDL_free(app->game);
    if (app->ren) SDL_DestroyRenderer(app->ren);
    if (app->win) SDL_DestroyWindow(app->win);
    SDL_free(app);
}
