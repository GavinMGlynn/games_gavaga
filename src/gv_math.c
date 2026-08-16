// gv_math.c - integer trig. Tables are built once at init so gameplay never
// touches floating point; the same inputs give the same motion on every
// platform.
#include "gv_common.h"

#define GV_SIN_BITS  10
#define GV_SIN_SIZE  (1 << GV_SIN_BITS)          // 1024 entries over a turn
#define GV_SIN_SHIFT (16 - GV_SIN_BITS)          // ang_t -> table index

#define GV_ATAN_SIZE 257                          // atan(i/256), i in [0,256]

static fix_t s_sin[GV_SIN_SIZE];
static ang_t s_atan[GV_ATAN_SIZE];
static bool  s_ready = false;

void gv_math_init(void) {
    if (s_ready) return;

    for (int i = 0; i < GV_SIN_SIZE; i++) {
        double a = (double)i * (2.0 * SDL_PI_D) / (double)GV_SIN_SIZE;
        s_sin[i] = (fix_t)SDL_lround(SDL_sin(a) * (double)GV_FIX_ONE);
    }
    for (int i = 0; i < GV_ATAN_SIZE; i++) {
        double t = (double)i / 256.0;
        // atan over [0,1] maps to [0, 45deg] == [0, 8192] in BAM.
        s_atan[i] = (ang_t)SDL_lround(SDL_atan(t) * (65536.0 / (2.0 * SDL_PI_D)));
    }
    s_ready = true;
}

fix_t gv_sin(ang_t a) { return s_sin[a >> GV_SIN_SHIFT]; }
fix_t gv_cos(ang_t a) { return s_sin[(ang_t)(a + GV_ANG_90) >> GV_SIN_SHIFT]; }

// atan(num/den) for 0 <= num <= den, as BAM in [0, 8192].
static ang_t atan_ratio(int32_t num, int32_t den) {
    if (den <= 0) return 0;
    int32_t idx = (int32_t)(((int64_t)num * 256) / den);
    return s_atan[gv_clampi(idx, 0, GV_ATAN_SIZE - 1)];
}

ang_t gv_dir(fix_t dx, fix_t dy) {
    // Screen space has +y down, and heading 0 means up, so the "forward"
    // component is -dy. Fold into one octant, then unfold by quadrant.
    int32_t ax = dx < 0 ? -dx : dx;
    int32_t up = -dy;
    int32_t ay = up < 0 ? -up : up;

    ang_t base;   // angle from the up axis toward +x, in [0, 16384]
    if (ax <= ay) base = atan_ratio(ax, ay);
    else          base = (ang_t)(GV_ANG_90 - atan_ratio(ay, ax));

    if (dx >= 0) return up >= 0 ? base : (ang_t)(GV_ANG_180 - base);
    else         return up >= 0 ? (ang_t)(0u - base) : (ang_t)(GV_ANG_180 + base);
}
