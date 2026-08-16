// gv_common.h - shared types: fixed point, BAM angles, RNG, playfield constants.
#ifndef GV_COMMON_H
#define GV_COMMON_H

#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// MSVC's C mode has no C23 `nullptr` keyword (still true of 19.51), and
// Windows x64 is a supported target. CMake probes for the keyword and defines
// GV_NO_NULLPTR only when the compiler genuinely lacks it, so this never
// shadows a real keyword - a future MSVC that implements it simply stops
// failing the probe and this block disappears.
#ifdef GV_NO_NULLPTR
#  define nullptr ((void *)0)
#endif

// ---------------------------------------------------------------------------
// Playfield / timing
// ---------------------------------------------------------------------------
// The logical playfield. Everything in the game reasons in these units; the
// renderer scales to the window with SDL_SetRenderLogicalPresentation.
#define GV_SCREEN_W 224
#define GV_SCREEN_H 288

// 16.5 ms == 60.60606... Hz, the arcade refresh rate. Exact in integer ns.
#define GV_TICK_NS  16500000ULL
#define GV_TICK_HZ  (1000000000.0 / (double)GV_TICK_NS)

// If we fall behind by more than this many ticks in one frame, drop the debt
// rather than spiral.
#define GV_MAX_CATCHUP_TICKS 5

// ---------------------------------------------------------------------------
// 16.16 fixed point
// ---------------------------------------------------------------------------
typedef int32_t fix_t;

#define GV_FIX_SHIFT 16
#define GV_FIX_ONE   (1 << GV_FIX_SHIFT)

static inline fix_t   gv_fix(int32_t v)        { return (fix_t)((uint32_t)v << GV_FIX_SHIFT); }
static inline int32_t gv_unfix(fix_t v)        { return v >> GV_FIX_SHIFT; }
static inline float   gv_fixf(fix_t v)         { return (float)v / (float)GV_FIX_ONE; }
static inline fix_t   gv_fix_from_f(float f)   { return (fix_t)(f * (float)GV_FIX_ONE); }
static inline fix_t   gv_fmul(fix_t a, fix_t b){ return (fix_t)(((int64_t)a * (int64_t)b) >> GV_FIX_SHIFT); }
static inline fix_t   gv_fdiv(fix_t a, fix_t b){ return b ? (fix_t)(((int64_t)a << GV_FIX_SHIFT) / b) : 0; }
static inline fix_t   gv_fabs(fix_t a)         { return a < 0 ? -a : a; }

static inline int32_t gv_clampi(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline fix_t gv_clampf(fix_t v, fix_t lo, fix_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ---------------------------------------------------------------------------
// Angles: BAM (binary angle measure). 0 == pointing up (screen -Y),
// increasing clockwise, wrapping naturally at 65536.
// ---------------------------------------------------------------------------
typedef uint16_t ang_t;

#define GV_ANG_90  ((ang_t)16384)
#define GV_ANG_180 ((ang_t)32768)
#define GV_ANG_270 ((ang_t)49152)
#define GV_ANG_DEG(d) ((ang_t)(int32_t)((d) * (65536.0 / 360.0)))

void  gv_math_init(void);
fix_t gv_sin(ang_t a);        // 16.16, [-1,1]
fix_t gv_cos(ang_t a);
// Heading (0 == up, cw) of the vector (dx, dy) in screen space (+y down).
ang_t gv_dir(fix_t dx, fix_t dy);
// Shortest signed difference to -> from, in [-32768, 32767].
static inline int32_t gv_angdiff(ang_t from, ang_t to) {
    return (int32_t)(int16_t)(uint16_t)(to - from);
}

// Unit velocity components for a heading, scaled by speed.
static inline fix_t gv_vx(ang_t a, fix_t speed) { return gv_fmul(speed,  gv_sin(a)); }
static inline fix_t gv_vy(ang_t a, fix_t speed) { return gv_fmul(speed, -gv_cos(a)); }

// ---------------------------------------------------------------------------
// Deterministic RNG (xorshift32). Kept out of libc so replays stay stable
// across platforms.
// ---------------------------------------------------------------------------
typedef struct { uint32_t s; } gv_rng;

static inline void gv_rng_seed(gv_rng *r, uint32_t seed) { r->s = seed ? seed : 0x1234567u; }
static inline uint32_t gv_rng_next(gv_rng *r) {
    uint32_t x = r->s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    r->s = x;
    return x;
}
static inline uint32_t gv_rng_below(gv_rng *r, uint32_t n) {
    return n ? gv_rng_next(r) % n : 0;
}

#define GV_COUNTOF(a) ((int)(sizeof(a) / sizeof((a)[0])))

#endif // GV_COMMON_H
