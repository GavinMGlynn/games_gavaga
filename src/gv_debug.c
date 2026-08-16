// gv_debug.c - developer view, drawn into its own window.
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

// --- playfield view -------------------------------------------------------
static void draw_slots(const gv_game *g, SDL_Renderer *ren) {
    for (int s = 0; s < GV_FORM_SLOTS; s++) {
        fix_t sx, sy;
        gv_form_slot_pos(g, s, &sx, &sy);
        const float x = (float)gv_unfix(sx);
        const float y = (float)gv_unfix(sy);

        if (g->form.occupied[s]) SDL_SetRenderDrawColor(ren,  70, 140,  70, 200);
        else                     SDL_SetRenderDrawColor(ren, 110,  50,  50, 160);

        SDL_RenderLine(ren, x - 2.0f, y, x + 2.0f, y);
        SDL_RenderLine(ren, x, y - 2.0f, x, y + 2.0f);
    }
}

static void draw_trail(const gv_enemies *en, int i, SDL_Renderer *ren, SDL_Color c) {
    const int n = en->trail_n[i];
    if (n < 2) return;

    const int head  = en->trail_head[i];
    const int start = (head - n + GV_TRAIL_LEN) % GV_TRAIL_LEN;

    for (int k = 0; k < n; k++) {
        const int idx = (start + k) % GV_TRAIL_LEN;
        s_pts[k].x = (float)en->trail_x[i][idx];
        s_pts[k].y = (float)en->trail_y[i][idx];
    }
    set_color(ren, c, 110);
    SDL_RenderLines(ren, s_pts, n);
}

static void draw_prediction(const gv_game *g, int i, SDL_Renderer *ren, SDL_Color c) {
    const int n = gv_path_trace_from(&g->en.path[i], g->en.x[i], g->en.y[i],
                                     g->en.ang[i], g->en.spd[i],
                                     s_pts, PRED_MAX, PRED_STRIDE);
    if (n < 2) return;
    set_color(ren, c, 220);
    SDL_RenderLines(ren, s_pts, n);

    // Mark where the path ends and the entity hands off to another state.
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 230);
    const SDL_FRect end = { s_pts[n - 1].x - 1.0f, s_pts[n - 1].y - 1.0f, 3.0f, 3.0f };
    SDL_RenderRect(ren, &end);
}

static void draw_hitboxes(const gv_game *g, SDL_Renderer *ren) {
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 80);
    for (int i = 0; i < g->en.hi; i++) {
        if (g->en.state[i] == GV_ES_FREE) continue;
        const float half = g->en.kind[i] == GV_EK_FLAGSHIP ? 7.0f : 6.0f;
        const SDL_FRect r = { (float)gv_unfix(g->en.x[i]) - half,
                              (float)gv_unfix(g->en.y[i]) - half,
                              half * 2.0f, half * 2.0f };
        SDL_RenderRect(ren, &r);
    }
    if (g->player.alive) {
        const float half = g->player.dual ? 6.0f + (float)GV_DUAL_OFFSET : 6.0f;
        const SDL_FRect r = { (float)gv_unfix(g->player.x) - half,
                              (float)gv_unfix(g->player.y) - 6.0f,
                              half * 2.0f, 12.0f };
        SDL_SetRenderDrawColor(ren, 120, 255, 120, 110);
        SDL_RenderRect(ren, &r);
    }
}

static void draw_beam_outlines(const gv_game *g, SDL_Renderer *ren) {
    for (int i = 0; i < g->en.hi; i++) {
        fix_t top, bottom, half_bot;
        if (!gv_beam_shape(g, i, &top, &bottom, &half_bot)) continue;

        const float ex = (float)gv_unfix(g->en.x[i]);
        const float y0 = (float)gv_unfix(top);
        const float y1 = (float)gv_unfix(bottom);
        const float hb = (float)gv_unfix(half_bot);
        const float ht = (float)GV_BEAM_HALF_TOP;

        SDL_SetRenderDrawColor(ren, 140, 255, 200, 220);
        SDL_RenderLine(ren, ex - ht, y0, ex - hb, y1);
        SDL_RenderLine(ren, ex + ht, y0, ex + hb, y1);
        SDL_RenderLine(ren, ex - hb, y1, ex + hb, y1);
    }
}

// F3 catalogue: one path definition on its own, both mirrors, from an origin
// that suits its class. This is the view to use when editing gv_paths.c.
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

    for (int m = 0; m < 2; m++) {
        const int8_t mirror = m == 0 ? (int8_t)+1 : (int8_t)-1;
        const int n = gv_path_trace(id, mirror, ox, oy, oa, speed,
                                    s_pts, PRED_MAX, 1);
        if (n < 2) continue;
        if (m == 0) SDL_SetRenderDrawColor(ren,  90, 220, 255, 255);
        else        SDL_SetRenderDrawColor(ren, 230, 110, 230, 180);
        SDL_RenderLines(ren, s_pts, n);
    }

    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    const SDL_FRect o = { (float)gv_unfix(ox) - 2.0f, (float)gv_unfix(oy) - 2.0f, 5.0f, 5.0f };
    SDL_RenderRect(ren, &o);

    const SDL_Color w = { 255, 255, 255, 255 };
    const SDL_Color d = { 150, 150, 165, 255 };
    gv_font_printf(ren, 4, 6, w, "PATH %u/%u  %s", id, GV_PATH_COUNT - 1, gv_path_name(id));
    gv_font_printf(ren, 4, 15, d, "%d TICKS  %d STEPS  CYAN +1  PINK -1",
                   gv_path_ticks(id), gv_path_def(id) ? gv_path_def(id)->nsteps : 0);
}

// --- panel ----------------------------------------------------------------
static void draw_legend(SDL_Renderer *ren, int x, int y) {
    static const uint8_t order[] = {
        GV_ES_ENTER, GV_ES_TUCK, GV_ES_FORM, GV_ES_DIVE, GV_ES_BEAM, GV_ES_FLYBY
    };
    for (int i = 0; i < (int)(sizeof order / sizeof *order); i++) {
        const uint8_t st = order[i];
        const SDL_FRect sw = { (float)(x + i * 36), (float)y, 5.0f, 5.0f };
        set_color(ren, STATE_COLOR[st], 255);
        SDL_RenderFillRect(ren, &sw);
        const SDL_Color c = { 190, 190, 200, 255 };
        gv_font_draw(ren, x + i * 36 + 8, y - 1, c, STATE_NAME[st]);
    }
}

static void draw_panel(const gv_game *g, SDL_Renderer *ren) {
    const SDL_Color w = { 235, 235, 245, 255 };
    const SDL_Color y = { 250, 220, 110, 255 };
    const SDL_Color d = { 150, 150, 165, 255 };

    const int top = GV_SCREEN_H;

    SDL_SetRenderDrawColor(ren, 18, 18, 24, 255);
    const SDL_FRect bg = { 0.0f, (float)top, (float)GV_DBG_W, (float)GV_DBG_PANEL_H };
    SDL_RenderFillRect(ren, &bg);
    SDL_SetRenderDrawColor(ren, 70, 70, 90, 255);
    SDL_RenderLine(ren, 0.0f, (float)top, (float)GV_DBG_W, (float)top);

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

    int line = top + 4;
    gv_font_printf(ren, 3, line, y, "%s ST%d T%u %.1fFPS %dTK%s",
                   mode_name(g->mode), g->stage, g->tick, g->fps,
                   g->ticks_last_frame, g->paused ? " PAUSED" : "");
    line += 9;
    gv_font_printf(ren, 3, line, w, "EN%d E%d T%d F%d D%d B%d Y%d",
                   g->en.live, enter, tuck, form, dive, beam, flyby);
    line += 9;
    gv_font_printf(ren, 3, line, w, "PS%d ES%d FX%d BEAM%d%s%s COMBO%d",
                   ps, es, fx, g->beamer >= 0 ? 1 : 0,
                   g->any_captive ? " HELD" : "",
                   g->player.dual ? " DUAL" : "", g->combo);
    line += 9;
    gv_font_printf(ren, 3, line, w, "SCORE%u HIGH%u LIVES%d X%d",
                   g->score, g->high, g->player.lives, gv_unfix(g->player.x));
    line += 9;

    // Name what the first attacker is flying - handy while tuning tables.
    const char *what = "-";
    const char *pname = "-";
    int mirror = 0;
    for (int i = 0; i < g->en.hi; i++) {
        const uint8_t st = g->en.state[i];
        if (st != GV_ES_DIVE && st != GV_ES_BEAM && st != GV_ES_ENTER) continue;
        what   = STATE_NAME[st];
        pname  = gv_path_name(g->en.path[i].def);
        mirror = g->en.path[i].mirror;
        break;
    }
    gv_font_printf(ren, 3, line, d, "%s %s M%+d", what, pname, mirror);
    line += 9;

    draw_legend(ren, 3, line + 1);
    line += 10;
    gv_font_draw(ren, 3, line, d, "F1 CLOSE  F3 PATHS  P PAUSE  F2 STEP");
}

// --- entry point ----------------------------------------------------------
void gv_debug_draw(const gv_game *g, SDL_Renderer *ren) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(ren, 8, 8, 12, 255);
    SDL_RenderClear(ren);

    if (g->debug_path != GV_PATH_NONE) {
        draw_catalogue(g, ren);
    } else {
        draw_slots(g, ren);
        draw_beam_outlines(g, ren);

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

    // Frame the playfield so it is obvious where the 224x288 field ends.
    SDL_SetRenderDrawColor(ren, 60, 60, 80, 255);
    const SDL_FRect field = { 0.0f, 0.0f, (float)GV_SCREEN_W, (float)GV_SCREEN_H };
    SDL_RenderRect(ren, &field);

    draw_panel(g, ren);
}
