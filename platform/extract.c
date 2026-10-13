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

/* What can go wrong, for T[][] below */
enum { E_NONE, E_OPEN, E_GAME, E_BAD, E_WRITE };

/* Extracts `image` into `data`, telling `progress` (if any) how far, in
   percent; E_NONE when done, else what went wrong (native_extract_error).
   Also dw2003 --extract IMAGE. */
int native_extract(const char *image, const char *data, void (*progress)(int percent)) {
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
        return E_OPEN;
    }
    _fseeki64(s_disc, 0, SEEK_END);
    bytes = _ftelli64(s_disc);
    s_raw = bytes % 2352 == 0;
    s_sectors = (long)(bytes / (s_raw ? 2352 : 2048));
    exe_lba = find("SLES_039.36", &exe_size);
    if (exe_lba == 0 || exe_size <= 0 || exe_size > 4 << 20) {
        fclose(s_disc);
        return E_GAME;
    }
    exe = malloc((exe_size + 2047) & ~2047);
    if (!read_sectors(exe_lba, (exe_size + 2047) / 2048, exe) || SDL_crc32(0, exe, exe_size) != EXE_CRC) {
        free(exe);
        fclose(s_disc);
        return E_BAD;
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
    return E_NONE;
fail:
    free(buf);
    free(exe);
    fclose(s_disc);
    return E_WRITE;
}

/* --- the first-run screen: 480x270 units (twice as big on screen), SDL's
   8x8 font as in the options menu; nothing of the game, which isn't there yet --- */

#define SW 480
#define SH 270

/* The disc's five languages; the first is the default and the command line's */
enum { LANGS = 5 };
enum { L_NAME, L_PORT, L_NEED, L_ONCE, L_DROP, L_OR, L_BROWSE, L_READING, L_ISO, L_KEYS,
       L_OPEN, L_GAME, L_BAD, L_WRITE, L_FAN, L_COUNT };
/* each line fits 58 characters; no accented capital or i (draw_text) */
static const char *const T[LANGS][L_COUNT] = {
    {"English", "native PC port", "The port plays from your own copy of the game:",
     "read once into the data folder (about 850 MB).", "Drop your .cue or .bin here", "or", "Browse...",
     "Reading your disc...", "An .iso may cause issues: a .cue/.bin is best.", "Enter: browse     Esc: quit",
     "Can't open the image.", "This isn't Digimon World 2003 (Europe, SLES-03936).",
     "Not Digimon World 2003 (Europe), or the image is damaged.", "Can't write to the data folder.",
     "Fan project, not affiliated with Bandai Namco."},
    {"Español", "port nativo para PC", "El port se juega con tu propia copia del juego:",
     "se lee una sola vez a la carpeta data (unos 850 MB).", "Arrastra tu .cue o .bin a esta zona", "o",
     "Buscar...", "Leyendo tu disco...", "Una .iso puede dar problemas: mejor un .cue/.bin.",
     "Enter: buscar     Esc: salir", "No se puede abrir la imagen.", "No es Digimon World 2003 (Europa, SLES-03936).",
     "No es Digimon World 2003 (Europa) o la imagen está dañada.", "No se puede escribir en la carpeta data.",
     "Proyecto de fans, sin relación con Bandai Namco."},
    {"Français", "portage PC natif", "Le portage se joue avec votre propre copie du jeu :",
     "lue une seule fois dans le dossier data (environ 850 Mo).", "Déposez votre .cue ou .bin ici", "ou",
     "Parcourir...", "Lecture de votre disque...", "Un .iso peut poser problème : préférez un .cue/.bin.",
     "Entrée : parcourir     Esc : quitter", "Impossible d'ouvrir l'image.",
     "Ce n'est pas Digimon World 2003 (Europe, SLES-03936).", "Pas Digimon World 2003 (Europe), ou image endommagée.",
     "Impossible d'écrire dans le dossier data.", "Projet de fans, sans lien avec Bandai Namco."},
    {"Deutsch", "nativer PC-Port", "Der Port läuft mit deiner eigenen Kopie des Spiels:",
     "einmalig in den Ordner data gelesen (ca. 850 MB).", "Zieh deine .cue oder .bin hierher", "oder",
     "Durchsuchen...", "Deine Disc wird gelesen...", "Eine .iso kann Probleme machen: besser .cue/.bin.",
     "Enter: durchsuchen     Esc: beenden", "Das Image lässt sich nicht öffnen.",
     "Das ist nicht Digimon World 2003 (Europa, SLES-03936).", "Nicht Digimon World 2003 (Europa) oder Image beschädigt.",
     "Kann nicht in den Ordner data schreiben.", "Fanprojekt, nicht mit Bandai Namco verbunden."},
    {"Italiano", "port nativo per PC", "Il port usa la tua copia originale del gioco:",
     "letta una sola volta nella cartella data (circa 850 MB).", "Trascina qui il tuo .cue o .bin", "oppure",
     "Sfoglia...", "Lettura del disco...", "Una .iso può dare problemi: meglio un .cue/.bin.",
     "Invio: sfoglia     Esc: esci", "Impossibile aprire l'immagine.", "Non è Digimon World 2003 (Europa, SLES-03936).",
     "Non è Digimon World 2003 (Europa), o immagine danneggiata.", "Impossibile scrivere nella cartella data.",
     "Progetto di fan, non affiliato a Bandai Namco."},
};
static const char *const CREDIT = "\xC2\xA9 2026 Feerdus  -  MIT License"; /* the font has the (c) sign */

const char *native_extract_error(int e) {
    return T[0][L_OPEN + e - E_OPEN];
}

static SDL_Renderer *s_r;
static char s_picked[1100];
static SDL_AtomicInt s_dialog; /* 1 open, 2 answered */
static int s_error;            /* E_*, shown until the next try */
static int s_percent = -1;     /* reading, how far; -1 asking */
static int s_lang, s_menu;     /* the language, its list open */
static SDL_FPoint s_mouse = {-1, -1};
static const SDL_FRect ZONE = {64, 128, SW - 128, 86}, BUTTON = {SW / 2 - 64, 178, 128, 20},
                       PICKER = {SW - 92, 8, 84, 14};

static void color(Uint32 rgb, Uint8 a) {
    SDL_SetRenderDrawColor(s_r, rgb >> 16, rgb >> 8 & 0xFF, rgb & 0xFF, a);
}

static void fill(float x, float y, float w, float h, Uint32 rgb, Uint8 a) {
    SDL_FRect f = {x, y, w, h};
    color(rgb, a);
    SDL_RenderFillRect(s_r, &f);
}

static int over(const SDL_FRect *r) {
    return SDL_PointInRectFloat(&s_mouse, r);
}

/* The font has ASCII and the (c) sign only: an accented letter is its base
   letter with the accent drawn over it (rows 0-1, free above the lowercase;
   the cedilla in row 7, free below), in pixels of the cell */
#define ACUTE (0x30 | 0x18 << 8)
#define GRAVE (0x06 | 0x0C << 8)
#define CIRC (0x18 | 0x66 << 8)
#define DIAER 0x66
#define TILDE (0x4C | 0x32 << 8)
#define CEDILLA (1 << 16)
static const struct {
    Uint32 cp;
    char base;
    Uint32 mark; /* bits 0-15: rows 0 and 1, a bit a pixel; CEDILLA: row 7 */
} ACCENTS[] = {
    {0xE1, 'a', ACUTE}, {0xE0, 'a', GRAVE}, {0xE2, 'a', CIRC}, {0xE4, 'a', DIAER}, {0xE9, 'e', ACUTE},
    {0xE8, 'e', GRAVE}, {0xEA, 'e', CIRC},  {0xF3, 'o', ACUTE}, {0xF2, 'o', GRAVE}, {0xF4, 'o', CIRC},
    {0xF6, 'o', DIAER}, {0xFA, 'u', ACUTE}, {0xF9, 'u', GRAVE}, {0xFB, 'u', CIRC},  {0xFC, 'u', DIAER},
    {0xF1, 'n', TILDE}, {0xE7, 'c', CEDILLA},
};

static void draw_text(float x, float y, const char *s) {
    Uint32 cp;

    while ((cp = SDL_StepUTF8(&s, NULL)) != 0) {
        char one[8] = "?";
        Uint32 mark = 0;

        if (cp < 0x80 || cp == 0xA9) {
            SDL_UCS4ToUTF8(cp, one)[0] = 0;
        } else {
            for (size_t i = 0; i < SDL_arraysize(ACCENTS); i++) {
                if (ACCENTS[i].cp == cp) {
                    one[0] = ACCENTS[i].base;
                    mark = ACCENTS[i].mark;
                }
            }
        }
        SDL_RenderDebugText(s_r, x, y, one);
        for (int b = 0; b < 16; b++) {
            if (mark >> b & 1) {
                SDL_FRect px = {x + b % 8, y + b / 8, 1, 1};
                SDL_RenderFillRect(s_r, &px);
            }
        }
        if (mark & CEDILLA) {
            SDL_FRect px = {x + 3, y + 7, 2, 1};
            SDL_RenderFillRect(s_r, &px);
        }
        x += 8;
    }
}

/* at x, aligned by `align` (0 left, 0.5 centred, 1 right), `scale` times the font, with a shadow */
static void text(float x, float y, float align, Uint32 rgb, float scale, const char *s) {
    float w = 8 * SDL_utf8strlen(s);

    SDL_SetRenderScale(s_r, scale, scale);
    x = x / scale - w * align;
    y /= scale;
    color(0x000000, 160);
    draw_text(x + 1 / scale, y + 1 / scale, s);
    color(rgb, 255);
    draw_text(x, y, s);
    SDL_SetRenderScale(s_r, 1, 1);
}

#define CENTRE(y, rgb, s) text(SW / 2, y, 0.5f, rgb, 1, s)

static void dashed(const SDL_FRect *r) {
    for (float x = r->x; x < r->x + r->w; x += 8) {
        SDL_RenderLine(s_r, x, r->y, SDL_min(x + 4, r->x + r->w), r->y);
        SDL_RenderLine(s_r, x, r->y + r->h, SDL_min(x + 4, r->x + r->w), r->y + r->h);
    }
    for (float y = r->y; y < r->y + r->h; y += 8) {
        SDL_RenderLine(s_r, r->x, y, r->x, SDL_min(y + 4, r->y + r->h));
        SDL_RenderLine(s_r, r->x + r->w, y, r->x + r->w, SDL_min(y + 4, r->y + r->h));
    }
}

/* the language list, under PICKER */
static SDL_FRect item(int i) {
    SDL_FRect r = {PICKER.x, PICKER.y + PICKER.h + 2 + i * 14, PICKER.w, 14};
    return r;
}

static void draw(void) {
    const char *const *t = T[s_lang];
    int asking = s_percent < 0;

    color(0x0B1220, 255);
    SDL_RenderClear(s_r);
    for (int y = 0; y < SH; y += 2) { /* a soft glow from the top */
        fill(0, y, SW, 2, 0x2A4A8A, (Uint8)(70 * (1 - (float)y / SH) * (1 - (float)y / SH)));
    }
    text(SW / 2, 26, 0.5f, 0xF5A623, 2, "DIGIMON WORLD 2003");
    CENTRE(50, 0x8FA3C8, t[L_PORT]);
    fill(SW / 2 - 120, 66, 240, 1, 0x3A4E78, 255);
    CENTRE(80, 0xE8ECF4, t[L_NEED]);
    CENTRE(94, 0xF5A623, "Digimon World 2003 (Europe), SLES-03936");
    CENTRE(108, 0x8FA3C8, t[L_ONCE]);

    fill(ZONE.x, ZONE.y, ZONE.w, ZONE.h, 0xFFFFFF, asking && !s_menu && over(&ZONE) ? 14 : 8);
    color(0x4F6AA0, 255);
    dashed(&ZONE);
    if (!asking) {
        char pct[16];
        CENTRE(146, 0xE8ECF4, t[L_READING]);
        fill(112, 166, SW - 224, 10, 0x1C2A44, 255);
        fill(112, 166, (SW - 224) * s_percent / 100.0f, 10, 0xF5A623, 255);
        SDL_snprintf(pct, sizeof pct, "%d%%", s_percent);
        CENTRE(186, 0x8FA3C8, pct);
    } else {
        CENTRE(142, 0xE8ECF4, t[L_DROP]);
        CENTRE(158, 0x6C7FA6, t[L_OR]);
        fill(BUTTON.x, BUTTON.y, BUTTON.w, BUTTON.h, !s_menu && over(&ZONE) ? 0xFFB940 : 0xF5A623, 255);
        color(0x0B1220, 255);
        draw_text(SW / 2 - 4 * SDL_utf8strlen(t[L_BROWSE]), BUTTON.y + 6, t[L_BROWSE]);
    }
    if (s_error) {
        CENTRE(226, 0xFF6B6B, t[L_OPEN + s_error - E_OPEN]);
    } else {
        CENTRE(226, 0x6C7FA6, t[L_ISO]);
    }
    CENTRE(242, 0x4A5A7A, asking ? t[L_KEYS] : "");
    /* the credits, at the font's own size on screen */
    text(8, SH - 9, 0, 0x4A5A7A, 0.5f, CREDIT);
    text(SW - 8, SH - 9, 1, 0x4A5A7A, 0.5f, t[L_FAN]);

    /* the language, top right; its list over everything */
    if (asking) {
        fill(PICKER.x, PICKER.y, PICKER.w, PICKER.h, 0x1C2A44, over(&PICKER) || s_menu ? 255 : 200);
        color(0x4F6AA0, 255);
        SDL_RenderRect(s_r, &PICKER);
        color(0xE8ECF4, 255);
        draw_text(PICKER.x + 5, PICKER.y + 3, t[L_NAME]);
        for (int i = 0; i < 3; i++) { /* the arrow */
            SDL_RenderLine(s_r, PICKER.x + PICKER.w - 12 + i, PICKER.y + 5 + i, PICKER.x + PICKER.w - 6 - i, PICKER.y + 5 + i);
        }
        for (int i = 0; s_menu && i < LANGS; i++) {
            SDL_FRect r = item(i);
            fill(r.x, r.y, r.w, r.h, i == s_lang ? 0x2A4A8A : over(&r) ? 0x3A4E78 : 0x1C2A44, 250);
            color(i == s_lang ? 0xF5A623 : 0xE8ECF4, 255);
            draw_text(r.x + 5, r.y + 3, T[i][L_NAME]);
        }
        if (s_menu) {
            SDL_FRect all = {PICKER.x, PICKER.y + PICKER.h + 2, PICKER.w, LANGS * 14};
            color(0x4F6AA0, 255);
            SDL_RenderRect(s_r, &all);
        }
    }
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

/* the system's language, if it is one of the five */
static int system_lang(void) {
    static const char *const CODES[LANGS] = {"en", "es", "fr", "de", "it"};
    int n, lang = 0;
    SDL_Locale **l = SDL_GetPreferredLocales(&n);

    for (int i = n - 1; l != NULL && i >= 0; i--) { /* the first preferred wins */
        for (int k = 0; k < LANGS; k++) {
            if (SDL_strcasecmp(l[i]->language, CODES[k]) == 0) {
                lang = k;
            }
        }
    }
    SDL_free(l);
    return lang;
}

/* No extracted game in `data`: asks for the disc image until one works or
   the player gives up. 1 when the game is there. */
int native_extract_disc(const char *data) {
    static const SDL_DialogFileFilter FILTERS[] = {{"Disc images (.cue, .bin, .iso)", "cue;bin;iso"}};
    SDL_Window *window;
    SDL_Rect usable = {0, 0, SW * 2, SH * 2};
    int done = 0, quit = 0, k;

    /* a whole multiple of 480x270, for an even font, about 3/4 of the screen's height */
    if (SDL_Init(SDL_INIT_VIDEO)) {
        SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable);
    }
    k = SDL_max(1, SDL_min(usable.w * 3 / 4 / SW, usable.h * 3 / 4 / SH));
    if (!SDL_CreateWindowAndRenderer("Digimon World 2003", SW * k, SH * k, SDL_WINDOW_RESIZABLE, &window, &s_r)) {
        native_log("first-run screen: %s", SDL_GetError());
        return 0;
    }
    SDL_SetRenderLogicalPresentation(s_r, SW, SH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawBlendMode(s_r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderVSync(s_r, 1);
    s_lang = system_lang();
    while (!done && !quit) {
        SDL_Event e;
        int browse = 0;

        while (SDL_PollEvent(&e)) {
            SDL_ConvertEventToRenderCoordinates(s_r, &e);
            if (e.type == SDL_EVENT_QUIT) {
                quit = 1;
            } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                quit = !s_menu;
                s_menu = 0;
            } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
                s_mouse.x = e.motion.x, s_mouse.y = e.motion.y;
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                s_mouse.x = e.button.x, s_mouse.y = e.button.y;
                if (s_menu) {
                    for (int i = 0; i < LANGS; i++) {
                        SDL_FRect r = item(i);
                        if (over(&r)) {
                            s_lang = i;
                        }
                    }
                    s_menu = 0;
                } else if (over(&PICKER)) {
                    s_menu = 1;
                } else {
                    browse = over(&ZONE);
                }
            } else if (e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) {
                browse = !s_menu;
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
                s_menu = 0;
                SDL_CreateDirectory(data);
                s_error = native_extract(s_picked, data, progress);
                s_percent = -1;
                done = s_error == E_NONE;
            }
        }
        draw();
    }
    SDL_DestroyRenderer(s_r);
    SDL_DestroyWindow(window);
    return done;
}
