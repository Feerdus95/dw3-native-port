/* The model viewer and the field editor: found if they're there, else downloaded next to the launcher, set up to
   read the port's data, and started. Their releases unpack to a folder of their own, whose ../data is the port's.
   Nothing here deletes or stops anything but its own download. */
#include <windows.h> /* before launcher.h, whose L and enums its headers would trip on */
#include "launcher.h"
#include "extract.h"

enum { VIEWER, EDITOR, TOOLS_COUNT };
enum { P_DOWNLOAD, P_UNPACK, P_SETUP };
static const struct { const char *repo, *folder, *exe, *dev; } TOOLS[TOOLS_COUNT] = {
    {"Feerdus95/dw3-model-viewer", "dw3-model-viewer", "dw3mv.exe", "../../viewer/build/"},
    {"Feerdus95/dw3-field-editor", "dw3-field-editor", "dw3fe.exe", "../../field-editor/build/"},
};

static SDL_Thread *s_thread;
static SDL_AtomicInt s_done, s_stop, s_percent, s_phase;
static int s_tool;
static char s_image[1200];

static bool exists(const char *path) { return SDL_GetPathInfo(path, NULL); }

/* in its own folder here, this folder, its folder beside this one, or its build next to the port's source */
static bool tool_find(int i, char *out, int size)
{
    const char *base = SDL_GetBasePath(), *f = TOOLS[i].folder, *e = TOOLS[i].exe;
    SDL_snprintf(out, size, "%s%s/%s", base, f, e);
    if (exists(out)) {
        return true;
    }
    SDL_snprintf(out, size, "%s%s", base, e);
    if (exists(out)) {
        return true;
    }
    SDL_snprintf(out, size, "%s../%s/%s", base, f, e);
    if (exists(out)) {
        return true;
    }
    SDL_snprintf(out, size, "%s%s%s", base, TOOLS[i].dev, e);
    return exists(out);
}

static void folder_of(const char *exe, char *out, int size)
{
    SDL_strlcpy(out, exe, size);
    char *slash = SDL_max(SDL_strrchr(out, '/'), SDL_strrchr(out, '\\'));
    if (slash) {
        slash[1] = 0;
    }
}

/* what the viewer looks in: the folder it was told, its data/ or ../data */
static bool viewer_has_data(const char *exe)
{
    char dir[1200], path[1300];
    folder_of(exe, dir, sizeof dir);
    SDL_snprintf(path, sizeof path, "%sdata_dir.txt", dir);
    if (exists(path)) {
        return true;
    }
    SDL_snprintf(path, sizeof path, "%sdata/names.txt", dir);
    if (exists(path)) {
        return true;
    }
    SDL_snprintf(path, sizeof path, "%s../data/names.txt", dir);
    return exists(path);
}

static bool tool_ready(int i, const char *exe)
{
    return i == VIEWER ? viewer_has_data(exe) : data_has_file("stages/characters.json");
}

/* the first .zip for Windows in a release of `repo`, and only one of its own */
static bool pick_zip(const char *json, const char *repo, char *out, int size)
{
    static const char KEY[] = "\"browser_download_url\":\"";
    char prefix[128];
    SDL_snprintf(prefix, sizeof prefix, "https://github.com/%s/releases/download/", repo);
    for (const char *p = SDL_strstr(json, KEY); p; p = SDL_strstr(p, KEY)) {
        p += sizeof KEY - 1;
        const char *end = SDL_strchr(p, '"');
        if (end && end - p < size && end - p > 10 && !SDL_strncmp(p, prefix, SDL_strlen(prefix)) && !SDL_strncmp(end - 10, "-win64.zip", 10)) {
            SDL_strlcpy(out, p, (size_t)(end - p) + 1);
            return true;
        }
    }
    return false;
}

static bool to_file(const void *data, int size, void *ctx)
{
    return !SDL_GetAtomicInt(&s_stop) && SDL_WriteIO(ctx, data, size) == (size_t)size;
}

/* ponytail: a failed unpacking stays in .tools-tmp, which the next try overwrites; deleted by hand if it never comes */
static int install(int i)
{
    char tmp[1200], zip[1300], api[160], url[512], from[1300], to[1300], tar[MAX_PATH + 16];
    path_in_root(tmp, sizeof tmp, ".tools-tmp");
    SDL_snprintf(zip, sizeof zip, "%s/tool.zip", tmp);
    SDL_snprintf(api, sizeof api, "https://api.github.com/repos/%s/releases/latest", TOOLS[i].repo);
    char *json = net_text(api);
    const bool found = json && pick_zip(json, TOOLS[i].repo, url, sizeof url);
    SDL_free(json);
    if (!found || !SDL_CreateDirectory(tmp)) {
        native_log("no release of %s to download into %s", TOOLS[i].repo, tmp);
        return S_T_FAILED;
    }
    SDL_IOStream *file = SDL_IOFromFile(zip, "wb");
    bool ok = file && net_get(url, to_file, file, &s_percent);
    ok = file && SDL_CloseIO(file) && ok;
    if (!ok) {
        const bool stopped = SDL_GetAtomicInt(&s_stop);
        native_log(stopped ? "%s: cancelled" : "%s: can't download it", url);
        SDL_RemovePath(zip);
        SDL_RemovePath(tmp);
        return stopped ? S_T_CANCELLED : S_T_FAILED;
    }
    /* Windows' own tar, by its full path: the zip's in a folder of plain names, whatever the path above it */
    SDL_SetAtomicInt(&s_phase, P_UNPACK);
    GetSystemDirectoryA(tar, MAX_PATH);
    SDL_strlcat(tar, "\\tar.exe", sizeof tar);
    const char *args[] = {tar, "-xf", "tool.zip", NULL};
    const int code = process_wait(args, tmp);
    SDL_snprintf(from, sizeof from, "%s/%s/%s", tmp, TOOLS[i].folder, TOOLS[i].exe);
    if (code != 0 || !exists(from)) {
        native_log("%s: tar ended with %d, and no %s", zip, code, from);
        SDL_RemovePath(zip);
        return S_T_FAILED;
    }
    SDL_snprintf(from, sizeof from, "%s/%s", tmp, TOOLS[i].folder);
    path_in_root(to, sizeof to, TOOLS[i].folder);
    if (!SDL_RenamePath(from, to)) {
        native_log("can't move %s to %s: %s", from, to, SDL_GetError());
        SDL_RemovePath(zip);
        return S_T_FAILED;
    }
    SDL_RemovePath(zip);
    SDL_RemovePath(tmp);
    return S_T_READY;
}

static int names_progress(int percent)
{
    (void)percent;
    return !SDL_GetAtomicInt(&s_stop);
}

static bool viewer_point_at_data(const char *exe)
{
    char dir[1200], path[1300];
    folder_of(exe, dir, sizeof dir);
    SDL_snprintf(path, sizeof path, "%sdata_dir.txt", dir);
    return SDL_SaveFile(path, path_data(), SDL_strlen(path_data()));
}

static int setup(int i, const char *exe)
{
    if (i == VIEWER) {
        if (!data_has_file("names.txt")) {
            const int e = native_disc_names(s_image, path_data(), names_progress);
            if (e != E_NONE) {
                return e == E_CANCELLED ? S_T_CANCELLED : first_run_error(e);
            }
        }
        if (!viewer_has_data(exe) && !viewer_point_at_data(exe)) {
            native_log("can't point %s at %s", exe, path_data());
            return S_T_FAILED;
        }
    } else if (!data_has_file("stages/characters.json")) {
        /* the fields' tables, as the editor's README asks of the port */
        char game[1200];
        path_in_root(game, sizeof game, "dw2003.exe");
        const char *args[] = {game, "--dump-stages", "stages", NULL};
        const int code = process_wait(args, path_data());
        if (code != 0 || !data_has_file("stages/characters.json")) {
            native_log("%s --dump-stages ended with %d", game, code);
            return S_T_FAILED;
        }
    }
    return S_T_READY;
}

static int SDLCALL tool_thread(void *unused)
{
    (void)unused;
    char exe[1200];
    int result = S_T_READY;
    if (!tool_find(s_tool, exe, sizeof exe)) {
        result = install(s_tool);
    }
    if (result == S_T_READY && !SDL_GetAtomicInt(&s_stop) && tool_find(s_tool, exe, sizeof exe)) {
        SDL_SetAtomicInt(&s_phase, P_SETUP);
        result = setup(s_tool, exe);
    }
    SDL_SetAtomicInt(&s_done, 1);
    return result;
}

static void start(int i, const char *image)
{
    s_tool = i;
    SDL_strlcpy(s_image, image, sizeof s_image);
    SDL_SetAtomicInt(&s_done, 0);
    SDL_SetAtomicInt(&s_stop, 0);
    SDL_SetAtomicInt(&s_percent, 0);
    SDL_SetAtomicInt(&s_phase, P_DOWNLOAD);
    g_app.note = NULL;
    s_thread = SDL_CreateThread(tool_thread, "tool", NULL);
}

static void disc_chosen(const char *path)
{
    if (path[0] && !s_thread) {
        start(VIEWER, path);
    }
}

static void launch(int i, const char *exe)
{
    char dir[1200];
    folder_of(exe, dir, sizeof dir);
    const char *viewer_args[] = {exe, NULL}, *editor_args[] = {exe, "--data", path_data(), NULL};
    if (!process_open(i == VIEWER ? viewer_args : editor_args, dir)) {
        g_app.note = L->s[S_T_NO_START];
    }
}

void tool_activate(int i)
{
    char exe[1200];
    if (s_thread) {
        if (i == s_tool) {
            SDL_SetAtomicInt(&s_stop, 1);
        } else {
            g_app.note = L->s[S_T_BUSY];
        }
        return;
    }
    const bool found = tool_find(i, exe, sizeof exe);
    if (found && tool_ready(i, exe)) {
        launch(i, exe);
    } else if (i == VIEWER && !data_has_file("names.txt") && !(found && viewer_has_data(exe))) {
        /* a game read before the extraction kept the disc's file names: those the viewer needs are only on the disc */
        g_app.note = L->s[S_T_NEED_DISC];
        disc_ask(disc_chosen);
    } else {
        start(i, "");
    }
}

/* what the row says: what pressing it will do, or how far it's gone */
void tool_value(int i, char *out, int size)
{
    char exe[1200];
    if (s_thread && i == s_tool) {
        const int phase = SDL_GetAtomicInt(&s_phase);
        if (phase == P_DOWNLOAD) {
            SDL_snprintf(out, size, L->s[S_T_DOWNLOADING], SDL_GetAtomicInt(&s_percent));
        } else {
            SDL_strlcpy(out, L->s[phase == P_UNPACK ? S_T_UNPACKING : S_T_PREPARING], size);
        }
        return;
    }
    const bool found = tool_find(i, exe, sizeof exe);
    SDL_strlcpy(out, L->s[!found ? S_T_GET : tool_ready(i, exe) ? S_T_OPEN : S_T_SETUP], size);
}

bool tools_busy(void) { return s_thread != NULL; }

static void finish(void)
{
    int result = S_T_FAILED;
    SDL_WaitThread(s_thread, &result);
    s_thread = NULL;
    g_app.note = L->s[result];
}

void tools_poll(void)
{
    if (s_thread && SDL_GetAtomicInt(&s_done)) {
        finish();
    }
}

/* closing the launcher: the download stops, what's half unpacked finishes */
void tools_shutdown(void)
{
    if (s_thread) {
        SDL_SetAtomicInt(&s_stop, 1);
        finish();
    }
}

int tools_selftest(void)
{
    char out[256] = "";
    int bad = 0;
    static const char JSON[] =
        "{\"assets\":[{\"browser_download_url\":\"https://example.com/x/releases/download/v1/a-win64.zip\"},"
        "{\"browser_download_url\":\"https://github.com/o/r/releases/download/v1/a-src.zip\"},"
        "{\"browser_download_url\":\"https://github.com/o/r/releases/download/v1/a-win64.zip\"}]}";
    bad += !pick_zip(JSON, "o/r", out, sizeof out) || SDL_strcmp(out, "https://github.com/o/r/releases/download/v1/a-win64.zip");
    bad += pick_zip(JSON, "o/other", out, sizeof out); /* only the tool's own repo */
    bad += pick_zip("not json", "o/r", out, sizeof out);
    return bad;
}
