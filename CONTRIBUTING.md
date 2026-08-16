# Contributing to Gavaga

Bug reports, path tables and pull requests are all welcome. This is a small
codebase on purpose — it should stay readable enough that you can find your way
around in an afternoon.

## Getting set up

```sh
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DGAVAGA_WERROR=ON
cmake --build build -j
./build/gavaga
```

You need CMake 3.28+ and a C23 compiler (GCC 14+, Clang 19+, AppleClang 16+, or
MSVC 19.39+). SDL3 is fetched and built for you. The README has the per-distro
dependency lists.

Before opening a PR, please run the sanitizer build and a soak:

```sh
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DGAVAGA_ASAN=ON -DGAVAGA_WERROR=ON
cmake --build build-asan -j
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS=suppressions=.lsan-suppressions.txt \
    ./build-asan/gavaga --autoplay --godmode --seed 1 --debug \
                        --shot /tmp/soak.bmp --shot-at 30000
```

That is ~8 minutes of game time under ASan and UBSan in a few seconds.
`--autoplay` drives a bot that deliberately walks into tractor beams, so the
capture → rescue → dual-fighter chain actually gets exercised; `--godmode`
keeps it alive long enough to reach the later stages.

`.lsan-suppressions.txt` hides one fixed 145-byte allocation inside SDL's
PulseAudio backend. Nothing from `src/` belongs in there — if your own code
leaks, fix the leak. Note it is `LSAN_OPTIONS`, not `ASAN_OPTIONS`; ASan
refuses to parse the file.

CI builds Linux, Windows x64 and macOS arm64 on every push.

### Debug and test flags

The simulation is fully deterministic — integer maths, table trig, its own
xorshift RNG — so `--seed N` makes a run repeat exactly. That is how to
reproduce a bug or a screenshot.

| flag | |
|---|---|
| `--seed N` | fixed RNG seed; same seed, same game |
| `--stage N` | start at a later stage |
| `--autoplay` | bot input, for soak runs |
| `--godmode` | collisions cannot kill you (capture still works) |
| `--trace` | log state transitions with their tick |
| `--debug` / `--path N` | start with the overlay / path catalogue open |
| `--shot F --shot-at T` | fast-forward to tick T, write one frame, exit |
| `--mute` | start silent |

## House style

Match what is already there rather than importing a new style.

- Four spaces, no tabs. Braces on the same line.
- `gv_` prefix on anything with external linkage; `snake_case` throughout.
- Comments explain *why*, not *what*. If a constant was arrived at by tuning,
  say so.
- Keep the simulation pure: `gv_game.c` must not touch the renderer, the wall
  clock, the filesystem or the heap. Effects leave the simulation as data — see
  how the sound queue works — and something outside acts on them.
- No allocation after init. Every pool is a fixed-capacity struct-of-arrays.
  If you need a new entity type, add a pool; do not reach for `malloc`.
- Gameplay maths is integer: 16.16 fixed point and the BAM angle helpers in
  `gv_common.h`. Floats are for rendering. This is what keeps the game
  identical across platforms.
- The build is warning-clean under `-Wall -Wextra -Wconversion
  -Wsign-conversion`. Please keep it that way rather than silencing warnings.
- Stay inside the C23 subset every target compiler implements. MSVC is the
  binding constraint — see the note in the README about `nullptr`.

## Writing path tables

Most gameplay changes are really path changes, and they live in
`src/gv_paths.c`. A table is a list of `(turn, ticks)` steps:

```c
static const gv_pathstep P_ENTRY_LOOP[] = {
    GV_HOLD(30),           // fly straight
    GV_TURN(360, 66),      // one full clockwise loop over 66 ticks
    GV_TURN(150, 44),      // bank in toward the formation
};
```

Tables carry no position — the spawn point comes from the wave table in
`gv_game.c`, and `mirror = -1` flips every turn to give you the other side of
the screen for free.

To see what you have drawn, run the game, press <kbd>F1</kbd> for the overlay
and <kbd>F3</kbd> to cycle the path catalogue. It shows one table at a time
from a representative origin in both mirrors, with its step count and duration.
In game, <kbd>F1</kbd> also draws each entity's trail and its predicted
remaining flight. Radius, if you need to reason about it, is
`speed_px_per_tick * ticks / radians_turned`.

## Art and sound

Everything shipped here is placeholder work written in-tree, and no Namco
assets are used or wanted — please do not contribute any.

- Sprites are ASCII rows in `src/gv_sprite.c`, one character per pixel against
  a 16-colour palette. Edit the strings and rebuild.
- The 5×7 HUD font in `src/gv_font.c` works the same way.
- Sound is synthesised at runtime in `src/gv_audio.c` from waveform, frequency
  sweep and envelope. There are no audio files.

Anything you contribute is under the MIT licence, and must be yours to give.

## Pull requests

Keep them focused — one change per PR. Say what you changed and why, and if it
affects how something moves or looks, a screenshot helps. `--shot` makes that
easy:

```sh
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
    ./build/gavaga --play --debug --shot frame.bmp --shot-at 1500
```

CI must be green. If you are unsure whether an idea fits, open an issue first
and ask.
