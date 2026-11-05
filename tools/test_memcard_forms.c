/* The memory card's extra-form areas end to end (platform/memcard.c + forms.c), the way the game reads and writes a save: a
   sector (128 bytes) a call. A scratch card in a temp folder; nothing of the player's is touched. Run from build/:
   i686-w64-mingw32-clang -I<SDL>/include -I../platform ../tools/test_memcard_forms.c ../platform/memcard.c ../platform/forms.c -L<SDL>/lib -lSDL3 -o test_memcard_forms.exe && ./test_memcard_forms.exe */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"
#include "../include/native_forms.h"

int g_log;
static char s_dir[600];
const char *data_dir(void) { return s_dir; }

long MemCardReadFile(long chan, const char *file, void *adrs, long ofs, long bytes);
long MemCardWriteFile(long chan, const char *file, void *adrs, long ofs, long bytes);
long MemCardCreateFile(long chan, const char *file, long blocks);
long MemCardFormat(long chan);
long MemCardSync(long mode, long *cmds, unsigned long *result);
void MemCardInit(long val);

#define FILE_NAME "BESLES-03936DMW3"
#define SLOT(k) (0x300 + (k) * 0x2700)

/* a section a sector at a time, as readSave / writeSave */
static void io(int write, int slot, uint8_t *buf) {
    long ofs;

    for (ofs = 0; ofs < 0x2700; ofs += 128) {
        long cmd, r;
        unsigned long res;
        if (write) MemCardWriteFile(0, FILE_NAME, buf + ofs, SLOT(slot) + ofs, 128);
        else MemCardReadFile(0, FILE_NAME, buf + ofs, SLOT(slot) + ofs, 128);
        r = MemCardSync(0, &cmd, &res);
        assert(r == 1 && res == 0);
    }
}

static void card_bytes(uint8_t *out) {
    char path[700];
    FILE *f;

    snprintf(path, sizeof path, "%s/../saves/card1.mcd", s_dir);
    f = fopen(path, "rb");
    assert(f && fread(out, 0x20000, 1, f) == 1);
    fclose(f);
}

static int exists(const char *rel) {
    char path[700];

    snprintf(path, sizeof path, "%s/../saves/%s", s_dir, rel);
    return SDL_GetPathInfo(path, NULL);
}

/* the file's 4 blocks in the card image: block b of the file's chain */
static int file_ofs(const uint8_t *card, long ofs) {
    int b;

    for (b = 1; b < 16; b++) {
        if (card[b * 0x80] == 0x51 && strncmp((const char *)card + b * 0x80 + 10, FILE_NAME, 20) == 0) {
            long k = ofs / 0x2000;
            while (k-- > 0) {
                int n = card[b * 0x80 + 8] | card[b * 0x80 + 9] << 8;
                b = n + 1;
            }
            return b * 0x2000 + (int)(ofs % 0x2000);
        }
    }
    assert(!"file not found");
    return 0;
}

int main(void) {
    static uint8_t data[3][0x2700], got[0x2700], card[0x20000], before[0x20000];
    NativeForm f, g;
    unsigned short ids[8];
    int i, k;
    const char *tmp = SDL_GetPrefPath("dw3test", "forms");

    snprintf(s_dir, sizeof s_dir, "%sdata", tmp);
    SDL_CreateDirectory(s_dir);
    {
        char p[700];
        snprintf(p, sizeof p, "%s/../saves/card1.mcd", s_dir);
        SDL_RemovePath(p);
        snprintf(p, sizeof p, "%s/../saves/card1.pre-dw3x.mcd", s_dir);
        SDL_RemovePath(p);
    }
    for (k = 0; k < 3; k++) for (i = 0; i < 0x2700; i++) data[k][i] = (uint8_t)(i * 7 + k * 31 + 1);

    /* a new card, a save file of 4 blocks, as the game makes it, its slots written the old way (no areas: tail zeroed) */
    MemCardInit(0);
    assert(MemCardCreateFile(0, FILE_NAME, 4) == 0);
    native_forms_reset();
    assert(!exists("card1.pre-dw3x.mcd"));
    io(1, 0, data[0]);
    assert(exists("card1.pre-dw3x.mcd")); /* the card as it was, kept before the first area */
    card_bytes(card);
    assert(memcmp(card + file_ofs(card, 0x7800), "DW3X", 4) == 0);                  /* slot 0 has an area (empty) */
    assert(card[file_ofs(card, 0x7800 + 680)] == 0);                                /* slot 1's isn't written */
    assert(!exists("card1.mcd.tmp"));                                               /* the rename left nothing behind */

    /* slot 1 saved with two forms; the game's section is as the game wrote it, slot 0's area untouched */
    f = (NativeForm){2, 471, 33, 5000, 0x1234};
    native_form_set(&f);
    f = (NativeForm){5, 471, 7, 70, 5};
    native_form_set(&f);
    card_bytes(before);
    io(1, 1, data[1]);
    card_bytes(card);
    for (i = 0; i < 0x2700; i++) assert(card[file_ofs(card, SLOT(1) + i)] == data[1][i]);
    for (i = 0; i < 680; i++) assert(card[file_ofs(card, 0x7800 + i)] == before[file_ofs(before, 0x7800 + i)]);
    for (i = 0; i < 0x2700; i++) assert(card[file_ofs(card, SLOT(0) + i)] == data[0][i]);

    /* loading slot 1 gives those forms, loading slot 2 (never saved: no area) gives none, loading slot 0 none */
    native_forms_reset();
    io(0, 1, got);
    assert(memcmp(got, data[1], sizeof got) == 0);
    assert(!native_form_get(2, 471, &g)); /* staged only: the game's slot isn't in GAME yet */
    native_forms_commit();
    assert(native_form_get(2, 471, &g) && g.level == 33 && g.exp == 5000 && g.skills == 0x1234);
    assert(native_form_get(5, 471, &g) && g.level == 7);
    io(0, 2, got);
    native_forms_commit();
    assert(native_form_list(2, ids, 8) == 0);
    io(0, 0, got);
    native_forms_commit();
    assert(native_form_list(5, ids, 8) == 0);

    /* the card is read again from the disk: the same */
    native_forms_reset();
    MemCardInit(0);
    io(0, 1, got);
    native_forms_commit();
    assert(native_form_get(2, 471, &g) && g.exp == 5000);

    /* slot 1 saved since by something that doesn't know the areas (the original game, an emulator, an imported card): its forms
       belong to another save and aren't loaded, and the card is kept as it was before they are replaced */
    {
        static uint8_t kept[0x20000], changed[0x20000];
        char p[700];
        FILE *fp;

        card_bytes(kept);
        memcpy(changed, kept, sizeof changed);
        changed[file_ofs(changed, SLOT(1) + 10)] ^= 0x5A;
        snprintf(p, sizeof p, "%s/../saves/card1.mcd", s_dir);
        fp = fopen(p, "wb");
        assert(fp && fwrite(changed, sizeof changed, 1, fp) == 1);
        fclose(fp);
        MemCardInit(0);
        native_forms_reset();
        assert(!exists("card1.pre-stale.mcd"));
        io(0, 1, got);
        assert(exists("card1.pre-stale.mcd"));
        native_forms_commit();
        assert(native_form_list(2, ids, 8) == 0 && native_form_list(5, ids, 8) == 0);
        snprintf(p, sizeof p, "%s/../saves/card1.pre-stale.mcd", s_dir);
        SDL_RemovePath(p);
        snprintf(p, sizeof p, "%s/../saves/card1.mcd", s_dir);
        fp = fopen(p, "wb");
        assert(fp && fwrite(kept, sizeof kept, 1, fp) == 1);
        fclose(fp);
        MemCardInit(0); /* the card as it was */
        native_forms_reset();
        io(0, 1, got);
        native_forms_commit();
        assert(native_form_get(2, 471, &g) && g.exp == 5000);
    }

    /* a card from before the areas (tail 0xFF, as a PSX's), read: the card is kept as it was; saved: the game's bytes as written */
    {
        long ofs;
        uint8_t ff[128];
        char p[700];

        memset(ff, 0xFF, sizeof ff);
        for (ofs = 0x7800; ofs < 0x8000; ofs += 128) MemCardWriteFile(0, FILE_NAME, ff, ofs, 128); /* a tail write isn't a slot's: no hook */
        snprintf(p, sizeof p, "%s/../saves/card1.pre-dw3x.mcd", s_dir);
        SDL_RemovePath(p);
        native_forms_reset();
        io(0, 1, got);
        assert(exists("card1.pre-dw3x.mcd"));
        native_forms_commit();
        assert(native_form_list(2, ids, 8) == 0);
        f = (NativeForm){1, 471, 2, 3, 4};
        native_form_set(&f);
        io(1, 1, data[1]);
        card_bytes(card);
        assert(memcmp(card + file_ofs(card, 0x7800 + 680), "DW3X", 4) == 0);
    }

    /* an area that's not ours is left as it is, the save goes on */
    {
        uint8_t other[128];
        uint8_t t[128];

        memset(other, 0xFF, sizeof other);
        memcpy(other, "XYZZY", 5);
        MemCardWriteFile(0, FILE_NAME, other, 0x7800 + 1408, 128); /* inside slot 2's area (1360-2039) */
        io(1, 2, data[2]);
        card_bytes(card);
        assert(memcmp(card + file_ofs(card, 0x7800 + 1408), "XYZZY", 5) == 0);
        assert(memcmp(card + file_ofs(card, 0x7800 + 1360), "DW3X", 4) != 0); /* no area was written over it */
        for (i = 0; i < 0x2700; i++) assert(card[file_ofs(card, SLOT(2) + i)] == data[2][i]);
        (void)t;
    }

    printf("test_memcard_forms: ok\n");
    return 0;
}
