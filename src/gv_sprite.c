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

static const char *const ART_SENTINEL_A[] = {
    "................",
    "................",
    "......8888......",
    ".....898898.....",
    "....89988998....",
    "...8999889998...",
    "..899998899998..",
    ".89999988999998.",
    ".89919988991998.",
    ".89919988991998.",
    "..899998899998..",
    "...8999889998...",
    "....89988998....",
    ".....898898.....",
    "......8888......",
    "................",
    nullptr
};

static const char *const ART_SENTINEL_B[] = {
    "................",
    "......8888......",
    ".....898898.....",
    "....89988998....",
    "...8999889998...",
    "..899998899998..",
    ".89999988999998.",
    "8899199889919988",
    "8899199889919988",
    ".89999988999998.",
    "..899998899998..",
    "...8999889998...",
    "....89988998....",
    ".....898898.....",
    "......8888......",
    "................",
    nullptr
};

static const char *const ART_DARTER_A[] = {
    "................",
    "......aaaa......",
    ".....abbbba.....",
    ".....abbbba.....",
    "....aab11baa....",
    "...aabb11bbaa...",
    "..aabbb11bbbaa..",
    ".aabbbb11bbbbaa.",
    ".abbbbb11bbbbba.",
    "..abbbb11bbbba..",
    "...abbb11bbba...",
    "....aab11baa....",
    ".....ab11ba.....",
    "......aaaa......",
    "................",
    "................",
    nullptr
};

static const char *const ART_DARTER_B[] = {
    "................",
    "................",
    "......aaaa......",
    ".....abbbba.....",
    "....aabbbbaa....",
    "...aabb11bbaa...",
    "..aabbb11bbbaa..",
    ".aabbbb11bbbbaa.",
    ".abbbbb11bbbbba.",
    "..abbbb11bbbba..",
    "...abbb11bbba...",
    "....aab11baa....",
    ".....ab11ba.....",
    "......aaaa......",
    "................",
    "................",
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

// Some sprites are an existing drawing under a different palette, which beats
// keeping two copies of the same 16 rows in step by hand. `remap` is a string
// of from/to palette-character pairs applied while blitting.
typedef struct {
    const char *const *rows;
    const char        *remap;
} gv_art;

static const gv_art ART[GV_SPR_COUNT] = {
    [GV_SPR_PLAYER]          = { ART_PLAYER,      nullptr },
    [GV_SPR_GRUNT_A]         = { ART_GRUNT_A,     nullptr },
    [GV_SPR_GRUNT_B]         = { ART_GRUNT_B,     nullptr },
    [GV_SPR_GUARD_A]         = { ART_GUARD_A,     nullptr },
    [GV_SPR_GUARD_B]         = { ART_GUARD_B,     nullptr },
    [GV_SPR_FLAGSHIP_A]      = { ART_FLAGSHIP_A,  nullptr },
    [GV_SPR_FLAGSHIP_B]      = { ART_FLAGSHIP_B,  nullptr },
    // Damaged: green/blue becomes magenta/purple/red, so one hit reads at a
    // glance without changing the silhouette.
    [GV_SPR_FLAGSHIP_HURT_A] = { ART_FLAGSHIP_A,  "8a9b3546" },
    [GV_SPR_FLAGSHIP_HURT_B] = { ART_FLAGSHIP_B,  "8a9b3546" },
    // A captured fighter: your ship repainted in their colours.
    [GV_SPR_CAPTIVE]         = { ART_PLAYER,      "16273464" },
    [GV_SPR_PSHOT]           = { ART_PSHOT,       nullptr },
    [GV_SPR_ESHOT]           = { ART_ESHOT,       nullptr },
    [GV_SPR_BOOM0]           = { ART_BOOM0,       nullptr },
    [GV_SPR_BOOM1]           = { ART_BOOM1,       nullptr },
    [GV_SPR_BOOM2]           = { ART_BOOM2,       nullptr },
    [GV_SPR_BOOM3]           = { ART_BOOM3,       nullptr },
    [GV_SPR_LIFE]            = { ART_LIFE,        nullptr },
    [GV_SPR_BADGE]           = { ART_BADGE,       nullptr },
    // Stage badges are tiered so a high stage count still fits the corner:
    // grey for five, gold for ten.
    [GV_SPR_BADGE5]          = { ART_BADGE,       "8d3e" },
    [GV_SPR_BADGE10]         = { ART_BADGE,       "8637" },
    [GV_SPR_SENTINEL_A]      = { ART_SENTINEL_A,  nullptr },
    [GV_SPR_SENTINEL_B]      = { ART_SENTINEL_B,  nullptr },
    [GV_SPR_DARTER_A]        = { ART_DARTER_A,    nullptr },
    [GV_SPR_DARTER_B]        = { ART_DARTER_B,    nullptr },
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

static void blit_art(const gv_art *art, int ox, int oy, int *out_w, int *out_h) {
    const char *const *rows = art->rows;
    int h = 0;
    while (rows[h]) h++;
    const int w = (int)SDL_strlen(rows[0]);

    // Build a character substitution table from the from/to pairs.
    unsigned char map[256];
    for (int i = 0; i < 256; i++) map[i] = (unsigned char)i;
    if (art->remap)
        for (const char *p = art->remap; p[0] && p[1]; p += 2)
            map[(unsigned char)p[0]] = (unsigned char)p[1];

    for (int y = 0; y < h; y++) {
        const char *row = rows[y];
        // Rows are authored by hand; a short one would otherwise read past the
        // end, so clamp to the row's real length.
        const int rw = (int)SDL_strlen(row);
        for (int x = 0; x < w; x++) {
            const int idx = x < rw ? pal_index((char)map[(unsigned char)row[x]]) : -1;
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

// Painting the art into s_pixels needs no renderer, which is what lets the
// window icon be built before there is one.
static bool s_baked;

static bool bake_atlas(void) {
    if (s_baked) return true;
    SDL_memset(s_pixels, 0, sizeof s_pixels);

    for (int i = 0; i < GV_SPR_COUNT; i++) {
        if (!ART[i].rows) continue;

        const int cx = (i % GV_ATLAS_COLS) * GV_ATLAS_CELL;
        const int cy = (i / GV_ATLAS_COLS) * GV_ATLAS_CELL;

        int w = 0, h = 0;
        blit_art(&ART[i], cx, cy, &w, &h);

        if (w > GV_ATLAS_CELL || h > GV_ATLAS_CELL) {
            SDL_Log("gavaga: sprite %d is %dx%d, larger than the %dpx atlas cell",
                    i, w, h, GV_ATLAS_CELL);
            return false;
        }
        s_rects[i] = (SDL_FRect){ (float)cx, (float)cy, (float)w, (float)h };
    }
    s_baked = true;
    return true;
}

// The window icon, drawn from the same art as the ship you fly. Scaled with
// whole-pixel blocks rather than by the compositor, so it stays pixel art
// instead of turning into a 16px smudge, and centred in a square cell because
// that is the shape every desktop expects.
SDL_Surface *gv_sprite_icon(int scale) {
    if (!bake_atlas()) return nullptr;
    if (scale < 1) scale = 1;

    const int side = GV_ATLAS_CELL * scale;
    SDL_Surface *icon = SDL_CreateSurface(side, side, SDL_PIXELFORMAT_RGBA32);
    if (!icon) return nullptr;

    const SDL_FRect *r = &s_rects[GV_SPR_PLAYER];
    const int sw = (int)r->w, sh = (int)r->h;
    const int ox = (GV_ATLAS_CELL - sw) / 2, oy = (GV_ATLAS_CELL - sh) / 2;

    SDL_ClearSurface(icon, 0, 0, 0, 0);
    if (!SDL_LockSurface(icon)) { SDL_DestroySurface(icon); return nullptr; }

    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            const uint8_t *src =
                &s_pixels[(((size_t)(int)r->y + (size_t)y) * ATLAS_W +
                           ((size_t)(int)r->x + (size_t)x)) * 4];
            if (!src[3]) continue;   // leave transparent pixels alone

            for (int by = 0; by < scale; by++) {
                uint8_t *row = (uint8_t *)icon->pixels
                             + (size_t)((oy + y) * scale + by) * (size_t)icon->pitch
                             + (size_t)((ox + x) * scale) * 4;
                for (int bx = 0; bx < scale; bx++) {
                    row[bx * 4 + 0] = src[0];
                    row[bx * 4 + 1] = src[1];
                    row[bx * 4 + 2] = src[2];
                    row[bx * 4 + 3] = src[3];
                }
            }
        }
    }
    SDL_UnlockSurface(icon);
    return icon;
}

bool gv_sprite_init(SDL_Renderer *ren) {
    if (!bake_atlas()) return false;

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
