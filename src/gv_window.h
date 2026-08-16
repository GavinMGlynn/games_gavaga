// gv_window.h - remembering where the window was last time.
#ifndef GV_WINDOW_H
#define GV_WINDOW_H

#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct {
    int  x, y;             // position, in the coordinates SDL_CreateWindow takes
    int  w, h;             // client size
    bool maximized;
} gv_window_geom;

// Tracks the difference between the position we ask a window manager for and
// the one it actually gives us. Lives in the app struct; the game only has to
// pass it back to the two calls below.
typedef struct {
    int      ask_x, ask_y;      // what we asked for at creation
    int      bias_x, bias_y;    // what the window manager added, once known
    bool     placed;            // we asked for a position at all
    bool     bias_known;
    uint64_t settle_until_ns;   // first move before this is the WM, not the user
} gv_window_state;

// Creates the window at the saved geometry if there is a usable one, otherwise
// at the given default size with placement left to the window manager.
SDL_Window *gv_window_create(const char *title, int def_w, int def_h,
                             SDL_WindowFlags flags, gv_window_state *st);

// Call on SDL_EVENT_WINDOW_MOVED. The first move to arrive shortly after
// creation is the window manager settling the window, not the player dragging
// it, and it is what tells us the offset.
void gv_window_moved(gv_window_state *st, SDL_Window *win);

// Saves the window's current geometry, with the offset taken back off.
void gv_window_save(SDL_Window *win, const gv_window_state *st);

#endif
