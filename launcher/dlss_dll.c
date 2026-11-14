/* DLSS's four DLLs (NVIDIA Streamline and its DLSS runtime), fetched when the player asks: the SDK's release on GitHub is a zip, unpacked in a
   folder of its own, and each DLL is kept only if its SHA-256 is the one of version 2.14.16. They go next to the game; the port carries none. */
#include <windows.h> /* before launcher.h, whose L and enums its headers would trip on */
#include <bcrypt.h>
#include "launcher.h"
#include "tools.h"

#define URL "https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.14.16/streamline-sdk-v2.14.16.zip"

static const struct { const char *name, *sha256; } FILES[] = {
    {"sl.interposer.dll", "b1d7395aebd7b9722841d64dcf7433680020b6a8e421f851c16d5ba0c2182edf"},
    {"sl.common.dll", "38eea3b4209afc455636ea372b5933d65fcc13becec6c84d101dcfc4d8648f75"},
    {"sl.dlss.dll", "18871eea87c90345da97d8109faa81ea7d5840e8d99b51b55b0e46cfa177f84e"},
    {"nvngx_dlss.dll", "d00b5b0ab29c3ca101b70c3a5ecb3a01b7ef33766a3383a28a18623083e6123e"},
};
enum { N = SDL_arraysize(FILES) };

static SDL_Thread *s_thread;
static SDL_AtomicInt s_done;

static bool sha256_is(const char *path, const char *want)
{
    size_t size = 0;
    unsigned char *data = SDL_LoadFile(path, &size), sum[32];
    char hex[65];
    bool ok = false;
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;

    if (data && BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 && BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) == 0 &&
        BCryptHashData(hash, data, (ULONG)size, 0) == 0 && BCryptFinishHash(hash, sum, sizeof sum, 0) == 0) {
        for (int b = 0; b < 32; b++) {
            SDL_snprintf(hex + 2 * b, 3, "%02x", sum[b]);
        }
        ok = SDL_strcmp(hex, want) == 0;
    }
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (alg) {
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    SDL_free(data);
    return ok;
}

bool dlss_dll_have(void)
{
    char dest[1300];
    for (int i = 0; i < N; i++) {
        path_in_root(dest, sizeof dest, FILES[i].name);
        if (!SDL_GetPathInfo(dest, NULL)) {
            return false;
        }
    }
    return true;
}

static int SDLCALL thread(void *unused)
{
    (void)unused;
    char tmp[1200], from[1400], dest[1300];
    path_in_root(tmp, sizeof tmp, ".dlss-tmp");
    int result = fetch_unpack(URL, tmp);
    for (int i = 0; i < N && result == S_T_READY; i++) {
        SDL_snprintf(from, sizeof from, "%s/bin/x64/%s", tmp, FILES[i].name);
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
    return result == S_T_READY ? S_DLSS_DONE : result;
}

/* ponytail: shares the tools' stop flag and percent; a tool's install started during this one would reset them, and the busy check below only goes one way */
void dlss_dll_activate(void)
{
    if (s_thread) {
        SDL_SetAtomicInt(&s_stop, 1);
    } else if (tools_busy() || port_updating() || fsr_dll_busy() || ds_dll_busy()) {
        g_app.note = L->s[S_T_BUSY];
    } else if (dlss_dll_have()) {
        g_app.note = L->s[S_FSR_HAVE];
    } else {
        SDL_SetAtomicInt(&s_done, 0);
        SDL_SetAtomicInt(&s_stop, 0);
        SDL_SetAtomicInt(&s_percent, 0);
        g_app.note = NULL;
        s_thread = SDL_CreateThread(thread, "ds dll", NULL);
    }
}

void dlss_dll_value(char *out, int size)
{
    if (s_thread) {
        SDL_snprintf(out, size, L->s[S_T_DOWNLOADING], SDL_GetAtomicInt(&s_percent));
    } else {
        SDL_strlcpy(out, L->s[dlss_dll_have() ? S_FSR_HAVE : S_T_GET], size);
    }
}

bool dlss_dll_busy(void) { return s_thread != NULL; }

void dlss_dll_poll(void)
{
    if (s_thread && SDL_GetAtomicInt(&s_done)) {
        int result = S_T_FAILED;
        SDL_WaitThread(s_thread, &result);
        s_thread = NULL;
        g_app.note = L->s[result];
    }
}

void dlss_dll_shutdown(void)
{
    if (s_thread) {
        SDL_SetAtomicInt(&s_stop, 1);
        SDL_WaitThread(s_thread, NULL);
        s_thread = NULL;
    }
}
