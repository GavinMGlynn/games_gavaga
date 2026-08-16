// gv_font.c - a hand-drawn 5x7 font, ASCII 32..95, baked to a texture strip.
// Lowercase is folded to uppercase at draw time.
#include "gv_font.h"

#include <stdarg.h>

#define GV_FONT_FIRST 32
#define GV_FONT_LAST  95
#define GV_FONT_COUNT (GV_FONT_LAST - GV_FONT_FIRST + 1)
#define GV_FONT_CELL  8

// 7 rows of 5 columns per glyph, in ASCII order from space.
static const char *const GLYPHS[GV_FONT_COUNT * GV_GLYPH_H] = {
/* sp */ ".....",".....",".....",".....",".....",".....",".....",
/* !  */ "..#..","..#..","..#..","..#..","..#..",".....","..#..",
/* "  */ ".#.#.",".#.#.",".....",".....",".....",".....",".....",
/* #  */ ".#.#.",".#.#.","#####",".#.#.","#####",".#.#.",".#.#.",
/* $  */ "..#..",".####","#.#..",".###.","..#.#","####.","..#..",
/* %  */ "##..#","##..#","...#.","..#..",".#...","#..##","#..##",
/* &  */ ".##..","#..#.","#.#..",".#...","#.#.#","#..#.",".##.#",
/* '  */ "..#..","..#..",".....",".....",".....",".....",".....",
/* (  */ "...#.","..#..",".#...",".#...",".#...","..#..","...#.",
/* )  */ ".#...","..#..","...#.","...#.","...#.","..#..",".#...",
/* *  */ ".....","#.#.#",".###.","#####",".###.","#.#.#",".....",
/* +  */ ".....","..#..","..#..","#####","..#..","..#..",".....",
/* ,  */ ".....",".....",".....",".....",".##..","..#..",".#...",
/* -  */ ".....",".....",".....","#####",".....",".....",".....",
/* .  */ ".....",".....",".....",".....",".....",".##..",".##..",
/* /  */ "....#","...#.","..#..","..#..",".#...","#....",".....",
/* 0  */ ".###.","#...#","#..##","#.#.#","##..#","#...#",".###.",
/* 1  */ "..#..",".##..","..#..","..#..","..#..","..#..",".###.",
/* 2  */ ".###.","#...#","....#","...#.","..#..",".#...","#####",
/* 3  */ "#####","...#.","..#..","...#.","....#","#...#",".###.",
/* 4  */ "...#.","..##.",".#.#.","#..#.","#####","...#.","...#.",
/* 5  */ "#####","#....","####.","....#","....#","#...#",".###.",
/* 6  */ "..##.",".#...","#....","####.","#...#","#...#",".###.",
/* 7  */ "#####","....#","...#.","..#..",".#...",".#...",".#...",
/* 8  */ ".###.","#...#","#...#",".###.","#...#","#...#",".###.",
/* 9  */ ".###.","#...#","#...#",".####","....#","...#.",".##..",
/* :  */ ".....",".##..",".##..",".....",".##..",".##..",".....",
/* ;  */ ".....",".##..",".##..",".....",".##..","..#..",".#...",
/* <  */ "...#.","..#..",".#...","#....",".#...","..#..","...#.",
/* =  */ ".....",".....","#####",".....","#####",".....",".....",
/* >  */ ".#...","..#..","...#.","....#","...#.","..#..",".#...",
/* ?  */ ".###.","#...#","....#","...#.","..#..",".....","..#..",
/* @  */ ".###.","#...#","....#",".##.#","#.#.#","#.#.#",".##..",
/* A  */ "..#..",".#.#.","#...#","#...#","#####","#...#","#...#",
/* B  */ "####.","#...#","#...#","####.","#...#","#...#","####.",
/* C  */ ".###.","#...#","#....","#....","#....","#...#",".###.",
/* D  */ "###..","#..#.","#...#","#...#","#...#","#..#.","###..",
/* E  */ "#####","#....","#....","####.","#....","#....","#####",
/* F  */ "#####","#....","#....","####.","#....","#....","#....",
/* G  */ ".###.","#...#","#....","#.###","#...#","#...#",".####",
/* H  */ "#...#","#...#","#...#","#####","#...#","#...#","#...#",
/* I  */ ".###.","..#..","..#..","..#..","..#..","..#..",".###.",
/* J  */ "..###","...#.","...#.","...#.","...#.","#..#.",".##..",
/* K  */ "#...#","#..#.","#.#..","##...","#.#..","#..#.","#...#",
/* L  */ "#....","#....","#....","#....","#....","#....","#####",
/* M  */ "#...#","##.##","#.#.#","#.#.#","#...#","#...#","#...#",
/* N  */ "#...#","#...#","##..#","#.#.#","#..##","#...#","#...#",
/* O  */ ".###.","#...#","#...#","#...#","#...#","#...#",".###.",
/* P  */ "####.","#...#","#...#","####.","#....","#....","#....",
/* Q  */ ".###.","#...#","#...#","#...#","#.#.#","#..#.",".##.#",
/* R  */ "####.","#...#","#...#","####.","#.#..","#..#.","#...#",
/* S  */ ".####","#....","#....",".###.","....#","....#","####.",
/* T  */ "#####","..#..","..#..","..#..","..#..","..#..","..#..",
/* U  */ "#...#","#...#","#...#","#...#","#...#","#...#",".###.",
/* V  */ "#...#","#...#","#...#","#...#","#...#",".#.#.","..#..",
/* W  */ "#...#","#...#","#...#","#.#.#","#.#.#","##.##","#...#",
/* X  */ "#...#","#...#",".#.#.","..#..",".#.#.","#...#","#...#",
/* Y  */ "#...#","#...#",".#.#.","..#..","..#..","..#..","..#..",
/* Z  */ "#####","....#","...#.","..#..",".#...","#....","#####",
/* [  */ "..##.","..#..","..#..","..#..","..#..","..#..","..##.",
/* \  */ "#....",".#...","..#..","..#..","...#.","....#",".....",
/* ]  */ ".##..","..#..","..#..","..#..","..#..","..#..",".##..",
/* ^  */ "..#..",".#.#.","#...#",".....",".....",".....",".....",
/* _  */ ".....",".....",".....",".....",".....",".....","#####",
};

#define FONT_W (GV_FONT_COUNT * GV_FONT_CELL)
#define FONT_H GV_FONT_CELL

// Textures belong to a renderer, and the debug view lives in a second window
// with its own. The glyph bitmap is rasterised once and uploaded per renderer;
// every draw call already takes the renderer, so lookup costs nothing.
#define GV_FONT_SLOTS 4

typedef struct {
    SDL_Renderer *ren;
    SDL_Texture  *tex;
} gv_font_slot;

static gv_font_slot s_slots[GV_FONT_SLOTS];
static uint8_t      s_pixels[FONT_W * FONT_H * 4];
static bool         s_rasterised;

static SDL_Texture *font_tex(SDL_Renderer *ren) {
    for (int i = 0; i < GV_FONT_SLOTS; i++)
        if (s_slots[i].ren == ren) return s_slots[i].tex;
    return nullptr;
}

static void rasterise(void) {
    if (s_rasterised) return;
    SDL_memset(s_pixels, 0, sizeof s_pixels);

    for (int g = 0; g < GV_FONT_COUNT; g++) {
        for (int row = 0; row < GV_GLYPH_H; row++) {
            const char *bits = GLYPHS[g * GV_GLYPH_H + row];
            for (int col = 0; col < GV_GLYPH_W; col++) {
                if (bits[col] != '#') continue;
                const size_t o = ((size_t)row * FONT_W + (size_t)(g * GV_FONT_CELL + col)) * 4;
                s_pixels[o + 0] = 255;
                s_pixels[o + 1] = 255;
                s_pixels[o + 2] = 255;
                s_pixels[o + 3] = 255;
            }
        }
    }
    s_rasterised = true;
}

bool gv_font_init(SDL_Renderer *ren) {
    if (font_tex(ren)) return true;   // already have one for this renderer

    int slot = -1;
    for (int i = 0; i < GV_FONT_SLOTS; i++)
        if (!s_slots[i].ren) { slot = i; break; }
    if (slot < 0) {
        SDL_Log("gavaga: out of font slots");
        return false;
    }

    rasterise();

    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC, FONT_W, FONT_H);
    if (!tex) {
        SDL_Log("gavaga: could not create font texture: %s", SDL_GetError());
        return false;
    }
    if (!SDL_UpdateTexture(tex, nullptr, s_pixels, FONT_W * 4)) {
        SDL_Log("gavaga: could not upload font texture: %s", SDL_GetError());
        SDL_DestroyTexture(tex);
        return false;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    s_slots[slot].ren = ren;
    s_slots[slot].tex = tex;
    return true;
}

void gv_font_quit_renderer(SDL_Renderer *ren) {
    for (int i = 0; i < GV_FONT_SLOTS; i++) {
        if (s_slots[i].ren != ren) continue;
        SDL_DestroyTexture(s_slots[i].tex);
        s_slots[i].ren = nullptr;
        s_slots[i].tex = nullptr;
    }
}

void gv_font_quit(void) {
    for (int i = 0; i < GV_FONT_SLOTS; i++) {
        if (!s_slots[i].ren) continue;
        SDL_DestroyTexture(s_slots[i].tex);
        s_slots[i].ren = nullptr;
        s_slots[i].tex = nullptr;
    }
}

int gv_font_width(const char *text) {
    int n = 0;
    for (const char *p = text; *p; p++) n++;
    return n > 0 ? n * GV_GLYPH_ADV - 1 : 0;
}

void gv_font_draw(SDL_Renderer *ren, int x, int y, SDL_Color c, const char *text) {
    SDL_Texture *s_tex = font_tex(ren);
    if (!s_tex || !text) return;

    SDL_SetTextureColorMod(s_tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(s_tex, c.a);

    float px = (float)x;
    for (const char *p = text; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\n') { px = (float)x; y += GV_LINE_ADV; continue; }
        if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 32);
        if (ch < GV_FONT_FIRST || ch > GV_FONT_LAST) ch = '?';

        const SDL_FRect src = { (float)((ch - GV_FONT_FIRST) * GV_FONT_CELL), 0.0f,
                                (float)GV_GLYPH_W, (float)GV_GLYPH_H };
        const SDL_FRect dst = { px, (float)y, (float)GV_GLYPH_W, (float)GV_GLYPH_H };
        SDL_RenderTexture(ren, s_tex, &src, &dst);
        px += (float)GV_GLYPH_ADV;
    }
    SDL_SetTextureColorMod(s_tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(s_tex, 255);
}

void gv_font_printf(SDL_Renderer *ren, int x, int y, SDL_Color c, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    gv_font_draw(ren, x, y, c, buf);
}

void gv_font_center(SDL_Renderer *ren, int y, SDL_Color c, const char *text) {
    gv_font_draw(ren, (GV_SCREEN_W - gv_font_width(text)) / 2, y, c, text);
}
