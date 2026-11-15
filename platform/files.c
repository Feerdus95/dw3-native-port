/* The game's files: data/files/<fid>.bin from the user's disc
   (extract.c), read whole and at once. A mod's file in
   data/mods/<fid>.bin wins. Sectors stay 2048 bytes, the game's unit. */
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "launch_slots.h"

#define SECTOR 2048
#define MAX_FILES NATIVE_MAX_FIDS /* the disc's, then the field editor's new ones (3000 on) */

static long s_size[MAX_FILES]; /* bytes; 0 unknown, -1 missing */

/* Check the same priority used by open_file. A known file ID alone does not
   prove that its bytes are the generated script: user mods always win. */
int native_launch_generated_source(int fid) {
    char path[1200];
    FILE *f;
    long made_size;

    if (!native_launch_archives_ready() || !native_launch_archive_listed(fid) ||
        made_file(fid, &made_size) != NULL) return 0;
    snprintf(path, sizeof path, "%s/mods/%04d.bin", data_dir(), fid);
    f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
        return 0;
    }
    snprintf(path, sizeof path, "%s/generated/%04d.bin", data_dir(), fid);
    f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

int native_visual_archive_enabled(int archive) {
    char path[1200];
    FILE *f;
    int fid = (unsigned int)archive >> 16;

    /* file 1 is bladegaru.c's NEW_FID, BladeGarurumon's archive, built from MetalGarurumon's: prepared only if that donor was, and not under a mods/0001.bin */
    if (fid != 1) return native_launch_archive_matches(archive) && native_launch_generated_source(fid);
    if (!native_bladegarurumon_launch_ready()) return 0;
    snprintf(path, sizeof path, "%s/mods/%04d.bin", data_dir(), fid);
    f = fopen(path, "rb");
    if (f == NULL) return 1;
    fclose(f);
    return 0;
}

static char s_path[1200]; /* the last file open_file found (the game's thread) */

static FILE *open_file(int fid) {
    char *path = s_path;
    FILE *f;

    snprintf(path, sizeof s_path, "%s/mods/%04d.bin", data_dir(), fid);
    f = fopen(path, "rb");
    if (f == NULL && native_launch_archives_ready() && native_launch_archive_listed(fid)) {
        snprintf(path, sizeof s_path, "%s/generated/%04d.bin", data_dir(), fid);
        f = fopen(path, "rb");
    }
    if (f == NULL) {
        snprintf(path, sizeof s_path, "%s/files/%04d.bin", data_dir(), fid);
        f = fopen(path, "rb");
    }
    return f;
}

/* the size on disk, without the files options make (made_file) */
static long disk_size(int fid) {
    FILE *f;

    if (fid < 0 || fid >= MAX_FILES) {
        return -1;
    }
    if (s_size[fid] == 0) {
        f = open_file(fid);
        if (f == NULL) {
            s_size[fid] = -1;
        } else {
            fseek(f, 0, SEEK_END);
            s_size[fid] = ftell(f);
            fclose(f);
        }
    }
    return s_size[fid];
}

static long file_size(int fid) {
    long size;

    return made_file(fid, &size) != NULL ? size : disk_size(fid);
}

int native_disk_file(int fid) {
    return disk_size(fid) > 0;
}

int native_disc_file(int fid) {
    char path[1200];
    FILE *f;

    if (fid < 0 || fid >= MAX_FILES) {
        return 0;
    }
    snprintf(path, sizeof path, "%s/files/%04d.bin", data_dir(), fid);
    if ((f = fopen(path, "rb")) == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

int native_file_exists(int fid) {
    return file_size(fid) > 0;
}

int native_file_sectors(int fid) {
    long size = file_size(fid);

    return size > 0 ? (int)((size + SECTOR - 1) / SECTOR) : 0;
}

/* A file on disk whole into *out (malloc'd, its last sector filled out with
   zeroes): its size in bytes, or 0 */
long native_read_raw(int fid, uint8_t **out) {
    long size = disk_size(fid);
    FILE *f = size > 0 ? open_file(fid) : NULL;

    *out = NULL;
    if (f == NULL || (*out = calloc(1, (size + SECTOR - 1) / SECTOR * SECTOR)) == NULL) {
        size = 0;
    } else {
        fread(*out, 1, size, f);
    }
    if (f != NULL) {
        fclose(f);
    }
    return size;
}

/* `count` sectors of F (closed here) from `sector` into dst; past the end reads as zeroes */
static void read_from(FILE *f, int sector, int count, void *dst) {
    size_t got = 0;

    if (f != NULL) {
        fseek(f, (long)sector * SECTOR, SEEK_SET);
        got = fread(dst, 1, (size_t)count * SECTOR, f);
        fclose(f);
    }
    memset((char *)dst + got, 0, (size_t)count * SECTOR - got);
}

/* `count` sectors from sector `sector`; past the end reads as zeroes */
void native_read_file(int fid, int sector, int count, void *dst) {
    FILE *f;
    size_t got = 0;
    long size;
    const uint8_t *made = made_file(fid, &size); /* a file an option makes anew (mods.c) */

    LOGC(LOG_CD, "cd: file %d, %d sectors from %d", fid, count, sector);
    if (made != NULL) {
        got = (long)sector * SECTOR < size ? (size_t)(size - (long)sector * SECTOR) : 0;
        got = got < (size_t)count * SECTOR ? got : (size_t)count * SECTOR;
        memcpy(dst, made + (long)sector * SECTOR, got);
        memset((char *)dst + got, 0, (size_t)count * SECTOR - got);
        return;
    }
    f = open_file(fid);
    if (f == NULL) {
        LOG("files: no file %d", fid);
    }
    read_from(f, sector, count, dst);
}

/* The game's reads, as the console's drive did them: asked now, done a few frames later (the game polls isCdReading / its done flag).
   One at a time, as the game's reader is: a thread reads, the game's thread opens the file and finishes the request.
   With dstorage.dll beside the exe, DirectStorage reads (dstorage.c); without it, or if it fails, the thread reads with plain file reads. */
static struct { FILE *f; char path[1200]; int ds, fid, sector, count; void *dst; Uint64 t0, t1; double open_ms, read_ms; } s_req;
static FILE *s_log; /* perf = 1: perf/reads.csv, a row per disc read */
static SDL_Semaphore *s_wake;
static SDL_AtomicInt s_rstate; /* 0 nothing asked, 1 reading, 2 read */

int ds_ready(int buffered); /* dstorage.c */
int ds_start(const char *path, long long offset, unsigned bytes, void *dst);
int ds_wait(void);

static void do_read(void) {
    if (s_req.ds) { /* DirectStorage reads it; if it can't, the plain read */
        if (!ds_wait()) {
            read_from(fopen(s_req.path, "rb"), s_req.sector, s_req.count, s_req.dst);
        }
    } else {
        read_from(s_req.f, s_req.sector, s_req.count, s_req.dst);
    }
}

static double ms_since(Uint64 t) {
    return (double)(SDL_GetPerformanceCounter() - t) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

static int SDLCALL reader(void *unused) {
    (void)unused;
    for (;;) {
        SDL_WaitSemaphore(s_wake);
        Uint64 t = s_req.ds ? s_req.t1 : SDL_GetPerformanceCounter();

        do_read();
        s_req.read_ms = ms_since(t);
        SDL_SetAtomicInt(&s_rstate, 2);
    }
    return 0;
}

/* 1 when the reader thread is there: tried once, and up only with both its semaphore and its thread made */
static int reader_up(void) {
    static int s_reader; /* 0 not tried, 1 up, -1 not: every read at once */

    if (s_reader == 0) {
        s_wake = SDL_CreateSemaphore(0);
        s_reader = s_wake != NULL && SDL_CreateThread(reader, "cd read", NULL) != NULL ? 1 : -1;
        if (s_reader < 0) {
            LOG("files: no reader thread, disc reads at once");
            SDL_DestroySemaphore(s_wake), s_wake = NULL;
        }
    }
    return s_reader > 0;
}

void native_read_start(int fid, int sector, int count, void *dst) {
    long size;

    if (made_file(fid, &size) != NULL || !reader_up()) {
        native_read_file(fid, sector, count, dst); /* a made file is in memory; no thread: at once */
        return;
    }
    LOGC(LOG_CD, "cd: file %d, %d sectors from %d (async)", fid, count, sector);
    s_req.t0 = SDL_GetPerformanceCounter();
    s_req.f = open_file(fid);
    s_req.open_ms = ms_since(s_req.t0);
    if (s_req.f == NULL) {
        LOG("files: no file %d", fid);
    }
    s_req.fid = fid, s_req.sector = sector, s_req.count = count, s_req.dst = dst;
    /* DW3_DS_BUFFERED: DirectStorage with the OS file cache on, to try on a hard disk */
    s_req.ds = s_req.f != NULL && ds_ready(SDL_getenv("DW3_DS_BUFFERED") != NULL);
    if (s_req.ds) {
        long long at = (long long)sector * SECTOR, have = disk_size(fid) - at, want = (long long)count * SECTOR, bytes = have < want ? (have > 0 ? have : 0) : want;

        SDL_strlcpy(s_req.path, s_path, sizeof s_req.path);
        fclose(s_req.f);
        s_req.f = NULL;
        memset((char *)dst + bytes, 0, (size_t)(want - bytes)); /* past the file's end reads as zeroes */
        s_req.t1 = SDL_GetPerformanceCounter();
        if (bytes == 0) {
            s_req.read_ms = 0;
            SDL_SetAtomicInt(&s_rstate, 2);
            return;
        }
        s_req.ds = ds_start(s_req.path, at, (unsigned)bytes, dst);
        if (!s_req.ds) { /* it wouldn't: the reader thread's read, as without DirectStorage */
            s_req.f = fopen(s_req.path, "rb");
        }
    }
    SDL_SetAtomicInt(&s_rstate, 1);
    SDL_SignalSemaphore(s_wake);
}

/* 1 when nothing is being read (the request is done, or there is none) */
int native_read_done(void) {
    if (SDL_GetAtomicInt(&s_rstate) == 1) {
        return 0;
    }
    if (SDL_GetAtomicInt(&s_rstate) == 2 && g_perf) {
        if (s_log == NULL) {
            char path[1200];

            SDL_snprintf(path, sizeof path, "%sperf/reads.csv", SDL_GetBasePath());
            s_log = fopen(path, "w");
        }
        if (s_log != NULL && ftell(s_log) == 0) {
            fprintf(s_log, "ticks_ms,file,sectors,mode,open_ms,read_ms,done_after_ms\n");
        }
        if (s_log != NULL) {
            fprintf(s_log, "%llu,%d,%d,%s,%.2f,%.2f,%.2f\n", (unsigned long long)SDL_GetTicks(), s_req.fid, s_req.count, s_req.ds ? "ds" : "async", s_req.open_ms, s_req.read_ms, ms_since(s_req.t0));
            fflush(s_log);
        }
    }
    SDL_SetAtomicInt(&s_rstate, 0);
    return 1;
}

/* waits for the read in flight (a savestate load is about to replace the memory it writes to) */
void native_read_wait(void) {
    while (!native_read_done()) {
        SDL_DelayNS(100000);
    }
}
