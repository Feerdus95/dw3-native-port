/* The player's disc, read once into data/ on the first run (what
   tools/extract_iso.py did): the launcher or --extract gives its image (.bin/.cue,
   or a plain .iso, whose movies have no sound). It checks it is Digimon World 2003
   (Europe, SLES-03936) and writes

       data/files/NNNN.bin   every file of the game's file table, by fid
       data/files/NNNN.str   the movies again, whole sectors (2336 bytes:
                             XA subheader + data), as their audio is in
                             Form 2 sectors that the 2048-byte copy loses
       data/names.txt        the disc's file names, line N for file N (for the model viewer)
       data/SLES_039.36      the executable, last: its presence means done

   All of it goes to data/.extract-tmp first, and moves into place only when complete.
   The game finds its files by fid through two tables of the executable
   (FILE_SECTORS: first sector, FILE_SECTOR_COUNTS: sectors). */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "extract.h"

void native_log(const char *fmt, ...);

#define EXE_CRC 0x3334ED7Au /* zlib's crc32 of the EU SLES_039.36 */
#define EXE_VRAM 0x80010000u
#define FILE_SECTORS 0x80044F6Cu       /* s32[2382] */
#define FILE_SECTOR_COUNTS 0x800474A4u /* u16[2382] */
#define FILE_COUNT 2382
/* ponytail: the tested disc's size with its movies, rounded up; a plain .iso needs less, but the check is only a courtesy */
#define NEED_BYTES (900ull << 20)

static FILE *s_disc;
static int s_raw;     /* raw 2352-byte sectors (.bin), or plain 2048 (.iso) */
static long s_sectors;

/* fopen with the UTF-8 paths SDL's dialogs and drops give, whatever the system's code page */
static FILE *open_utf8(const char *path, const wchar_t *mode) {
    wchar_t wide[1200];

    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, SDL_arraysize(wide)) == 0) {
        return NULL;
    }
    return _wfopen(wide, mode);
}

/* The 2048 user bytes of `count` sectors (Mode 2 Form 1 in a raw image) */
static int read_sectors(long lba, long count, uint8_t *out) {
    for (long n = lba; n < lba + count; n++, out += 2048) {
        if (_fseeki64(s_disc, (long long)n * (s_raw ? 2352 : 2048) + (s_raw ? 24 : 0), SEEK_SET) != 0 ||
            fread(out, 1, 2048, s_disc) != 2048) {
            return 0;
        }
    }
    return 1;
}

/* A file of the ISO 9660 root directory: its first sector, 0 if none */
static long find(const char *name, long *size) {
    uint8_t pvd[2048], *dir;
    long lba, len, i = 0;

    if (!read_sectors(16, 1, pvd) || memcmp(pvd + 1, "CD001", 5) != 0) {
        return 0;
    }
    memcpy(&lba, pvd + 156 + 2, 4);
    memcpy(&len, pvd + 156 + 10, 4);
    dir = malloc(len + 2048);
    if (dir == NULL || !read_sectors(lba, (len + 2047) / 2048, dir)) {
        free(dir);
        return 0;
    }
    lba = 0;
    while (i < len) {
        uint8_t n = dir[i];
        if (n == 0) {
            i = (i / 2048 + 1) * 2048;
            continue;
        }
        if (dir[i + 32] >= strlen(name) && memcmp(dir + i + 33, name, strlen(name)) == 0) {
            memcpy(&lba, dir + i + 2, 4);
            memcpy(size, dir + i + 10, 4);
            break;
        }
        i += n;
    }
    free(dir);
    return lba;
}

static int write_file(const char *path, const void *data, size_t size) {
    FILE *f = open_utf8(path, L"wb");
    int ok = f != NULL && fwrite(data, 1, size, f) == size;

    if (f != NULL) {
        ok &= fclose(f) == 0;
    }
    if (!ok) {
        native_log("can't write %s", path);
    }
    return ok;
}

/* A .cue names its data file in a FILE line, relative to the .cue. If that isn't there (a renamed .bin), the
   .bin of the .cue's own name. Anything but a .cue is the data itself. E_NONE, or what's wrong. */
static int open_image(const char *image) {
    char path[1200], name[1000];
    const char *dot = strrchr(image, '.');
    FILE *cue;
    int files = 0;

    s_disc = NULL;
    if (dot == NULL || SDL_strcasecmp(dot, ".cue") != 0) {
        s_disc = open_utf8(image, L"rb");
        return s_disc != NULL ? E_NONE : E_OPEN;
    }
    if ((cue = open_utf8(image, L"r")) == NULL) {
        return E_OPEN;
    }
    name[0] = 0;
    while (fgets(path, sizeof path, cue) != NULL) {
        char quoted[1000];
        if (sscanf(path, " FILE \"%999[^\"]\"", quoted) == 1 || sscanf(path, " FILE %999s", quoted) == 1) {
            if (files++ == 0) {
                SDL_strlcpy(name, quoted, sizeof name);
            }
        }
    }
    fclose(cue);
    if (files > 1) {
        return E_CUE; /* the tested disc is one data track in one file */
    }
    if (name[0] != 0) {
        const char *slash = SDL_max(strrchr(image, '/'), strrchr(image, '\\'));
        int dir = slash != NULL ? (int)(slash - image) + 1 : 0;
        int absolute = name[0] == '/' || name[0] == '\\' || name[1] == ':';
        snprintf(path, sizeof path, "%.*s%s", absolute ? 0 : dir, image, name);
        s_disc = open_utf8(path, L"rb");
    }
    if (s_disc == NULL) {
        snprintf(path, sizeof path, "%.*s.bin", (int)(dot - image), image);
        s_disc = open_utf8(path, L"rb");
    }
    return s_disc != NULL ? E_NONE : E_OPEN;
}

/* The files of a folder with no folders in it, then the folder */
static void remove_folder(const char *dir) {
    char path[1200];
    int n = 0;
    char **names = SDL_GlobDirectory(dir, NULL, 0, &n);

    for (int i = 0; names != NULL && i < n; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        SDL_RemovePath(path);
    }
    SDL_free(names);
    SDL_RemovePath(dir);
}

static void remove_tmp(const char *tmp) {
    char path[1200];

    snprintf(path, sizeof path, "%s/files", tmp);
    remove_folder(path);
    remove_folder(tmp);
}

static int enough_space(const char *data) {
    wchar_t wide[1200];
    ULARGE_INTEGER free_bytes;

    if (MultiByteToWideChar(CP_UTF8, 0, data, -1, wide, SDL_arraysize(wide)) == 0 ||
        !GetDiskFreeSpaceExW(wide, &free_bytes, NULL, NULL)) {
        return 1; /* can't tell: the writes will */
    }
    return free_bytes.QuadPart >= NEED_BYTES;
}

/* The complete game from tmp into data: the marker goes first and comes back last */
static int move_into_place(const char *tmp, const char *data) {
    char from[1200], to[1200];

    snprintf(to, sizeof to, "%s/SLES_039.36", data);
    SDL_RemovePath(to);
    snprintf(to, sizeof to, "%s/files", data);
    remove_folder(to); /* what an earlier run left: the marker wasn't there, so it wasn't complete */
    snprintf(from, sizeof from, "%s/files", tmp);
    if (!SDL_RenamePath(from, to)) {
        native_log("can't move %s to %s: %s", from, to, SDL_GetError());
        return 0;
    }
    snprintf(from, sizeof from, "%s/names.txt", tmp);
    snprintf(to, sizeof to, "%s/names.txt", data);
    if (!SDL_RenamePath(from, to)) {
        native_log("can't move %s to %s: %s", from, to, SDL_GetError());
        return 0;
    }
    snprintf(from, sizeof from, "%s/SLES_039.36", tmp);
    snprintf(to, sizeof to, "%s/SLES_039.36", data);
    if (!SDL_RenamePath(from, to)) {
        native_log("can't move %s to %s: %s", from, to, SDL_GetError());
        return 0;
    }
    return 1;
}

static int copy_files(const uint8_t *exe, long exe_size, const char *tmp, int (*progress)(int percent), long *movies) {
    const int32_t *starts = (const int32_t *)(exe + 0x800 + (FILE_SECTORS - EXE_VRAM));
    const uint16_t *counts = (const uint16_t *)(exe + 0x800 + (FILE_SECTOR_COUNTS - EXE_VRAM));
    char path[1200];
    uint8_t *buf = NULL;
    int result = E_NONE;

    for (int fid = 0; fid < FILE_COUNT && result == E_NONE; fid++) {
        long start = starts[fid], count = counts[fid];

        if (progress != NULL && !progress(fid * 100 / FILE_COUNT)) {
            result = E_CANCELLED;
        } else if (start <= 0 || count == 0) {
            continue;
        } else if (start + count > s_sectors) {
            native_log("the image ends at sector %ld, and file %d goes on to %ld", s_sectors, fid, start + count);
            result = E_TRUNCATED;
        } else {
            buf = realloc(buf, count * 2352);
            snprintf(path, sizeof path, "%s/files/%04d.bin", tmp, fid);
            if (buf == NULL || !read_sectors(start, count, buf)) {
                native_log("can't read file %d from the image", fid);
                result = E_READ;
            } else if (!write_file(path, buf, count * 2048)) {
                result = E_WRITE;
            } else if (s_raw) {
                /* whole sectors past the sync and header: subheader + 2328 bytes */
                int form2 = 0;
                for (long n = 0; n < count && result == E_NONE; n++) {
                    if (_fseeki64(s_disc, (long long)(start + n) * 2352 + 16, SEEK_SET) != 0 ||
                        fread(buf + n * 2336, 1, 2336, s_disc) != 2336) {
                        native_log("can't read file %d from the image", fid);
                        result = E_READ;
                    } else {
                        form2 |= buf[n * 2336 + 2] & 0x20;
                    }
                }
                snprintf(path, sizeof path, "%s/files/%04d.str", tmp, fid);
                if (result == E_NONE && form2 && !write_file(path, buf, count * 2336)) {
                    result = E_WRITE;
                }
                *movies += form2 != 0;
            }
        }
    }
    free(buf);
    snprintf(path, sizeof path, "%s/SLES_039.36", tmp);
    if (result == E_NONE && !write_file(path, exe, exe_size)) {
        result = E_WRITE;
    }
    return result;
}

/* Every file's name, line N for file N, into dir/names.txt. They are in directory records the disc's root doesn't
   list, so every sector is searched for them; the file table says which name is which file. */
static int write_names(const uint8_t *exe, const char *dir, int (*progress)(int percent)) {
    const int32_t *starts = (const int32_t *)(exe + 0x800 + (FILE_SECTORS - EXE_VRAM));
    char (*by_lba)[16] = calloc(s_sectors, 16), path[1200];
    uint8_t sector[2048];
    FILE *f;
    int result = by_lba != NULL ? E_NONE : E_READ;

    for (long lba = 0; lba < s_sectors && result == E_NONE; lba++) {
        if (lba % 4096 == 0 && progress != NULL && !progress(99)) {
            result = E_CANCELLED;
        } else if (!read_sectors(lba, 1, sector)) {
            result = E_READ;
        }
        for (int i = 0; result == E_NONE && i + 34 <= 2048;) {
            const int n = sector[i], len = sector[i + 32];
            uint32_t start;

            /* a record's extent is stored little and big-endian, and a file's name ends ;1 */
            if (n < 34 || i + n > 2048 || 33 + len > n || sector[i + 2] != sector[i + 9] || sector[i + 3] != sector[i + 8] ||
                sector[i + 4] != sector[i + 7] || sector[i + 5] != sector[i + 6]) {
                break;
            }
            memcpy(&start, sector + i + 2, 4);
            if (len > 2 && len - 2 < 16 && memcmp(sector + i + 33 + len - 2, ";1", 2) == 0 && start < (uint32_t)s_sectors) {
                memcpy(by_lba[start], sector + i + 33, len - 2);
                by_lba[start][len - 2] = 0;
            }
            i += n;
        }
    }
    snprintf(path, sizeof path, "%s/names.txt", dir);
    if (result == E_NONE && (f = open_utf8(path, L"wb")) != NULL) {
        for (int fid = 0; fid < FILE_COUNT; fid++) {
            fprintf(f, "%s\n", starts[fid] > 0 && starts[fid] < s_sectors ? by_lba[starts[fid]] : "");
        }
        result = fclose(f) == 0 ? E_NONE : E_WRITE;
    } else if (result == E_NONE) {
        native_log("can't write %s", path);
        result = E_WRITE;
    }
    free(by_lba);
    return result;
}

/* The image opened and checked: E_NONE with s_disc open and the exe to free, else what's wrong, with nothing open */
static int open_game(const char *image, uint8_t **out, long *size) {
    long exe_lba;
    long long bytes;
    uint8_t *exe;
    int result = open_image(image);

    if (result != E_NONE) {
        native_log("can't use %s: %s", image, native_extract_error(result));
        return result;
    }
    _fseeki64(s_disc, 0, SEEK_END);
    bytes = _ftelli64(s_disc);
    s_raw = bytes % 2352 == 0;
    s_sectors = (long)(bytes / (s_raw ? 2352 : 2048));
    exe_lba = find("SLES_039.36", size);
    if (exe_lba == 0 || *size <= 0 || *size > 4 << 20) {
        fclose(s_disc);
        return E_GAME;
    }
    exe = malloc((*size + 2047) & ~2047);
    if (exe == NULL || !read_sectors(exe_lba, (*size + 2047) / 2048, exe) || SDL_crc32(0, exe, *size) != EXE_CRC) {
        free(exe);
        fclose(s_disc);
        return E_BAD;
    }
    *out = exe;
    return E_NONE;
}

int native_disc_names(const char *image, const char *data, int (*progress)(int percent)) {
    uint8_t *exe;
    long exe_size = 0;
    int result = open_game(image, &exe, &exe_size);

    if (result == E_NONE) {
        result = write_names(exe, data, progress);
        free(exe);
        fclose(s_disc);
    }
    return result;
}

int native_extract(const char *image, const char *data, int (*progress)(int percent)) {
    char path[1200], tmp[1200];
    uint8_t *exe;
    long exe_size = 0, movies = 0;
    int result = open_game(image, &exe, &exe_size);

    if (result != E_NONE) {
        return result;
    }
    snprintf(tmp, sizeof tmp, "%s/.extract-tmp", data);
    remove_tmp(tmp); /* what a killed run left */
    snprintf(path, sizeof path, "%s/files", tmp);
    if (!SDL_CreateDirectory(path)) { /* with the folders above it */
        native_log("can't create %s: %s", path, SDL_GetError());
        result = E_WRITE;
    } else if (!enough_space(data)) {
        native_log("less than %llu MB free for %s", NEED_BYTES >> 20, data);
        result = E_SPACE;
    } else {
        result = copy_files(exe, exe_size, tmp, progress, &movies);
    }
    if (result == E_NONE) {
        result = write_names(exe, tmp, progress);
    }
    if (result == E_NONE && !move_into_place(tmp, data)) {
        result = E_WRITE;
    }
    remove_tmp(tmp);
    free(exe);
    fclose(s_disc);
    if (result == E_NONE) {
        native_log("disc extracted into %s (%ld movies%s)", data, movies, s_raw ? "" : "; a plain .iso: silent movies");
    } else {
        native_log("disc not extracted: %s", native_extract_error(result));
    }
    return result;
}

const char *native_extract_error(int e) {
    static const char *const TEXT[] = {
        "OK", "Can't open the image.", "This isn't Digimon World 2003 (Europe, SLES-03936).",
        "Not Digimon World 2003 (Europe), or the image is damaged.", "Can't write to the data folder.",
        "Can't read the image.", "The image is cut short: it ends before the game does.",
        "This .cue has several files; only a single-file image works.", "Not enough free space: about 900 MB is needed.",
        "Cancelled.",
    };
    return TEXT[e];
}
