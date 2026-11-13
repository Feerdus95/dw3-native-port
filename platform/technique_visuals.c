/* Optional player-technique visual choices. The source map is copied beside
   the EXE by build.py. Loading it does not alter the disc's original files. */
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "../include/native.h"

#define MAX_TECHNIQUE 0xB7
#define VIS_FIELDS 8

static const char *const field_names[VIS_FIELDS] = {
    "projectile_model", "launch_sprite", "launch_sound",
    "impact_sprite", "impact_sound", "stage", "launch_sound_duration",
    "launch_timing"
};
static int values[MAX_TECHNIQUE + 1][VIS_FIELDS];
static unsigned char present[MAX_TECHNIQUE + 1][VIS_FIELDS];

static char *trim(char *text) {
    char *end;
    while (isspace((unsigned char)*text)) text++;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) *--end = 0;
    return text;
}

static int parse_number(const char *text, int *out) {
    char *end;
    long number = strtol(text, &end, 0);
    if (end == text || *trim(end) != 0 || number < 0 || number > 32767) return 0;
    *out = (int)number;
    return 1;
}

int native_visual_load(void) {
    char path[1200], line[256];
    unsigned char sections[MAX_TECHNIQUE + 1] = {0};
    FILE *file;
    int technique = 0, line_number = 0;

    memset(present, 0, sizeof present);
    snprintf(path, sizeof path, "%stechnique_visuals.ini", SDL_GetBasePath());
    file = fopen(path, "r");
    if (file == NULL) return 0; /* no map: every value stays original */

    while (fgets(line, sizeof line, file) != NULL) {
        char *p, *equal, *end;
        int field, number;
        line_number++;
        if (strchr(line, '\n') == NULL && !feof(file)) goto invalid;
        p = trim(line);
        if (*p == 0 || *p == '#' || *p == ';') continue;
        if (*p == '[') {
            if (strncmp(p, "[technique.", 11) != 0) goto invalid;
            number = (int)strtol(p + 11, &end, 0);
            if (end == p + 11 || *end != ']' || *trim(end + 1) != 0 ||
                number < 1 || number > MAX_TECHNIQUE || sections[number]) goto invalid;
            sections[number] = 1;
            technique = number;
            continue;
        }
        if (technique == 0 || (equal = strchr(p, '=')) == NULL) goto invalid;
        *equal = 0;
        p = trim(p);
        equal = trim(equal + 1);
        for (field = 0; field < VIS_FIELDS; field++) {
            if (strcmp(p, field_names[field]) == 0) break;
        }
        if (field == VIS_FIELDS || present[technique][field]) goto invalid;
        if (field == NATIVE_VIS_LAUNCH_TIMING) {
            if (strcmp(equal, "original") == 0) {
                number = NATIVE_LAUNCH_ORIGINAL;
            } else if (strcmp(equal, "aligned") == 0) {
                number = NATIVE_LAUNCH_ALIGNED;
            } else {
                goto invalid;
            }
        } else if (strcmp(equal, "none") == 0 &&
                   field != NATIVE_VIS_LAUNCH_SOUND_DURATION) {
            number = -1;
        } else if (!parse_number(equal, &number)) {
            goto invalid;
        }
        present[technique][field] = 1;
        values[technique][field] = number;
    }
    if (ferror(file)) goto invalid;
    fclose(file);
    return 1;

invalid:
    fclose(file);
    memset(present, 0, sizeof present);
    native_log("Invalid technique_visuals.ini at line %d; using original visuals", line_number);
    return 0;
}

int native_visual_get(int technique, int field, int original) {
    if (technique <= 0 || technique > MAX_TECHNIQUE || field < 0 || field >= VIS_FIELDS ||
        !present[technique][field]) return original;
    return values[technique][field];
}
