/* libmcrd over a raw 128 KB card image (saves/card1.mcd, next to data/, the
   same .mcd format as emulators). Commands finish at once: the one result
   waits for MemCardSync. Only slot 1 (chan 0x00) has a card. */
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"
#include "../include/native_forms.h"

#define CARD_SIZE 0x20000
#define BLOCK 0x2000
#define FRAME 0x80

enum { ERR_NONE, ERR_NO_CARD, ERR_INVALID, ERR_NEW, ERR_UNFORMATTED, ERR_NO_FILE, ERR_EXISTS, ERR_FULL };

/* the kernel's DIRENTRY (the game's CardDirEntry) */
typedef struct DirEntry {
    char name[20];
    int32_t attr, size;
    struct DirEntry *next;
    int32_t head;
    char system[4];
} DirEntry;

static uint8_t s_card[CARD_SIZE];
static char s_path[1100];
SAVED static long s_cmd, s_result;
SAVED static int s_pending;

/* The card image, written whole to a file beside and renamed over the card: a crash
   or a full disk mid-write leaves the card as it was, not half a save */
static void save_card(void) {
    char tmp[1110];
    FILE *f;

    snprintf(tmp, sizeof tmp, "%s.tmp", s_path);
    f = fopen(tmp, "wb");
    if (f != NULL && fwrite(s_card, CARD_SIZE, 1, f) == 1 && fclose(f) == 0 && SDL_RenamePath(tmp, s_path)) {
        return;
    }
    LOG("memcard: can't write %s", s_path);
    if (f != NULL) {
        fclose(f);
    }
}

static uint8_t *dir(int block) {
    return &s_card[block * FRAME];
}

static void dir_checksum(uint8_t *d) {
    uint8_t x = 0;
    int i;

    for (i = 0; i < FRAME - 1; i++) {
        x ^= d[i];
    }
    d[FRAME - 1] = x;
}

static int formatted(void) {
    return s_card[0] == 'M' && s_card[1] == 'C';
}

/* the first block of `name`, or 0 */
static int find_file(const char *name) {
    int b;

    for (b = 1; b < 16; b++) {
        if (dir(b)[0] == 0x51 && strncmp((char *)dir(b) + 10, name, 20) == 0) {
            return b;
        }
    }
    return 0;
}

static int next_block(int b) {
    uint16_t n = (uint16_t)(dir(b)[8] | dir(b)[9] << 8);

    return n == 0xFFFF ? 0 : n + 1;
}

/* copies `bytes` at `ofs` of a file to or from `buf`, the card in memory only */
static long file_copy(const char *name, uint8_t *buf, long ofs, long bytes, int write) {
    int b = find_file(name);

    if (!formatted()) {
        return ERR_UNFORMATTED;
    }
    if (b == 0) {
        return ERR_NO_FILE;
    }
    while (bytes > 0) {
        long n;

        while (b != 0 && ofs >= BLOCK) {
            ofs -= BLOCK;
            b = next_block(b);
        }
        if (b == 0) {
            return ERR_INVALID;
        }
        n = BLOCK - ofs < bytes ? BLOCK - ofs : bytes;
        if (write) {
            memcpy(&s_card[b * BLOCK + ofs], buf, n);
        } else {
            memcpy(buf, &s_card[b * BLOCK + ofs], n);
        }
        buf += n;
        ofs += n;
        bytes -= n;
    }
    return ERR_NONE;
}

/* the same, and the card written when it was a write */
static long file_io(const char *name, uint8_t *buf, long ofs, long bytes, int write) {
    long r = file_copy(name, buf, ofs, bytes, write);

    if (write && r == ERR_NONE) {
        save_card();
    }
    return r;
}

/* --- the extra forms (forms.c): the save file is 4 blocks, a summary and three slots' data sections, and the
   0x800 bytes after them are never touched by the game, which is where the forms' areas are --- */
#define TAIL_OFS 0x7800
#define TAIL_SIZE 0x800
#define SLOT_OFS 0x300 /* the first slot's data section: the header, 3 icons and the summary come first */
#define SLOT_SIZE 0x2700
#define SLOTS 3

static int tail_io(const char *file, uint8_t *tail, int write) {
    return file_copy(file, tail, TAIL_OFS, TAIL_SIZE, write) == ERR_NONE;
}

/* a copy of the card as it was before the first thing this port's areas did to it, kept once */
static void backup_once(void) {
    char bk[1110];
    FILE *f;

    snprintf(bk, sizeof bk, "%s/../saves/card1.pre-dw3x.mcd", data_dir());
    if (SDL_GetPathInfo(bk, NULL)) {
        return;
    }
    f = fopen(bk, "wb");
    if (f != NULL) {
        if (fwrite(s_card, CARD_SIZE, 1, f) == 1) {
            LOG("memcard: the card as it was is kept as %s", bk);
        }
        fclose(f);
    }
}

/* the slot whose data section starts at `ofs`, or -1 */
static int slot_at(long ofs) {
    return ofs >= SLOT_OFS && (ofs - SLOT_OFS) % SLOT_SIZE == 0 && (ofs - SLOT_OFS) / SLOT_SIZE < SLOTS ? (int)((ofs - SLOT_OFS) / SLOT_SIZE) : -1;
}

/* a read of a slot's data section begins: its area is staged for the load (the game's once the slot is in GAME) */
static void forms_reading(const char *file, int slot) {
    uint8_t tail[TAIL_SIZE];

    if (!tail_io(file, tail, 0)) {
        return;
    }
    if (native_forms_stage(tail, slot) != NATIVE_AREA_OK) {
        backup_once(); /* a save from before the areas, or one whose area isn't ours: the card as the player had it */
    }
}

/* a write of a slot's data section begins */
static void forms_writing(const char *file, int slot) {
    uint8_t tail[TAIL_SIZE];

    if (tail_io(file, tail, 0) && native_forms_area_state(tail, slot) != NATIVE_AREA_OK) {
        backup_once();
    }
}

/* a slot's data section is written: the store goes to its area */
static void forms_written(const char *file, int slot) {
    uint8_t tail[TAIL_SIZE];

    if (!tail_io(file, tail, 0)) {
        return;
    }
    if (native_forms_write(tail, slot) != 0) {
        LOG("memcard: slot %d's area holds data this port doesn't own, left as it is", slot + 1);
        return;
    }
    tail_io(file, tail, 1);
}

static long finish(long cmd, long chan, long result) {
    LOGC(LOG_MC, "mc: command %ld on port %ld: %ld", cmd, chan, result);
    s_cmd = cmd;
    s_result = chan == 0 ? result : ERR_NO_CARD;
    s_pending = 1;
    return 1;
}

long MemCardFormat(long chan);

void MemCardInit(long val) {
    FILE *f;

    (void)val;
    snprintf(s_path, sizeof s_path, "%s/../saves", data_dir());
    SDL_CreateDirectory(s_path); /* a first run has none */
    snprintf(s_path, sizeof s_path, "%s/../saves/card1.mcd", data_dir());
    f = fopen(s_path, "rb");
    if (f == NULL) {
        /* a new player: a formatted card, so the first save doesn't ask to format */
        LOG("memcard: a new card at %s", s_path);
        MemCardFormat(0);
    } else {
        if (fread(s_card, CARD_SIZE, 1, f) != 1) { /* left as it is: it may be the player's */
            LOG("memcard: %s isn't a card image, a blank one is used", s_path);
            memset(s_card, 0, CARD_SIZE);
        }
        fclose(f);
    }
}

void MemCardStart(void) {}

/* 1 once a command finished (its McFunc* and McErr*), -1 when none is pending */
long MemCardSync(long mode, long *cmds, unsigned long *result) {
    (void)mode;
    if (!s_pending) {
        return -1;
    }
    s_pending = 0;
    *cmds = s_cmd;
    *result = (unsigned long)s_result;
    return 1;
}

long MemCardExist(long chan) {
    return finish(1, chan, ERR_NONE);
}

long MemCardAccept(long chan) {
    return finish(2, chan, formatted() ? ERR_NONE : ERR_UNFORMATTED);
}

long MemCardReadFile(long chan, const char *file, void *adrs, long ofs, long bytes) {
    long r = chan == 0 ? file_io(file, adrs, ofs, bytes, 0) : ERR_NO_CARD;

    if (r == ERR_NONE && slot_at(ofs) >= 0) {
        forms_reading(file, slot_at(ofs));
    }
    return finish(3, chan, r);
}

long MemCardWriteFile(long chan, const char *file, void *adrs, long ofs, long bytes) {
    long r;

    if (chan == 0 && slot_at(ofs) >= 0) {
        forms_writing(file, slot_at(ofs));
    }
    r = chan == 0 ? file_copy(file, adrs, ofs, bytes, 1) : ERR_NO_CARD;
    if (r == ERR_NONE && ofs + bytes >= SLOT_OFS + SLOT_SIZE && (ofs + bytes - SLOT_OFS) % SLOT_SIZE == 0 && (ofs + bytes - SLOT_OFS) / SLOT_SIZE <= SLOTS) {
        forms_written(file, (int)((ofs + bytes - SLOT_OFS) / SLOT_SIZE) - 1); /* the slot's last sector */
    }
    if (r == ERR_NONE) {
        save_card();
    }
    return finish(4, chan, r);
}

/* the synchronous ones return their McErr* themselves */
long MemCardGetDirentry(long chan, char *name, DirEntry *d, long *files, long ofs, long max) {
    long n = 0;
    int b;

    (void)name; /* the game only lists "*" */
    *files = 0;
    if (chan != 0) {
        return ERR_NO_CARD;
    }
    if (!formatted()) {
        return ERR_UNFORMATTED;
    }
    for (b = 1; b < 16; b++) {
        if (dir(b)[0] != 0x51 || n - ofs >= max) {
            continue;
        }
        if (n++ < ofs) {
            continue;
        }
        memset(d, 0, sizeof *d);
        memcpy(d->name, dir(b) + 10, 20);
        d->name[19] = 0;
        d->attr = 0x51;
        d->size = (int32_t)(dir(b)[4] | dir(b)[5] << 8 | dir(b)[6] << 16 | (uint32_t)dir(b)[7] << 24);
        d->head = b;
        d++;
    }
    *files = n - ofs;
    return ERR_NONE;
}

long MemCardCreateFile(long chan, const char *file, long blocks) {
    int free[15], nfree = 0, b, i;

    if (chan != 0) {
        return ERR_NO_CARD;
    }
    if (!formatted()) {
        return ERR_UNFORMATTED;
    }
    if (find_file(file) != 0) {
        return ERR_EXISTS;
    }
    for (b = 1; b < 16; b++) {
        if ((dir(b)[0] & 0xF0) == 0xA0) {
            free[nfree++] = b;
        }
    }
    if (blocks < 1 || nfree < blocks) {
        return ERR_FULL;
    }
    for (i = 0; i < blocks; i++) {
        uint8_t *d = dir(free[i]);
        uint16_t next = i + 1 < blocks ? (uint16_t)(free[i + 1] - 1) : 0xFFFF;

        memset(d, 0, FRAME);
        d[0] = i == 0 ? 0x51 : i + 1 < blocks ? 0x52 : 0x53;
        if (i == 0) {
            uint32_t size = (uint32_t)blocks * BLOCK;

            memcpy(d + 4, &size, 4);
            strncpy((char *)d + 10, file, 20);
        }
        d[8] = (uint8_t)next;
        d[9] = (uint8_t)(next >> 8);
        dir_checksum(d);
        memset(&s_card[free[i] * BLOCK], 0, BLOCK);
    }
    save_card();
    return ERR_NONE;
}

long MemCardFormat(long chan) {
    int f;

    if (chan != 0) {
        return ERR_NO_CARD;
    }
    memset(s_card, 0, CARD_SIZE);
    s_card[0] = 'M';
    s_card[1] = 'C';
    dir_checksum(dir(0));
    for (f = 1; f < 16; f++) {
        dir(f)[0] = 0xA0;
        dir(f)[8] = dir(f)[9] = 0xFF;
        dir_checksum(dir(f));
    }
    for (f = 16; f < 36; f++) { /* the broken sector list: none */
        memset(dir(f), 0xFF, 4);
        dir(f)[8] = dir(f)[9] = 0xFF;
        dir_checksum(dir(f));
    }
    memcpy(dir(63), dir(0), FRAME);
    save_card();
    return ERR_NONE;
}

long MemCardUnformat(long chan) {
    if (chan != 0) {
        return ERR_NO_CARD;
    }
    memset(s_card, 0, BLOCK);
    save_card();
    return ERR_NONE;
}
