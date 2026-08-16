// gv_replay.h - record a run as its seed plus one input byte per tick, and
// play it back.
//
// The simulation is integer-only and deterministic, so the seed and the input
// stream are the whole recording: no positions, no scores, nothing about the
// world. A minute of play is about 3.6 KB.
#ifndef GV_REPLAY_H
#define GV_REPLAY_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

// One byte per tick. Held controls are levels; start and restart are edges.
#define GV_RP_LEFT    0x01u
#define GV_RP_RIGHT   0x02u
#define GV_RP_FIRE    0x04u
#define GV_RP_START   0x08u
#define GV_RP_RESTART 0x10u

// Everything the run needs to be set up identically before tick zero.
typedef struct {
    uint32_t seed;
    uint16_t start_stage;   // stage passed to gv_game_start, 0 if it was not called
    bool     started;       // a game was begun at init rather than attract mode
    bool     godmode;       // changes collision outcomes, so it is part of the run
    bool     autoplay;      // the demo brain was driving
} gv_replay_head;

bool gv_replay_record_open(const char *path, const gv_replay_head *h);
void gv_replay_record_tick(uint8_t bits);

// Fills h from the file. The caller must set the game up to match before
// ticking, or the playback will diverge immediately.
bool gv_replay_play_open(const char *path, gv_replay_head *h);

// False once the recording runs out.
bool gv_replay_play_tick(uint8_t *bits);

void gv_replay_close(void);
bool gv_replay_recording(void);
bool gv_replay_playing(void);
uint32_t gv_replay_ticks(void);   // total ticks in a loaded replay

#endif
