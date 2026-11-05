/* HTTPS GETs, on the caller's thread. Redirects are followed, as a release's download makes. */
#include <windows.h> /* before launcher.h, whose L and enums its headers would trip on */
#include <winhttp.h>
#include "launcher.h"

enum { CHUNK = 65536, TEXT_MOST = 8 << 20 };

bool net_get(const char *url, NetSink sink, void *ctx, SDL_AtomicInt *percent)
{
    wchar_t host[256], path[1024];
    const char *host_start = url + 8, *slash = SDL_strncmp(url, "https://", 8) ? NULL : SDL_strchr(host_start, '/');
    bool ok = false;
    if (!slash || slash - host_start >= (int)SDL_arraysize(host) ||
        !MultiByteToWideChar(CP_UTF8, 0, host_start, (int)(slash - host_start), host, SDL_arraysize(host)) ||
        !MultiByteToWideChar(CP_UTF8, 0, slash, -1, path, SDL_arraysize(path))) {
        return false;
    }
    host[slash - host_start] = 0;
    char *buffer = SDL_malloc(CHUNK);
    HINTERNET session = WinHttpOpen(L"dw3launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connect = session ? WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0) : NULL;
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;
    if (request && buffer) {
        DWORD status = 0, total = 0, size = sizeof status;
        WinHttpSetTimeouts(request, 10000, 10000, 10000, 10000);
        if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, NULL) &&
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) && status == 200) {
            DWORD got = 1, sum = 0;
            size = sizeof total;
            WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &total, &size, WINHTTP_NO_HEADER_INDEX);
            ok = true;
            while (ok && got > 0) {
                ok = WinHttpReadData(request, buffer, CHUNK, &got) && (got == 0 || sink(buffer, (int)got, ctx));
                sum += got;
                if (percent && total) {
                    SDL_SetAtomicInt(percent, (int)((Uint64)sum * 100 / total));
                }
            }
            ok = ok && (!total || sum == total); /* a connection cut short isn't a file */
        }
    }
    if (request) {
        WinHttpCloseHandle(request);
    }
    if (connect) {
        WinHttpCloseHandle(connect);
    }
    if (session) {
        WinHttpCloseHandle(session);
    }
    SDL_free(buffer);
    return ok;
}

typedef struct { char *data; size_t len; } Text_;

static bool to_memory(const void *data, int size, void *ctx)
{
    Text_ *t = ctx;
    char *grown = t->len + size < TEXT_MOST ? SDL_realloc(t->data, t->len + size + 1) : NULL;
    if (!grown) {
        return false;
    }
    SDL_memcpy(grown + t->len, data, size);
    t->data = grown;
    t->len += size;
    t->data[t->len] = 0;
    return true;
}

char *net_text(const char *url)
{
    Text_ t = {0};
    if (!net_get(url, to_memory, &t, NULL)) {
        SDL_free(t.data);
        return NULL;
    }
    return t.data;
}
