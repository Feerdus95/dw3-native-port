/* The player's disc, read once into data/ on the first run (what
   tools/extract_iso.py did): the game asks for its image (.bin/.cue, or a
   plain .iso, without the movies), checks it is Digimon World 2003
   (Europe, SLES-03936) and writes

       data/files/NNNN.bin   every file of the game's file table, by fid
       data/files/NNNN.str   the movies again, whole sectors (2336 bytes:
                             XA subheader + data), as their audio is in
                             Form 2 sectors that the 2048-byte copy loses
       data/SLES_039.36      the executable, last: its presence means done

   The game finds its files by fid through two tables of the executable
   (FILE_SECTORS: first sector, FILE_SECTOR_COUNTS: sectors). */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void native_log(const char *fmt, ...);

#define EXE_CRC 0x3334ED7Au /* zlib's crc32 of the EU SLES_039.36 */
#define EXE_VRAM 0x80010000u
#define FILE_SECTORS 0x80044F6Cu       /* s32[2382] */
#define FILE_SECTOR_COUNTS 0x800474A4u /* u16[2382] */
#define FILE_COUNT 2382

static FILE *s_disc;
static int s_raw;     /* raw 2352-byte sectors (.bin), or plain 2048 (.iso) */
static long s_sectors;

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
    FILE *f = fopen(path, "wb");
    int ok = f != NULL && fwrite(data, 1, size, f) == size;

    if (f != NULL) {
        ok &= fclose(f) == 0;
    }
    return ok;
}

/* Extracts `image` into `data`, the title of `progress` (if any) saying how
   far; NULL when done, else what went wrong. Also dw2003 --extract IMAGE. */
const char *native_extract(const char *image, const char *data, SDL_Window *progress) {
    static char why[256];
    char path[1200];
    uint8_t *exe, *buf = NULL;
    long exe_lba, exe_size = 0, movies = 0;
    const int32_t *starts;
    const uint16_t *counts;
    long long bytes;
    char *dot;

    /* a .cue's data is the .bin of the same name */
    snprintf(path, sizeof path, "%s", image);
    dot = strrchr(path, '.');
    if (dot != NULL && SDL_strcasecmp(dot, ".cue") == 0) {
        strcpy(dot, ".bin");
    }
    s_disc = fopen(path, "rb");
    if (s_disc == NULL) {
        return "Can't open the image.";
    }
    _fseeki64(s_disc, 0, SEEK_END);
    bytes = _ftelli64(s_disc);
    s_raw = bytes % 2352 == 0;
    s_sectors = (long)(bytes / (s_raw ? 2352 : 2048));
    exe_lba = find("SLES_039.36", &exe_size);
    if (exe_lba == 0 || exe_size <= 0 || exe_size > 4 << 20) {
        fclose(s_disc);
        return "This isn't Digimon World 2003 (Europe, SLES-03936).";
    }
    exe = malloc((exe_size + 2047) & ~2047);
    if (!read_sectors(exe_lba, (exe_size + 2047) / 2048, exe) || SDL_crc32(0, exe, exe_size) != EXE_CRC) {
        free(exe);
        fclose(s_disc);
        return "This isn't Digimon World 2003 (Europe, SLES-03936), or the image is damaged.";
    }
    starts = (const int32_t *)(exe + 0x800 + (FILE_SECTORS - EXE_VRAM));
    counts = (const uint16_t *)(exe + 0x800 + (FILE_SECTOR_COUNTS - EXE_VRAM));
    snprintf(path, sizeof path, "%s/files", data);
    SDL_CreateDirectory(path);
    for (int fid = 0; fid < FILE_COUNT; fid++) {
        long start = starts[fid], count = counts[fid];

        if (start <= 0 || count == 0 || start + count > s_sectors) {
            continue;
        }
        buf = realloc(buf, count * 2352);
        snprintf(path, sizeof path, "%s/files/%04d.bin", data, fid);
        if (!read_sectors(start, count, buf) || !write_file(path, buf, count * 2048)) {
            snprintf(why, sizeof why, "Can't write %s.", path);
            goto fail;
        }
        if (s_raw) {
            /* whole sectors past the sync and header: subheader + 2328 bytes */
            int form2 = 0;
            for (long n = 0; n < count; n++) {
                _fseeki64(s_disc, (long long)(start + n) * 2352 + 16, SEEK_SET);
                fread(buf + n * 2336, 1, 2336, s_disc);
                form2 |= buf[n * 2336 + 2] & 0x20;
            }
            snprintf(path, sizeof path, "%s/files/%04d.str", data, fid);
            if (form2 && !write_file(path, buf, count * 2336)) {
                snprintf(why, sizeof why, "Can't write %s.", path);
                goto fail;
            }
            movies += form2 != 0;
        }
        if (progress != NULL && fid % 16 == 0) {
            char title[96];
            snprintf(title, sizeof title, "Digimon World 2003: reading your disc, %d%%", fid * 100 / FILE_COUNT);
            SDL_SetWindowTitle(progress, title);
            SDL_PumpEvents();
        }
    }
    snprintf(path, sizeof path, "%s/SLES_039.36", data);
    if (!write_file(path, exe, exe_size)) {
        snprintf(why, sizeof why, "Can't write %s.", path);
        goto fail;
    }
    native_log("disc extracted into %s (%ld movies%s)", data, movies, s_raw ? "" : ", none: a plain .iso");
    free(buf);
    free(exe);
    fclose(s_disc);
    return NULL;
fail:
    free(buf);
    free(exe);
    fclose(s_disc);
    return why;
}

static char s_picked[1100];
static int s_done;

static void SDLCALL picked(void *user, const char *const *files, int filter) {
    if (files != NULL && files[0] != NULL) {
        snprintf(s_picked, sizeof s_picked, "%s", files[0]);
    }
    s_done = 1;
}

/* No extracted game in `data`: asks for the disc image until one works or
   the player gives up. 1 when the game is there. */
int native_extract_disc(const char *data) {
    static const SDL_DialogFileFilter FILTERS[] = {{"Disc images", "cue;bin;iso"}};
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose the disc image..."},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"},
    };
    SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, NULL, "Digimon World 2003",
                              "The game needs your own disc of Digimon World 2003 (Europe, SLES-03936), "
                              "as an image: .bin/.cue (an .iso lacks what the movies need).\n\n"
                              "It is read once, into the data folder next to the game (about 850 MB).",
                              2, buttons, NULL};
    SDL_Window *window;
    int choice = 0;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        native_log("SDL_Init: %s", SDL_GetError());
        return 0;
    }
    window = SDL_CreateWindow("Digimon World 2003", 640, 120, 0);
    box.window = window;
    for (;;) {
        const char *why;

        if (!SDL_ShowMessageBox(&box, &choice) || choice != 1) {
            break;
        }
        s_done = 0, s_picked[0] = 0;
        SDL_ShowOpenFileDialog(picked, NULL, window, FILTERS, 1, NULL, false);
        while (!s_done) {
            SDL_PumpEvents();
            SDL_Delay(10);
        }
        if (s_picked[0] == 0) {
            continue;
        }
        SDL_CreateDirectory(data);
        why = native_extract(s_picked, data, window);
        if (why == NULL) {
            SDL_DestroyWindow(window);
            return 1;
        }
        SDL_SetWindowTitle(window, "Digimon World 2003");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Digimon World 2003", why, window);
    }
    SDL_DestroyWindow(window);
    return 0;
}
