// gv_audio.c - a small square/triangle/saw/noise synth and the mixer it feeds.
//
// Each effect is described by a waveform, a frequency sweep and a duration;
// longer cues are a short list of notes played in sequence. Nothing here is
// loaded from disk.
//
// Signal path, per sample:
//
//     voice -> one-pole low-pass -> envelope -> dry bus
//                                            \-> reverb send bus
//     music bed ------------------------------> dry bus (ducked by the send)
//     dry + reverb(send) -> master gain -> soft clip -> out
//
// The filter is what stops the square waves sounding like a smoke alarm: a
// raw square is all odd harmonics forever, and rolling the top off leaves the
// shape while taking the glare. The reverb is two combs and an allpass, which
// is not a room so much as a suggestion of one, and that is all these blips
// need to stop sounding like they were recorded in a vacuum.
#include "gv_audio.h"
#include "gv_game.h"     // for the GV_SFX_* ids
#include "gv_sfx_pcm.h"  // Kenney's effects, decoded at bake time

#define GV_SR       44100
#define GV_VOICES   10
#define GV_MASTER   0.34f
#define GV_PCM_GAIN 0.55f   // recorded clips against the synth's own level

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
    float          cut;         // low-pass corner, Hz. 0 means leave it alone
    float          send;        // how much of it goes to the reverb, 0..1
    const gv_note *melody;      // when set, played note by note instead
    int            melody_len;
    const short   *clip;        // when set, a recorded effect wins over all of it
    int            clip_len;
} gv_sfxdef;

// --- the sound set --------------------------------------------------------
static const gv_note M_STAGE[]     = { {523,90},{659,90},{784,90},{1047,140} };
static const gv_note M_RESCUE[]    = { {523,70},{659,70},{784,70},{1047,70},{1319,160} };
static const gv_note M_EXTRA[]     = { {784,60},{1047,60},{1319,140} };
static const gv_note M_CAPTURED[]  = { {659,110},{554,110},{440,110},{330,110},{247,220} };
static const gv_note M_GAMEOVER[]  = { {392,180},{349,180},{294,180},{196,340} };
static const gv_note M_PERFECT[]   = { {1047,80},{1319,80},{1568,80},{2093,200} };

// SWEEP(wave, from, to, vol, ms, cutoff, reverb send)
#define SWEEP(w, a, b, v, t, c, s) { (w), (a), (b), (v), (t), (c), (s), nullptr, 0, nullptr, 0 }
#define TUNE(w, m, v, c, s)        { (w), 0, 0, (v), 0, (c), (s), (m), GV_COUNTOF(m), nullptr, 0 }

static const gv_sfxdef SFX[GV_SFX_COUNT] = {
    [GV_SFX_NONE]          = SWEEP(W_SQUARE, 0, 0, 0.00f, 0, 0, 0.00f),
    // The player's own shot stays dry and bright: it fires constantly, and
    // anything with a tail on it turns into mush at that rate.
    [GV_SFX_SHOT]          = SWEEP(W_SQUARE, 900,  220, 0.26f,  70, 5200, 0.00f),
    [GV_SFX_ENEMY_BOOM]    = SWEEP(W_NOISE,  420,   60, 0.42f, 260, 2600, 0.28f),
    [GV_SFX_FLAGSHIP_HIT]  = SWEEP(W_SQUARE, 300,  520, 0.30f,  60, 4000, 0.12f),
    [GV_SFX_FLAGSHIP_BOOM] = SWEEP(W_NOISE,  300,   40, 0.52f, 420, 2000, 0.40f),
    [GV_SFX_PLAYER_BOOM]   = SWEEP(W_NOISE,  220,   30, 0.58f, 700, 1500, 0.50f),
    [GV_SFX_DIVE]          = SWEEP(W_SAW,    180,  700, 0.18f, 240, 3000, 0.10f),
    [GV_SFX_BEAM]          = SWEEP(W_TRI,    120,  900, 0.26f, 520, 3400, 0.35f),
    [GV_SFX_CAPTURED]      = TUNE(W_TRI,    M_CAPTURED, 0.32f, 2800, 0.45f),
    [GV_SFX_RESCUE]        = TUNE(W_SQUARE, M_RESCUE,   0.30f, 4200, 0.30f),
    [GV_SFX_EXTRA_LIFE]    = TUNE(W_SQUARE, M_EXTRA,    0.30f, 4200, 0.30f),
    [GV_SFX_STAGE]         = TUNE(W_SQUARE, M_STAGE,    0.28f, 4200, 0.25f),
    [GV_SFX_GAMEOVER]      = TUNE(W_TRI,    M_GAMEOVER, 0.34f, 2200, 0.50f),
    [GV_SFX_PERFECT]       = TUNE(W_SQUARE, M_PERFECT,  0.30f, 5000, 0.30f),
};

// --- the music bed --------------------------------------------------------
// A two-bar walking bass in A minor with the root doubled an octave up on the
// off-beats. Deliberately plain: it has to sit under gunfire for minutes at a
// time without asking for attention.
typedef struct { float hz; uint8_t sixteenths; } gv_step;

static const gv_step MUSIC[] = {
    {110.00f,2},{  0.0f,1},{220.00f,1},{110.00f,2},{164.81f,2},
    {130.81f,2},{  0.0f,1},{261.63f,1},{130.81f,2},{196.00f,2},
    { 98.00f,2},{  0.0f,1},{196.00f,1},{ 98.00f,2},{146.83f,2},
    {110.00f,2},{  0.0f,1},{220.00f,1},{164.81f,2},{123.47f,2},
};

#define MUSIC_VOL 0.13f

// --- reverb ---------------------------------------------------------------
// Lengths are coprime so the combs do not line up and ring on one note.
#define RV_A   1237
#define RV_B   1687
#define RV_AP   389

static float s_comb_a[RV_A], s_comb_b[RV_B], s_allpass[RV_AP];
static int   s_ia, s_ib, s_iap;

static float reverb(float in) {
    float a = s_comb_a[s_ia];
    s_comb_a[s_ia] = in + a * 0.76f;
    if (++s_ia >= RV_A) s_ia = 0;

    float b = s_comb_b[s_ib];
    s_comb_b[s_ib] = in + b * 0.72f;
    if (++s_ib >= RV_B) s_ib = 0;

    float sum = (a + b) * 0.5f;

    const float d = s_allpass[s_iap];
    const float y = d - sum * 0.6f;
    s_allpass[s_iap] = sum + d * 0.6f;
    if (++s_iap >= RV_AP) s_iap = 0;

    return y;
}

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

    float    lp, lp_k;          // one-pole low-pass state and coefficient
    float    send;              // reverb send, 0..1

    // A recorded clip, when this effect has one. The synth fields above are
    // unused then: a sample already has its own envelope and tone.
    const short *pcm;

    const gv_note *melody;      // remaining notes, if this is a tune
    int      melody_len, melody_idx;
} gv_voice;

static SDL_AudioStream *s_stream;
static SDL_Mutex       *s_lock;
static gv_voice         s_voices[GV_VOICES];
static gv_voice         s_music;        // its own voice, so it cannot be stolen
static int              s_music_level;
static int              s_music_step;
static bool             s_muted;
static bool             s_ok;
static float            s_duck = 1.0f;  // music gain, pulled down by loud effects

// One-pole coefficient for a corner frequency, without calling expf per note.
static float lp_coeff(float hz) {
    if (hz <= 0.0f) return 1.0f;                 // 1.0 == no filtering
    const float k = hz / (hz + (float)GV_SR / 6.2831853f);
    return k > 1.0f ? 1.0f : k;
}

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
    const float lp = v->lp;      // keep the filter state; zeroing it clicks
    SDL_zerop(v);
    v->lp   = lp;
    v->lp_k = lp_coeff(d->cut);
    v->send = d->send;

    if (d->clip && d->clip_len > 0) {
        v->active = true;
        v->pcm    = d->clip;
        v->len    = (uint32_t)d->clip_len;
        v->pos    = 0;
        v->vol    = GV_PCM_GAIN;
        return;
    }

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

// Advances one voice by a sample and returns its filtered, enveloped output.
// Returns 0 and clears `active` when the voice (or its tune) is finished.
static float voice_sample(gv_voice *v) {
    // A recording carries its own shape; the synth envelope would only fight
    // it. The clips are faded at the tail at bake time so the end does not
    // click.
    if (v->pcm) {
        const float s = (float)v->pcm[v->pos] * (1.0f / 32768.0f) * v->vol;
        if (++v->pos >= v->len) v->active = false;
        return s;
    }

    // Linear attack then linear decay - percussive, which is what these blips
    // want.
    float env;
    if (v->pos < v->attack) {
        env = (float)v->pos / (float)(v->attack ? v->attack : 1u);
    } else {
        const uint32_t span = v->len - v->attack;
        env = span ? 1.0f - (float)(v->pos - v->attack) / (float)span : 0.0f;
    }
    if (env < 0.0f) env = 0.0f;

    const float raw = wave_sample(v);
    v->lp += (raw - v->lp) * v->lp_k;
    const float out = v->lp * v->vol * env;

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
        // arm() deliberately leaves the melody cursor alone, so a tune can just
        // step to its next note in place.
        if (v->melody && ++v->melody_idx < v->melody_len) {
            const gv_note *n = &v->melody[v->melody_idx];
            arm(v, v->wave, n->hz, n->hz, v->vol, n->ms);
        } else {
            v->active = false;
        }
    }
    return out;
}

// The bed steps itself: when a note runs out the sequencer advances and arms
// the next one, looping forever until the level goes to zero.
static void music_step_next(void) {
    // Tempo and brightness both come off the level, so stage 8 sounds busier
    // than stage 1 without a second tune to maintain.
    const int   lvl  = gv_clampi(s_music_level, 1, 4);
    const float bpm  = 104.0f + 12.0f * (float)(lvl - 1);
    const float six  = 60000.0f / bpm / 4.0f;    // ms per sixteenth
    const float cut  = 700.0f + 260.0f * (float)(lvl - 1);

    const gv_step *st = &MUSIC[s_music_step];
    s_music_step = (s_music_step + 1) % (int)GV_COUNTOF(MUSIC);

    const uint16_t ms = (uint16_t)(six * (float)st->sixteenths);
    if (st->hz <= 0.0f) {                 // a rest still has to take up time
        SDL_zerop(&s_music);
        s_music.active = true;
        s_music.vol    = 0.0f;
        s_music.len    = (uint32_t)ms * GV_SR / 1000u;
        s_music.len    = s_music.len ? s_music.len : 1u;
        s_music.lp_k   = 1.0f;
        return;
    }

    const float lp = s_music.lp;
    SDL_zerop(&s_music);
    s_music.lp   = lp;
    s_music.lp_k = lp_coeff(cut);
    s_music.send = 0.18f;
    arm(&s_music, W_TRI, st->hz, st->hz, MUSIC_VOL, ms);
}

static void mix(float *out, int frames) {
    for (int i = 0; i < frames; i++) {
        float dry = 0.0f, send = 0.0f;

        for (int vi = 0; vi < GV_VOICES; vi++) {
            gv_voice *v = &s_voices[vi];
            if (!v->active) continue;
            const float s = voice_sample(v);
            dry  += s;
            send += s * v->send;
        }

        // Effects duck the bed rather than fighting it. Fast down, slow back
        // up, so a burst of explosions holds it out of the way instead of
        // pumping once per blast.
        const float want = 1.0f - SDL_min(SDL_fabsf(dry) * 1.6f, 0.75f);
        if (want < s_duck) s_duck += (want - s_duck) * 0.01f;
        else               s_duck += (want - s_duck) * 0.0004f;

        if (!s_muted && s_music_level > GV_MUSIC_OFF) {
            if (!s_music.active) music_step_next();
            const float m = voice_sample(&s_music) * s_duck;
            dry  += m;
            send += m * s_music.send;
        }

        float s = (dry + reverb(send) * 0.9f) * GV_MASTER;

        // Soft clip. The hard clamp this replaces turned every overlapping
        // explosion into a buzz; this leans on the peaks instead of squaring
        // them off.
        if (s >  1.0f) s =  1.0f;
        else if (s < -1.0f) s = -1.0f;
        else s = s * (1.5f - 0.5f * s * s);

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

    SDL_zeroa(s_comb_a);
    SDL_zeroa(s_comb_b);
    SDL_zeroa(s_allpass);
    s_ia = s_ib = s_iap = 0;
    s_duck = 1.0f;

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
    SDL_Log("gavaga: audio driver '%s', %d Hz mono f32", drv ? drv : "?", GV_SR);
    return true;
}

void gv_audio_quit(void) {
    if (s_stream) { SDL_DestroyAudioStream(s_stream); s_stream = nullptr; }
    if (s_lock)   { SDL_DestroyMutex(s_lock);         s_lock   = nullptr; }
    s_ok = false;
}

void gv_audio_play(int sfx) {
    if (!s_ok || s_muted || sfx <= GV_SFX_NONE || sfx >= GV_SFX_COUNT) return;

    // The recorded effects are held in their own generated table rather than
    // written into SFX[], so the synth definitions stay readable and stay the
    // fallback for every event the pack does not cover.
    gv_sfxdef def = SFX[sfx];
    if (sfx < (int)GV_COUNTOF(GV_PCM_FOR_SFX) && GV_PCM_FOR_SFX[sfx].pcm) {
        def.clip     = GV_PCM_FOR_SFX[sfx].pcm;
        def.clip_len = GV_PCM_FOR_SFX[sfx].len;
    }
    const gv_sfxdef *d = &def;

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
    if (muted) {
        for (int i = 0; i < GV_VOICES; i++) s_voices[i].active = false;
        s_music.active = false;
    }
    SDL_UnlockMutex(s_lock);
}

void gv_audio_set_music(int level) {
    if (level < GV_MUSIC_OFF) level = GV_MUSIC_OFF;
    if (level == s_music_level) return;      // called every frame; do not restart

    if (!s_ok) { s_music_level = level; return; }

    SDL_LockMutex(s_lock);
    s_music_level = level;
    if (level == GV_MUSIC_OFF) {
        s_music.active = false;
        s_music_step   = 0;                  // next bed starts at the top of the bar
    }
    SDL_UnlockMutex(s_lock);
}

int  gv_audio_music(void) { return s_music_level; }
bool gv_audio_muted(void) { return s_muted; }
bool gv_audio_ok(void)    { return s_ok; }
