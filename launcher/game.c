/* Starts the game next to the launcher and hears when it ends. Closing the launcher leaves the game running. */
#include "launcher.h"

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
    s_game = process_start(args, SDL_GetBasePath(), false);
    return s_game != NULL;
}

bool game_running(void) { return s_game != NULL; }

bool game_ended(int *code)
{
    if (!s_game || !SDL_WaitProcess(s_game, false, code)) {
        return false;
    }
    SDL_DestroyProcess(s_game);
    s_game = NULL;
    return true;
}
