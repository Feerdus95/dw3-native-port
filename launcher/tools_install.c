/* A tool's download: which release is newer than what's installed, and putting it in the tool's folder. */
#include <windows.h> /* before launcher.h, whose L and enums its headers would trip on */
#include "launcher.h"
#include "tools.h"

static bool exists(const char *path) { return SDL_GetPathInfo(path, NULL); }

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

/* the version the launcher put in the tool's own folder */
static void stamp_path(int i, char *out, int size)
{
    char folder[1200];
    path_in_root(folder, sizeof folder, TOOLS[i].folder);
    SDL_snprintf(out, size, "%s/launcher_installed.txt", folder);
}

/* only tools in their folder next to the launcher, where it installs: a dev build or a copy elsewhere is the user's own.
   One without a stamp, from a launcher before this one or unpacked by hand, is offered the release once */
void tools_check(void)
{
    for (int i = 0; i < TOOLS_COUNT; i++) {
        char path[1300], folder[1200], api[160], url[512], own[32] = "v0.0.0", out[32] = "";
        path_in_root(folder, sizeof folder, TOOLS[i].folder);
        SDL_snprintf(path, sizeof path, "%s/%s", folder, TOOLS[i].exe);
        if (!exists(path)) {
            s_update[i][0] = 0;
            continue;
        }
        stamp_path(i, path, sizeof path);
        char *saved = SDL_LoadFile(path, NULL);
        if (saved) {
            SDL_strlcpy(own, saved, sizeof own);
            SDL_free(saved);
        }
        SDL_snprintf(api, sizeof api, "https://api.github.com/repos/%s/releases/latest", TOOLS[i].repo);
        char *json = net_text(api);
        if (json) { /* without an answer, what was known stays */
            if (pick_zip(json, TOOLS[i].repo, url, sizeof url)) {
                release_pick(json, own, out, sizeof out);
            }
            SDL_strlcpy(s_update[i], out, sizeof s_update[i]);
            SDL_free(json);
        }
    }
}

/* ponytail: a failed unpacking stays in .tools-tmp, which the next try overwrites; deleted by hand if it never comes */
int tool_install(int i)
{
    char tmp[1200], zip[1300], api[160], url[512], from[1300], to[1300], tar[MAX_PATH + 16];
    path_in_root(tmp, sizeof tmp, ".tools-tmp");
    SDL_snprintf(zip, sizeof zip, "%s/tool.zip", tmp);
    SDL_snprintf(api, sizeof api, "https://api.github.com/repos/%s/releases/latest", TOOLS[i].repo);
    char *json = net_text(api), tag[32] = "";
    const bool found = json && pick_zip(json, TOOLS[i].repo, url, sizeof url);
    if (found) {
        release_pick(json, "v0.0.0", tag, sizeof tag);
    }
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
    if (exists(to)) {
        /* over what's there, whose settings and data aren't in the zip. The program first: Windows refuses that while it's open, and then nothing has changed */
        char a[1400], b[1400];
        SDL_snprintf(a, sizeof a, "%s/%s", from, TOOLS[i].exe);
        SDL_snprintf(b, sizeof b, "%s/%s", to, TOOLS[i].exe);
        if (!SDL_RenamePath(a, b)) {
            native_log("can't replace %s: %s", b, SDL_GetError());
            SDL_RemovePath(zip);
            return S_T_CLOSE_IT;
        }
        if (!path_move_tree(from, to) || !SDL_RemovePath(from)) {
            native_log("can't move %s over %s: %s", from, to, SDL_GetError());
            SDL_RemovePath(zip);
            return S_T_FAILED;
        }
    } else if (!SDL_RenamePath(from, to)) {
        native_log("can't move %s to %s: %s", from, to, SDL_GetError());
        SDL_RemovePath(zip);
        return S_T_FAILED;
    }
    stamp_path(i, from, sizeof from);
    if (!SDL_SaveFile(from, tag, SDL_strlen(tag))) {
        native_log("can't write %s", from); /* it's offered again, nothing worse */
    }
    SDL_RemovePath(zip);
    SDL_RemovePath(tmp);
    return S_T_READY;
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
