# Gavaga

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
| ← → / A D | move |
| Space / Z / Ctrl | fire (two shots on screen, like the original) |
| Enter | start |
| F1 | debug overlay |
| F3 | cycle the path catalogue |
| P / F2 | pause / single-step one tick |
| R | restart |
| F11 / Alt-Enter | fullscreen |
| Esc | quit |

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

**Tuning them:** press F1, then F3 to cycle the catalogue. It draws one path on
its own from a representative origin, both mirrors at once (cyan `+1`, pink
`-1`), with its step count and total duration. Edit the table, rebuild, look
again. In-game, F1 alone draws each entity's trail plus its *predicted*
remaining flight — the prediction runs the same integration the simulation
does, so what you see is exactly where it will go.

## How it fits together

| | |
|---|---|
| `gv_path.c` / `gv_paths.c` | the turn-table interpreter and the tables |
| `gv_game.c` | simulation: pools, formation, waves, collisions. Touches no renderer, no clock, no heap |
| `gv_star.c` | LFSR starfield |
| `gv_sprite.c` | placeholder art as ASCII rows, baked to one atlas at startup |
| `gv_render.c` | all drawing |
| `gv_debug.c` | the F1/F3 overlay |
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
platform.

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

## Headless capture

Useful for CI and for eyeballing a change without opening a window: the
simulation is fast-forwarded to a tick, one frame is drawn and written, and the
program exits.

```sh
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
    ./build/gavaga --play --debug --shot frame.bmp --shot-at 1500
```

`--play` starts a game immediately, `--debug` turns the overlay on, `--path N`
opens the catalogue on path `N`.

## Where to go next

Not in yet, roughly in the order I'd add them:

- **Audio.** No sound at all right now.
- **The tractor beam.** A flagship capturing the player's ship, and shooting it
  free for the dual fighter. The path system already has what it needs.
- **Two-hit flagships changing colour** on the first hit, as a visible tell.
- **Per-stage difficulty** beyond dive frequency — speed and fire rate scale.
- **Persistent high score.**
