// gv_replay.c - see gv_replay.h.
//
// Binary rather than text, because this is the one file in the project that is
// per-tick rather than a handful of numbers, and it is written with SDL's
// little-endian helpers so a recording made on one machine plays on another.
//
// The tick count is not in the header: it is derived from the file size, so a
// recording that was cut short - a crash, a kill, a full disk - still plays
// back as far as it got instead of being unreadable.
#include "gv_replay.h"
#include "gv_common.h"

#define GV_RP_MAGIC0 'G'
#define GV_RP_MAGIC1 'V'
#define GV_RP_MAGIC2 'R'
#define GV_RP_MAGIC3 'P'
#define GV_RP_VERSION 1u
#define GV_RP_HEADER  16          // bytes before the input stream

#define GV_RP_F_STARTED  0x01u
#define GV_RP_F_GODMODE  0x02u
#define GV_RP_F_AUTOPLAY 0x04u

static SDL_IOStream *s_io;
static bool          s_recording;
static bool          s_playing;
static uint32_t      s_ticks;      // total in a loaded replay
static uint32_t      s_pos;        // ticks consumed so far

bool gv_replay_record_open(const char *path, const gv_replay_head *h) {
    gv_replay_close();

    s_io = SDL_IOFromFile(path, "wb");
    if (!s_io) {
        SDL_Log("gavaga: cannot record to %s: %s", path, SDL_GetError());
        return false;
    }

    uint8_t flags = 0;
    if (h->started)  flags |= GV_RP_F_STARTED;
    if (h->godmode)  flags |= GV_RP_F_GODMODE;
    if (h->autoplay) flags |= GV_RP_F_AUTOPLAY;

    bool ok = true;
    ok &= SDL_WriteU8(s_io, GV_RP_MAGIC0);
    ok &= SDL_WriteU8(s_io, GV_RP_MAGIC1);
    ok &= SDL_WriteU8(s_io, GV_RP_MAGIC2);
    ok &= SDL_WriteU8(s_io, GV_RP_MAGIC3);
    ok &= SDL_WriteU16LE(s_io, GV_RP_VERSION);
    ok &= SDL_WriteU32LE(s_io, h->seed);
    ok &= SDL_WriteU16LE(s_io, h->start_stage);
    ok &= SDL_WriteU8(s_io, flags);
    for (int i = 0; i < 3 && ok; i++) ok &= SDL_WriteU8(s_io, 0);   // pad to 16
    if (!ok) {
        SDL_Log("gavaga: could not write the replay header: %s", SDL_GetError());
        gv_replay_close();
        return false;
    }

    s_recording = true;
    SDL_Log("gavaga: recording to %s (seed %u)", path, h->seed);
    return true;
}

void gv_replay_record_tick(uint8_t bits) {
    if (!s_recording) return;
    if (!SDL_WriteU8(s_io, bits)) {
        SDL_Log("gavaga: replay write failed, recording stopped: %s", SDL_GetError());
        gv_replay_close();
        return;
    }
    s_pos++;
}

bool gv_replay_play_open(const char *path, gv_replay_head *h) {
    gv_replay_close();

    s_io = SDL_IOFromFile(path, "rb");
    if (!s_io) {
        SDL_Log("gavaga: cannot open replay %s: %s", path, SDL_GetError());
        return false;
    }

    const Sint64 size = SDL_GetIOSize(s_io);
    if (size < GV_RP_HEADER) {
        SDL_Log("gavaga: %s is too short to be a replay", path);
        gv_replay_close();
        return false;
    }

    uint8_t m[4] = { 0 };
    uint16_t version = 0, stage = 0;
    uint32_t seed = 0;
    uint8_t flags = 0, pad = 0;

    bool ok = true;
    for (int i = 0; i < 4 && ok; i++) ok &= SDL_ReadU8(s_io, &m[i]);
    ok &= SDL_ReadU16LE(s_io, &version);
    ok &= SDL_ReadU32LE(s_io, &seed);
    ok &= SDL_ReadU16LE(s_io, &stage);
    ok &= SDL_ReadU8(s_io, &flags);
    for (int i = 0; i < 3 && ok; i++) ok &= SDL_ReadU8(s_io, &pad);
    if (!ok) {
        SDL_Log("gavaga: %s: truncated replay header", path);
        gv_replay_close();
        return false;
    }

    if (m[0] != GV_RP_MAGIC0 || m[1] != GV_RP_MAGIC1 ||
        m[2] != GV_RP_MAGIC2 || m[3] != GV_RP_MAGIC3) {
        SDL_Log("gavaga: %s is not a gavaga replay", path);
        gv_replay_close();
        return false;
    }
    if (version != GV_RP_VERSION) {
        SDL_Log("gavaga: %s is a version %u replay, this build reads version %u",
                path, version, GV_RP_VERSION);
        gv_replay_close();
        return false;
    }

    h->seed        = seed;
    h->start_stage = stage;
    h->started     = (flags & GV_RP_F_STARTED)  != 0;
    h->godmode     = (flags & GV_RP_F_GODMODE)  != 0;
    h->autoplay    = (flags & GV_RP_F_AUTOPLAY) != 0;

    s_ticks   = (uint32_t)(size - GV_RP_HEADER);
    s_pos     = 0;
    s_playing = true;
    SDL_Log("gavaga: replaying %s - seed %u, %u ticks (%.1fs)",
            path, seed, s_ticks, (double)s_ticks / GV_TICK_HZ);
    return true;
}

bool gv_replay_play_tick(uint8_t *bits) {
    if (!s_playing || s_pos >= s_ticks) return false;
    if (!SDL_ReadU8(s_io, bits)) return false;
    s_pos++;
    return true;
}

void gv_replay_close(void) {
    if (s_io) {
        if (s_recording)
            SDL_Log("gavaga: recorded %u ticks (%.1fs)", s_pos, (double)s_pos / GV_TICK_HZ);
        SDL_CloseIO(s_io);
        s_io = nullptr;
    }
    s_recording = false;
    s_playing   = false;
    s_ticks     = 0;
    s_pos       = 0;
}

bool     gv_replay_recording(void) { return s_recording; }
bool     gv_replay_playing(void)   { return s_playing; }
uint32_t gv_replay_ticks(void)     { return s_ticks; }
