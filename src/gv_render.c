// gv_render.c - draws one frame of whatever the simulation last produced.
//
// Positions are snapped to whole logical pixels. The playfield is 224x288 and
// is presented with INTEGER_SCALE, so sub-pixel interpolation between ticks
// would only smear the pixel grid; the render rate is still free-running and
// independent of the 60.606 Hz logic rate.
#include "gv_render.h"
#include "gv_sprite.h"
#include "gv_font.h"
#include "gv_audio.h"

static const SDL_Color C_WHITE = { 255, 255, 255, 255 };
static const SDL_Color C_RED   = { 232,  56,  40, 255 };
static const SDL_Color C_CYAN  = {  72, 224, 224, 255 };
static const SDL_Color C_YELL  = { 248, 224,  88, 255 };
static const SDL_Color C_DIM   = { 128, 128, 144, 255 };

bool gv_render_init(SDL_Renderer *ren) {
    return gv_sprite_init(ren) && gv_font_init(ren);
}

void gv_render_quit(void) {
    gv_font_quit();
    gv_sprite_quit();
}

void gv_draw_sprite_px(SDL_Renderer *ren, int spr, int x, int y) {
    const SDL_FRect *src = gv_sprite_rect(spr);
    const SDL_FRect dst = { (float)x, (float)y, src->w, src->h };
    SDL_RenderTexture(ren, gv_sprite_texture(), src, &dst);
}

void gv_draw_sprite(SDL_Renderer *ren, int spr, fix_t x, fix_t y) {
    const SDL_FRect *src = gv_sprite_rect(spr);
    gv_draw_sprite_px(ren, spr,
                      gv_unfix(x) - (int)src->w / 2,
                      gv_unfix(y) - (int)src->h / 2);
}

void gv_draw_sprite_rot(SDL_Renderer *ren, int spr, fix_t x, fix_t y, ang_t a) {
    const SDL_FRect *src = gv_sprite_rect(spr);
    const SDL_FRect dst = { (float)(gv_unfix(x) - (int)src->w / 2),
                            (float)(gv_unfix(y) - (int)src->h / 2),
                            src->w, src->h };
    const double deg = (double)a * (360.0 / 65536.0);
    SDL_RenderTextureRotated(ren, gv_sprite_texture(), src, &dst, deg,
                             nullptr, SDL_FLIP_NONE);
}

// --- tractor beams --------------------------------------------------------
static void draw_beams(const gv_game *g, SDL_Renderer *ren) {
    #define CAP ((GV_BEAM_LEN + 2) * 2)
    static SDL_FRect bright[CAP], dim[CAP];
    int nbright = 0, ndim = 0;

    SDL_BlendMode prev = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(ren, &prev);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    for (int i = 0; i < g->en.hi; i++) {
        fix_t top, bottom, half_bot;
        if (!gv_beam_shape(g, i, &top, &bottom, &half_bot)) continue;

        const int   y0 = gv_unfix(top);
        const int   y1 = gv_unfix(bottom);
        const float ex = (float)gv_unfix(g->en.x[i]);
        const float hb = (float)gv_unfix(half_bot);

        // Same batching problem as the starfield: a colour change per scanline
        // is 128 draw calls. Collect the two band shades and issue two.
        for (int y = y0; y <= y1 && (nbright < CAP && ndim < CAP); y++) {
            const float t = y1 > y0 ? (float)(y - y0) / (float)(y1 - y0) : 0.0f;
            const float half = (float)GV_BEAM_HALF_TOP
                             + (hb - (float)GV_BEAM_HALF_TOP) * t;
            const SDL_FRect row = { ex - half, (float)y, half * 2.0f, 1.0f };
            // Bands scrolling down the cone sell the pulling motion.
            if (((y + (int)(g->tick * 2u)) / 4) & 1) bright[nbright++] = row;
            else                                     dim[ndim++]       = row;
        }
    }

    if (nbright) {
        SDL_SetRenderDrawColor(ren, 150, 235, 255, 150);
        SDL_RenderFillRects(ren, bright, nbright);
    }
    if (ndim) {
        SDL_SetRenderDrawColor(ren, 150, 235, 255, 60);
        SDL_RenderFillRects(ren, dim, ndim);
    }
    SDL_SetRenderDrawBlendMode(ren, prev);
}

// --- HUD ------------------------------------------------------------------
static void draw_hud(const gv_game *g, SDL_Renderer *ren) {
    // Blink "1UP" like the original marquee.
    if ((g->tick / 20) & 1u) gv_font_draw(ren, 8, 2, C_RED, "1UP");
    gv_font_printf(ren, 8, 11, C_WHITE, "%06u", g->score);

    gv_font_center(ren, 2, C_RED, "HIGH SCORE");
    char buf[16];
    SDL_snprintf(buf, sizeof buf, "%06u", g->high);
    gv_font_center(ren, 11, C_WHITE, buf);

    // Spare ships, bottom left. Past five it becomes a count rather than a row
    // of icons running into the stage badges.
    const int spare = gv_clampi(g->player.lives - 1, 0, 99);
    const int icons = spare > 5 ? 1 : spare;
    for (int i = 0; i < icons; i++)
        gv_draw_sprite_px(ren, GV_SPR_LIFE, 4 + i * 10, GV_SCREEN_H - 10);
    if (spare > 5) gv_font_printf(ren, 15, GV_SCREEN_H - 9, C_WHITE, "X%d", spare);

    // Stage badges, bottom right: tens, then fives, then ones, so stage 37 is
    // three gold, one grey and two green rather than 37 icons.
    static const struct { int worth; int spr; } TIERS[] = {
        { 10, GV_SPR_BADGE10 }, { 5, GV_SPR_BADGE5 }, { 1, GV_SPR_BADGE }
    };
    int x = GV_SCREEN_W - 12, left = gv_clampi(g->stage, 0, 99), drawn = 0;
    for (int t = 0; t < 3 && drawn < 8; t++) {
        while (left >= TIERS[t].worth && drawn < 8) {
            gv_draw_sprite_px(ren, TIERS[t].spr, x, GV_SCREEN_H - 10);
            x -= 10;
            left -= TIERS[t].worth;
            drawn++;
        }
    }

    if (gv_audio_muted()) gv_font_draw(ren, GV_SCREEN_W - 29, 11, C_DIM, "MUTE");
}

static void draw_mode_text(const gv_game *g, SDL_Renderer *ren) {
    char buf[32];

    switch (g->mode) {
    case GV_MODE_ATTRACT:
        // Sits below the formation block so the two never overlap.
        gv_font_center(ren, 148, C_CYAN, "G A V A G A");
        gv_font_center(ren, 160, C_DIM,  "A GALAGA-ISH THING");
        if ((g->tick / 30) & 1u)
            gv_font_center(ren, 184, C_YELL, "PRESS ENTER TO START");
        gv_font_center(ren, 210, C_DIM, "ARROWS MOVE   SPACE FIRE");
        gv_font_center(ren, 220, C_DIM, "F1 DEBUG  P PAUSE  M MUTE");
        break;

    case GV_MODE_READY:
        SDL_snprintf(buf, sizeof buf, "STAGE %d", g->stage);
        gv_font_center(ren, 140, C_CYAN, g->challenge ? "CHALLENGING STAGE" : buf);
        if (g->mode_timer > 40) gv_font_center(ren, 156, C_RED, "READY");
        break;

    case GV_MODE_CAPTURED:
        if ((g->tick / 8) & 1u)
            gv_font_center(ren, 176, C_RED, "FIGHTER CAPTURED");
        break;

    case GV_MODE_STAGE_CLEAR:
        if (g->challenge) {
            SDL_snprintf(buf, sizeof buf, "%u OF %u HITS", g->chal_killed, g->chal_spawned);
            gv_font_center(ren, 140, C_CYAN, buf);
            if (g->chal_spawned > 0 && g->chal_killed >= g->chal_spawned)
                gv_font_center(ren, 156, C_YELL, "PERFECT  1000 PTS");
        } else {
            gv_font_center(ren, 148, C_CYAN, "STAGE CLEAR");
        }
        break;

    case GV_MODE_GAMEOVER:
        gv_font_center(ren, 148, C_RED, "GAME OVER");
        break;

    default: break;
    }

    if (g->paused) gv_font_center(ren, 176, C_YELL, "PAUSED  F2 STEPS");
}

// --- frame ----------------------------------------------------------------
void gv_render_frame(const gv_game *g, SDL_Renderer *ren) {
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);

    gv_star_draw(&g->stars, ren);

    // Beams go under everything so the ships stay readable inside them.
    draw_beams(g, ren);

    // Enemies, and any fighter one of them is holding.
    for (int i = 0; i < g->en.hi; i++) {
        if (g->en.state[i] == GV_ES_FREE) continue;
        gv_draw_sprite(ren, gv_enemy_sprite(g, i), g->en.x[i], g->en.y[i]);
        if (g->en.captive[i])
            gv_draw_sprite(ren, GV_SPR_CAPTIVE, g->en.x[i], g->en.y[i] + gv_fix(14));
    }

    // Player shots.
    for (int i = 0; i < g->ps.hi; i++) {
        if (!g->ps.used[i]) continue;
        gv_draw_sprite(ren, GV_SPR_PSHOT, g->ps.x[i], g->ps.y[i]);
    }

    // Enemy shots.
    for (int i = 0; i < g->es.hi; i++) {
        if (!g->es.used[i]) continue;
        gv_draw_sprite(ren, GV_SPR_ESHOT, g->es.x[i], g->es.y[i]);
    }

    // Player - flashes while the respawn invulnerability is running, and is
    // two hulls wide once a rescued fighter has docked.
    if (g->player.alive && (g->player.invuln == 0 || ((g->tick / 4) & 1u))) {
        if (g->player.dual) {
            gv_draw_sprite(ren, GV_SPR_PLAYER,
                           g->player.x - gv_fix(GV_DUAL_OFFSET), g->player.y);
            gv_draw_sprite(ren, GV_SPR_PLAYER,
                           g->player.x + gv_fix(GV_DUAL_OFFSET), g->player.y);
        } else {
            gv_draw_sprite(ren, GV_SPR_PLAYER, g->player.x, g->player.y);
        }
    }

    // A freed fighter on its way down, flickering between their colours and
    // yours as it changes hands.
    if (g->rescue_active) {
        const int spr = ((g->tick / 5) & 1u) ? GV_SPR_PLAYER : GV_SPR_CAPTIVE;
        gv_draw_sprite(ren, spr, g->rescue_x, g->rescue_y);
    }

    // The capture itself: your ship spiralling up into the boss.
    if (g->mode == GV_MODE_CAPTURED)
        gv_draw_sprite_rot(ren, GV_SPR_PLAYER, g->cap_x, g->cap_y, g->cap_ang);

    // Explosions.
    for (int i = 0; i < g->fx.hi; i++) {
        if (!g->fx.used[i]) continue;
        gv_draw_sprite(ren, GV_SPR_BOOM0 + g->fx.frame[i], g->fx.x[i], g->fx.y[i]);
    }

    draw_hud(g, ren);
    draw_mode_text(g, ren);
}
