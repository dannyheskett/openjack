#include "sound.h"
#include "audio.h"
#include <stdint.h>
#include <stdlib.h>

// Effects are short square-wave clips synthesized at startup. Playback goes
// through the audio.h backend (raylib on desktop/web/android, AVAudioEngine on
// iOS); this file is backend-agnostic and does the synthesis.

#define SAMPLE_RATE 44100

static bool        enabled = false;
static AudioHandle effects[SFX_COUNT];

static AudioHandle sound_from_samples(int16_t* samples, int count) {
    AudioHandle h = audio_load(samples, count, SAMPLE_RATE);
    free(samples);
    return h;
}

static AudioHandle make_tone(float freq, float dur, float duty, float vol) {
    int n = (int)(dur * SAMPLE_RATE);
    int16_t* buf = malloc(sizeof(int16_t) * n);
    if (!buf) return -1;
    for (int i = 0; i < n; i++) {
        float phase = freq * ((float)i / SAMPLE_RATE);
        phase -= (int)phase;
        float env = 1.0f - (float)i / n;
        float v = (phase < duty ? 1.0f : -1.0f) * vol * env;
        buf[i] = (int16_t)(v * 32767.0f);
    }
    return sound_from_samples(buf, n);
}

static AudioHandle make_sweep(float f0, float f1, float dur, float duty, float vol) {
    int n = (int)(dur * SAMPLE_RATE);
    int16_t* buf = malloc(sizeof(int16_t) * n);
    if (!buf) return -1;
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float freq = f0 + (f1 - f0) * ((float)i / n);
        phase += freq / SAMPLE_RATE;
        float p = phase - (int)phase;
        float env = 1.0f - (float)i / n;
        float v = (p < duty ? 1.0f : -1.0f) * vol * env;
        buf[i] = (int16_t)(v * 32767.0f);
    }
    return sound_from_samples(buf, n);
}

static AudioHandle make_arp(const float* freqs, int count, float per_note, float duty, float vol) {
    int note_n = (int)(per_note * SAMPLE_RATE);
    int n = note_n * count;
    int16_t* buf = malloc(sizeof(int16_t) * n);
    if (!buf) return -1;
    for (int j = 0; j < count; j++)
        for (int i = 0; i < note_n; i++) {
            float phase = freqs[j] * ((float)i / SAMPLE_RATE);
            phase -= (int)phase;
            float env = 1.0f - (float)i / note_n;
            float v = (phase < duty ? 1.0f : -1.0f) * vol * env;
            buf[j * note_n + i] = (int16_t)(v * 32767.0f);
        }
    return sound_from_samples(buf, n);
}

void sound_init(void) {
    audio_init();
    if (!audio_ready()) return;

    static const float win_arp[]  = {523.25f, 659.25f, 783.99f};            // C E G
    static const float bj_arp[]   = {523.25f, 659.25f, 783.99f, 1046.50f};  // C E G C
    static const float lose_arp[] = {392.0f, 311.13f, 233.08f};             // G Eb Bb
    static const float push_arp[] = {440.0f, 440.0f};
    static const float sel_arp[]  = {659.25f, 987.77f};

    effects[SFX_DEAL]        = make_tone(380.0f, 0.05f, 0.5f, 0.16f);
    effects[SFX_FLIP]        = make_tone(520.0f, 0.04f, 0.35f, 0.16f);
    effects[SFX_WIN]         = make_arp(win_arp, 3, 0.06f, 0.5f, 0.26f);
    effects[SFX_LOSE]        = make_arp(lose_arp, 3, 0.10f, 0.5f, 0.24f);
    effects[SFX_PUSH]        = make_arp(push_arp, 2, 0.05f, 0.5f, 0.18f);
    effects[SFX_BLACKJACK]   = make_arp(bj_arp, 4, 0.06f, 0.5f, 0.30f);
    effects[SFX_BUST]        = make_sweep(300.0f, 90.0f, 0.30f, 0.5f, 0.26f);
    effects[SFX_MENU_MOVE]   = make_tone(440.0f, 0.03f, 0.5f, 0.18f);
    effects[SFX_MENU_SELECT] = make_arp(sel_arp, 2, 0.05f, 0.5f, 0.26f);
}

void sound_shutdown(void) {
    if (!audio_ready()) return;
    for (int i = 0; i < SFX_COUNT; i++) audio_unload(effects[i]);
    audio_shutdown();
}

bool sound_is_enabled(void) { return enabled; }
void sound_toggle(void)     { enabled = !enabled; }

void sound_play(SfxId id) {
    if (!enabled || !audio_ready() || id < 0 || id >= SFX_COUNT) return;
    audio_play(effects[id]);
}
