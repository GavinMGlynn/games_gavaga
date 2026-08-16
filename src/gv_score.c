// gv_score.c - the high score, stored as a line of text in the platform's
// per-user preferences directory.
//
// Text rather than a packed struct: it is two dozen bytes, it survives being
// looked at, and there is no endianness or padding to get wrong across the
// three platforms.
#include "gv_score.h"

#define GV_SCORE_ORG   "gavaga"
#define GV_SCORE_APP   "gavaga"
#define GV_SCORE_FILE  "highscore.txt"
#define GV_SCORE_MAGIC "gavaga-high"
#define GV_SCORE_MAX   99999999u

// Builds "<prefpath>highscore.txt" into buf. False if there is no pref path.
static bool score_path(char *buf, size_t buflen) {
    char *pref = SDL_GetPrefPath(GV_SCORE_ORG, GV_SCORE_APP);
    if (!pref) return false;
    SDL_snprintf(buf, buflen, "%s%s", pref, GV_SCORE_FILE);
    SDL_free(pref);
    return true;
}

uint32_t gv_score_load(void) {
    char path[1024];
    if (!score_path(path, sizeof path)) return 0;

    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) return 0;   // first run: no file yet, which is not an error

    char buf[64];
    const size_t n = SDL_ReadIO(io, buf, sizeof buf - 1);
    SDL_CloseIO(io);
    buf[n] = '\0';

    unsigned long value = 0;
    char magic[32] = { 0 };
    if (SDL_sscanf(buf, "%31s %lu", magic, &value) != 2) return 0;
    if (SDL_strcmp(magic, GV_SCORE_MAGIC) != 0) return 0;
    if (value > GV_SCORE_MAX) return 0;

    return (uint32_t)value;
}

void gv_score_save(uint32_t high) {
    if (high > GV_SCORE_MAX) high = GV_SCORE_MAX;

    char path[1024];
    if (!score_path(path, sizeof path)) return;

    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) {
        SDL_Log("gavaga: could not write %s: %s", path, SDL_GetError());
        return;
    }

    char line[64];
    const int len = SDL_snprintf(line, sizeof line, "%s %u\n", GV_SCORE_MAGIC, high);
    if (len > 0) SDL_WriteIO(io, line, (size_t)len);
    SDL_CloseIO(io);
}
