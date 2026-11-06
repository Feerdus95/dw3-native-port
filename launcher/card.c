/* A memory card image (128 KB): this game's file in it, its three slots, and whether each slot's extra Digimon (the port's DW3X
   area, platform/forms.c) still belong to the save they sit beside. Read only. */
#include "launcher.h"

enum {
    BLOCK = 0x2000, FRAME = 128, SLOT_START = 0x300, SLOT_SIZE = 0x2700, SAVE_BYTES = 0x26C4, SUM_BYTES = 9920, TAIL = 0x7800,
    AREA = 680, G_TIME = 0x4C
};

static Uint32 crc32(const Uint8 *p, int n)
{
    Uint32 c = 0xFFFFFFFFu;
    for (int i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) {
            c = c >> 1 ^ (0xEDB88320u & -(c & 1));
        }
    }
    return ~c;
}

static int s16(const Uint8 *p) { return (Sint16)(p[0] | p[1] << 8); }
static Uint32 u32(const Uint8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (Uint32)p[3] << 24; }

/* the file's first block in the directory, or 0 */
static int first_block(const Uint8 *d)
{
    for (int f = 1; f < 16; f++) {
        if ((d[f * FRAME] & 0xF0) == 0x50 && !SDL_strncmp((const char *)d + f * FRAME + 10, "BESLES-03936DMW3-EUR", 20)) {
            return f;
        }
    }
    return 0;
}

/* N bytes of the file from OFS, following its blocks */
static bool file_read(const Uint8 *d, int block, int ofs, Uint8 *out, int n)
{
    while (n > 0) {
        for (; block && ofs >= BLOCK; ofs -= BLOCK) {
            const int next = d[block * FRAME + 8] | d[block * FRAME + 9] << 8;
            block = next == 0xFFFF ? 0 : next + 1;
        }
        const int part = SDL_min(n, BLOCK - ofs);
        if (!block) {
            return false;
        }
        SDL_memcpy(out, d + block * BLOCK + ofs, part);
        out += part, ofs += part, n -= part;
    }
    return true;
}

/* the extra Digimon area of a slot against the save it sits beside (version 2 holds a crc of the save) */
static void forms_of(const Uint8 *a, const Uint8 *g, SlotInfo *s)
{
    int header = 16, i = 0;
    s->forms = F_NONE;
    s->count = 0;
    for (; i < AREA && a[i] == a[0]; i++) {}
    if (i == AREA && (a[0] == 0 || a[0] == 0xFF)) {
        return;
    }
    if (SDL_memcmp(a, "DW3X", 4) != 0) {
        s->forms = F_BAD;
    } else if (a[4] > 2) {
        s->forms = F_NEWER;
    } else {
        const int n = a[6] | a[7] << 8;
        header = a[4] == 1 ? 12 : 16;
        s->count = n;
        s->forms = a[4] == 0 || n > (AREA - header) / 10 || crc32(a + header, n * 10) != u32(a + 8) ? F_BAD
                 : a[4] >= 2 && u32(a + 12) != crc32(g, SAVE_BYTES) ? F_STALE : F_READY;
    }
}

/* a memory card image with this game's file: its slots; false when it is anything else */
bool card_parse(const Uint8 *d, size_t size, Card *c)
{
    static Uint8 g[SLOT_SIZE];
    const int block = size == CARD_SIZE && d[0] == 'M' && d[1] == 'C' ? first_block(d) : 0;
    bool any = false;
    if (!block) {
        return false;
    }
    for (int k = 0; k < SAVE_SLOTS; k++) {
        Uint8 area[AREA], x = 0;
        SlotInfo *s = &c->slot[k];
        *s = (SlotInfo){0};
        if (!file_read(d, block, SLOT_START + k * SLOT_SIZE, g, SLOT_SIZE) || !file_read(d, block, TAIL + k * AREA, area, AREA)) {
            return false;
        }
        for (int i = 4; i < 4 + SUM_BYTES; i++) {
            x ^= g[i];
        }
        bool zero = true;
        for (int i = 0; i < FRAME; i++) {
            zero = zero && g[i] == 0;
        }
        s->state = zero ? SL_EMPTY : x == g[0] ? SL_OK : SL_DAMAGED;
        s->hours = s16(g + G_TIME), s->minutes = s16(g + G_TIME + 2);
        if (s->state == SL_OK) {
            forms_of(area, g, s);
        }
        any = any || s->state == SL_OK;
    }
    return any;
}

/* a card as the game makes it: its file of 4 blocks, slot 1 (the second) saved, with an area of version 2 and one record */
void card_make(Uint8 *d)
{
    SDL_memset(d, 0, CARD_SIZE);
    d[0] = 'M', d[1] = 'C';
    for (int f = 1; f <= 4; f++) {
        d[f * FRAME] = f == 1 ? 0x51 : 0x52;
        d[f * FRAME + 8] = f == 4 ? 0xFF : f, d[f * FRAME + 9] = f == 4 ? 0xFF : 0;
    }
    SDL_memcpy(d + FRAME + 10, "BESLES-03936DMW3-EUR", 20);
    Uint8 *g = d + BLOCK + SLOT_START + SLOT_SIZE;
    g[8] = 1;
    for (int i = 4; i < 4 + SUM_BYTES; i++) {
        g[0] ^= g[i];
    }
    Uint8 *a = d + BLOCK + TAIL + AREA;
    SDL_memset(a, 0xFF, AREA);
    SDL_memcpy(a, "DW3X", 4);
    a[4] = 2, a[5] = 0, a[6] = 1, a[7] = 0;
    SDL_memset(a + 16, 7, 10);
    const Uint32 records = crc32(a + 16, 10), save = crc32(g, SAVE_BYTES);
    SDL_memcpy(a + 8, &records, 4), SDL_memcpy(a + 12, &save, 4);
}

/* the card made above, then a slot saved by someone else, an area of version 1 and one of a newer version */
int card_selftest(void)
{
    static Uint8 d[CARD_SIZE];
    Card c;
    int bad = 0;
    card_make(d);
    Uint8 *g = d + BLOCK + SLOT_START + SLOT_SIZE, *a = d + BLOCK + TAIL + AREA;
    bad += !card_parse(d, sizeof d, &c) || c.slot[0].state != SL_EMPTY || c.slot[1].state != SL_OK || c.slot[1].forms != F_READY || c.slot[1].count != 1;
    d[FRAME] = 0; /* no file of the game's */
    bad += card_parse(d, sizeof d, &c);
    d[FRAME] = 0x51;
    g[SAVE_BYTES - 1] ^= 1, g[0] ^= 1; /* saved since by someone else (its checksum kept): the area's save is no longer the slot's */
    bad += !card_parse(d, sizeof d, &c) || c.slot[1].forms != F_STALE;
    a[4] = 1; /* version 1 has nothing to compare */
    SDL_memmove(a + 12, a + 16, 10);
    SDL_memset(a + 22, 0xFF, AREA - 22);
    bad += !card_parse(d, sizeof d, &c) || c.slot[1].forms != F_READY;
    a[4] = 3;
    bad += !card_parse(d, sizeof d, &c) || c.slot[1].forms != F_NEWER;
    return bad;
}
