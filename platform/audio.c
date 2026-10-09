/* The sound output: each vsync the SPU mixes that vsync's worth of samples
   (spu.c) into an SDL audio stream. The queue is kept about three vsyncs
   long by mixing a little more or less, so it never runs dry or lags. */
#include <SDL3/SDL.h>
#include "platform.h"

static SDL_AudioStream *s_stream;
static FILE *s_dump; /* DW3_WAV=<file>: the mix as raw s16 stereo 44.1 kHz, to check by ear or by script */
static int s_failed;

void audio_vblank(int rate) {
    static int16_t buf[AUDIO_RATE / 25 * 2];
    static int frac;
    int base, frames, queued, target;

    if (s_dump == NULL && SDL_getenv("DW3_WAV") != NULL) {
        s_dump = fopen(SDL_getenv("DW3_WAV"), "wb");
    }
    if (s_stream == NULL && !s_failed) {
        SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, AUDIO_RATE};

        if (!SDL_InitSubSystem(SDL_INIT_AUDIO) ||
            (s_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL)) == NULL) {
            LOG("no sound: %s", SDL_GetError());
            s_failed = 1;
        } else {
            SDL_ResumeAudioStreamDevice(s_stream);
        }
    }
    frac += AUDIO_RATE;
    base = frac / rate;
    frac %= rate;
    frames = base;
    if (s_stream != NULL) {
        queued = SDL_GetAudioStreamQueued(s_stream) / 4;
        target = base * 3;
        if (queued > target * 4) {
            SDL_ClearAudioStream(s_stream); /* after a long stall: catch up */
            queued = 0;
        }
        frames += (target - queued) / 8;
        frames = frames < base / 2 ? base / 2 : frames > base * 2 ? base * 2 : frames;
    }
    spu_render(buf, frames);
    if (g_log & LOG_SND) {
        static int count;
        int n, env, active = 0, rel = 0;

        for (n = 0; n < SPU_VOICES; n++) {
            int st = spu_voice_state(n, &env);

            active += st == 2;
            rel += st == 1;
        }
        if (++count % 25 == 0) {
            LOG("voices: %d playing, %d releasing", active, rel);
        }
    }
    if (s_stream != NULL) {
        SDL_PutAudioStreamData(s_stream, buf, frames * 4);
    }
    if (s_dump != NULL) {
        fwrite(buf, 4, frames, s_dump);
        fflush(s_dump);
    }
}
