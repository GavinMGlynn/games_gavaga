// gv_audio.c - a small square/triangle/saw/noise synth.
//
// Each effect is described by a waveform, a frequency sweep and a duration;
// longer cues are a short list of notes played in sequence. Voices are mixed
// per sample on the audio thread. Nothing here is loaded from disk.
#include "gv_audio.h"
#include "gv_game.h"     // for the GV_SFX_* ids

#define GV_SR       44100
#define GV_VOICES   10
#define GV_MASTER   0.34f

enum { W_SQUARE = 0, W_TRI, W_SAW, W_NOISE };

typedef struct {
    float    hz;
    uint16_t ms;
} gv_note;

typedef struct {
    uint8_t        wave;
    float          f0, f1;      // Hz at the start and end of the sweep
    float          vol;
    uint16_t       ms;
    const gv_note *melody;      // when set, played note by note instead
    int            melody_len;
} gv_sfxdef;

// --- the sound set --------------------------------------------------------
static const gv_note M_STAGE[]     = { {523,90},{659,90},{784,90},{1047,140} };
static const gv_note M_RESCUE[]    = { {523,70},{659,70},{784,70},{1047,70},{1319,160} };
static const gv_note M_EXTRA[]     = { {784,60},{1047,60},{1319,140} };
static const gv_note M_CAPTURED[]  = { {659,110},{554,110},{440,110},{330,110},{247,220} };
static const gv_note M_GAMEOVER[]  = { {392,180},{349,180},{294,180},{196,340} };
static const gv_note M_PERFECT[]   = { {1047,80},{1319,80},{1568,80},{2093,200} };

#define SWEEP(w, a, b, v, t) { (w), (a), (b), (v), (t), nullptr, 0 }
#define TUNE(w, m, v)        { (w), 0, 0, (v), 0, (m), GV_COUNTOF(m) }

static const gv_sfxdef SFX[GV_SFX_COUNT] = {
    [GV_SFX_NONE]          = SWEEP(W_SQUARE, 0, 0, 0.00f, 0),
    [GV_SFX_SHOT]          = SWEEP(W_SQUARE, 900,  220, 0.26f,  70),
    [GV_SFX_ENEMY_BOOM]    = SWEEP(W_NOISE,  420,   60, 0.42f, 260),
    [GV_SFX_FLAGSHIP_HIT]  = SWEEP(W_SQUARE, 300,  520, 0.30f,  60),
    [GV_SFX_FLAGSHIP_BOOM] = SWEEP(W_NOISE,  300,   40, 0.52f, 420),
    [GV_SFX_PLAYER_BOOM]   = SWEEP(W_NOISE,  220,   30, 0.58f, 700),
    [GV_SFX_DIVE]          = SWEEP(W_SAW,    180,  700, 0.18f, 240),
    [GV_SFX_BEAM]          = SWEEP(W_TRI,    120,  900, 0.26f, 520),
    [GV_SFX_CAPTURED]      = TUNE(W_TRI,    M_CAPTURED, 0.32f),
    [GV_SFX_RESCUE]        = TUNE(W_SQUARE, M_RESCUE,   0.30f),
    [GV_SFX_EXTRA_LIFE]    = TUNE(W_SQUARE, M_EXTRA,    0.30f),
    [GV_SFX_STAGE]         = TUNE(W_SQUARE, M_STAGE,    0.28f),
    [GV_SFX_GAMEOVER]      = TUNE(W_TRI,    M_GAMEOVER, 0.34f),
    [GV_SFX_PERFECT]       = TUNE(W_SQUARE, M_PERFECT,  0.30f),
};

// --- voices ---------------------------------------------------------------
typedef struct {
    bool     active;
    uint8_t  wave;
    float    phase;
    float    inc, inc_step;     // phase increment per sample, and its drift
    float    vol;
    uint32_t len, pos, attack;  // samples
    uint32_t noise;
    float    noise_val;

    const gv_note *melody;      // remaining notes, if this is a tune
    int      melody_len, melody_idx;
} gv_voice;

static SDL_AudioStream *s_stream;
static SDL_Mutex       *s_lock;
static gv_voice         s_voices[GV_VOICES];
static bool             s_muted;
static bool             s_ok;

static void arm(gv_voice *v, uint8_t wave, float f0, float f1, float vol, uint16_t ms) {
    const uint32_t len = (uint32_t)ms * GV_SR / 1000u;
    v->active   = true;
    v->wave     = wave;
    v->phase    = 0.0f;
    v->inc      = f0 / (float)GV_SR;
    v->inc_step = len ? ((f1 - f0) / (float)GV_SR) / (float)len : 0.0f;
    v->vol      = vol;
    v->len      = len ? len : 1u;
    v->pos      = 0;
    // A couple of milliseconds of fade-in, otherwise every note starts with a
    // click.
    v->attack   = SDL_min((uint32_t)(GV_SR / 500), v->len / 4u);
    if (!v->noise) v->noise = 0x1234u;
}

static void voice_start(gv_voice *v, const gv_sfxdef *d) {
    SDL_zerop(v);
    if (d->melody && d->melody_len > 0) {
        v->melody     = d->melody;
        v->melody_len = d->melody_len;
        v->melody_idx = 0;
        arm(v, d->wave, d->melody[0].hz, d->melody[0].hz, d->vol, d->melody[0].ms);
    } else {
        arm(v, d->wave, d->f0, d->f1, d->vol, d->ms);
    }
}

static float wave_sample(gv_voice *v) {
    switch (v->wave) {
    case W_TRI:  return 4.0f * SDL_fabsf(v->phase - 0.5f) - 1.0f;
    case W_SAW:  return 2.0f * v->phase - 1.0f;
    case W_NOISE: return v->noise_val;
    default:     return v->phase < 0.5f ? 1.0f : -1.0f;   // square
    }
}

static void mix(float *out, int frames) {
    SDL_memset(out, 0, (size_t)frames * sizeof *out);

    for (int vi = 0; vi < GV_VOICES; vi++) {
        gv_voice *v = &s_voices[vi];
        if (!v->active) continue;

        for (int i = 0; i < frames && v->active; i++) {
            // Linear attack then linear decay - percussive, which is what
            // these blips want.
            float env;
            if (v->pos < v->attack) {
                env = (float)v->pos / (float)(v->attack ? v->attack : 1u);
            } else {
                const uint32_t span = v->len - v->attack;
                env = span ? 1.0f - (float)(v->pos - v->attack) / (float)span : 0.0f;
            }
            if (env < 0.0f) env = 0.0f;

            out[i] += wave_sample(v) * v->vol * env;

            v->phase += v->inc;
            if (v->phase >= 1.0f) {
                v->phase -= (float)(int)v->phase;
                if (v->wave == W_NOISE) {
                    uint32_t n = v->noise;
                    n ^= n << 13; n ^= n >> 17; n ^= n << 5;
                    v->noise = n;
                    v->noise_val = (float)(int32_t)(n >> 16) / 32768.0f - 1.0f;
                }
            }
            v->inc += v->inc_step;
            if (v->inc < 0.0f) v->inc = 0.0f;

            if (++v->pos >= v->len) {
                // arm() deliberately leaves the melody cursor alone, so a tune
                // can just step to its next note in place.
                if (v->melody && ++v->melody_idx < v->melody_len) {
                    const gv_note *n = &v->melody[v->melody_idx];
                    arm(v, v->wave, n->hz, n->hz, v->vol, n->ms);
                } else {
                    v->active = false;
                }
            }
        }
    }

    for (int i = 0; i < frames; i++) {
        float s = out[i] * GV_MASTER;
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        out[i] = s;
    }
}

static void SDLCALL audio_cb(void *userdata, SDL_AudioStream *stream,
                             int additional_amount, int total_amount) {
    (void)userdata; (void)total_amount;

    float buf[512];
    while (additional_amount > 0) {
        const int want = SDL_min(additional_amount / (int)sizeof(float), 512);
        if (want <= 0) break;

        SDL_LockMutex(s_lock);
        mix(buf, want);
        SDL_UnlockMutex(s_lock);

        SDL_PutAudioStreamData(stream, buf, want * (int)sizeof(float));
        additional_amount -= want * (int)sizeof(float);
    }
}

// --- api ------------------------------------------------------------------
bool gv_audio_init(void) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("gavaga: no audio subsystem (%s) - running silent", SDL_GetError());
        return false;
    }

    s_lock = SDL_CreateMutex();
    if (!s_lock) return false;

    const SDL_AudioSpec spec = { .format = SDL_AUDIO_F32, .channels = 1, .freq = GV_SR };
    s_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                         &spec, audio_cb, nullptr);
    if (!s_stream) {
        SDL_Log("gavaga: no audio device (%s) - running silent", SDL_GetError());
        SDL_DestroyMutex(s_lock);
        s_lock = nullptr;
        return false;
    }

    SDL_ResumeAudioStreamDevice(s_stream);
    s_ok = true;

    // Worth saying out loud: "no sound" is usually the driver being a fallback
    // or the host not wiring the device up, and that is invisible otherwise.
    const char *drv = SDL_GetCurrentAudioDriver();
    SDL_Log("gavaga: audio driver '%s', %d Hz mono f32",
            drv ? drv : "?", GV_SR);
    return true;
}

void gv_audio_quit(void) {
    if (s_stream) { SDL_DestroyAudioStream(s_stream); s_stream = nullptr; }
    if (s_lock)   { SDL_DestroyMutex(s_lock);         s_lock   = nullptr; }
    s_ok = false;
}

void gv_audio_play(int sfx) {
    if (!s_ok || s_muted || sfx <= GV_SFX_NONE || sfx >= GV_SFX_COUNT) return;
    const gv_sfxdef *d = &SFX[sfx];

    SDL_LockMutex(s_lock);

    // Prefer a free voice; otherwise take the one nearest the end of its life
    // rather than dropping the new sound.
    gv_voice *pick = nullptr;
    float best = -1.0f;
    for (int i = 0; i < GV_VOICES; i++) {
        gv_voice *v = &s_voices[i];
        if (!v->active) { pick = v; break; }
        const float progress = (float)v->pos / (float)v->len;
        if (progress > best) { best = progress; pick = v; }
    }
    if (pick) voice_start(pick, d);

    SDL_UnlockMutex(s_lock);
}

void gv_audio_set_muted(bool muted) {
    s_muted = muted;
    if (!s_ok) return;
    SDL_LockMutex(s_lock);
    if (muted) for (int i = 0; i < GV_VOICES; i++) s_voices[i].active = false;
    SDL_UnlockMutex(s_lock);
}

bool gv_audio_muted(void) { return s_muted; }
bool gv_audio_ok(void)    { return s_ok; }
