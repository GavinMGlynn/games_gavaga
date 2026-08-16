// main.c - SDL3 callback entry points, the fixed-timestep loop, and all
// platform I/O: input devices, the debug window, audio and the score file.
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
#include "gv_debug.h"
#include "gv_font.h"
#include "gv_sprite.h"
#include "gv_audio.h"
#include "gv_score.h"
#include "gv_window.h"
#include "gv_replay.h"

// The high score is written at most this often while playing, plus once on
// exit - otherwise leading the board would mean a file write per point.
#define SAVE_INTERVAL_NS 5000000000ULL

#define WINDOW_SCALE 3   // default; --scale N overrides
#define DEBUG_SCALE  2

// Analog stick travel before it counts as a direction, and trigger pull before
// it counts as a press.
#define PAD_DEADZONE 8000
#define PAD_TRIGGER  8000

// One set of held controls per input device, OR-ed together each frame so the
// keyboard and a gamepad cannot stamp on each other's state.
typedef struct {
    bool left, right, fire;
} gv_held;

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *ren;

    // The debug view gets its own window so it never draws over the game.
    SDL_Window   *dbg_win;
    SDL_Renderer *dbg_ren;

    gv_game      *game;

    uint64_t last_ns;
    uint64_t accum_ns;

    uint64_t fps_mark_ns;
    int      fps_frames;

    gv_held        kb, pad;
    SDL_Gamepad   *pad_dev;
    SDL_JoystickID pad_id;

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
    int         scale;
    uint16_t    start_path;
    uint32_t    seed;
    bool        seed_set;

    // Borderless fullscreen, and the windowed geometry to come back to.
    bool     faux_fs;
    int      pre_fs_x, pre_fs_y, pre_fs_w, pre_fs_h;

    bool     pad_rumbles;   // the pad reports rumble support
    bool     no_rumble;     // --no-rumble

    // Replay. Edge actions are latched here rather than applied straight to
    // the game, so the recorder sees exactly what the simulation sees.
    const char *record_path, *replay_path;
    bool        edge_start, edge_restart;
    bool        replay_done;

    bool     save_due;
    uint64_t last_save_ns;
    bool     vsync_on;

    // Where the window was last run, and what the window manager did to the
    // position we asked for. See gv_window.c.
    gv_window_state win_state;

    // Frame-pacing telemetry, printed with --trace.
    uint64_t frame_worst_ns;
    int      frame_long;      // frames that overran a tick
    int      tick_bursts;     // frames that had to run 2+ ticks

    // --trace: log state transitions with their tick, so a soak run can be
    // pointed at the exact moment something interesting happened.
    bool     trace;
    uint8_t  tr_mode;
    int      tr_stage;
    bool     tr_captive, tr_dual, tr_rescue, tr_beam;
} gv_app;

// --- tracing --------------------------------------------------------------
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

// WSL announces itself in the kernel release string, and nowhere else that is
// both cheap to read and reliable.
static bool running_under_wsl(void) {
    SDL_IOStream *io = SDL_IOFromFile("/proc/sys/kernel/osrelease", "rb");
    if (!io) return false;

    char buf[128] = { 0 };
    const size_t n = SDL_ReadIO(io, buf, sizeof buf - 1);
    SDL_CloseIO(io);
    buf[n] = '\0';

    for (char *p = buf; *p; p++) *p = (char)SDL_tolower((unsigned char)*p);
    return SDL_strstr(buf, "microsoft") || SDL_strstr(buf, "wsl");
}

// The title bar and the taskbar get the ship you fly, drawn from the same
// procedural art as the game. Purely cosmetic, so a failure is not worth
// mentioning to the player - the window simply keeps the default icon.
static void set_window_icon(SDL_Window *win) {
    if (!win) return;

    // 32px is the size most desktops reach for, and it is an exact 2x of the
    // 16px art so it stays crisp. The alternates are for Windows and macOS: on
    // X11, SDL writes only the base surface into _NET_WM_ICON.
    //
    // Note this does nothing under WSLg, whose window manager draws its own
    // generic glyph and ignores the icon entirely - see the README.
    static const int SCALES[] = { 2, 1, 3, 4 };   // 32, 16, 48 and 64 px
    SDL_Surface *icons[GV_COUNTOF(SCALES)] = { nullptr };

    icons[0] = gv_sprite_icon(SCALES[0]);
    if (!icons[0]) return;

    for (size_t i = 1; i < GV_COUNTOF(SCALES); i++) {
        icons[i] = gv_sprite_icon(SCALES[i]);
        if (icons[i]) SDL_AddSurfaceAlternateImage(icons[0], icons[i]);
    }

    SDL_SetWindowIcon(win, icons[0]);
    for (size_t i = 0; i < GV_COUNTOF(SCALES); i++)
        if (icons[i]) SDL_DestroySurface(icons[i]);
}

// Fullscreen, the long way round.
//
// SDL_SetWindowFullscreen is the obvious call and it does not work under WSLg:
// the real window becomes the size of the display, SDL is never told, and the
// renderer goes on drawing at the old size - which lands the playfield in a
// corner of the screen. SDL_SetWindowSize cannot rescue it either, because a
// fullscreen window ignores resizes. Measured: display 2560x1600, SDL still
// reporting 672x864 afterwards.
//
// A borderless window stretched over the display avoids the whole problem. We
// set the size ourselves, so SDL knows it, so the renderer is right - and on a
// desktop that handles fullscreen properly this looks the same to the player.
static void toggle_fullscreen(gv_app *app) {
    if (!app->win) return;

    if (app->faux_fs) {
        // Let the decoration change land before resizing. Stacking the two
        // together loses the border under WSLg, and a window with no title bar
        // and no way to get one back is a great deal worse than no fullscreen.
        SDL_SetWindowBordered(app->win, true);
        SDL_SyncWindow(app->win);
        SDL_SetWindowSize(app->win, app->pre_fs_w, app->pre_fs_h);
        SDL_SetWindowPosition(app->win, app->pre_fs_x, app->pre_fs_y);
        SDL_SyncWindow(app->win);
        app->faux_fs = false;
    } else {
        SDL_Rect b;
        const SDL_DisplayID id = SDL_GetDisplayForWindow(app->win);
        if (!id || !SDL_GetDisplayBounds(id, &b)) return;

        SDL_GetWindowPosition(app->win, &app->pre_fs_x, &app->pre_fs_y);
        SDL_GetWindowSize(app->win, &app->pre_fs_w, &app->pre_fs_h);

        SDL_SetWindowBordered(app->win, false);
        SDL_SyncWindow(app->win);
        SDL_SetWindowPosition(app->win, b.x, b.y);
        SDL_SetWindowSize(app->win, b.w, b.h);
        SDL_SyncWindow(app->win);
        app->faux_fs = true;
    }

    if (app->trace) {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(app->win, &w, &h);
        SDL_Log("gavaga: fullscreen %s, SDL now sees %dx%d",
                app->faux_fs ? "on" : "off", w, h);
    }
}

// --- rumble ---------------------------------------------------------------
// The simulation already hands its effects out as data, so the pad reads the
// same queue the synth does and nothing in gv_game.c has to know a pad exists.
//
// GV_SFX_SHOT is deliberately absent. With two shots allowed on screen and a
// ten-tick cooldown it fires several times a second for the whole game, which
// is a continuous buzz rather than feedback.
typedef struct { uint16_t lo, hi; uint32_t ms; } gv_rumbledef;

static const gv_rumbledef RUMBLE[GV_SFX_COUNT] = {
    [GV_SFX_PLAYER_BOOM]   = { 0xE000, 0xB000, 480 },   // losing a ship
    [GV_SFX_CAPTURED]      = { 0x9000, 0x4000, 400 },   // dragged up the beam
    [GV_SFX_FLAGSHIP_BOOM] = { 0x8000, 0x5000, 240 },
    [GV_SFX_BEAM]          = { 0x2800, 0x1000, 320 },   // a cone opening on you
    [GV_SFX_ENEMY_BOOM]    = { 0x3000, 0x1800,  80 },
    [GV_SFX_RESCUE]        = { 0x3000, 0x6000, 200 },
    [GV_SFX_EXTRA_LIFE]    = { 0x3000, 0x6000, 200 },
    [GV_SFX_FLAGSHIP_HIT]  = { 0x1800, 0x2800,  50 },
};

// One rumble per tick, the strongest of whatever happened. Restarting the
// effect for each event in the queue would let a busy tick cut its own biggest
// jolt short.
static void pad_rumble(gv_app *app) {
    if (!app->pad_dev || !app->pad_rumbles || app->no_rumble) return;

    const gv_rumbledef *best = nullptr;
    const gv_game *g = app->game;
    for (int i = 0; i < g->sfx_n; i++) {
        const gv_rumbledef *d = &RUMBLE[g->sfx[i]];
        if (!d->ms) continue;
        if (!best || (uint32_t)d->lo + d->hi > (uint32_t)best->lo + best->hi) best = d;
    }
    if (best) SDL_RumbleGamepad(app->pad_dev, best->lo, best->hi, best->ms);
}

// Effects leave the simulation as data; this is where they become sound and
// filesystem writes.
static void drain_events(gv_app *app) {
    gv_game *g = app->game;
    for (int i = 0; i < g->sfx_n; i++) gv_audio_play(g->sfx[i]);
    pad_rumble(app);
    g->sfx_n = 0;

    // The bed follows the mode, and gets busier as the stages climb.
    // gv_audio_set_music ignores a repeat of the level it is already at, so
    // calling it every tick costs nothing and never restarts the bar.
    gv_audio_set_music(g->mode == GV_MODE_PLAY
                       ? gv_clampi(1 + (g->stage - 1) / 3, 1, 4)
                       : GV_MUSIC_OFF);

    if (g->want_save_high) {
        g->want_save_high = false;
        app->save_due = true;
    }
    if (app->trace) trace_tick(app);
}

// --- gamepad --------------------------------------------------------------
static void pad_open_first(gv_app *app) {
    if (app->pad_dev) return;

    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (!ids) return;

    for (int i = 0; i < count && !app->pad_dev; i++) {
        app->pad_dev = SDL_OpenGamepad(ids[i]);
        if (app->pad_dev) {
            app->pad_id = ids[i];
            app->pad_rumbles = SDL_GetBooleanProperty(
                SDL_GetGamepadProperties(app->pad_dev),
                SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
            const char *name = SDL_GetGamepadName(app->pad_dev);
            SDL_Log("gavaga: gamepad connected: %s (rumble %s)",
                    name ? name : "unnamed",
                    app->no_rumble  ? "disabled" :
                    app->pad_rumbles ? "supported" : "not supported");
        }
    }
    SDL_free(ids);
}

static void pad_close(gv_app *app) {
    if (!app->pad_dev) return;
    // Stop any effect still running, or the pad can be left buzzing after the
    // program has gone.
    if (app->pad_rumbles) SDL_RumbleGamepad(app->pad_dev, 0, 0, 0);
    SDL_CloseGamepad(app->pad_dev);
    app->pad_dev     = nullptr;
    app->pad_id      = 0;
    app->pad_rumbles = false;
    SDL_zero(app->pad);
    SDL_Log("gavaga: gamepad disconnected");
}

static void pad_poll(gv_app *app) {
    SDL_zero(app->pad);
    if (!app->pad_dev) return;

    const Sint16 x = SDL_GetGamepadAxis(app->pad_dev, SDL_GAMEPAD_AXIS_LEFTX);
    if (x < -PAD_DEADZONE) app->pad.left  = true;
    if (x >  PAD_DEADZONE) app->pad.right = true;

    if (SDL_GetGamepadButton(app->pad_dev, SDL_GAMEPAD_BUTTON_DPAD_LEFT))
        app->pad.left = true;
    if (SDL_GetGamepadButton(app->pad_dev, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))
        app->pad.right = true;

    // Any face button fires, and so do the shoulders and triggers - whatever
    // your thumb lands on should shoot.
    if (SDL_GetGamepadButton(app->pad_dev, SDL_GAMEPAD_BUTTON_SOUTH) ||
        SDL_GetGamepadButton(app->pad_dev, SDL_GAMEPAD_BUTTON_EAST)  ||
        SDL_GetGamepadButton(app->pad_dev, SDL_GAMEPAD_BUTTON_WEST)  ||
        SDL_GetGamepadButton(app->pad_dev, SDL_GAMEPAD_BUTTON_NORTH) ||
        SDL_GetGamepadButton(app->pad_dev, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) ||
        SDL_GetGamepadButton(app->pad_dev, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)  ||
        SDL_GetGamepadAxis(app->pad_dev, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > PAD_TRIGGER ||
        SDL_GetGamepadAxis(app->pad_dev, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)  > PAD_TRIGGER)
        app->pad.fire = true;
}

// --- debug window ---------------------------------------------------------
static void debug_window_open(gv_app *app) {
    if (app->dbg_win) return;

    if (!SDL_CreateWindowAndRenderer("Gavaga - debug",
                                     GV_DBG_W * DEBUG_SCALE, GV_DBG_H * DEBUG_SCALE,
                                     SDL_WINDOW_RESIZABLE,
                                     &app->dbg_win, &app->dbg_ren)) {
        SDL_Log("gavaga: could not open the debug window: %s", SDL_GetError());
        app->dbg_win = nullptr;
        app->dbg_ren = nullptr;
        app->game->debug = false;
        return;
    }
    set_window_icon(app->dbg_win);
    SDL_SetRenderVSync(app->dbg_ren, 1);
    SDL_SetRenderLogicalPresentation(app->dbg_ren, GV_DBG_W, GV_DBG_H,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    gv_font_init(app->dbg_ren);
}

static void debug_window_close(gv_app *app) {
    if (!app->dbg_win) return;
    gv_font_quit_renderer(app->dbg_ren);
    SDL_DestroyRenderer(app->dbg_ren);
    SDL_DestroyWindow(app->dbg_win);
    app->dbg_ren = nullptr;
    app->dbg_win = nullptr;
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

// --- init -----------------------------------------------------------------
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    SDL_SetAppMetadata("Gavaga", "0.1.0", "dev.gavin.gavaga");

    // WSL2 has no kernel driver for an Xbox pad: it speaks GIP, a
    // vendor-specific protocol, so usbhid will not bind it and this kernel
    // ships no xpad. SDL can still drive it through libusb from userspace, but
    // only if its whitelist is waived - that whitelist exists to stop libusb
    // stealing devices from kernel drivers, and under WSL there are none to
    // steal. Scoped to WSL for exactly that reason: on a real Linux box this
    // hint could take a device away from the driver already handling it.
    if (running_under_wsl()) {
        SDL_SetHint("SDL_HIDAPI_LIBUSB_WHITELIST", "0");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    }

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
        else if (SDL_strcmp(argv[i], "--godmode") == 0)
            app->godmode = true;
        else if (SDL_strcmp(argv[i], "--stage") == 0 && i + 1 < argc)
            app->start_stage = SDL_atoi(argv[++i]);
        else if (SDL_strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
            app->scale = gv_clampi(SDL_atoi(argv[++i]), 1, 8);
        else if (SDL_strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            app->seed = (uint32_t)SDL_strtoul(argv[++i], nullptr, 10);
            app->seed_set = true;   // so --seed 0 means 0, not "unset"
        }
        else if (SDL_strcmp(argv[i], "--trace") == 0)
            app->trace = true;
        else if (SDL_strcmp(argv[i], "--no-rumble") == 0)
            app->no_rumble = true;
        else if (SDL_strcmp(argv[i], "--record") == 0 && i + 1 < argc)
            app->record_path = argv[++i];
        else if (SDL_strcmp(argv[i], "--replay") == 0 && i + 1 < argc)
            app->replay_path = argv[++i];
        else if (SDL_strcmp(argv[i], "--help") == 0) {
            SDL_Log("usage: gavaga [--play] [--stage N] [--seed N] [--mute]\n"
                    "              [--debug] [--path N] [--autoplay] [--godmode]\n"
                    "              [--no-rumble] [--record F.gvr] [--replay F.gvr]\n"
                    "              [--trace] [--shot FILE.bmp] [--shot-at TICK]");
            return SDL_APP_SUCCESS;
        }
    }

    // An explicit --scale is an instruction, so it wins over the saved size.
    const int scale = app->scale > 0 ? app->scale : WINDOW_SCALE;
    app->win = gv_window_create("Gavaga",
                                GV_SCREEN_W * scale,
                                GV_SCREEN_H * scale,
                                SDL_WINDOW_RESIZABLE,
                                &app->win_state);
    if (app->win && app->scale > 0)
        SDL_SetWindowSize(app->win, GV_SCREEN_W * scale, GV_SCREEN_H * scale);

    // Before the renderer: SDL's X11 backend is reported to drop the icon if it
    // arrives after one exists.
    set_window_icon(app->win);

    if (!app->win || !(app->ren = SDL_CreateRenderer(app->win, nullptr))) {
        SDL_Log("gavaga: could not create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    app->vsync_on = SDL_SetRenderVSync(app->ren, 1);   // best effort

    // The whole game draws into a 224x288 playfield. INTEGER_SCALE keeps the
    // pixel grid square and sharp, letterboxing whatever is left over.
    if (!SDL_SetRenderLogicalPresentation(app->ren, GV_SCREEN_W, GV_SCREEN_H,
                                          SDL_LOGICAL_PRESENTATION_INTEGER_SCALE)) {
        SDL_Log("gavaga: logical presentation failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!gv_render_init(app->ren)) return SDL_APP_FAILURE;

    // Audio and gamepads are both optional: a missing one is not fatal.
    gv_audio_init();
    gv_audio_set_muted(app->start_mute);

    if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        pad_open_first(app);
        if (!app->pad_dev) SDL_Log("gavaga: no gamepad detected (keyboard only)");
    } else {
        SDL_Log("gavaga: gamepad subsystem unavailable: %s", SDL_GetError());
    }

    app->game = SDL_calloc(1, sizeof *app->game);
    if (!app->game) {
        SDL_Log("gavaga: out of memory");
        return SDL_APP_FAILURE;
    }
    // The only allocations in the program happen above this line.

    // A fixed seed makes a run byte-for-byte repeatable, which is the whole
    // point of the integer-only simulation - handy for reproducing a bug or a
    // screenshot.
    // A replay carries everything that decides how the run unfolds, so it
    // overrides the command line rather than being merged with it. Anything
    // left out here is a divergence waiting to happen.
    gv_replay_head rh = { 0 };
    const bool replaying = app->replay_path && gv_replay_play_open(app->replay_path, &rh);
    if (app->replay_path && !replaying) return SDL_APP_FAILURE;
    if (replaying) {
        app->godmode     = rh.godmode;
        app->autoplay    = rh.autoplay;
        app->start_stage = rh.start_stage;
    }

    const uint32_t seed = replaying ? rh.seed
                        : app->seed_set ? app->seed
                        : (uint32_t)SDL_GetPerformanceCounter();

    // The high score is display only - it never reaches the simulation - so a
    // replay does not have to carry it.
    gv_game_init(app->game, seed, gv_score_load());
    app->game->debug      = app->start_debug || app->start_path != GV_PATH_NONE;
    app->game->debug_path = app->start_path;
    app->game->godmode    = app->godmode;

    const bool started = replaying ? rh.started
                       : (app->start_play || app->start_stage > 0 || app->autoplay);
    if (started)
        gv_game_start(app->game, app->start_stage > 0 ? app->start_stage : 1);

    if (app->record_path && !replaying) {
        const gv_replay_head h = {
            .seed        = seed,
            .start_stage = (uint16_t)(app->start_stage > 0 ? app->start_stage : 0),
            .started     = started,
            .godmode     = app->godmode,
            .autoplay    = app->autoplay,
        };
        gv_replay_record_open(app->record_path, &h);
    } else if (app->record_path) {
        SDL_Log("gavaga: --record is ignored while replaying");
    }

    app->last_ns     = SDL_GetTicksNS();
    app->fps_mark_ns = app->last_ns;

    // Which renderer got picked matters a lot for frame pacing - a software
    // fallback inside a VM behaves very differently from an accelerated one.
    const char *rname = SDL_GetRendererName(app->ren);
    SDL_Log("gavaga: renderer '%s', vsync %s",
            rname ? rname : "?", app->vsync_on ? "on" : "off");
    SDL_Log("gavaga: %dx%d logical, logic at %.3f Hz (%llu ns/tick)",
            GV_SCREEN_W, GV_SCREEN_H, GV_TICK_HZ, (unsigned long long)GV_TICK_NS);
    {
        int ww = 0, wh = 0;
        SDL_GetWindowSize(app->win, &ww, &wh);
        if (app->win_state.placed)
            SDL_Log("gavaga: window %dx%d restored to %d,%d",
                    ww, wh, app->win_state.ask_x, app->win_state.ask_y);
        else
            SDL_Log("gavaga: window %dx%d, placement left to the window manager", ww, wh);
    }
    return SDL_APP_CONTINUE;
}

// --- events ---------------------------------------------------------------
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    gv_app  *app = appstate;
    gv_game *g   = app->game;

    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_WINDOW_MOVED:
        // Tells gv_window where the window manager actually put us, which is
        // not what SDL_GetWindowPosition says immediately after creation.
        if (event->window.windowID == SDL_GetWindowID(app->win))
            gv_window_moved(&app->win_state, app->win);
        return SDL_APP_CONTINUE;

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        // Closing the debug window just puts the view away; closing the game
        // window quits.
        if (app->dbg_win && event->window.windowID == SDL_GetWindowID(app->dbg_win)) {
            g->debug = false;
            return SDL_APP_CONTINUE;
        }
        return SDL_APP_SUCCESS;

    case SDL_EVENT_GAMEPAD_ADDED:
        pad_open_first(app);
        break;

    case SDL_EVENT_GAMEPAD_REMOVED:
        if (event->gdevice.which == app->pad_id) pad_close(app);
        break;

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        switch (event->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_START: app->edge_start = true; break;
        case SDL_GAMEPAD_BUTTON_BACK:  gv_game_action(g, GV_ACT_PAUSE, true); break;
        default: break;
        }
        break;

    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = event->type == SDL_EVENT_KEY_DOWN;

        // Held controls first - these need the key-up too.
        switch (event->key.scancode) {
        case SDL_SCANCODE_LEFT:  case SDL_SCANCODE_A:
            app->kb.left = down;  return SDL_APP_CONTINUE;
        case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D:
            app->kb.right = down; return SDL_APP_CONTINUE;
        case SDL_SCANCODE_SPACE: case SDL_SCANCODE_Z: case SDL_SCANCODE_LCTRL:
            app->kb.fire = down;  return SDL_APP_CONTINUE;
        default: break;
        }

        if (!down || event->key.repeat) return SDL_APP_CONTINUE;

        switch (event->key.scancode) {
        case SDL_SCANCODE_ESCAPE:
            return SDL_APP_SUCCESS;
        case SDL_SCANCODE_F11:
            toggle_fullscreen(app);
            break;
        case SDL_SCANCODE_M:
            gv_audio_set_muted(!gv_audio_muted());
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            if (event->key.mod & SDL_KMOD_ALT)
                toggle_fullscreen(app);
            else
                app->edge_start = true;
            break;
        case SDL_SCANCODE_P:  gv_game_action(g, GV_ACT_PAUSE,   true); break;
        case SDL_SCANCODE_F2: gv_game_action(g, GV_ACT_STEP,    true); break;
        case SDL_SCANCODE_R:  app->edge_restart = true; break;
        case SDL_SCANCODE_F1: gv_game_action(g, GV_ACT_DEBUG,   true); break;
        case SDL_SCANCODE_F3: gv_game_action(g, GV_ACT_PATH,    true); break;
        default: break;
        }
        break;
    }

    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

// --- frame ----------------------------------------------------------------
// Everything the simulation is told this tick, as one byte. Going through a
// single value is what makes a recording faithful: play back the bytes and the
// game cannot tell the difference between them and a pair of hands.
static uint8_t compose_input(gv_app *app) {
    uint8_t bits = 0;
    if (app->kb.left  || app->pad.left)  bits |= GV_RP_LEFT;
    if (app->kb.right || app->pad.right) bits |= GV_RP_RIGHT;
    if (app->kb.fire  || app->pad.fire)  bits |= GV_RP_FIRE;
    if (app->edge_start)   bits |= GV_RP_START;
    if (app->edge_restart) bits |= GV_RP_RESTART;
    return bits;
}

static void apply_input(gv_game *g, uint8_t bits) {
    gv_game_action(g, GV_ACT_LEFT,  (bits & GV_RP_LEFT)  != 0);
    gv_game_action(g, GV_ACT_RIGHT, (bits & GV_RP_RIGHT) != 0);
    gv_game_action(g, GV_ACT_FIRE,  (bits & GV_RP_FIRE)  != 0);
    if (bits & GV_RP_START)   gv_game_action(g, GV_ACT_START,   true);
    if (bits & GV_RP_RESTART) gv_game_action(g, GV_ACT_RESTART, true);
}

static void step_once(gv_app *app) {
    gv_game *g = app->game;

    uint8_t bits = 0;
    if (gv_replay_playing()) {
        if (!gv_replay_play_tick(&bits)) {
            SDL_Log("gavaga: replay finished at tick %u", g->tick);
            app->replay_done = true;
            return;
        }
    } else {
        bits = compose_input(app);
        if (gv_replay_recording()) gv_replay_record_tick(bits);
    }
    app->edge_start = app->edge_restart = false;

    apply_input(g, bits);
    if (app->autoplay) gv_game_demo(g);   // the demo brain overrides if driving

    gv_game_tick(g);
    drain_events(app);
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    gv_app  *app = appstate;
    gv_game *g   = app->game;

    pad_poll(app);

    // Capture mode: fast-forward the simulation, draw once, write, exit.
    if (app->shot_path) {
        // step_once stops ticking once a replay is exhausted, so this has to
        // watch for that or it spins forever on a replay shorter than --shot-at.
        while (g->tick < app->shot_at && !app->replay_done) step_once(app);
        SDL_Renderer *target = app->ren;
        if (g->debug) {
            debug_window_open(app);
            if (app->dbg_ren) target = app->dbg_ren;
        }
        if (target == app->ren) gv_render_frame(g, app->ren);
        else                    gv_debug_draw(g, app->dbg_ren);

        const bool ok = save_shot(target, app->shot_path);
        SDL_Log("gavaga: wrote %s at tick %u", app->shot_path, g->tick);
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    const uint64_t now = SDL_GetTicksNS();
    const uint64_t raw_delta = now - app->last_ns;   // before clamping, for telemetry
    uint64_t delta = raw_delta;
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
            step_once(app);
            ticks++;
        } else if (g->step_once) {
            step_once(app);
            g->step_once = false;
            ticks++;
        }
        // While paused the accumulated time is still consumed, just not
        // simulated. Letting it pile up instead would burst-run every missed
        // tick the moment you unpause.
        if (app->replay_done) break;
    }
    g->ticks_last_frame = ticks;

    // A replay that has run out has nothing left to show; carrying on would
    // just be the attract screen wearing the replay's name.
    if (app->replay_done) return SDL_APP_SUCCESS;

    if (app->save_due && now - app->last_save_ns >= SAVE_INTERVAL_NS) {
        gv_score_save(g->high);
        app->save_due     = false;
        app->last_save_ns = now;
    }

    // Rolling FPS for the debug panel.
    // Record the raw delta: recording the clamped one only ever reports the
    // ceiling and tells you nothing about how bad a stall really was.
    if (raw_delta > app->frame_worst_ns) app->frame_worst_ns = raw_delta;
    if (raw_delta > GV_TICK_NS) app->frame_long++;
    if (ticks > 1) app->tick_bursts++;

    app->fps_frames++;
    const uint64_t span = now - app->fps_mark_ns;
    if (span >= 500000000ULL) {
        g->fps = (double)app->fps_frames * 1e9 / (double)span;
        if (app->trace)
            SDL_Log("fps %5.1f  frames %3d  long %3d  bursts %3d  worst %6.2f ms",
                    g->fps, app->fps_frames, app->frame_long, app->tick_bursts,
                    (double)app->frame_worst_ns / 1e6);
        app->fps_frames    = 0;
        app->fps_mark_ns   = now;
        app->frame_worst_ns = 0;
        app->frame_long    = 0;
        app->tick_bursts   = 0;
    }

    gv_render_frame(g, app->ren);
    SDL_RenderPresent(app->ren);

    if (g->debug && !app->dbg_win) debug_window_open(app);
    if (!g->debug && app->dbg_win) debug_window_close(app);
    if (app->dbg_win) {
        gv_debug_draw(g, app->dbg_ren);
        SDL_RenderPresent(app->dbg_ren);
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)result;
    gv_app *app = appstate;
    if (!app) return;

    if (app->game && (app->save_due || app->game->want_save_high))
        gv_score_save(app->game->high);

    // Not for --shot: that run's window is a capture buffer, not a place the
    // player put anything.
    // Borderless fullscreen is a view, not a place the player put the window.
    // Put the windowed geometry back before it gets written out, or the next
    // run opens the size of a monitor.
    if (app->faux_fs && app->win) {
        SDL_SetWindowBordered(app->win, true);
        SDL_SyncWindow(app->win);
        SDL_SetWindowSize(app->win, app->pre_fs_w, app->pre_fs_h);
        SDL_SetWindowPosition(app->win, app->pre_fs_x, app->pre_fs_y);
        SDL_SyncWindow(app->win);
        app->faux_fs = false;
    }

    if (!app->shot_path) {
        if (app->trace && app->win) {
            int x = 0, y = 0;
            SDL_GetWindowPosition(app->win, &x, &y);
            SDL_Log("gavaga: window at %d,%d on exit, wm offset %+d,%+d "
                    "(next launch asks for %d,%d)",
                    x, y, app->win_state.bias_x, app->win_state.bias_y,
                    x - app->win_state.bias_x, y - app->win_state.bias_y);
        }
        gv_window_save(app->win, &app->win_state);
    }

    gv_replay_close();
    pad_close(app);
    debug_window_close(app);
    gv_audio_quit();
    gv_render_quit();
    SDL_free(app->game);
    if (app->ren) SDL_DestroyRenderer(app->ren);
    if (app->win) SDL_DestroyWindow(app->win);
    SDL_free(app);
}
