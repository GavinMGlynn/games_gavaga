// gv_audio.h - runtime sound synthesis. There are no audio files: every
// effect is a waveform, a frequency sweep and an envelope, mixed on the audio
// thread. Sound ids are the GV_SFX_* game events from gv_game.h.
#ifndef GV_AUDIO_H
#define GV_AUDIO_H

#include "gv_common.h"

// Never fatal: if there is no audio device (headless CI, for instance) this
// returns false and the game runs silently.
bool gv_audio_init(void);
void gv_audio_quit(void);

void gv_audio_play(int sfx);
void gv_audio_set_muted(bool muted);
bool gv_audio_muted(void);
bool gv_audio_ok(void);

// The music bed. 0 is silence; 1 and up pick up the tempo and open the filter,
// so later stages sound busier without needing a second tune. Safe to call
// every frame - setting the level it is already at does nothing, so the bed
// does not restart.
#define GV_MUSIC_OFF 0
void gv_audio_set_music(int level);
int  gv_audio_music(void);

#endif // GV_AUDIO_H
