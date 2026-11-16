/* Japanese (LANGUAGE 0): the European disc has every text file in Japanese too, and its
   language screen (CNTY_SEL) the option, the flag and the highlight for it, which only a hidden
   second column reaches (options 5 and 6). The screen gets a sixth row, as the other five: the
   rows close up from 34 pixels apart to 31, and the label, NIHONGO, is made of the other labels'
   letters, as the game has no word for it. */
#include <stdint.h>
#include <string.h>

const void *native_cnty_bank(const void *bank);

/* The screen's sprite bank (CNTY_SEL_SPRITES): s32 offsets to its parts, its frame ids and
   each frame; a part is 7 s16 (u, v, w, h, clutX, clutY, mode), a frame its count, CLUT row
   and blending, then a part, x and y per piece */
#define BANK_MAX 4096
#define FRAMES_MAX 16
#define PANEL 2         /* the right panel: flags, their shadows and labels */
#define OPTION 4        /* the highlight of option 0; 5 options, then the hidden two */
#define JAPANESE 9      /* option 5's highlight, the sixth row's */
#define OLD_STEP 34
#define NEW_STEP 31
#define TOP 50          /* the first row's highlight is at 51, its flag at 52 */
#define FLAG_JP 17      /* the hidden column's flags: 17 Japan, 18 the USA */
#define SHADOW 19
#define LABEL_X 73
#define LABEL_DY 7

static const struct {
    int16_t u, v, w, h, advance;
} LETTERS[] = {
    /* N, I and H of ENGLISH, O of ITALIANO, G of ENGLISH */
    {9, 136, 9, 14, 9}, {39, 136, 6, 14, 6}, {54, 136, 10, 14, 9}, {212, 120, 10, 14, 9},
    {9, 136, 9, 14, 9}, {18, 136, 10, 14, 10}, {212, 120, 10, 14, 9},
};
#define N_LETTERS ((int)(sizeof LETTERS / sizeof LETTERS[0]))
#define LETTER_CLUT_X 32
#define LETTER_CLUT_Y 255

static int32_t rd(const uint8_t *p) {
    int32_t v;

    memcpy(&v, p, 4);
    return v;
}

static int16_t rd16(const uint8_t *p) {
    int16_t v;

    memcpy(&v, p, 2);
    return v;
}

/* The pieces that move with their row: the flags, their shadows and the labels */
static int on_a_row(int part) {
    return part == SHADOW || (part >= 5 && part <= 9) || (part >= 12 && part <= 16);
}

/* The bank with the sixth row, or the bank as it is when it isn't the one expected */
const void *native_cnty_bank(const void *bank) {
    static int32_t words[BANK_MAX / 4]; /* aligned for the s16 */
    uint8_t *out = (uint8_t *)words;
    const uint8_t *in = bank;
    int32_t parts, ids, frame[FRAMES_MAX];
    int nparts, nframes, i, at, f;

    if (in == NULL) {
        return bank;
    }
    parts = rd(in);
    ids = rd(in + 4);
    for (nframes = 0; nframes < FRAMES_MAX && in[ids + nframes] != 0xFF; nframes++) {
        if (in[ids + nframes] != nframes) {
            return bank;
        }
    }
    nparts = (ids - parts) / 14;
    if (nframes != 12 || parts != 8 + 4 * nframes || nparts != 23) {
        return bank;
    }
    for (f = 0; f < nframes; f++) {
        frame[f] = rd(in + 8 + 4 * f);
    }
    /* the header, the parts and the letters, the ids */
    at = 8 + 4 * nframes;
    memcpy(out + at, in + parts, nparts * 14);
    at += nparts * 14;
    for (i = 0; i < N_LETTERS; i++, at += 14) {
        const int16_t p[7] = {LETTERS[i].u, LETTERS[i].v, LETTERS[i].w, LETTERS[i].h, LETTER_CLUT_X, LETTER_CLUT_Y, 0};

        memcpy(out + at, p, 14);
    }
    memcpy(out, &(int32_t){8 + 4 * nframes}, 4);
    memcpy(out + 4, &at, 4);
    memcpy(out + at, in + ids, nframes + 1);
    at = (at + nframes + 1 + 3) & ~3;
    for (f = 0; f < nframes; f++) {
        const uint8_t *src = in + frame[f];
        int count = rd16(src), n = 0, k;
        int16_t *dst;

        if (at + 6 + (count + 9) * 6 > BANK_MAX) {
            return bank;
        }
        memcpy(out + 8 + 4 * f, &at, 4);
        dst = (int16_t *)(out + at);
        dst[1] = rd16(src + 2), dst[2] = rd16(src + 4);
        if (f == JAPANESE) { /* option 5: the highlight of the sixth row, as the others' */
            const int16_t *first = (const int16_t *)(in + frame[OPTION] + 6);

            for (k = 0; k < 2; k++, n++) {
                dst[3 + 3 * n] = first[3 * k];
                dst[4 + 3 * n] = first[3 * k + 1];
                dst[5 + 3 * n] = (int16_t)(first[3 * k + 2] + 5 * NEW_STEP);
            }
        } else {
            /* the panel's tiles first, then its rows, which move onto other tiles: on top */
            int pass;

            for (pass = 0; pass < (f == PANEL ? 2 : 1); pass++) {
                for (k = 0; k < count; k++) {
                    int16_t part = rd16(src + 6 + 6 * k), x = rd16(src + 8 + 6 * k), y = rd16(src + 10 + 6 * k);
                    int row = (y - TOP) / OLD_STEP, moves = (f == PANEL && on_a_row(part)) || (f >= OPTION && f < OPTION + 5);

                    if (f == PANEL && moves != pass) {
                        continue;
                    }
                    if (moves) {
                        y = (int16_t)(y - row * (OLD_STEP - NEW_STEP));
                    }
                    dst[3 + 3 * n] = part, dst[4 + 3 * n] = x, dst[5 + 3 * n] = y;
                    n++;
                }
            }
        }
        if (f == PANEL) { /* the sixth row: Japan's flag on its shadow, then NIHONGO */
            int y = TOP + 2 + 5 * NEW_STEP, x = LABEL_X;

            dst[3 + 3 * n] = SHADOW, dst[4 + 3 * n] = 22, dst[5 + 3 * n] = (int16_t)(y + 3), n++;
            dst[3 + 3 * n] = FLAG_JP, dst[4 + 3 * n] = 20, dst[5 + 3 * n] = (int16_t)y, n++;
            for (i = 0; i < N_LETTERS; i++, n++) {
                dst[3 + 3 * n] = (int16_t)(nparts + i), dst[4 + 3 * n] = (int16_t)x, dst[5 + 3 * n] = (int16_t)(y + LABEL_DY);
                x += LETTERS[i].advance;
            }
        }
        dst[0] = (int16_t)n;
        at += 6 + 6 * n;
    }
    return out;
}
