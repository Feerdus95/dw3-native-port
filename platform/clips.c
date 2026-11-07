/* The port's own sounds (native.h): a WAV file, read and converted to AUDIO_RATE stereo the first time it is needed, and the
   voices that play them, mixed into the game's sound each vsync (audio.c). Used by the effects' sounds (effects.c) and by the
   sound library (soundlib.c). Only the path is kept at the start, so a mod with many sounds costs no memory and no start-up time. */
#include <SDL3/SDL.h>
#include "platform.h"

#define SFX_VOICES 8
#define SFX_MAX_FRAMES (AUDIO_RATE * 10)

static struct {
    const int16_t *data;
    int frames, pos, vol;
} s_voice[SFX_VOICES];

/* PATH (kept) is C's file, at VOL (256 = as it is) */
void native_clip_set(NativeClip *c, const char *path, int vol) {
    native_clip_clear(c);
    c->path = SDL_strdup(path);
    c->vol = vol;
}

void native_clip_clear(NativeClip *c) {
    SDL_free(c->path);
    c->path = NULL;
}

/* C, read and converted the first time: 1 when it can play. One that can't be read stops counting as a sound */
int native_clip_ready(NativeClip *c) {
    SDL_AudioSpec src, dst = {SDL_AUDIO_S16, 2, AUDIO_RATE};
    Uint8 *raw = NULL, *out = NULL;
    Uint32 raw_len = 0;
    int out_len = 0;

    if (c->data != NULL) {
        return 1;
    }
    if (c->path == NULL) {
        return 0;
    }
    if (!SDL_LoadWAV(c->path, &src, &raw, &raw_len)) {
        LOG("sounds: %s: %s", c->path, SDL_GetError());
    } else if (SDL_ConvertAudioSamples(&src, raw, (int)raw_len, &dst, &out, &out_len) && out_len >= 4) {
        c->frames = SDL_min(out_len / 4, SFX_MAX_FRAMES);
        c->data = (int16_t *)out;
        SDL_free(raw);
        return 1;
    } else {
        LOG("sounds: %s: %s", c->path, SDL_GetError());
    }
    SDL_free(raw);
    SDL_free(out);
    native_clip_clear(c);
    return 0;
}

/* Starts C, when it is ready and a voice is free (the sound isn't kept: it plays to its end) */
void native_clip_play(NativeClip *c) {
    int v;

    if (!native_clip_ready(c)) {
        return;
    }
    for (v = 0; v < SFX_VOICES && s_voice[v].data != NULL; v++) {
    }
    if (v < SFX_VOICES) {
        s_voice[v].data = c->data;
        s_voice[v].frames = c->frames;
        s_voice[v].pos = 0;
        s_voice[v].vol = c->vol;
    }
}

/* Adds the playing sounds to BUF, FRAMES stereo frames (audio.c, each vsync) */
void native_sfx_mix(int16_t *buf, int frames) {
    int v, i;

    for (v = 0; v < SFX_VOICES; v++) {
        if (s_voice[v].data == NULL) {
            continue;
        }
        for (i = 0; i < frames && s_voice[v].pos < s_voice[v].frames; i++, s_voice[v].pos++) {
            int k;

            for (k = 0; k < 2; k++) {
                int m = buf[i * 2 + k] + (s_voice[v].data[s_voice[v].pos * 2 + k] * s_voice[v].vol >> 8);

                buf[i * 2 + k] = (int16_t)SDL_clamp(m, -32768, 32767);
            }
        }
        if (s_voice[v].pos >= s_voice[v].frames) {
            s_voice[v].data = NULL;
        }
    }
}
