/* The folders, in the game's order (platform/main.c), the log, and ini files edited line by line:
   whatever isn't being changed stays as it is, comments and pad bindings included. */
#include "launcher.h"
#include <stdarg.h>

static char s_data[1200];

void path_in_root(char *out, int size, const char *name) { SDL_snprintf(out, size, "%s%s", SDL_GetBasePath(), name); }

void paths_init(void)
{
    char value[1100];
    const char *env = SDL_getenv("DW3_DATA");
    if (env) {
        SDL_strlcpy(s_data, env, sizeof s_data);
    } else if (ini_get("native.ini", "data", value, sizeof value)) {
        const bool absolute = value[0] == '/' || value[0] == '\\' || (value[0] && value[1] == ':');
        SDL_snprintf(s_data, sizeof s_data, "%s%s", absolute ? "" : SDL_GetBasePath(), value);
    } else {
        SDL_snprintf(s_data, sizeof s_data, "%s../data/SLES_039.36", SDL_GetBasePath());
        SDL_snprintf(s_data, sizeof s_data, SDL_GetPathInfo(s_data, NULL) ? "%s../data" : "%sdata", SDL_GetBasePath());
    }
}

const char *path_data(void) { return s_data; }

bool path_move_tree(const char *from, const char *to)
{
    int n = 0;
    char **names = SDL_GlobDirectory(from, "*", 0, &n);
    bool ok = names != NULL;
    for (int i = 0; ok && i < n; i++) {
        char a[1400], b[1400];
        SDL_PathInfo info;
        SDL_snprintf(a, sizeof a, "%s/%s", from, names[i]);
        SDL_snprintf(b, sizeof b, "%s/%s", to, names[i]);
        if (SDL_GetPathInfo(a, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
            ok = SDL_CreateDirectory(b) && path_move_tree(a, b) && SDL_RemovePath(a);
        } else {
            ok = SDL_RenamePath(a, b);
        }
    }
    SDL_free(names);
    return ok;
}

bool data_has_file(const char *name)
{
    char path[1300];
    SDL_snprintf(path, sizeof path, "%s/%s", s_data, name);
    return SDL_GetPathInfo(path, NULL);
}

/* extract.c's log, next to the exe */
void native_log(const char *fmt, ...)
{
    char path[1200], line[1400];
    va_list ap;
    va_start(ap, fmt);
    const int n = SDL_vsnprintf(line, sizeof line - 2, fmt, ap);
    va_end(ap);
    path_in_root(path, sizeof path, "launcher.log");
    SDL_IOStream *f = SDL_IOFromFile(path, "a");
    if (f) {
        SDL_WriteIO(f, line, SDL_min(n, (int)sizeof line - 3));
        SDL_WriteIO(f, "\n", 1);
        SDL_CloseIO(f);
    }
}

/* the line's key, as the game reads it: not a comment, and followed by = */
static bool line_key(const char *line, char *key)
{
    return line[0] != '#' && line[0] != ';' && SDL_sscanf(line, " %31[^= ] =", key) == 1;
}

bool ini_get(const char *file, const char *key, char *value, int size)
{
    char path[1200], line[1300], k[32], format[32];
    bool found = false;
    path_in_root(path, sizeof path, file);
    char *text = SDL_LoadFile(path, NULL), *p = text;
    SDL_snprintf(format, sizeof format, " %%31[^= ] = %%%d[^\r\n]", size - 1);
    while (p && *p) {
        char *end = SDL_strchr(p, '\n');
        const size_t n = end ? (size_t)(end - p) + 1 : SDL_strlen(p);
        SDL_strlcpy(line, p, SDL_min(n + 1, sizeof line));
        if (line_key(line, k) && !SDL_strcasecmp(k, key) && SDL_sscanf(line, format, k, value) == 2) {
            found = true; /* the last one wins, as in the game */
        }
        p += n;
    }
    SDL_free(text);
    return found;
}

char *ini_apply(const char *in, IniEdit *edits, int count)
{
    size_t room = SDL_strlen(in) + 64;
    for (int i = 0; i < count; i++) {
        room += SDL_strlen(edits[i].key) + (edits[i].value ? SDL_strlen(edits[i].value) : 0) + 8;
        edits[i].done = false;
    }
    const char *eol = SDL_strstr(in, "\r\n") ? "\r\n" : "\n";
    char *out = SDL_malloc(room * 2), *o = out;
    const char *p = in;
    if (!out) {
        return NULL;
    }
    while (*p) {
        const char *end = SDL_strchr(p, '\n');
        const size_t n = end ? (size_t)(end - p) + 1 : SDL_strlen(p);
        char line[1300], key[32];
        int edit = -1;
        if (n < sizeof line) {
            SDL_strlcpy(line, p, n + 1);
            for (int i = 0; i < count && line_key(line, key); i++) {
                if (!SDL_strcasecmp(key, edits[i].key)) {
                    edit = i;
                }
            }
        }
        if (edit < 0) {
            SDL_memcpy(o, p, n);
            o += n;
        } else {
            edits[edit].done = true;
            if (edits[edit].value) {
                o += SDL_snprintf(o, room * 2 - (size_t)(o - out), "%s = %s%s", edits[edit].key, edits[edit].value, end ? eol : "");
            }
        }
        p += n;
    }
    for (int i = 0; i < count; i++) {
        if (!edits[i].done && edits[i].value) {
            if (o > out && o[-1] != '\n') {
                o += SDL_snprintf(o, room * 2 - (size_t)(o - out), "%s", eol);
            }
            o += SDL_snprintf(o, room * 2 - (size_t)(o - out), "%s = %s%s", edits[i].key, edits[i].value, eol);
        }
    }
    *o = 0;
    return out;
}

/* the edits on the file as it is now, so that what the game wrote since it was read stays; through a temporary file */
bool ini_write(const char *file, IniEdit *edits, int count)
{
    char path[1200], tmp[1300];
    path_in_root(path, sizeof path, file);
    SDL_snprintf(tmp, sizeof tmp, "%s.tmp", path);
    char *in = SDL_LoadFile(path, NULL);
    char *out = ini_apply(in ? in : "", edits, count);
    const bool ok = out && SDL_SaveFile(tmp, out, SDL_strlen(out)) && SDL_RenamePath(tmp, path);
    if (!ok) {
        native_log("can't write %s: %s", path, SDL_GetError());
        SDL_RemovePath(tmp);
    }
    SDL_free(in);
    SDL_free(out);
    return ok;
}
