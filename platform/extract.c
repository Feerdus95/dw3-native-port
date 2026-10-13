/* The player's disc, read once into data/ on the first run (what
   tools/extract_iso.py did): the game asks for its image (.bin/.cue, or a
   plain .iso, whose movies have no sound), checks it is Digimon World 2003
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

/* Extracts `image` into `data`, telling `progress` (if any) how far, in
   percent; NULL when done, else what went wrong. Also dw2003 --extract IMAGE. */
const char *native_extract(const char *image, const char *data, void (*progress)(int percent)) {
    const char *why = "Can't write to the data folder: is it read-only?";
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
        return "Not Digimon World 2003 (Europe), or the image is damaged.";
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
            native_log("can't write %s", path);
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
                native_log("can't write %s", path);
                goto fail;
            }
            movies += form2 != 0;
        }
        if (progress != NULL && fid % 8 == 0) {
            progress(fid * 100 / FILE_COUNT);
        }
    }
    snprintf(path, sizeof path, "%s/SLES_039.36", data);
    if (!write_file(path, exe, exe_size)) {
        native_log("can't write %s", path);
        goto fail;
    }
    native_log("disc extracted into %s (%ld movies%s)", data, movies, s_raw ? "" : "; a plain .iso: silent movies");
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

/* --- the first-run screen: 480x270 units (twice as big on screen), SDL's
   8x8 font as in the options menu; nothing of the game, which isn't there yet --- */

#define SW 480
#define SH 270

static SDL_Renderer *s_r;
static char s_picked[1100];
static SDL_AtomicInt s_dialog; /* 1 open, 2 answered */
static const char *s_error;
static int s_percent = -1;     /* reading, how far; -1 asking */
static int s_hover;            /* the pointer on the button */
static const SDL_FRect BUTTON = {SW / 2 - 64, 178, 128, 20};

static void color(Uint32 rgb, Uint8 a) {
    SDL_SetRenderDrawColor(s_r, rgb >> 16, rgb >> 8 & 0xFF, rgb & 0xFF, a);
}

/* centred on x = SW/2, `scale` times the font */
static void text(float y, Uint32 rgb, float scale, const char *s) {
    SDL_SetRenderScale(s_r, scale, scale);
    color(0x000000, 160);
    SDL_RenderDebugText(s_r, (SW / scale - 8 * SDL_strlen(s)) / 2 + 1, y / scale + 1, s);
    color(rgb, 255);
    SDL_RenderDebugText(s_r, (SW / scale - 8 * SDL_strlen(s)) / 2, y / scale, s);
    SDL_SetRenderScale(s_r, 1, 1);
}

static void fill(float x, float y, float w, float h, Uint32 rgb, Uint8 a) {
    SDL_FRect f = {x, y, w, h};
    color(rgb, a);
    SDL_RenderFillRect(s_r, &f);
}

static void draw(void) {
    color(0x0B1220, 255);
    SDL_RenderClear(s_r);
    for (int y = 0; y < SH; y += 2) { /* a soft glow from the top */
        fill(0, y, SW, 2, 0x2A4A8A, (Uint8)(70 * (1 - (float)y / SH) * (1 - (float)y / SH)));
    }
    text(26, 0xF5A623, 2, "DIGIMON WORLD 2003");
    text(50, 0x8FA3C8, 1, "native PC port");
    fill(SW / 2 - 120, 66, 240, 1, 0x3A4E78, 255);
    text(80, 0xE8ECF4, 1, "The port plays from your own copy of the game:");
    text(94, 0xF5A623, 1, "Digimon World 2003 (Europe), SLES-03936");
    text(108, 0x8FA3C8, 1, "read once into the data folder (about 850 MB).");

    /* the drop zone, dashed */
    SDL_FRect zone = {64, 128, SW - 128, 86};
    fill(zone.x, zone.y, zone.w, zone.h, 0xFFFFFF, s_percent < 0 && s_hover ? 14 : 8);
    color(0x4F6AA0, 255);
    for (float x = zone.x; x < zone.x + zone.w; x += 8) {
        SDL_RenderLine(s_r, x, zone.y, SDL_min(x + 4, zone.x + zone.w), zone.y);
        SDL_RenderLine(s_r, x, zone.y + zone.h, SDL_min(x + 4, zone.x + zone.w), zone.y + zone.h);
    }
    for (float y = zone.y; y < zone.y + zone.h; y += 8) {
        SDL_RenderLine(s_r, zone.x, y, zone.x, SDL_min(y + 4, zone.y + zone.h));
        SDL_RenderLine(s_r, zone.x + zone.w, y, zone.x + zone.w, SDL_min(y + 4, zone.y + zone.h));
    }
    if (s_percent >= 0) {
        char pct[32];
        text(146, 0xE8ECF4, 1, "Reading your disc...");
        fill(112, 166, SW - 224, 10, 0x1C2A44, 255);
        fill(112, 166, (SW - 224) * s_percent / 100.0f, 10, 0xF5A623, 255);
        SDL_snprintf(pct, sizeof pct, "%d%%", s_percent);
        text(186, 0x8FA3C8, 1, pct);
    } else {
        text(142, 0xE8ECF4, 1, "Drop your .cue or .bin here");
        text(158, 0x6C7FA6, 1, "or");
        fill(BUTTON.x, BUTTON.y, BUTTON.w, BUTTON.h, s_hover ? 0xFFB940 : 0xF5A623, 255);
        SDL_SetRenderScale(s_r, 1, 1);
        color(0x0B1220, 255);
        SDL_RenderDebugText(s_r, SW / 2 - 4 * 9, BUTTON.y + 6, "Browse...");
    }
    if (s_error != NULL) {
        text(226, 0xFF6B6B, 1, s_error);
    } else {
        text(226, 0x6C7FA6, 1, "An .iso works too, but its movies play without sound.");
    }
    text(252, 0x4A5A7A, 1, s_percent < 0 ? "Enter: browse     Esc: quit" : "");
    SDL_RenderPresent(s_r);
}

static void progress(int percent) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) { /* keep the window answering; it can't be stopped halfway */
    }
    s_percent = percent;
    draw();
}

static void SDLCALL picked(void *user, const char *const *files, int filter) {
    s_picked[0] = 0;
    if (files != NULL && files[0] != NULL) {
        SDL_strlcpy(s_picked, files[0], sizeof s_picked);
    }
    SDL_SetAtomicInt(&s_dialog, 2);
}

/* No extracted game in `data`: asks for the disc image until one works or
   the player gives up. 1 when the game is there. */
int native_extract_disc(const char *data) {
    static const SDL_DialogFileFilter FILTERS[] = {{"Disc images (.cue, .bin, .iso)", "cue;bin;iso"}};
    SDL_Window *window;
    int done = 0, quit = 0;

    if (!SDL_Init(SDL_INIT_VIDEO) ||
        !SDL_CreateWindowAndRenderer("Digimon World 2003", SW * 2, SH * 2, SDL_WINDOW_RESIZABLE, &window, &s_r)) {
        native_log("first-run screen: %s", SDL_GetError());
        return 0;
    }
    SDL_SetRenderLogicalPresentation(s_r, SW, SH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawBlendMode(s_r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderVSync(s_r, 1);
    while (!done && !quit) {
        SDL_Event e;
        int browse = 0;

        while (SDL_PollEvent(&e)) {
            SDL_ConvertEventToRenderCoordinates(s_r, &e);
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) {
                quit = 1;
            } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
                SDL_FPoint p = {e.motion.x, e.motion.y};
                s_hover = SDL_PointInRectFloat(&p, &BUTTON);
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                SDL_FPoint p = {e.button.x, e.button.y};
                browse = SDL_PointInRectFloat(&p, &BUTTON);
            } else if (e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) {
                browse = 1;
            } else if (e.type == SDL_EVENT_DROP_FILE && e.drop.data != NULL) {
                SDL_strlcpy(s_picked, e.drop.data, sizeof s_picked);
                SDL_SetAtomicInt(&s_dialog, 2);
            }
        }
        if (browse && SDL_GetAtomicInt(&s_dialog) == 0) {
            SDL_SetAtomicInt(&s_dialog, 1);
            SDL_ShowOpenFileDialog(picked, NULL, window, FILTERS, 1, NULL, false);
        }
        if (SDL_GetAtomicInt(&s_dialog) == 2) {
            SDL_SetAtomicInt(&s_dialog, 0);
            if (s_picked[0] != 0) {
                s_error = NULL;
                SDL_CreateDirectory(data);
                s_error = native_extract(s_picked, data, progress);
                s_percent = -1;
                done = s_error == NULL;
            }
        }
        draw();
    }
    SDL_DestroyRenderer(s_r);
    SDL_DestroyWindow(window);
    return done;
}
