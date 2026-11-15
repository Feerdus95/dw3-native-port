/* DirectStorage's two DLLs from Microsoft's NuGet package (MIT), fetched when the player asks: the package is a zip, unpacked in a
   folder of its own, and each DLL is kept only if its SHA-256 is the one of version 1.3.0. They go next to the game; the port carries none. */
#include <windows.h> /* before launcher.h, whose L and enums its headers would trip on */
#include <bcrypt.h>
#include "launcher.h"
#include "tools.h"

#define URL "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.DirectStorage/1.3.0"

static const struct { const char *name, *sha256; } FILES[] = {
    {"dstorage.dll", "cae65787f61514bec6f012f44546e68b19e39d8854a9ab52af813370d44b272a"},
    {"dstoragecore.dll", "cf85b19a92a8276f8baca0db9bbcd60735bad50768deb303d310e84d5f4fdd11"},
};
enum { N = SDL_arraysize(FILES) };

static SDL_Thread *s_thread;
static SDL_AtomicInt s_done;
static signed char s_valid[N]; /* dll_valid's, per file: written by the main thread only */

bool ds_dll_have(void)
{
    char dest[1300];
    for (int i = 0; i < N; i++) {
        path_in_root(dest, sizeof dest, FILES[i].name);
        if (!dll_valid(&s_valid[i], dest, FILES[i].sha256)) {
            return false;
        }
    }
    return true;
}

static int SDLCALL thread(void *unused)
{
    (void)unused;
    char tmp[1200], from[1400], dest[1300];
    path_in_root(tmp, sizeof tmp, ".ds-tmp");
    int result = fetch_unpack(URL, tmp);
    for (int i = 0; i < N && result == S_T_READY; i++) {
        SDL_snprintf(from, sizeof from, "%s/native/bin/x64/%s", tmp, FILES[i].name);
        path_in_root(dest, sizeof dest, FILES[i].name);
        if (!sha256_is(from, FILES[i].sha256)) {
            native_log("%s: not the file expected", from);
            result = S_T_FAILED;
        } else if (!SDL_RenamePath(from, dest)) {
            native_log("can't move %s to %s: %s", from, dest, SDL_GetError());
            result = S_T_FAILED;
        }
    }
    path_remove_tree(tmp);
    SDL_SetAtomicInt(&s_done, 1);
    return result == S_T_READY ? S_DS_DONE : result;
}

/* ponytail: shares the tools' stop flag and percent; a tool's install started during this one would reset them, and the busy check below only goes one way */
void ds_dll_activate(void)
{
    if (s_thread) {
        SDL_SetAtomicInt(&s_stop, 1);
    } else if (tools_busy() || port_updating() || fsr_dll_busy() || dlss_dll_busy()) {
        g_app.note = L->s[S_T_BUSY];
    } else if (ds_dll_have()) {
        g_app.note = L->s[S_FSR_HAVE];
    } else {
        SDL_SetAtomicInt(&s_done, 0);
        SDL_SetAtomicInt(&s_stop, 0);
        SDL_SetAtomicInt(&s_percent, 0);
        g_app.note = NULL;
        s_thread = SDL_CreateThread(thread, "ds dll", NULL);
    }
}

void ds_dll_value(char *out, int size)
{
    if (s_thread) {
        SDL_snprintf(out, size, L->s[S_T_DOWNLOADING], SDL_GetAtomicInt(&s_percent));
    } else {
        SDL_strlcpy(out, L->s[ds_dll_have() ? S_FSR_HAVE : S_T_GET], size);
    }
}

bool ds_dll_busy(void) { return s_thread != NULL; }

void ds_dll_poll(void)
{
    if (s_thread && SDL_GetAtomicInt(&s_done)) {
        int result = S_T_FAILED;
        SDL_WaitThread(s_thread, &result);
        s_thread = NULL;
        SDL_memset(s_valid, 0, sizeof s_valid); /* the files may have changed: checked again, once */
        g_app.note = L->s[result];
    }
}

void ds_dll_shutdown(void)
{
    if (s_thread) {
        SDL_SetAtomicInt(&s_stop, 1);
        SDL_WaitThread(s_thread, NULL);
        s_thread = NULL;
    }
}
