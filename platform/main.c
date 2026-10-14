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
int native_fill(const char *data);
int native_extract_disc(const char *data);
int native_extract(const char *image, const char *data, void (*progress)(int percent));
const char *native_extract_error(int e);
extern uint8_t STAGE_VRAM[], OVERLAY_VRAM[];
extern uint32_t HEAP_BASE[];

/* Where the extracted game is (extract.c): DW3_DATA, native.ini's data, or data/ next to the exe */
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
        LOG("no %s", path);
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
   debug = 0/1, log = <subsystems>, deadzone = <0..32767>, rumble = 0/1, fps = 50/60,
   render_scale = 1-8, fast_text = 0/1, fast_travel = 0/1, map_objective = 0/1, extra_packs = 0/1,
   bladegarurumon = 0/1, soft_shadows = 0/1, true_color = 0/1, perspective = 0/1,
   smooth_3d = 0/1, battle_shadows = 0/1, widescreen = 0/1,
   vsync = 0/1;
   the options menu (menu.c) writes them back */
static int s_fullscreen, s_scale, s_fps60, s_res_set; /* s_scale 0: from the screen */
extern int NTSC_MODE;
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

/* data_only: just the data folder, before the game's tables are filled in */
static void read_ini(int data_only) {
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
            if (!data_only) {
                continue; /* read already, and DW3_DATA may have overridden it */
            }
            /* relative to the exe's folder, as everything else here */
            int absolute = value[0] == '/' || value[0] == '\\' || (value[0] != 0 && value[1] == ':');

            snprintf(s_data, sizeof s_data, "%s%s", absolute ? "" : SDL_GetBasePath(), value);
        } else if (data_only) {
            continue;
        } else if (SDL_strcasecmp(key, "fullscreen") == 0) {
            s_fullscreen = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "scale") == 0 && atoi(value) >= 1) {
            s_scale = atoi(value);
        } else if (SDL_strcasecmp(key, "dither") == 0) {
            g_dither_on = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "true_color") == 0) {
            g_true_color = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "vsync") == 0) {
            g_vsync = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "widescreen") == 0) {
            g_widescreen = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "battle_shadows") == 0) {
            g_battle_shadows = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "smooth_3d") == 0) {
            g_smooth_3d = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "perspective") == 0) {
            g_perspective = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "soft_shadows") == 0) {
            g_soft_shadows = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "smooth_movies") == 0) {
            g_smooth_movies = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "debug") == 0) {
            g_debug = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "log") == 0) {
            g_log = log_bits(value);
        } else if (SDL_strcasecmp(key, "deadzone") == 0) {
            g_deadzone = SDL_clamp(atoi(value), 0, 32767);
        } else if (SDL_strcasecmp(key, "rumble") == 0) {
            g_rumble = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "render_scale") == 0) {
            g_res_scale = SDL_clamp(atoi(value), 1, 8);
            s_res_set = 1;
        } else if (SDL_strcasecmp(key, "fast_text") == 0) {
            g_fast_text = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "extra_packs") == 0) {
            g_extra_packs = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "fast_travel") == 0) {
            g_fast_travel = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "map_objective") == 0) {
            g_map_objective = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "bladegarurumon") == 0) {
            g_bladegarurumon = atoi(value) != 0;
        } else if (SDL_strcasecmp(key, "fps") == 0) {
            s_fps60 = atoi(value) == 60;
        }
    }
    fclose(f);
}

int main(int argc, char **argv) {
    const char *env = getenv("DW3_DATA");

    char exe[1100];

    install_crash_handler();
    read_ini(1);
    if (env != NULL) {
        snprintf(s_data, sizeof s_data, "%s", env);
    } else if (s_data[0] == 0) {
        /* data/ next to the exe, or native/data from native/build */
        snprintf(s_data, sizeof s_data, "%s../data/SLES_039.36", SDL_GetBasePath());
        snprintf(s_data, sizeof s_data, SDL_GetPathInfo(s_data, NULL) ? "%s../data" : "%sdata", SDL_GetBasePath());
    }
    if (argc == 3 && SDL_strcmp(argv[1], "--extract") == 0) {
        int e = native_extract(argv[2], s_data, NULL);
        LOG("%s", e ? native_extract_error(e) : "OK");
        return e;
    }
    /* the first run reads the player's disc; then the game's tables, before
       anything touches the game's variables (fill.c) */
    snprintf(exe, sizeof exe, "%s/SLES_039.36", s_data);
    if (!SDL_GetPathInfo(exe, NULL) && (argc > 1 || !native_extract_disc(s_data))) {
        LOG("no game in %s", s_data);
        return 1;
    }
    if (!native_fill(s_data)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Digimon World 2003",
                                 "The game's data doesn't match this version of the port. Delete the data folder "
                                 "and start the game again to read your disc anew.", NULL);
        return 1;
    }
    native_bladegarurumon_setup(); /* the game's tables as read, then its own */
    native_snapshot_overlays();
    native_fast_travel_setup(); /* the stages' doors, their variables put back */
    read_ini(0);
    NTSC_MODE = s_fps60; /* the game's own 60 Hz mode, set before it boots */
    if (getenv("DW3_LOG") != NULL) {
        g_log = log_bits(getenv("DW3_LOG"));
    }
    if ((uintptr_t)STAGE_VRAM + 512 * 1024 > 0x01000000 || (uintptr_t)OVERLAY_VRAM + 512 * 1024 > 0x01000000 ||
        (uintptr_t)HEAP_BASE + NATIVE_HEAP_SIZE > 0x01000000) {
        LOG("the game's memory is above 16 MB: the GPU's 24-bit tags can't reach it");
        return 1;
    }
    if (argc == 3 && SDL_strcmp(argv[1], "--dump-stages") == 0) {
        return stage_dump(argv[2]);
    }
    for (int i = 1; i + 1 < argc; i++) {
        if (SDL_strcmp(argv[i], "--field") == 0) {
            g_field_mode = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--arg") == 0) {
            g_field_arg = (int)SDL_strtol(argv[++i], NULL, 0);
        } else if (SDL_strcmp(argv[i], "--event") == 0 && i + 1 < argc) {
            g_field_event = (int)SDL_strtol(argv[++i], NULL, 0);
        } else if (SDL_strcmp(argv[i], "--at") == 0) {
            SDL_sscanf(argv[++i], "%d,%d", &g_field_at_x, &g_field_at_y);
        } else if (SDL_strcmp(argv[i], "--party") == 0 && i + 1 < argc) {
            g_field_party = SDL_clamp(SDL_atoi(argv[++i]), 0, 2);
        } else if (SDL_strcmp(argv[i], "--progress") == 0) {
            g_field_progress = (int)SDL_strtol(argv[++i], NULL, 0);
        } else if (SDL_strcmp(argv[i], "--set") == 0 && g_field_set_count < 64) {
            int *s = g_field_sets[g_field_set_count];
            if (SDL_sscanf(argv[++i], "%x=%d", &s[0], &s[1]) == 2) {
                g_field_set_count++;
            }
        }
    }
    if (s_scale == 0) {
        /* the biggest whole multiple of 320x240 that fits the screen, title bar
           and taskbar aside: the PSX's pixels even; the 3D as fine, up to 4x */
        SDL_Rect usable = {0, 0, 960, 720};

        SDL_InitSubSystem(SDL_INIT_VIDEO);
        SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable);
        s_scale = SDL_max(1, SDL_min(usable.w / (g_widescreen ? 427 : 320), (usable.h - 40) / 240));
        if (!s_res_set) {
            g_res_scale = SDL_min(s_scale, 4);
        }
    }
    if (!load_boot_image() || !video_init(s_scale, s_fullscreen)) {
        return 1;
    }
    return psx_main();
}
