/* Every frame of every movie through our MDEC's bitstream decoder, without the
   game: each frame must decode to at least (w/16)*(h/16)*6 whole blocks, no bad
   code, and every audio sector must decode. Run from native/:
   clang -m32 -I platform tools/test_movies.c -o test_movies.exe && ./test_movies.exe data */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../platform/movie.c"

static const char *s_data;
static int s_bad_vlc, s_not_v2, s_xa_frames;

const char *data_dir(void) { return s_data; }
void native_wait_vblank(void) {}
void spu_cd_push(const int16_t *lr, int frames) { (void)lr; s_xa_frames += frames; }
void spu_cd_clear(void) {}
void log_once(const char *what) {
    if (strstr(what, "bad VLC")) s_bad_vlc++;
    if (strstr(what, "version 2")) s_not_v2++;
}

/* The blocks a frame's run-level halfwords hold before its zero padding */
static int real_blocks(const uint32_t *buf) {
    const uint16_t *p = (const uint16_t *)(buf + 1);
    int n = (int)buf[0], i = 0, blocks = 0;

    while (i < n) {
        if (p[i] == 0xFE00) { /* a padding block: DC 0xFE00, then its end */
            i += 2;
            continue;
        }
        blocks++;
        i++;
        while (i < n && p[i] != 0xFE00) {
            i++;
        }
        i++;
    }
    return blocks;
}

int main(int argc, char **argv) {
    static const int FIDS[] = {2026, 2027, 2028, 2029, 2030, 2031, 2032, 2034, 2035, 2036, 2070, 2071, 2213, 2380};
    static uint32_t rle[MAX_FRAME];
    size_t m;
    int fails = 0;

    s_data = argc > 1 ? argv[1] : "data";
    for (m = 0; m < sizeof FIDS / sizeof FIDS[0]; m++) {
        int frames = 0, short_frames = 0, expect = 0;

        s_bad_vlc = s_not_v2 = s_xa_frames = 0;
        movie_stream_start(FIDS[m] * SPAN);
        if (s_file == NULL) {
            printf("%04d: missing\n", FIDS[m]);
            fails++;
            continue;
        }
        while (read_sector()) {
            while (s_ready > 0) {
                Frame *f = &s_queue[s_head];

                expect = (f->header.width / 16) * (f->header.height / 16) * 6;
                DecDCTvlc2(f->data, rle, NULL);
                if (real_blocks(rle) < expect) { /* the header rounds the size up: a tail of leftover bits may follow */
                    short_frames++;
                }
                frames++;
                StFreeRing(f->data);
            }
        }
        printf("%04d: %4d frames (%d blocks each), %d short, %d bad codes, %d not v2, %.1f s of audio\n", FIDS[m],
               frames, expect, short_frames, s_bad_vlc, s_not_v2, s_xa_frames / 44100.0);
        if (frames == 0 || short_frames || s_bad_vlc || s_not_v2) {
            fails++;
        }
        movie_stream_stop();
    }
    puts(fails ? "FAIL" : "OK");
    return fails != 0;
}
