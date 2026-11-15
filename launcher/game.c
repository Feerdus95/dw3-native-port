/* Starts the game next to the launcher and hears when it ends. Closing the launcher leaves the game running. */
#include "launcher.h"
#include "sounds_version.h"

static SDL_Process *s_game;

static SDL_Process *process_start(const char *const *args, const char *cwd, bool hidden)
{
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, hidden);
    SDL_Process *p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!p) {
        native_log("can't start %s: %s", args[0], SDL_GetError());
    }
    return p;
}

/* SDL_DestroyProcess only lets go of it: the program goes on */
bool process_open(const char *const *args, const char *cwd)
{
    SDL_Process *p = process_start(args, cwd, false);
    if (p) {
        SDL_DestroyProcess(p);
    }
    return p != NULL;
}

int process_wait(const char *const *args, const char *cwd)
{
    int code = -1;
    SDL_Process *p = process_start(args, cwd, true);
    if (p) {
        SDL_WaitProcess(p, true, &code);
        SDL_DestroyProcess(p);
    }
    return code;
}

/* data/sounds/NAME holds SOUNDS_VERSION: version.txt after a full render, failed.txt after one that did not finish */
static bool sounds_mark(const char *name)
{
    char path[1300];
    SDL_snprintf(path, sizeof path, "%s/sounds/%s", path_data(), name);
    char *text = SDL_LoadFile(path, NULL);
    const bool yes = text && SDL_atoi(text) == SOUNDS_VERSION;
    SDL_free(text);
    return yes;
}

void sounds_render(void)
{
    char exe[1200], dir[1200];
    path_in_root(exe, sizeof exe, "dw2003.exe");
    SDL_snprintf(dir, sizeof dir, "%s/sounds", path_data());
    if (SDL_GetPathInfo(exe, NULL)) {
        const char *args[] = {exe, "--render-sounds", dir, NULL};
        process_wait(args, SDL_GetBasePath());
        if (!sounds_mark("version.txt")) { /* not retried at every start: only after a new SOUNDS_VERSION or an extraction */
            char path[1300], text[16];
            SDL_snprintf(path, sizeof path, "%s/failed.txt", dir);
            SDL_snprintf(text, sizeof text, "%d\n", SOUNDS_VERSION);
            SDL_SaveFile(path, text, SDL_strlen(text));
        }
    }
}

static int SDLCALL sounds_thread(void *unused)
{
    (void)unused;
    sounds_render();
    return 0;
}

void sounds_prepare(void)
{
    if (!sounds_mark("version.txt") && !sounds_mark("failed.txt")) {
        SDL_DetachThread(SDL_CreateThread(sounds_thread, "sounds", NULL));
    }
}

bool game_start(void)
{
    char path[1200];
    path_in_root(path, sizeof path, "dw2003.exe");
    if (!SDL_GetPathInfo(path, NULL)) {
        native_log("no %s", path);
        return false;
    }
    /* so that a game that finds no data reports it, instead of opening a launcher again */
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "DW3_FROM_LAUNCHER", "1", true);
    const char *args[] = {path, NULL};
    char log[1200];
    path_in_root(log, sizeof log, "log.txt");
    SDL_IOStream *out = SDL_IOFromFile(log, "w"); /* what the game reports, for Tools' log.txt */
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args);
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, SDL_GetBasePath());
    if (out) {
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_POINTER, out);
    }
    s_game = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (out) {
        SDL_CloseIO(out); /* the game keeps its own copy of the handle */
    }
    if (!s_game) {
        native_log("can't start %s: %s", path, SDL_GetError());
    }
    return s_game != NULL;
}

bool game_running(void) { return s_game != NULL; }

/* The machine in dw2003.exe's PE header, read once */
bool game_x64(void)
{
    static int s_x64 = -1;
    if (s_x64 < 0) {
        char path[1200];
        Uint32 pe = 0;
        Uint16 machine = 0;
        path_in_root(path, sizeof path, "dw2003.exe");
        SDL_IOStream *f = SDL_IOFromFile(path, "rb");
        s_x64 = f && SDL_SeekIO(f, 0x3C, SDL_IO_SEEK_SET) >= 0 && SDL_ReadU32LE(f, &pe) && SDL_SeekIO(f, pe + 4, SDL_IO_SEEK_SET) >= 0 &&
                SDL_ReadU16LE(f, &machine) && machine == 0x8664;
        if (f) {
            SDL_CloseIO(f);
        }
    }
    return s_x64 == 1;
}

bool game_ended(int *code)
{
    if (!s_game || !SDL_WaitProcess(s_game, false, code)) {
        return false;
    }
    SDL_DestroyProcess(s_game);
    s_game = NULL;
    return true;
}
