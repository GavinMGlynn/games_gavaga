// gv_debug.c - developer overlay.
#include "gv_debug.h"
#include "gv_font.h"

#define PRED_MAX    320    // samples of predicted flight per entity
#define PRED_STRIDE 2      // ticks between samples

static SDL_FPoint s_pts[PRED_MAX];

static const SDL_Color STATE_COLOR[] = {
    [GV_ES_FREE]  = {   0,   0,   0,   0 },
    [GV_ES_ENTER] = {  80, 220, 255, 255 },   // cyan
    [GV_ES_TUCK]  = { 255, 210,  80, 255 },   // amber
    [GV_ES_FORM]  = { 130, 130, 150, 255 },   // grey
    [GV_ES_DIVE]  = { 255,  90,  90, 255 },   // red
    [GV_ES_BEAM]  = { 120, 255, 160, 255 },   // green
    [GV_ES_FLYBY] = { 230, 110, 230, 255 },   // magenta
};

static const char *const STATE_NAME[] = {
    [GV_ES_FREE] = "free", [GV_ES_ENTER] = "enter", [GV_ES_TUCK] = "tuck",
    [GV_ES_FORM] = "form", [GV_ES_DIVE]  = "dive",  [GV_ES_BEAM] = "beam",
    [GV_ES_FLYBY] = "flyby",
};

static const char *mode_name(uint8_t m) {
    switch (m) {
    case GV_MODE_ATTRACT:     return "ATTRACT";
    case GV_MODE_READY:       return "READY";
    case GV_MODE_PLAY:        return "PLAY";
    case GV_MODE_DYING:       return "DYING";
    case GV_MODE_CAPTURED:    return "CAUGHT";
    case GV_MODE_STAGE_CLEAR: return "CLEAR";
    case GV_MODE_GAMEOVER:    return "OVER";
    default:                  return "?";
    }
}

static void set_color(SDL_Renderer *ren, SDL_Color c, uint8_t a) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, a);
}

// --- pieces ---------------------------------------------------------------
static void draw_slots(const gv_game *g, SDL_Renderer *ren) {
    for (int s = 0; s < GV_FORM_SLOTS; s++) {
        fix_t sx, sy;
        gv_form_slot_pos(g, s, &sx, &sy);
        const float x = (float)gv_unfix(sx);
        const float y = (float)gv_unfix(sy);

        if (g->form.occupied[s]) SDL_SetRenderDrawColor(ren,  60, 110,  60, 160);
        else                     SDL_SetRenderDrawColor(ren,  90,  40,  40, 130);

        SDL_RenderLine(ren, x - 2.0f, y, x + 2.0f, y);
        SDL_RenderLine(ren, x, y - 2.0f, x, y + 2.0f);
    }
}

static void draw_trail(const gv_enemies *en, int i, SDL_Renderer *ren, SDL_Color c) {
    const int n = en->trail_n[i];
    if (n < 2) return;

    const int head = en->trail_head[i];
    const int start = (head - n + GV_TRAIL_LEN) % GV_TRAIL_LEN;

    for (int k = 0; k < n; k++) {
        const int idx = (start + k) % GV_TRAIL_LEN;
        s_pts[k].x = (float)en->trail_x[i][idx];
        s_pts[k].y = (float)en->trail_y[i][idx];
    }
    // Fade the tail out so direction of travel is obvious.
    set_color(ren, c, 90);
    SDL_RenderLines(ren, s_pts, n);
}

static void draw_prediction(const gv_game *g, int i, SDL_Renderer *ren, SDL_Color c) {
    const int n = gv_path_trace_from(&g->en.path[i], g->en.x[i], g->en.y[i],
                                     g->en.ang[i], g->en.spd[i],
                                     s_pts, PRED_MAX, PRED_STRIDE);
    if (n < 2) return;
    set_color(ren, c, 200);
    SDL_RenderLines(ren, s_pts, n);

    // Mark where the path ends and the entity hands off to another state.
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 220);
    const SDL_FRect end = { s_pts[n - 1].x - 1.0f, s_pts[n - 1].y - 1.0f, 3.0f, 3.0f };
    SDL_RenderRect(ren, &end);
}

static void draw_hitboxes(const gv_game *g, SDL_Renderer *ren) {
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 70);
    for (int i = 0; i < g->en.hi; i++) {
        if (g->en.state[i] == GV_ES_FREE) continue;
        const float half = g->en.kind[i] == GV_EK_FLAGSHIP ? 7.0f : 6.0f;
        const SDL_FRect r = { (float)gv_unfix(g->en.x[i]) - half,
                              (float)gv_unfix(g->en.y[i]) - half,
                              half * 2.0f, half * 2.0f };
        SDL_RenderRect(ren, &r);
    }
    if (g->player.alive) {
        const SDL_FRect r = { (float)gv_unfix(g->player.x) - 6.0f,
                              (float)gv_unfix(g->player.y) - 6.0f, 12.0f, 12.0f };
        SDL_SetRenderDrawColor(ren, 120, 255, 120, 90);
        SDL_RenderRect(ren, &r);
    }
}

// F3 catalogue: draw one path definition on its own, both mirrors, from an
// origin that suits its class. This is the view to use when editing the
// tables in gv_paths.c.
static void draw_catalogue(const gv_game *g, SDL_Renderer *ren) {
    const uint16_t id = g->debug_path;
    if (id == GV_PATH_NONE || id >= GV_PATH_COUNT) return;

    fix_t ox, oy, speed;
    ang_t oa;
    if (id <= GV_PATH_ENTRY_CROSS) {
        ox = gv_fix(GV_SCREEN_W / 2); oy = gv_fix(276); oa = 0;
        speed = gv_fix_from_f(1.75f);
    } else if (id <= GV_PATH_BEAM_DIVE) {
        ox = gv_fix(GV_SCREEN_W / 2); oy = gv_fix(72); oa = GV_ANG_180;
        speed = gv_fix_from_f(1.55f);
    } else {
        ox = gv_fix(20); oy = gv_fix(140); oa = GV_ANG_90;
        speed = gv_fix_from_f(1.75f);
    }

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 238);
    const SDL_FRect bg = { 0.0f, 0.0f, (float)GV_SCREEN_W, (float)GV_SCREEN_H };
    SDL_RenderFillRect(ren, &bg);

    for (int m = 0; m < 2; m++) {
        const int8_t mirror = m == 0 ? (int8_t)+1 : (int8_t)-1;
        const int n = gv_path_trace(id, mirror, ox, oy, oa, speed,
                                    s_pts, PRED_MAX, 1);
        if (n < 2) continue;
        if (m == 0) SDL_SetRenderDrawColor(ren,  90, 220, 255, 255);
        else        SDL_SetRenderDrawColor(ren, 230, 110, 230, 160);
        SDL_RenderLines(ren, s_pts, n);
    }

    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    const SDL_FRect o = { (float)gv_unfix(ox) - 2.0f, (float)gv_unfix(oy) - 2.0f, 5.0f, 5.0f };
    SDL_RenderRect(ren, &o);

    const SDL_Color w = { 255, 255, 255, 255 };
    const SDL_Color d = { 150, 150, 165, 255 };
    gv_font_printf(ren, 4, 24, w, "PATH %u/%u  %s", id, GV_PATH_COUNT - 1, gv_path_name(id));
    gv_font_printf(ren, 4, 33, d, "%d TICKS  %d STEPS", gv_path_ticks(id),
                   gv_path_def(id) ? gv_path_def(id)->nsteps : 0);
    gv_font_draw(ren, 4, 42, d, "F3 NEXT  CYAN +1  PINK -1");
}

static void draw_panel(const gv_game *g, SDL_Renderer *ren) {
    const SDL_Color w = { 235, 235, 245, 255 };
    const SDL_Color y = { 250, 220, 110, 255 };

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 170);
    const SDL_FRect bg = { 0.0f, (float)(GV_SCREEN_H - 46), (float)GV_SCREEN_W, 36.0f };
    SDL_RenderFillRect(ren, &bg);

    int enter = 0, tuck = 0, form = 0, dive = 0, flyby = 0, beam = 0;
    for (int i = 0; i < g->en.hi; i++) {
        switch (g->en.state[i]) {
        case GV_ES_ENTER: enter++; break;
        case GV_ES_TUCK:  tuck++;  break;
        case GV_ES_FORM:  form++;  break;
        case GV_ES_DIVE:  dive++;  break;
        case GV_ES_BEAM:  beam++;  break;
        case GV_ES_FLYBY: flyby++; break;
        default: break;
        }
    }
    int ps = 0, es = 0, fx = 0;
    for (int i = 0; i < g->ps.hi; i++) ps += g->ps.used[i] ? 1 : 0;
    for (int i = 0; i < g->es.hi; i++) es += g->es.used[i] ? 1 : 0;
    for (int i = 0; i < g->fx.hi; i++) fx += g->fx.used[i] ? 1 : 0;

    const int top = GV_SCREEN_H - 44;
    gv_font_printf(ren, 3, top, y, "%s ST%d T%u %.1fFPS %dTK",
                   mode_name(g->mode), g->stage, g->tick, g->fps, g->ticks_last_frame);
    gv_font_printf(ren, 3, top + 9, w, "EN%d E%d T%d F%d D%d B%d Y%d",
                   g->en.live, enter, tuck, form, dive, beam, flyby);
    gv_font_printf(ren, 3, top + 18, w, "PS%d ES%d FX%d CAP%d%s%s",
                   ps, es, fx, g->beamer >= 0 ? 1 : 0,
                   g->any_captive ? " HELD" : "",
                   g->player.dual ? " DUAL" : "");
}

// --- entry point ----------------------------------------------------------
void gv_debug_draw(const gv_game *g, SDL_Renderer *ren) {
    SDL_BlendMode prev = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(ren, &prev);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    if (g->debug_path != GV_PATH_NONE) {
        draw_catalogue(g, ren);
    } else {
        draw_slots(g, ren);

        for (int i = 0; i < g->en.hi; i++) {
            const uint8_t st = g->en.state[i];
            if (st == GV_ES_FREE) continue;
            const SDL_Color c = STATE_COLOR[st];
            draw_trail(&g->en, i, ren, c);
            if (st == GV_ES_ENTER || st == GV_ES_DIVE || st == GV_ES_FLYBY)
                draw_prediction(g, i, ren, c);
        }
        draw_hitboxes(g, ren);
    }

    draw_panel(g, ren);

    // Name whatever the first diving enemy is doing - handy while tuning.
    for (int i = 0; i < g->en.hi; i++) {
        if (g->en.state[i] != GV_ES_DIVE) continue;
        const SDL_Color c = { 255, 160, 160, 255 };
        gv_font_printf(ren, 3, GV_SCREEN_H - 8, c, "%s %s M%+d",
                       STATE_NAME[g->en.state[i]], gv_path_name(g->en.path[i].def),
                       g->en.path[i].mirror);
        break;
    }

    SDL_SetRenderDrawBlendMode(ren, prev);
}
