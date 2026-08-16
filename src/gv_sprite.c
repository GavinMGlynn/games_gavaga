// gv_sprite.c - the placeholder art, and the code that bakes it into one
// atlas texture at startup.
//
// Palette key used by the rows below:
//   . transparent   0 black    1 white     2 lt-blue   3 blue
//   4 red           5 dk-red   6 yellow    7 orange    8 green
//   9 dk-green      a magenta  b purple    c cyan      d grey    e dk-grey
#include "gv_sprite.h"

static const SDL_Color GV_PAL[16] = {
    {  16,  16,  16, 255 },  // 0 black
    { 255, 255, 255, 255 },  // 1 white
    { 120, 200, 248, 255 },  // 2 lt blue
    {  40,  88, 200, 255 },  // 3 blue
    { 232,  56,  40, 255 },  // 4 red
    { 144,  32,  24, 255 },  // 5 dk red
    { 248, 224,  88, 255 },  // 6 yellow
    { 248, 144,  40, 255 },  // 7 orange
    {  72, 200,  72, 255 },  // 8 green
    {  32, 136,  56, 255 },  // 9 dk green
    { 224,  96, 192, 255 },  // a magenta
    { 120,  72, 200, 255 },  // b purple
    {  72, 224, 224, 255 },  // c cyan
    { 160, 160, 168, 255 },  // d grey
    {  88,  88,  96, 255 },  // e dk grey
    { 255,   0, 255, 255 },  // f (unused - hot pink so mistakes show)
};

// --- the art --------------------------------------------------------------

static const char *const ART_PLAYER[] = {
    "................",
    ".......11.......",
    ".......11.......",
    "......1221......",
    "......1221......",
    "......1221......",
    ".....112211.....",
    ".....122221.....",
    "..111222222111..",
    ".11223333332211.",
    "1122333333332211",
    "1122333333332211",
    "11.2333333332.11",
    "1..2233333322..1",
    "1..22.6666.22..1",
    "....66....66....",
    nullptr
};

static const char *const ART_GRUNT_A[] = {
    "................",
    "..3..........3..",
    "..33........33..",
    "...33......33...",
    "....33cccc33....",
    "....3cc11cc3....",
    "...33c1111c33...",
    "...3ccc11ccc3...",
    "..33cc3333cc33..",
    "..3cc333333cc3..",
    "...cc333333cc...",
    "....c333333c....",
    ".....3c33c3.....",
    "......3cc3......",
    ".......33.......",
    "................",
    nullptr
};

static const char *const ART_GRUNT_B[] = {
    "................",
    "................",
    "....3......3....",
    "....33cccc33....",
    "....3cc11cc3....",
    "...33c1111c33...",
    "...3ccc11ccc3...",
    "..33cc3333cc33..",
    "..3cc333333cc3..",
    ".33cc333333cc33.",
    "..33c333333c33..",
    "...3.c3333c.3...",
    ".....3c33c3.....",
    "......3cc3......",
    ".......33.......",
    "................",
    nullptr
};

static const char *const ART_GUARD_A[] = {
    ".......44.......",
    "......4114......",
    ".....441144.....",
    "....44111144....",
    "...4441111444...",
    "..445511115544..",
    ".44555511555544.",
    "4455551111555544",
    "1144445555444411",
    ".14444555544441.",
    "..444555555444..",
    "...4455555544...",
    "....45555554....",
    ".....445544.....",
    "......4444......",
    "................",
    nullptr
};

static const char *const ART_GUARD_B[] = {
    "................",
    ".......44.......",
    "......4114......",
    ".....441144.....",
    "....44111144....",
    "...4441111444...",
    "..445511115544..",
    ".44555511555544.",
    "4455551111555544",
    "1144445555444411",
    ".14444555544441.",
    "..444555555444..",
    "...4455555544...",
    "....45555554....",
    ".....445544.....",
    "......4444......",
    nullptr
};

static const char *const ART_FLAGSHIP_A[] = {
    ".......88.......",
    "......8998......",
    ".....899998.....",
    "....89911998....",
    "...8899119988...",
    "..338891198833..",
    ".33889911998833.",
    "3388991111998833",
    "6688991111998866",
    ".66889999998866.",
    "..668899998866..",
    "...6889999886...",
    "....88999988....",
    ".....889988.....",
    "......9999......",
    "................",
    nullptr
};

static const char *const ART_FLAGSHIP_B[] = {
    "................",
    ".......88.......",
    "......8998......",
    ".....899998.....",
    "....89911998....",
    "...8899119988...",
    "..338891198833..",
    ".33889911998833.",
    "3388991111998833",
    "6688991111998866",
    ".66889999998866.",
    "..668899998866..",
    "...6889999886...",
    "....88999988....",
    ".....889988.....",
    "......9999......",
    nullptr
};

static const char *const ART_PSHOT[] = {
    ".11.",
    ".11.",
    ".11.",
    ".22.",
    ".22.",
    ".22.",
    ".33.",
    ".33.",
    nullptr
};

static const char *const ART_ESHOT[] = {
    "..66..",
    ".6776.",
    "677776",
    "677776",
    ".6776.",
    "..66..",
    nullptr
};

static const char *const ART_BOOM0[] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "......6666......",
    ".....677776.....",
    ".....677776.....",
    "......6666......",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    nullptr
};

static const char *const ART_BOOM1[] = {
    "................",
    "................",
    "................",
    "................",
    "......7..7......",
    ".....674476.....",
    "....67744776....",
    "...6774aa4776...",
    "...6774aa4776...",
    "....67744776....",
    ".....674476.....",
    "......7..7......",
    "................",
    "................",
    "................",
    "................",
    nullptr
};

static const char *const ART_BOOM2[] = {
    "................",
    "..7..........7..",
    ".7..6......6..7.",
    "...6..4444..6...",
    "..6..477774..6..",
    ".7.4477aa7744.7.",
    "...47aaaaaa74...",
    "..64aaaaaaaa46..",
    "..64aaaaaaaa46..",
    "...47aaaaaa74...",
    ".7.4477aa7744.7.",
    "..6..477774..6..",
    "...6..4444..6...",
    ".7..6......6..7.",
    "..7..........7..",
    "................",
    nullptr
};

static const char *const ART_BOOM3[] = {
    "7....7....7....7",
    "................",
    "..6..........6..",
    "................",
    "6...4......4...6",
    "................",
    "...a........a...",
    "................",
    "................",
    "...a........a...",
    "................",
    "6...4......4...6",
    "................",
    "..6..........6..",
    "................",
    "7....7....7....7",
    nullptr
};

static const char *const ART_LIFE[] = {
    "...11...",
    "..1221..",
    "..1221..",
    ".112211.",
    "11233211",
    "11233211",
    "1.2332.1",
    "..6..6..",
    nullptr
};

static const char *const ART_BADGE[] = {
    "........",
    ".888888.",
    ".811118.",
    ".888888.",
    "...3....",
    "...3....",
    "...3....",
    "........",
    nullptr
};

static const char *const *const ART[GV_SPR_COUNT] = {
    [GV_SPR_PLAYER]     = ART_PLAYER,
    [GV_SPR_GRUNT_A]    = ART_GRUNT_A,
    [GV_SPR_GRUNT_B]    = ART_GRUNT_B,
    [GV_SPR_GUARD_A]    = ART_GUARD_A,
    [GV_SPR_GUARD_B]    = ART_GUARD_B,
    [GV_SPR_FLAGSHIP_A] = ART_FLAGSHIP_A,
    [GV_SPR_FLAGSHIP_B] = ART_FLAGSHIP_B,
    [GV_SPR_PSHOT]      = ART_PSHOT,
    [GV_SPR_ESHOT]      = ART_ESHOT,
    [GV_SPR_BOOM0]      = ART_BOOM0,
    [GV_SPR_BOOM1]      = ART_BOOM1,
    [GV_SPR_BOOM2]      = ART_BOOM2,
    [GV_SPR_BOOM3]      = ART_BOOM3,
    [GV_SPR_LIFE]       = ART_LIFE,
    [GV_SPR_BADGE]      = ART_BADGE,
};

// --- baking ---------------------------------------------------------------

#define ATLAS_ROWS ((GV_SPR_COUNT + GV_ATLAS_COLS - 1) / GV_ATLAS_COLS)
#define ATLAS_W    (GV_ATLAS_COLS * GV_ATLAS_CELL)
#define ATLAS_H    (ATLAS_ROWS * GV_ATLAS_CELL)

static SDL_Texture *s_atlas;
static SDL_FRect    s_rects[GV_SPR_COUNT];
static uint8_t      s_pixels[ATLAS_W * ATLAS_H * 4];   // RGBA, static: no malloc

static int pal_index(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;   // '.' and anything else is transparent
}

static void blit_art(const char *const *rows, int ox, int oy, int *out_w, int *out_h) {
    int h = 0;
    while (rows[h]) h++;
    const int w = (int)SDL_strlen(rows[0]);

    for (int y = 0; y < h; y++) {
        const char *row = rows[y];
        // Rows are authored by hand; a short one would otherwise read past the
        // end, so clamp to the row's real length.
        const int rw = (int)SDL_strlen(row);
        for (int x = 0; x < w; x++) {
            const int idx = x < rw ? pal_index(row[x]) : -1;
            uint8_t *px = &s_pixels[((size_t)(oy + y) * ATLAS_W + (size_t)(ox + x)) * 4];
            if (idx < 0) {
                px[0] = px[1] = px[2] = px[3] = 0;
            } else {
                px[0] = GV_PAL[idx].r;
                px[1] = GV_PAL[idx].g;
                px[2] = GV_PAL[idx].b;
                px[3] = 255;
            }
        }
    }
    *out_w = w;
    *out_h = h;
}

bool gv_sprite_init(SDL_Renderer *ren) {
    SDL_memset(s_pixels, 0, sizeof s_pixels);

    for (int i = 0; i < GV_SPR_COUNT; i++) {
        const char *const *rows = ART[i];
        if (!rows) continue;

        const int cx = (i % GV_ATLAS_COLS) * GV_ATLAS_CELL;
        const int cy = (i / GV_ATLAS_COLS) * GV_ATLAS_CELL;

        int w = 0, h = 0;
        blit_art(rows, cx, cy, &w, &h);

        if (w > GV_ATLAS_CELL || h > GV_ATLAS_CELL) {
            SDL_Log("gavaga: sprite %d is %dx%d, larger than the %dpx atlas cell",
                    i, w, h, GV_ATLAS_CELL);
            return false;
        }
        s_rects[i] = (SDL_FRect){ (float)cx, (float)cy, (float)w, (float)h };
    }

    s_atlas = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                SDL_TEXTUREACCESS_STATIC, ATLAS_W, ATLAS_H);
    if (!s_atlas) {
        SDL_Log("gavaga: could not create sprite atlas: %s", SDL_GetError());
        return false;
    }
    if (!SDL_UpdateTexture(s_atlas, nullptr, s_pixels, ATLAS_W * 4)) {
        SDL_Log("gavaga: could not upload sprite atlas: %s", SDL_GetError());
        return false;
    }
    // Chunky pixels, no smoothing.
    SDL_SetTextureScaleMode(s_atlas, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(s_atlas, SDL_BLENDMODE_BLEND);
    return true;
}

void gv_sprite_quit(void) {
    if (s_atlas) { SDL_DestroyTexture(s_atlas); s_atlas = nullptr; }
}

SDL_Texture *gv_sprite_texture(void) { return s_atlas; }

const SDL_FRect *gv_sprite_rect(int id) {
    if (id < 0 || id >= GV_SPR_COUNT) id = GV_SPR_PLAYER;
    return &s_rects[id];
}
