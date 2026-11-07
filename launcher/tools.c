/* The model viewer and the field editor: found if they're there, else downloaded next to the launcher, set up to
   read the port's data, and started. Their releases unpack to a folder of their own, whose ../data is the port's.
   Nothing here deletes or stops anything but its own download. */
#include "launcher.h"
#include "extract.h"
#include "tools.h"

const ToolInfo TOOLS[TOOLS_COUNT] = {
    {"Feerdus95/dw3-model-viewer", "dw3-model-viewer", "dw3mv.exe", "../../viewer/build/"},
    {"Feerdus95/dw3-field-editor", "dw3-field-editor", "dw3fe.exe", "../../field-editor/build/"},
};

static SDL_Thread *s_thread;
static SDL_AtomicInt s_done;
SDL_AtomicInt s_stop, s_percent, s_phase;
static int s_tool;
static char s_image[1200];
bool s_faked;
char s_update[TOOLS_COUNT][32];
static bool s_updating;

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
    if (s_updating || !tool_find(s_tool, exe, sizeof exe)) {
        result = tool_install(s_tool);
    }
    if (s_updating) {
        result = result == S_T_READY ? S_T_UPDATED : result; /* set up already */
    } else if (result == S_T_READY && !SDL_GetAtomicInt(&s_stop) && tool_find(s_tool, exe, sizeof exe)) {
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
    if (port_updating()) {
        g_app.note = L->s[S_T_BUSY];
        return;
    }
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

int tool_update_index(void)
{
    if (s_thread && s_updating) {
        return s_tool;
    }
    if (release_busy()) {
        return -1; /* the check is writing them */
    }
    for (int i = 0; i < TOOLS_COUNT; i++) {
        if (s_update[i][0]) {
            return i;
        }
    }
    return -1;
}

bool tool_update_has(int i) { return s_update[i][0] && !release_busy() && !s_thread; }

const char *tool_update_tag(int i) { return s_update[i]; }

bool tool_update_focus(int i) { return g_app.btn && tool_update_has(i); }

void tool_update_activate(int i)
{
    if (port_updating() || s_thread) {
        g_app.note = L->s[S_T_BUSY];
    } else if (s_faked) { /* --fake-tool-update: nothing is downloaded */
        s_update[i][0] = 0;
        g_app.btn = 0;
        g_app.note = L->s[S_T_UPDATED];
    } else if (s_update[i][0]) {
        s_updating = true;
        start(i, "");
    }
}

void tool_update_fake(const char *tag)
{
    s_faked = true;
    SDL_strlcpy(s_update[VIEWER], tag, sizeof s_update[VIEWER]);
}

bool tool_update_now(int i)
{
    tools_check();
    if (!s_update[i][0]) {
        return false;
    }
    native_log("updating %s to %s", TOOLS[i].folder, s_update[i]);
    return tool_install(i) == S_T_READY;
}

bool tools_busy(void) { return s_thread != NULL; }

static void finish(void)
{
    int result = S_T_FAILED;
    SDL_WaitThread(s_thread, &result);
    s_thread = NULL;
    if (result == S_T_UPDATED) {
        s_update[s_tool][0] = 0;
    }
    s_updating = false;
    g_app.btn = 0;
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
