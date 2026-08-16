// gv_window.c - persist the window's position and size between runs.
//
// Stored as a line of text next to the high score, for the same reasons: it is
// a handful of numbers, it survives being looked at, and there is nothing to
// get wrong across the three platforms.
//
// The wrinkle is that asking for a position and reading one back do not always
// agree. Under WSLg's X server, a window created at 600,300 settles at 606,327
// - a constant offset. Persist the reported number, restore it verbatim, and
// the window walks down the screen a little further every launch.
//
// So the file stores two things: where the window actually sat on screen, and
// the offset observed that session. The next run asks for position-minus-offset
// and lands back where it was. Keeping the on-screen position - rather than the
// already-corrected one - is what lets it be checked against display bounds
// directly; correcting first can push a window that sits just below a screen
// edge to a coordinate above that edge, and the off-screen guard then throws
// away a perfectly good position.
//
// Measuring the offset is the fiddly part: right after SDL_CreateWindow the
// position has not settled and reads back as something unrelated, so instead we
// wait for the window manager to tell us where it put the window. The first
// SDL_EVENT_WINDOW_MOVED to arrive in the first moment of the window's life is
// that placement; later ones are the player dragging it. A window manager that
// honours the request exactly sends no such event, the offset stays zero, and
// none of this does anything.
#include "gv_window.h"
#include "gv_common.h"   // for the C23 nullptr shim on compilers that lack it

#define GV_WIN_ORG   "gavaga"
#define GV_WIN_APP   "gavaga"
#define GV_WIN_FILE  "window.txt"
#define GV_WIN_MAGIC "gavaga-window"

#define GV_WIN_MIN   64                       // too small to find again
#define GV_WIN_MAX   16384
#define GV_WIN_BIAS_MAX 512                   // a decoration inset, not a teleport
#define GV_WIN_SETTLE_NS (SDL_NS_PER_SECOND * 2)

static bool window_path(char *buf, size_t buflen) {
    char *pref = SDL_GetPrefPath(GV_WIN_ORG, GV_WIN_APP);
    if (!pref) return false;
    SDL_snprintf(buf, buflen, "%s%s", pref, GV_WIN_FILE);
    SDL_free(pref);
    return true;
}

// A saved position is only good if the window would still be reachable. A
// monitor that has since been unplugged, or a laptop docked at a different
// resolution, would otherwise drop the window somewhere the mouse cannot go.
static bool on_some_display(const gv_window_geom *g) {
    int n = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&n);
    if (!ids) return false;

    // Require a usable strip of the title bar to overlap a display, not just a
    // corner pixel, so the window can always be grabbed and moved.
    const SDL_Rect want = { g->x, g->y, g->w, 32 };
    bool ok = false;
    for (int i = 0; i < n && !ok; i++) {
        SDL_Rect b, isect;
        if (!SDL_GetDisplayBounds(ids[i], &b)) continue;
        if (SDL_GetRectIntersection(&want, &b, &isect) && isect.w >= 96 && isect.h >= 16)
            ok = true;
    }
    SDL_free(ids);
    return ok;
}

bool gv_window_parse(const char *text, gv_window_geom *out) {
    if (!text || !out) return false;

    char magic[32] = { 0 };
    int x = 0, y = 0, w = 0, h = 0, maxed = 0, bx = 0, by = 0;
    if (SDL_sscanf(text, "%31s %d %d %d %d %d %d %d",
                   magic, &x, &y, &w, &h, &maxed, &bx, &by) != 8)
        return false;
    if (SDL_strcmp(magic, GV_WIN_MAGIC) != 0) return false;
    if (w < GV_WIN_MIN || h < GV_WIN_MIN || w > GV_WIN_MAX || h > GV_WIN_MAX)
        return false;
    // A window manager offset is a decoration inset, not a teleport. Anything
    // larger means a corrupt file, and applying it would fling the window.
    if (bx < -GV_WIN_BIAS_MAX || bx > GV_WIN_BIAS_MAX ||
        by < -GV_WIN_BIAS_MAX || by > GV_WIN_BIAS_MAX)
        return false;

    out->x = x; out->y = y; out->w = w; out->h = h;
    out->maximized = maxed != 0;
    out->bias_x = bx; out->bias_y = by;
    return true;
}

bool gv_window_format(char *buf, size_t buflen, const gv_window_geom *g) {
    if (!buf || !g) return false;
    const int len = SDL_snprintf(buf, buflen, "%s %d %d %d %d %d %d %d\n",
                                 GV_WIN_MAGIC, g->x, g->y, g->w, g->h,
                                 g->maximized ? 1 : 0, g->bias_x, g->bias_y);
    return len > 0 && (size_t)len < buflen;
}

static bool window_load(gv_window_geom *out) {
    char path[1024];
    if (!window_path(path, sizeof path)) return false;

    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) return false;   // first run

    char buf[GV_WIN_LINE_MAX];
    const size_t n = SDL_ReadIO(io, buf, sizeof buf - 1);
    SDL_CloseIO(io);
    buf[n] = '\0';

    if (!gv_window_parse(buf, out)) return false;

    if (!on_some_display(out)) {
        SDL_Log("gavaga: saved window position is off-screen, using the default");
        return false;
    }
    return true;
}

SDL_Window *gv_window_create(const char *title, int def_w, int def_h,
                             SDL_WindowFlags flags, gv_window_state *st) {
    SDL_zerop(st);

    gv_window_geom g;
    const bool have = window_load(&g);

    SDL_PropertiesID p = SDL_CreateProperties();
    if (!p) return nullptr;

    SDL_SetStringProperty(p, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,  have ? g.w : def_w);
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, have ? g.h : def_h);
    if (flags & SDL_WINDOW_RESIZABLE)
        SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);

    // Ask for the position that lands the window back where it was, using last
    // session's offset as the estimate. If the estimate is wrong - a first run,
    // or a different window manager - the window is out by the difference once,
    // and the correct offset is learned and stored before the next launch.
    const int ask_x = g.x - g.bias_x;
    const int ask_y = g.y - g.bias_y;

    // Placing at creation time is the part window managers honour. Moving an
    // already-mapped window is asynchronous and, under WSLg, unreliable.
    if (have && !g.maximized) {
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_X_NUMBER, ask_x);
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_Y_NUMBER, ask_y);
    }
    if (have && g.maximized)
        SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_MAXIMIZED_BOOLEAN, true);

    SDL_Window *win = SDL_CreateWindowWithProperties(p);
    SDL_DestroyProperties(p);
    if (!win) return nullptr;

    if (have && !g.maximized) {
        st->placed          = true;
        st->ask_x           = ask_x;
        st->ask_y           = ask_y;
        st->bias_x          = g.bias_x;   // last session's, until this one settles
        st->bias_y          = g.bias_y;
        st->settle_until_ns = SDL_GetTicksNS() + GV_WIN_SETTLE_NS;
    }
    return win;
}

void gv_window_moved(gv_window_state *st, SDL_Window *win) {
    if (!st->placed || st->bias_known) return;
    if (SDL_GetTicksNS() > st->settle_until_ns) {
        st->bias_known = true;   // too late to be the window manager; assume none
        return;
    }

    int x = 0, y = 0;
    SDL_GetWindowPosition(win, &x, &y);
    const int dx = x - st->ask_x, dy = y - st->ask_y;

    // An offset this large is not a decoration inset - it is the window manager
    // declining to place the window where it was asked and putting it somewhere
    // of its own choosing, which WSLg does intermittently. Recording that as an
    // offset would poison the next launch, so treat it as no offset at all: the
    // position simply is not restorable on this setup, and the saved one stays
    // honest about where the window ended up.
    if (dx < -GV_WIN_BIAS_MAX || dx > GV_WIN_BIAS_MAX ||
        dy < -GV_WIN_BIAS_MAX || dy > GV_WIN_BIAS_MAX) {
        st->bias_x = 0;
        st->bias_y = 0;
    } else {
        st->bias_x = dx;
        st->bias_y = dy;
    }
    st->bias_known = true;
}

void gv_window_save(SDL_Window *win, const gv_window_state *st) {
    if (!win) return;

    // Headless runs - CI, and --shot capture - have no real geometry to speak
    // of. Saving 0,0 from them would move the player's window on the next run.
    const char *drv = SDL_GetCurrentVideoDriver();
    if (!drv || SDL_strcmp(drv, "dummy") == 0 || SDL_strcmp(drv, "offscreen") == 0)
        return;

    // Fullscreen is deliberately not persisted: the size behind it is what the
    // player wants back, and starting fullscreen unasked is a nasty surprise.
    const SDL_WindowFlags flags = SDL_GetWindowFlags(win);
    if (flags & SDL_WINDOW_FULLSCREEN) return;

    const bool maxed = (flags & SDL_WINDOW_MAXIMIZED) != 0;

    int x = 0, y = 0, w = 0, h = 0;
    SDL_GetWindowPosition(win, &x, &y);
    SDL_GetWindowSize(win, &w, &h);
    if (w < GV_WIN_MIN || h < GV_WIN_MIN) return;

    char path[1024];
    if (!window_path(path, sizeof path)) return;

    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) {
        SDL_Log("gavaga: could not write %s: %s", path, SDL_GetError());
        return;
    }

    // The on-screen position goes in as-is; the offset rides alongside it so
    // the next run can work back to what to ask for.
    const gv_window_geom g = {
        .x = x, .y = y, .w = w, .h = h, .maximized = maxed,
        .bias_x = st->bias_x, .bias_y = st->bias_y,
    };
    char line[GV_WIN_LINE_MAX];
    if (gv_window_format(line, sizeof line, &g))
        SDL_WriteIO(io, line, SDL_strlen(line));
    SDL_CloseIO(io);
}
