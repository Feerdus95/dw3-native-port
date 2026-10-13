/* Extra starter packs: DRAGON, TACTICAL and MYSTIC after the game's three
   (STPLNMET's choice, setParty, the lobby's scene in WSTAG212: patches/). This
   file has their partners, their texts in each language and the lobby
   Veemon's celebration, which its file lacks. */
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "packs_text.h"

int g_extra_packs = 1; /* extra_packs = 0: the choice has the game's three */

/* The partners (0 Kotemon, 1 Kumamon, 2 Monmon, 3 Agumon, 4 Veemon,
   5 Guilmon, 6 Renamon, 7 Patamon) of packs 3, 4 and 5 */
const int NATIVE_EXTRA_PACKS[3][3] = {{3, 5, 4}, {0, 2, 7}, {1, 6, 4}};

/* The texts' file in the game's language (LANGUAGE: 2 English to 6 Spanish) */
const void *native_packs_text(int language) {
    static const uint32_t *const BY_LANGUAGE[] = {PACKS_TEXT_ENG, PACKS_TEXT_ENG, PACKS_TEXT_ENG, PACKS_TEXT_FRA,
                                                  PACKS_TEXT_ITA, PACKS_TEXT_GER, PACKS_TEXT_SPN};

    return BY_LANGUAGE[language >= 0 && language < 7 ? language : 2];
}

static uint32_t rd32(const uint8_t *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

/* The lobby's NPC Veemon (P159NVMO) with animation 64, the celebration the
   other partners play after the greetings: two drawings of the partner
   Veemon's file (P007VMON's 0x47, ears up, and 0x59, the grin) and a jump in
   the timing of Agumon's, in the padding its file has. NULL when it can't */
#define NPC_VEEMON 0x2FA
#define PARTNER_VEEMON 0x18C
static uint8_t *celebrating_veemon(long *size) {
    /* each frame: its time, its drawing (the file's sub; 0x10 standing) and its offset */
    static const int FRAMES[7][4] = {{3, 0x10, -16, -27}, {4, -1, -16, -31}, {3, -1, -16, -35}, {37, -2, -16, -27},
                                     {4, -1, -16, -30}, {5, -1, -16, -27}, {4, 0x10, -16, -27}};
    uint8_t *npc, *par, *out = NULL;
    long nsize = native_read_raw(NPC_VEEMON, &npc), psize = native_read_raw(PARTNER_VEEMON, &par);
    uint32_t n, pn, end, at, k, s47, s59, fl, sub[64 + 4][2];
    int i, j;

    if (nsize < 8 || psize < 8 || (n = rd32(npc) / 4) < 2 || n > 64 || (pn = rd32(par) / 4) <= 0x5A) {
        goto done;
    }
    /* the subs but the last, the animation table, whose end is its 0 */
    for (k = 0; k < n; k++) {
        sub[k][0] = rd32(npc + 4 * k);
        sub[k][1] = (k + 1 < n ? rd32(npc + 4 * (k + 1)) : 0) - sub[k][0];
    }
    for (end = sub[n - 1][0]; end + 2 <= (uint32_t)nsize && (npc[end] | npc[end + 1]) != 0; end += 12) {
    }
    s47 = n, s59 = n + 1, fl = n + 2;
    out = calloc(1, nsize);
    at = 4 * (n + 3);
    for (k = 0; k + 1 < n; k++) { /* the subs as they are */
        wr32(out + 4 * k, at);
        memcpy(out + at, npc + sub[k][0], sub[k][1]);
        at += sub[k][1];
    }
    wr32(out + 4 * (n - 1), at); /* the table, with 64 for the five directions */
    memcpy(out + at, npc + sub[n - 1][0], end - sub[n - 1][0]);
    at += end - sub[n - 1][0];
    out[at] = 64, out[at + 1] = 0;
    for (i = 0; i < 5; i++) {
        out[at + 2 + 2 * i] = (uint8_t)fl, out[at + 3 + 2 * i] = (uint8_t)(fl >> 8);
    }
    at += 12 + 2;
    at = (at + 3) & ~3u;
    for (i = 0; i < 2; i++) { /* the two drawings */
        uint32_t s = i ? 0x59 : 0x47, from = rd32(par + 4 * s), to = rd32(par + 4 * (s + 1));

        wr32(out + 4 * (n + i), at);
        if (at + (to - from) > (uint32_t)nsize || to > (uint32_t)psize || to < from) {
            free(out), out = NULL;
            goto done;
        }
        memcpy(out + at, par + from, to - from);
        at += to - from;
    }
    wr32(out + 4 * fl, at); /* the frames */
    if (at + sizeof FRAMES + 4 > (uint32_t)nsize) {
        free(out), out = NULL;
        goto done;
    }
    for (i = 0; i < 7; i++) {
        for (j = 0; j < 4; j++) {
            int v = j == 1 && FRAMES[i][1] < 0 ? (int)(FRAMES[i][1] == -1 ? s47 : s59) : FRAMES[i][j];

            wr32(out + at, (uint32_t)v);
            at += 4;
        }
    }
    wr32(out + at, 0xFFFFFFFF);
    *size = nsize;
done:
    free(npc);
    free(par);
    if (out == NULL) {
        LOG("packs: the lobby's Veemon can't celebrate (file %d)", NPC_VEEMON);
    }
    return out;
}

/* A file the packs make anew, whole, or NULL for the game's (files.c) */
const uint8_t *packs_file(int fid, long *size) {
    static uint8_t *veemon;
    static long veemon_size;
    static int tried;

    if (fid != NPC_VEEMON || !g_extra_packs) {
        return NULL;
    }
    if (!tried) {
        tried = 1;
        veemon = celebrating_veemon(&veemon_size);
    }
    *size = veemon_size;
    return veemon;
}
