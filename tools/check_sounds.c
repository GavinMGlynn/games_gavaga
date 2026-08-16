// check_sounds.c - play every game sound and report whether it made a noise.
//
// Half the effects are recordings and half are synthesised, and either half can
// break quietly: a bad mapping, a clip trimmed to nothing, a synth definition
// with the volume left at zero. None of that shows up in a build, and none of
// it shows up in the unit tests, which have no audio device.
//
// This drives gv_audio straight into SDL's disk audio driver, so it needs no
// sound card, no window and no keypress - it works over ssh and in a container.
// Build it with -DGAVAGA_SOUNDCHECK=ON and run:
//
//     SDL_AUDIODRIVER=disk ./build/gavaga_soundcheck
//
// SDL writes the mix to sdlaudio.raw in the working directory. The verdict is
// printed per effect; the file is left behind if you want to listen to it:
//
//     ffplay -f f32le -ar 44100 -ch_layout mono sdlaudio.raw
#include "gv_audio.h"
#include "gv_game.h"

#include <stdio.h>

#define SLOT_MS 900     // each effect gets this long to finish before the next

static const char *NAMES[GV_SFX_COUNT] = {
    [GV_SFX_SHOT]          = "shot",
    [GV_SFX_ENEMY_BOOM]    = "enemy explosion",
    [GV_SFX_FLAGSHIP_HIT]  = "flagship hit",
    [GV_SFX_FLAGSHIP_BOOM] = "flagship explosion",
    [GV_SFX_PLAYER_BOOM]   = "player explosion",
    [GV_SFX_DIVE]          = "dive",
    [GV_SFX_BEAM]          = "tractor beam",
    [GV_SFX_CAPTURED]      = "captured",
    [GV_SFX_RESCUE]        = "rescue",
    [GV_SFX_EXTRA_LIFE]    = "extra life",
    [GV_SFX_STAGE]         = "stage start",
    [GV_SFX_GAMEOVER]      = "game over",
    [GV_SFX_PERFECT]       = "perfect stage",
};

int main(void) {
    if (!gv_audio_init()) {
        printf("audio would not start - with the disk driver that is a real failure\n");
        return 1;
    }

    printf("playing %d effects, %d ms apart\n\n", GV_SFX_COUNT - 1, SLOT_MS);
    for (int sfx = GV_SFX_NONE + 1; sfx < GV_SFX_COUNT; sfx++) {
        printf("  %-20s ", NAMES[sfx] ? NAMES[sfx] : "?");
        fflush(stdout);
        gv_audio_play(sfx);
        SDL_Delay(SLOT_MS);
        printf("played\n");
    }
    SDL_Delay(300);
    gv_audio_quit();

    printf("\nMix written by SDL to sdlaudio.raw (f32, mono, 44100).\n"
           "Every effect above should be an audible burst; a silent slot means\n"
           "that one is broken. tools/measure_sounds.py will tell you which.\n");
    return 0;
}
