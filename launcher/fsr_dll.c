/* FSR 3.1's two DLLs from AMD's FidelityFX SDK (MIT), fetched when the player asks: from the SDK's tag v2.4.0 on GitHub, next to the
   game, each checked by its SHA-256. The port carries none of AMD's binaries. */
#include <windows.h> /* before launcher.h, whose L and enums its headers would trip on */
#include <bcrypt.h>
#include "launcher.h"

#define BASE "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/v2.4.0/Kits/FidelityFX/signedbin/"

static const struct { const char *name, *sha256; } FILES[] = {
    {"amd_fidelityfx_loader_dx12.dll", "e2191d3c0828be45eb8affd4e3ec0378f2293adc345b772a7170edcf3b021ecb"},
    {"amd_fidelityfx_upscaler_dx12.dll", "aad4fccccb29084a655f9a4f9f613bd22c8c03fb4a0f8840148cc6543b4ccace"},
};
enum { N = SDL_arraysize(FILES) };

static SDL_Thread *s_thread;
static SDL_AtomicInt s_done, s_stop, s_percent;
static signed char s_valid[N]; /* dll_valid's, per file: filled by fsr_dll_have before the thread starts, which only reads it */

typedef struct { SDL_IOStream *io; BCRYPT_HASH_HANDLE hash; } Sink;

static bool sink(const void *data, int size, void *ctx)
{
    Sink *k = ctx;
    return !SDL_GetAtomicInt(&s_stop) && SDL_WriteIO(k->io, data, size) == (size_t)size && BCryptHashData(k->hash, (PUCHAR)data, size, 0) == 0;
}

static void file_path(int i, char *out, int size, const char *tail)
{
    char name[96];
    SDL_snprintf(name, sizeof name, "%s%s", FILES[i].name, tail);
    path_in_root(out, size, name);
}

/* one file: downloaded beside its place, kept only when the hash is AMD's. S_FSR_DONE, or why not */
static int fetch(int i)
{
    char url[300], part[1300], dest[1300], hex[65];
    unsigned char sum[32];
    BCRYPT_ALG_HANDLE alg = NULL;
    Sink k = {0};
    bool ok = false;

    SDL_snprintf(url, sizeof url, BASE "%s", FILES[i].name);
    file_path(i, part, sizeof part, ".part");
    file_path(i, dest, sizeof dest, "");
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 && BCryptCreateHash(alg, &k.hash, NULL, 0, NULL, 0, 0) == 0) {
        k.io = SDL_IOFromFile(part, "wb");
        ok = k.io && net_get(url, sink, &k, &s_percent);
        ok = k.io && SDL_CloseIO(k.io) && ok;
        ok = ok && BCryptFinishHash(k.hash, sum, sizeof sum, 0) == 0;
    }
    if (k.hash) {
        BCryptDestroyHash(k.hash);
    }
    if (alg) {
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    if (ok) {
        for (int b = 0; b < 32; b++) {
            SDL_snprintf(hex + 2 * b, 3, "%02x", sum[b]);
        }
        ok = SDL_strcmp(hex, FILES[i].sha256) == 0;
        if (!ok) {
            native_log("%s: its hash is %s, not the one expected", url, hex);
        }
    }
    if (!ok || !SDL_RenamePath(part, dest)) {
        const bool stopped = SDL_GetAtomicInt(&s_stop);
        native_log(stopped ? "%s: cancelled" : "%s: can't get it", url);
        SDL_RemovePath(part);
        return stopped ? S_T_CANCELLED : S_T_FAILED;
    }
    return S_FSR_DONE;
}

static int SDLCALL thread(void *unused)
{
    (void)unused;
    int result = S_FSR_DONE;
    for (int i = 0; i < N && result == S_FSR_DONE; i++) {
        if (s_valid[i] <= 0) { /* missing or not AMD's: fetched, over it */
            result = fetch(i);
        }
    }
    SDL_SetAtomicInt(&s_done, 1);
    return result;
}

bool fsr_dll_have(void)
{
    char dest[1300];
    bool all = true;
    for (int i = 0; i < N; i++) { /* each one checked: the thread fetches only the ones that aren't right */
        file_path(i, dest, sizeof dest, "");
        all = dll_valid(&s_valid[i], dest, FILES[i].sha256) && all;
    }
    return all;
}

void fsr_dll_activate(void)
{
    if (s_thread) {
        SDL_SetAtomicInt(&s_stop, 1);
    } else if (tools_busy() || port_updating()) {
        g_app.note = L->s[S_T_BUSY];
    } else if (fsr_dll_have()) {
        g_app.note = L->s[S_FSR_HAVE];
    } else {
        SDL_SetAtomicInt(&s_done, 0);
        SDL_SetAtomicInt(&s_stop, 0);
        SDL_SetAtomicInt(&s_percent, 0);
        g_app.note = NULL;
        s_thread = SDL_CreateThread(thread, "fsr dll", NULL);
    }
}

void fsr_dll_value(char *out, int size)
{
    if (s_thread) {
        SDL_snprintf(out, size, L->s[S_T_DOWNLOADING], SDL_GetAtomicInt(&s_percent));
    } else {
        SDL_strlcpy(out, L->s[fsr_dll_have() ? S_FSR_HAVE : S_T_GET], size);
    }
}

bool fsr_dll_busy(void) { return s_thread != NULL; }

void fsr_dll_poll(void)
{
    if (s_thread && SDL_GetAtomicInt(&s_done)) {
        int result = S_T_FAILED;
        SDL_WaitThread(s_thread, &result);
        s_thread = NULL;
        SDL_memset(s_valid, 0, sizeof s_valid); /* the files may have changed: checked again, once */
        g_app.note = L->s[result];
    }
}

/* closing the launcher: the download stops and its half file goes */
void fsr_dll_shutdown(void)
{
    if (s_thread) {
        SDL_SetAtomicInt(&s_stop, 1);
        SDL_WaitThread(s_thread, NULL);
        s_thread = NULL;
    }
}
