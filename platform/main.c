/* Entry point: the window, the game's data, then the game's own main()
   (renamed psx_main), which never returns. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdarg.h>
#include <stdlib.h>
#include "platform.h"
#include "../include/native.h"

int psx_main(void);
int video_init(int scale, int fullscreen);
void install_crash_handler(void);
void native_snapshot_overlays(void);
extern uint8_t STAGE_VRAM[], OVERLAY_VRAM[];
extern uint32_t HEAP_BASE[];

/* Where the extracted game is (tools/extract_iso.py): DW3_DATA, or data/ next to the exe */
static char s_data[1024];

const char *data_dir(void) {
    return s_data;
}

/* Logs `what` the first time only (keyed by the pointer: callers pass literals) */
void log_once(const char *what) {
    static const char *seen[1024];
    static int count;
    int i;

    for (i = 0; i < count; i++) {
        if (seen[i] == what) {
            return;
        }
    }
    if (count < 1024) {
        seen[count++] = what;
    }
    LOG("%s", what);
}

/* printf to stderr, for probes in game code (native.h) */
void native_log(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

/* The BIOS loaded all of SLES_039.36's text, and the 320x480 boot image at
   its end lands in the sub-overlay area, where main() reads it */
static int load_boot_image(void) {
    char path[1100];
    FILE *f;
    long size, off = 0x800 + (0x800A5DE0 - 0x80010000); /* STAGE_VRAM - EXE_VRAM + header */

    snprintf(path, sizeof path, "%s/SLES_039.36", s_data);
    f = fopen(path, "rb");
    if (f == NULL) {
        LOG("no %s: run native/tools/extract_iso.py with your disc first", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, off, SEEK_SET);
    fread(STAGE_VRAM, 1, size - off, f);
    fclose(f);
    return 1;
}

/* native.ini next to the exe, all optional: data = <path>, fullscreen = 0/1,
   scale = <window size in 320x240 steps>, dither = 0/1, smooth_movies = 0/1,
   debug = 0/1, log = <subsystems> */
static int s_fullscreen, s_scale = 3;
int g_debug, g_smooth_movies = 1, g_log;

/* "cd,snd" or "all" to LOG_* bits */
static int log_bits(const char *names) {
    static const char *NAMES[] = {"gpu", "cd", "snd", "pad", "mc", "ovl"};
    int bits = 0, i;

    for (i = 0; i < 6; i++) {
        if (SDL_strstr(names, NAMES[i]) != NULL || SDL_strstr(names, "all") != NULL) {
            bits |= 1 << i;
        }
    }
    return bits;
}

static void read_ini(void) {
    char path[1100], line[1200], key[32], value[1100];
    FILE *f;

    snprintf(path, sizeof path, "%snative.ini", SDL_GetBasePath());
    if ((f = fopen(path, "r")) == NULL) {
        return;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        if (line[0] == '#' || line[0] == ';' || sscanf(line, " %31[^= ] = %1099[^\r\n]", key, value) != 2) {
            continue;
        }
        if (SDL_strcasecmp(key, "data") == 0) {
            /* relative to the exe's folder, as everything else here */
            int absolute = value[0] == '/' || value[0] == '\\' || (value[0] != 0 && value[1] == ':');

            snprintf(s_data, sizeof s_data, "%s%s", absolute ? "" : SDL_GetBasePath(), value);
        } else if (SDL_strcasecmp(key, "fullscreen") == 0) {
            s_fullscreen = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "scale") == 0 && atoi(value) >= 1) {
            s_scale = atoi(value);
        } else if (SDL_strcasecmp(key, "dither") == 0) {
            g_dither_on = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "smooth_movies") == 0) {
            g_smooth_movies = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "debug") == 0) {
            g_debug = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "log") == 0) {
            g_log = log_bits(value);
        }
    }
    fclose(f);
}

int main(int argc, char **argv) {
    const char *env = getenv("DW3_DATA");

    install_crash_handler();
    native_snapshot_overlays();
    read_ini();
    if (getenv("DW3_LOG") != NULL) {
        g_log = log_bits(getenv("DW3_LOG"));
    }
    if (env != NULL) {
        snprintf(s_data, sizeof s_data, "%s", env);
    } else if (s_data[0] == 0) {
        /* data/ next to the exe, or native/data from native/build */
        snprintf(s_data, sizeof s_data, "%sdata", SDL_GetBasePath());
        if (!SDL_GetPathInfo(s_data, NULL)) {
            snprintf(s_data, sizeof s_data, "%s../data", SDL_GetBasePath());
        }
    }
    if ((uintptr_t)STAGE_VRAM + 512 * 1024 > 0x01000000 || (uintptr_t)OVERLAY_VRAM + 512 * 1024 > 0x01000000 ||
        (uintptr_t)HEAP_BASE + NATIVE_HEAP_SIZE > 0x01000000) {
        LOG("the game's memory is above 16 MB: the GPU's 24-bit tags can't reach it");
        return 1;
    }
    if (!load_boot_image() || !video_init(s_scale, s_fullscreen)) {
        return 1;
    }
    return psx_main();
}
