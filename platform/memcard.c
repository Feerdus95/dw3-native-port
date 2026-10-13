/* libmcrd over a raw 128 KB card image (saves/card1.mcd, next to data/, the
   same .mcd format as emulators). Commands finish at once: the one result
   waits for MemCardSync. Only slot 1 (chan 0x00) has a card. */
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"

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

static void save_card(void) {
    FILE *f = fopen(s_path, "wb");

    if (f == NULL || fwrite(s_card, CARD_SIZE, 1, f) != 1) {
        LOG("memcard: can't write %s", s_path);
    }
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

/* copies `bytes` at `ofs` of a file to or from `buf` */
static long file_io(const char *name, uint8_t *buf, long ofs, long bytes, int write) {
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
    if (write) {
        save_card();
    }
    return ERR_NONE;
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
    return finish(3, chan, chan == 0 ? file_io(file, adrs, ofs, bytes, 0) : ERR_NO_CARD);
}

long MemCardWriteFile(long chan, const char *file, void *adrs, long ofs, long bytes) {
    return finish(4, chan, chan == 0 ? file_io(file, adrs, ofs, bytes, 1) : ERR_NO_CARD);
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
