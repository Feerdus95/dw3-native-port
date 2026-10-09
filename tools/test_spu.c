/* The SPU's Gaussian interpolation and reverb, without the game. Run from native/:
   clang -m32 -I platform tools/test_spu.c -o test_spu.exe && ./test_spu.exe data/SLES_039.36 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../platform/spu.c"

void native_log(const char *fmt, ...) { (void)fmt; }

/* A one-shot sample: n blocks of a constant level, the last with the end flag */
static void put_sample(uint32_t addr, int blocks, int nibble) {
    uint8_t b[16];
    int i;

    for (i = 0; i < blocks; i++) {
        memset(b, (nibble & 15) * 0x11, sizeof b);
        b[0] = 0;                        /* shift 0, filter 0 */
        b[1] = i == blocks - 1 ? 1 : 0;  /* end, no repeat: the voice stops */
        spu_write(addr + i * 16, b, 16);
    }
}

static int32_t peak(const int16_t *s, int frames) {
    int32_t p = 0;
    int i;

    for (i = 0; i < frames * 2; i++) {
        p = abs(s[i]) > p ? abs(s[i]) : p;
    }
    return p;
}

int main(int argc, char **argv) {
    static int16_t out[44100 * 2];
    uint8_t exe[0x800 + 0x60000];
    uint32_t base, start;
    uint16_t regs[32];
    FILE *f = fopen(argc > 1 ? argv[1] : "data/SLES_039.36", "rb");
    int32_t dry_tail, wet_tail, wet_late;

    if (f == NULL || fread(exe, 1, sizeof exe, f) < 0x800) {
        puts("no exe");
        return 1;
    }
    fclose(f);
    memcpy(&base, exe + 0x18, 4);
    memcpy(&start, exe + 0x800 + (0x8005C810 - base) + 3 * 4, 4);
    memcpy(regs, exe + 0x800 + (0x8005C840 - base) + 3 * 68 + 4, sizeof regs);

    /* a constant through the Gaussian filter: 16384 in, 16315 out (the chip's weights
       sum to 0x7F80, not 0x8000, then the envelope and volumes take a step each) */
    put_sample(0x1000, 40, 4);
    spu_set_pitch(0, 0x1000);
    spu_set_volume(0, 0x3FFF, 0x3FFF);
    spu_key_on(0, 0x1000, 0x00FF, 0x0000); /* fastest attack, sustain at max */
    spu_render(out, 1000); /* the sample lasts 40 * 28 = 1120 frames */
    printf("constant: %d %d (16384 in)\n", out[500 * 2], out[600 * 2]);
    if (abs(out[500 * 2] - out[600 * 2]) > 4 || abs(out[500 * 2] - 16315) > 16) {
        puts("FAIL gauss");
        return 1;
    }

    /* a short burst, then the tail: silent dry, decaying wet */
    spu_set_reverb(regs, start * 8);
    spu_set_reverb_depth(0x7FFF * 64 / 127, 0x7FFF * 64 / 127);
    put_sample(0x2000, 4, 6);
    spu_voice_stop(0);
    spu_set_voice_reverb(0, 1);
    spu_set_reverb_on(0);
    spu_key_on(0, 0x2000, 0x00FF, 0x0000);
    spu_render(out, 4410);
    spu_render(out, 4410);
    dry_tail = peak(out, 4410);
    spu_set_reverb_on(1);
    spu_key_on(0, 0x2000, 0x00FF, 0x0000);
    spu_render(out, 4410);
    spu_render(out, 4410);
    wet_tail = peak(out, 4410);
    spu_render(out, 44100);
    spu_render(out, 44100);
    wet_late = peak(out, 44100);
    printf("tail 0.1-0.2 s: dry %d wet %d; 1.1-2.1 s wet %d\n", dry_tail, wet_tail, wet_late);
    if (dry_tail != 0 || wet_tail == 0 || wet_tail >= 32767 || wet_late >= wet_tail) {
        puts("FAIL reverb");
        return 1;
    }
    puts("OK");
    return 0;
}
