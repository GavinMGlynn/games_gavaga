# Gavaga

[![build](https://github.com/GavinMGlynn/games_gavaga/actions/workflows/build.yml/badge.svg)](https://github.com/GavinMGlynn/games_gavaga/actions/workflows/build.yml)
[![license: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C23](https://img.shields.io/badge/C-23-informational.svg)](CMakeLists.txt)
[![SDL3](https://img.shields.io/badge/SDL-3.2.30-informational.svg)](https://github.com/libsdl-org/SDL)

A Galaga-inspired fixed shooter in C23 and SDL3. Not a clone of the original —
its own art, its own tables — but built on the same architecture the arcade
hardware used: a 224×288 playfield, logic locked to 60.606 Hz, an LFSR
starfield, and enemy flight driven entirely by turn tables.

Runs on Linux, Windows (x64) and macOS (Apple Silicon).

```
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
./build/gavaga
```

SDL3 is fetched and built automatically — nothing to install first beyond a
compiler, CMake 3.28+, and the usual X11/Wayland dev headers on Linux.

## Controls

| Key | |
|---|---|
| Keyboard | Gamepad | |
|---|---|---|
| ← → / A D | left stick or d-pad | move |
| Space / Z / Ctrl | any face button, shoulder or trigger | fire |
| Enter | Start | start |
| P | Back | pause |
| M | | mute |
| F1 | | open the debug window |
| F3 | | cycle the path catalogue |
| F2 | | single-step one tick while paused |
| R | | restart |
| F11 / Alt-Enter | | fullscreen |
| Esc | | quit |

Gamepads hot-plug: connect one mid-game and it is picked up. The keyboard stays
live either way — the two sources are OR-ed together, so neither can hold the
other's input down.

## Rules worth knowing

**Lose a ship and the stage restarts** from the beginning.

**The tractor beam.** From stage 2, a flagship will occasionally drop out of
formation, hang over the playfield and open a cone beneath itself. Get caught
and it costs you a ship — but *not* the stage, deliberately: the boss stays on
screen carrying your fighter, and shooting him down sets it free. Catch the
freed fighter on its way back and it docks alongside you as a **dual fighter**:
twice as wide, twice the firepower, and twice as easy to hit. Die and you lose
it.

**Flagships take two hits** and repaint from green to magenta after the first,
so you can see which ones are one shot from dead.

Every third stage is a **challenging stage** — nothing shoots back, nothing
joins the formation, and clearing the lot is worth a bonus. The four normal
layouts rotate between them.

**Scoring rewards aim, not volume.** Ships killed while attacking chain: every
fourth one steps a multiplier up to ×4, and a shot that sails off the top of
the screen breaks the chain. Clearing a stage without losing a ship pays a
bonus that grows with the stage. Dive speed, fire rate and the number of
simultaneous attackers all scale with the stage number. A spare ship arrives at
20,000 points and every 60,000 after that, and the high score is kept between
sessions.

The attract screen is not a mock-up: it is the real game being played by the
same brain `--autoplay` uses, so the entry paths, dives and the tractor beam
are all on show.

## The path interpreter

This is the piece everything else is built on, so it went in first.

Enemy movement is never scripted as positions. A path is a list of
`(turn rate, duration)` pairs; an entity walking one moves at a constant speed
along its current heading, and each tick the heading advances by the current
step's turn rate:

```c
heading += turn * mirror;                       // BAM, 0 = up, clockwise
pos     += (sin(heading), -cos(heading)) * speed;   // 16.16 fixed point
```

Because a table only ever describes *relative* turning, one table can start
from any position and heading, and `mirror = -1` flips every turn to give the
left/right pair for free. Entry flights, dives and challenging-stage flybys all
run through this one interpreter — the only difference between them is which
table they point at and what the entity does when the table runs out.

Tables live in [`src/gv_paths.c`](src/gv_paths.c) and read the way you'd draw
them:

```c
static const gv_pathstep P_ENTRY_LOOP[] = {
    GV_HOLD(30),           // climb
    GV_TURN(360, 66),      // one full loop, clockwise, over 66 ticks
    GV_HOLD(30),
    GV_TURN(150, 44),      // bank in toward the formation
};
```

Radius, if you need it, is `speed_px_per_tick * ticks / radians_turned`.

**Tuning them:** press F1. The debug view opens in its own window — the game
window stays clean — showing each entity's trail, its *predicted* remaining
flight, hitboxes, beam cones and the formation slots, with a counters panel
underneath. The prediction runs the same integration the simulation does, so
what you see is exactly where it will go.

F3 cycles a catalogue that draws one path on its own from a representative
origin, both mirrors at once (cyan `+1`, pink `-1`), with its step count and
total duration. Edit the table, rebuild, look again.

## How it fits together

| | |
|---|---|
| `gv_path.c` / `gv_paths.c` | the turn-table interpreter and the tables |
| `gv_game.c` | simulation: pools, formation, waves, collisions, tractor beam. Touches no renderer, no clock, no filesystem, no heap |
| `gv_star.c` | LFSR starfield |
| `gv_sprite.c` | placeholder art as ASCII rows, baked to one atlas at startup |
| `gv_render.c` | all drawing |
| `gv_audio.c` | the synth: no sound files, everything generated at runtime |
| `gv_score.c` | the persistent high score |
| `gv_debug.c` | the F1 debug window and F3 path catalogue |
| `gv_math.c` | integer sin/cos/atan tables |
| `main.c` | SDL3 callbacks and the fixed-timestep loop |

**Fixed timestep.** Logic runs at exactly 16.5 ms per tick — 60.60606 Hz, the
arcade rate, and exact in integer nanoseconds. `SDL_AppIterate` accumulates
real time and drains it in whole ticks, capped at 5 ticks per frame so a stall
drops time instead of fast-forwarding. Rendering is decoupled and happens once
per `SDL_AppIterate` regardless. Positions are snapped to whole logical pixels
when drawn: with a 224×288 field at integer scale, interpolating between ticks
would only smear the pixel grid.

**Presentation.** `SDL_SetRenderLogicalPresentation(224, 288, INTEGER_SCALE)`
— the playfield is always a whole-number multiple of its native size, with
letterboxing around it, so pixels stay square at any window size.

**Memory.** Every pool is struct-of-arrays with a fixed capacity, and the whole
game state is one `SDL_calloc` at startup. There is no allocation after init —
the line where that stops is marked in `main.c`.

**Determinism.** Gameplay uses 16.16 fixed point and an integer trig table, and
a xorshift32 RNG rather than libc's. Same seed, same inputs, same game on any
platform — `--seed N` makes that usable, and two runs with the same seed
produce byte-identical traces.

**Effects leave as data.** The simulation never plays a sound, writes a file or
reads a device; it appends game events to a queue and sets a flag, and `main.c`
turns those into audio and a high-score write. Input arrives the same way round
— as abstract actions, so a keyboard and a gamepad are indistinguishable to the
game. That is what keeps `gv_game.c` testable and deterministic.

**SDL3 error convention.** Most SDL3 functions return `bool`, `true` on
success — the inverse of SDL2, where `0` meant OK.

## Starfield

The original generated its stars in hardware: a 16-bit LFSR clocked once per
pixel as the beam swept, emitting a star wherever the register hit a chosen
pattern, with spare bits picking the colour. `gv_star.c` does the same thing
with the classic maximal-length polynomial `x¹⁶ + x¹⁴ + x¹³ + x¹¹ + 1`, sweeping
the virtual field once at init and keeping the ~125 hits in a fixed array. Same
distribution as clocking it live, but free per frame. Four blink groups cycle to
twinkle, and scroll speed is set per game state.

## Sound

No audio files either. Each effect is a waveform, a frequency sweep and an
envelope, mixed per sample on the audio thread in `gv_audio.c`; longer cues are
short note lists. If there is no audio device — headless CI, say — the game
logs it and runs silently rather than failing to start.

## Art

All placeholder, all mine to throw away — no Namco assets anywhere in the tree.
Sprites are ASCII rows in `gv_sprite.c`, one character per pixel against a
16-colour palette:

```c
static const char *const ART_PLAYER[] = {
    "......1221......",
    ".....112211.....",
    ...
```

Edit the strings and rebuild, or swap `gv_sprite_init()` for a PNG loader when
you have real art — the rest of the game only ever asks for a sprite id and
gets back a source rect. The 5×7 HUD font in `gv_font.c` works the same way.

## Building elsewhere

C23 needs a recent compiler; CMake checks and fails with a clear message if
yours is too old.

| | minimum | note |
|---|---|---|
| GCC | 14 | Ubuntu 22.04 and RHEL 9 ship GCC 11 — too old |
| Clang | 19 | |
| AppleClang | 16 | Xcode 16 |
| MSVC | 19.39 | VS 2022 17.9 |

C23 support is uneven in practice, which the first Windows CI run found the
hard way — 22 × `error C2065: 'nullptr': undeclared identifier`. MSVC has no
`/std:c23` yet, so the build passes `/std:clatest` and `/Zc:__STDC__`
explicitly rather than relying on CMake's `C_STANDARD 23` mapping.

Taking the flag and implementing a feature are different things, though, so
configure also *probes* for `nullptr` with those same flags and defines a
compatibility macro only if it is genuinely missing. A compiler that gains the
keyword later just stops needing the shim — nothing to update. Which path you
got is printed at configure time:

```
-- gavaga: C23 nullptr - native
```

(For the record: including `<stddef.h>` does not help here. In C23 `nullptr` is
a keyword, not a macro — `<stddef.h>` only supplies `nullptr_t`.)

**Debian / Ubuntu**

```sh
sudo apt install build-essential git cmake ninja-build \
    libx11-dev libxext-dev libxrandr-dev libxi-dev libxcursor-dev \
    libwayland-dev libxkbcommon-dev libdecor-0-dev libgl-dev libegl-dev \
    libpulse-dev libasound2-dev libdbus-1-dev libudev-dev
# if the default gcc is older than 14:
sudo apt install gcc-14 && cmake -B build -DCMAKE_C_COMPILER=gcc-14
```

**RHEL 9 / Rocky 9** — the default GCC is 11, so use the toolset:

```sh
sudo dnf install gcc-toolset-14 cmake ninja-build git \
    libX11-devel libXext-devel libXrandr-devel libXi-devel libXcursor-devel \
    wayland-devel libxkbcommon-devel libdecor-devel mesa-libGL-devel \
    mesa-libEGL-devel pulseaudio-libs-devel alsa-lib-devel dbus-devel \
    systemd-devel
scl enable gcc-toolset-14 -- cmake -B build
scl enable gcc-toolset-14 -- cmake --build build -j
```

**macOS (Apple Silicon)** — Xcode 16 or newer, plus CMake.

```sh
cmake -B build && cmake --build build -j
```

**Windows x64** — VS 2022 17.9+, or MinGW-w64 with GCC 14+.

```pwsh
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config RelWithDebInfo
```

SDL3 is linked statically by default, so the Linux and Windows builds are a
single self-contained binary.

### CMake options

| option | default | |
|---|---|---|
| `GAVAGA_SDL_TAG` | `release-3.2.30` | which SDL3 tag to fetch |
| `GAVAGA_USE_SYSTEM_SDL` | `OFF` | `find_package(SDL3)` instead of fetching |
| `GAVAGA_SDL_SHARED` | `OFF` | build SDL3 as a shared library |
| `GAVAGA_WERROR` | `OFF` | warnings are errors |
| `GAVAGA_ASAN` | `OFF` | address + UB sanitizers |

## Headless capture and soak testing

Useful for CI and for eyeballing a change without opening a window: the
simulation is fast-forwarded to a tick, one frame is drawn and written, and the
program exits.

```sh
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
    ./build/gavaga --play --debug --seed 1 --shot frame.bmp --shot-at 1500
```

`--autoplay` adds a bot that deliberately walks into tractor beams, so an
unattended run exercises capture, rescue and the dual fighter; `--godmode`
keeps it alive to reach later stages, and `--trace` prints state transitions
with their tick so you can point `--shot-at` at the exact moment something
happened. The full list is in [CONTRIBUTING.md](CONTRIBUTING.md).

## Tests

```sh
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The simulation is a library (`gavaga_core`) that both the game and the tests
link, so the tests exercise the code that actually ships. They cover the
fixed-point maths, the trig tables and their inverse, every path table (arc
totals, mirror symmetry to within a pixel, resuming a part-way runner), the
LFSR starfield, formation geometry across the whole sway cycle, pool bounds
over a 40,000-tick unattended run, and determinism: two games from the same
seed must stay bit-identical for 6,000 ticks, and two different seeds must not.

They run on Linux, Windows and macOS in CI — a determinism test is only worth
something if it passes on more than one machine.

## Where to go next

- **A proper mixer** — the synth is deliberately tiny, with no filters,
  reverb, or music bed.
- **Rumble** on the gamepad; SDL3 has it, nothing uses it.
- **More enemy kinds** beyond the three, and per-kind attack behaviour.
- **Replays**, which the deterministic core makes almost free: record the
  seed and the input bits, play them back.

## Contributing

Bug reports and pull requests welcome — see [CONTRIBUTING.md](CONTRIBUTING.md)
for the build, the house style, and how to draw new path tables. By taking part
you agree to the [Code of Conduct](CODE_OF_CONDUCT.md).

## Licence

[MIT](LICENSE). The art, sound and path tables are all original work written
for this repository; no Namco assets are included, and please do not contribute
any.
