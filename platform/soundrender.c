/* dw2003 --render-sounds DIR [28,0x2A,...]: the game's own battle sounds (indexes of FIGHTSTG_battleSounds, the
   hit sound a technique's scriptSound is), each played by the game's sound code and the SPU's mix written to
   DIR/<index>.wav (44.1 kHz stereo 16 bit), for the model viewer's "Original" button. Without a list, every
   sound (the launcher renders them all once, into data/sounds, after the disc is read). The game boots with its window
   hidden and, right after it starts its sound (src/main/system/main.c, patched), plays them one after another, as
   fast as it can mix: no audio device, no waiting. The program ends there. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"
#include "sounds_version.h"

extern int NTSC_MODE;

int g_render_sounds;
static char s_dir[1024];
static int s_list[256], s_count;
static int16_t *s_buf;
static int s_frames, s_seen, s_quiet, s_all;

#define CAPTURE_SECONDS 8
#define QUIET_LEVEL 16 /* a sample above this is sound */

/* "28,0x2A": the indexes to render, every one if the list is empty; 0 when there are none */
int native_render_parse(const char *dir, const char *list) {
    char *end;

    SDL_strlcpy(s_dir, dir, sizeof s_dir);
    SDL_CreateDirectory(dir);
    s_count = 0;
    for (const char *p = list; *p && s_count < 256;) {
        long v = strtol(p, &end, 0);

        if (end == p) {
            break;
        }
        if (v >= 0 && v < 0x5A) { /* 0x5A and up are the music and its stop */
            s_list[s_count++] = (int)v;
        }
        p = end + strspn(end, ", ");
    }
    s_all = *list == 0;
    for (int i = 1; *list == 0 && i < 0x5A; i++) {
        s_list[s_count++] = i;
    }
    g_render_sounds = s_count > 0;
    return s_count;
}

int native_render_next(int i) {
    return i < s_count ? s_list[i] : -1;
}

void native_render_begin(int index) {
    free(s_buf);
    s_buf = native_must(calloc((size_t)AUDIO_RATE * CAPTURE_SECONDS, 4), "the sound capture");
    s_frames = s_seen = s_quiet = 0;
}

/* One vsync of the SPU's mix (the game ticks its sequences once a vsync); 1 while the sound goes on */
int native_render_frame(void) {
    int n = AUDIO_RATE / (NTSC_MODE ? 60 : 50), i;
    int16_t *out = s_buf + s_frames * 2;

    if (s_frames + n > AUDIO_RATE * CAPTURE_SECONDS) {
        return 0;
    }
    spu_render(out, n);
    s_frames += n;
    for (i = 0; i < n * 2 && abs(out[i]) <= QUIET_LEVEL; i++) {
    }
    if (i < n * 2) {
        s_seen = 1, s_quiet = 0;
    } else {
        s_quiet++;
    }
    /* a sound that ended (a third of a second quiet), or none at all in a second */
    return !(s_seen ? s_quiet * 3 >= (NTSC_MODE ? 60 : 50) : s_frames >= AUDIO_RATE);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

/* DIR/<INDEX>.wav; a sound that made none is left unwritten, so the viewer knows */
void native_render_end(int index) {
    char path[1200];
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 16, 0,
                     'd', 'a', 't', 'a'};
    int frames = s_frames;
    FILE *f;

    if (!s_seen) {
        LOG("render-sounds: sound %d made none", index);
        return;
    }
    frames -= (s_quiet > 1 ? s_quiet - 1 : 0) * (AUDIO_RATE / (NTSC_MODE ? 60 : 50)); /* the silence at its end, but one vsync */
    put32(h + 4, 36 + (uint32_t)frames * 4);
    put32(h + 24, AUDIO_RATE);
    put32(h + 28, AUDIO_RATE * 4);
    put32(h + 40, (uint32_t)frames * 4);
    SDL_snprintf(path, sizeof path, "%s/%d.wav", s_dir, index);
    if ((f = fopen(path, "wb")) == NULL) {
        LOG("render-sounds: can't write %s", path);
        return;
    }
    fwrite(h, 1, sizeof h, f);
    fwrite(s_buf, 4, (size_t)frames, f);
    fclose(f);
}

void native_render_done(void) {
    free(s_buf);
    if (s_all) { /* every sound was tried: the launcher need not render again until SOUNDS_VERSION changes */
        char path[1200], text[16];

        SDL_snprintf(path, sizeof path, "%s/version.txt", s_dir);
        SDL_snprintf(text, sizeof text, "%d\n", SOUNDS_VERSION);
        SDL_SaveFile(path, text, strlen(text));
    }
    exit(0);
}
