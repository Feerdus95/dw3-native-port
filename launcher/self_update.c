/* The port's own update, when the player asks for it: the release's zip is downloaded and unpacked beside the port, the
   programs Windows keeps open are set aside as .old (a running exe can be renamed, not overwritten), the rest goes over
   the files here, and the new launcher is started. native.ini, data/, saves/ and the tools aren't in the zip, so they stay.
   The game is never stopped: if it's running, the player is asked to close it. */
#include "launcher.h"
#include "tools.h"

static SDL_Thread *s_thread;
static SDL_AtomicInt s_done;
static char s_note[160];
static const char *const SWAP[] = {"dw2003.exe", "SDL3.dll", "dw3launcher.exe"}; /* the game first: it's the one likely open */

static void swap_path(char *out, int size, int i, const char *ext)
{
    char name[64];
    SDL_snprintf(name, sizeof name, "%s%s", SWAP[i], ext);
    path_in_root(out, size, name);
}

/* the last update's leftovers, once nothing holds them */
void port_cleanup(void)
{
    for (int i = 0; i < (int)SDL_arraysize(SWAP); i++) {
        char p[1300];
        swap_path(p, sizeof p, i, ".old");
        SDL_RemovePath(p);
    }
}

/* back to what was there, for the programs whose new version didn't arrive */
static void put_back(int count)
{
    for (int i = 0; i < count; i++) {
        char now[1300], old[1300];
        swap_path(now, sizeof now, i, "");
        swap_path(old, sizeof old, i, ".old");
        if (!SDL_GetPathInfo(now, NULL)) {
            SDL_RenamePath(old, now);
        }
    }
}

/* ponytail: a failed unpacking stays in .update-tmp and a retry unpacks over it; only a half-done update of another
   version could leave a stray file. Delete the folder by hand if it ever comes to that */
static int SDLCALL update_thread(void *unused)
{
    (void)unused;
    char tmp[1200], url[512], a[1300], b[1300];
    path_in_root(tmp, sizeof tmp, ".update-tmp");
    release_zip_url(url, sizeof url);
    int result = fetch_unpack(url, tmp);
    SDL_snprintf(a, sizeof a, "%s/dw2003.exe", tmp);
    SDL_snprintf(b, sizeof b, "%s/dw3launcher.exe", tmp);
    if (result == S_T_READY && !(SDL_GetPathInfo(a, NULL) && SDL_GetPathInfo(b, NULL))) {
        native_log("%s: no dw2003.exe and dw3launcher.exe in the zip", url);
        result = S_T_FAILED;
    }
    int set_aside = 0;
    for (; result == S_T_READY && set_aside < (int)SDL_arraysize(SWAP); set_aside++) {
        swap_path(a, sizeof a, set_aside, "");
        swap_path(b, sizeof b, set_aside, ".old");
        if (SDL_GetPathInfo(a, NULL) && !SDL_RenamePath(a, b)) {
            native_log("can't set %s aside: %s", a, SDL_GetError());
            result = S_U_CLOSE_GAME;
        }
    }
    if (result == S_T_READY) {
        path_in_root(a, sizeof a, "");
        if (!path_move_tree(tmp, a)) {
            native_log("can't move %s over %s: %s", tmp, a, SDL_GetError());
            result = S_T_FAILED;
        } else {
            SDL_RemovePath(tmp);
        }
    }
    if (result != S_T_READY) {
        put_back(set_aside);
    }
    SDL_SetAtomicInt(&s_done, 1);
    return result;
}

bool port_updating(void) { return s_thread != NULL; }

void port_update_activate(void)
{
    if (s_thread) {
        SDL_SetAtomicInt(&s_stop, 1);
    } else if (!release_tag()) {
        return;
    } else if (tools_busy()) { /* they share the download's counters */
        g_app.note = L->s[S_T_BUSY];
    } else if (game_running()) {
        g_app.note = L->s[S_U_CLOSE_GAME];
    } else {
        SDL_SetAtomicInt(&s_done, 0);
        SDL_SetAtomicInt(&s_stop, 0);
        SDL_SetAtomicInt(&s_percent, 0);
        SDL_SetAtomicInt(&s_phase, P_DOWNLOAD);
        s_thread = SDL_CreateThread(update_thread, "port", NULL);
    }
}

/* the main loop: progress while it goes; at its end, the new launcher or why not */
void port_update_poll(void)
{
    if (!s_thread) {
        return;
    }
    if (!SDL_GetAtomicInt(&s_done)) {
        if (SDL_GetAtomicInt(&s_phase) == P_DOWNLOAD) {
            SDL_snprintf(s_note, sizeof s_note, L->s[S_T_DOWNLOADING], SDL_GetAtomicInt(&s_percent));
            g_app.note = s_note;
        } else {
            g_app.note = L->s[S_T_UNPACKING];
        }
        return;
    }
    int result = S_T_FAILED;
    SDL_WaitThread(s_thread, &result);
    s_thread = NULL;
    if (result == S_T_READY) {
        char exe[1300];
        const char *args[] = {exe, NULL};
        path_in_root(exe, sizeof exe, "dw3launcher.exe");
        if (process_open(args, SDL_GetBasePath())) {
            g_app.quit = true;
            return;
        }
        result = S_T_NO_START;
    }
    g_app.note = L->s[result];
}

void port_update_shutdown(void)
{
    if (s_thread) {
        SDL_SetAtomicInt(&s_stop, 1);
        SDL_WaitThread(s_thread, NULL);
        s_thread = NULL;
    }
}
