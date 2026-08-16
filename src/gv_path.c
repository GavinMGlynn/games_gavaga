// gv_path.c - turn-table interpreter. Table data lives in gv_paths.c.
#include "gv_path.h"

extern const gv_pathdef gv_path_table[GV_PATH_COUNT];

const gv_pathdef *gv_path_def(uint16_t id) {
    if (id == GV_PATH_NONE || id >= GV_PATH_COUNT) return nullptr;
    return &gv_path_table[id];
}

const char *gv_path_name(uint16_t id) {
    const gv_pathdef *d = gv_path_def(id);
    return d ? d->name : "-";
}

int gv_path_ticks(uint16_t id) {
    const gv_pathdef *d = gv_path_def(id);
    if (!d) return 0;
    int total = 0;
    for (uint16_t i = 0; i < d->nsteps; i++) total += d->steps[i].ticks;
    return total;
}

void gv_path_clear(gv_path_runner *r) {
    r->def = GV_PATH_NONE;
    r->step = 0;
    r->timer = 0;
    r->mirror = 1;
    r->finished = true;
}

void gv_path_start(gv_path_runner *r, uint16_t def, int8_t mirror) {
    const gv_pathdef *d = gv_path_def(def);
    if (!d || d->nsteps == 0) { gv_path_clear(r); return; }
    r->def      = def;
    r->step     = 0;
    r->timer    = d->steps[0].ticks;
    r->mirror   = mirror < 0 ? -1 : 1;
    r->finished = false;

    // A zero-length leading step would otherwise stall for a tick.
    while (r->timer == 0 && !r->finished) {
        if (++r->step >= d->nsteps) { r->finished = true; break; }
        r->timer = d->steps[r->step].ticks;
    }
}

bool gv_path_step(gv_path_runner *r, ang_t *heading) {
    const gv_pathdef *d = gv_path_def(r->def);
    if (!d || r->finished) return false;

    const gv_pathstep *s = &d->steps[r->step];
    *heading = (ang_t)(*heading + (int32_t)s->turn * r->mirror);

    // This tick has been flown, so report success for it and only tell the
    // caller the table is spent on the *next* call. Returning false here as
    // well would conflate "I just flew a tick" with "there is nothing left",
    // and left gv_path_trace one tick short of what an entity really flies.
    if (--r->timer == 0) {
        do {
            if (++r->step >= d->nsteps) { r->finished = true; break; }
            r->timer = d->steps[r->step].ticks;
        } while (r->timer == 0);
    }
    return true;
}

int gv_path_trace(uint16_t def, int8_t mirror,
                  fix_t x, fix_t y, ang_t heading, fix_t speed,
                  SDL_FPoint *out, int max, int stride) {
    gv_path_runner r;
    gv_path_start(&r, def, mirror);
    return gv_path_trace_from(&r, x, y, heading, speed, out, max, stride);
}

int gv_path_trace_from(const gv_path_runner *src,
                       fix_t x, fix_t y, ang_t heading, fix_t speed,
                       SDL_FPoint *out, int max, int stride) {
    if (max <= 0 || stride <= 0) return 0;

    gv_path_runner r = *src;    // a copy, so the caller's runner is untouched
    if (r.finished || r.def == GV_PATH_NONE) return 0;

    int n = 0, t = 0;
    // The trace mirrors the integration in gv_game.c exactly, so what the
    // overlay draws is what the entity will actually fly.
    while (n < max) {
        if (!gv_path_step(&r, &heading)) break;
        x += gv_vx(heading, speed);
        y += gv_vy(heading, speed);
        if (++t % stride == 0) {
            out[n].x = gv_fixf(x);
            out[n].y = gv_fixf(y);
            n++;
        }
    }
    return n;
}
