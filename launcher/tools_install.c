/* A tool's download: which release is newer than what's installed, and putting it in the tool's folder. */
#include <windows.h> /* before launcher.h, whose L and enums its headers would trip on */
#include "launcher.h"
#include "tools.h"

static bool exists(const char *path) { return SDL_GetPathInfo(path, NULL); }

/* the version in where /releases/latest of `repo` points, ".../releases/tag/v1.2.3"; a repo without releases points elsewhere */
static bool tag_from_location(const char *location, const char *repo, char *out, int size)
{
    char prefix[128];
    const int n = SDL_snprintf(prefix, sizeof prefix, "https://github.com/%s/releases/tag/", repo);
    if (SDL_strncasecmp(location, prefix, n) || !release_newer(location + n, "v0.0.0")) {
        return false;
    }
    SDL_strlcpy(out, location + n, size);
    return true;
}

/* the web's redirect, not the API: that allows 60 requests an hour to everyone behind an address */
static bool latest_tag(int i, char *out, int size)
{
    char url[160], location[512];
    SDL_snprintf(url, sizeof url, "https://github.com/%s/releases/latest", TOOLS[i].repo);
    if (!net_redirect(url, location, sizeof location)) {
        return false;
    }
    if (!tag_from_location(location, TOOLS[i].repo, out, size)) {
        native_log("%s points at %s", url, location);
        return false;
    }
    return true;
}

static bool to_file(const void *data, int size, void *ctx)
{
    return !SDL_GetAtomicInt(&s_stop) && SDL_WriteIO(ctx, data, size) == (size_t)size;
}

/* url's zip into tmp and unpacked there, the zip gone: S_T_READY or the string that says why not */
int fetch_unpack(const char *url, const char *tmp)
{
    char zip[1300], tar[MAX_PATH + 16];
    SDL_snprintf(zip, sizeof zip, "%s/tool.zip", tmp);
    if (!SDL_CreateDirectory(tmp)) {
        native_log("can't make %s: %s", tmp, SDL_GetError());
        return S_T_FAILED;
    }
    SDL_IOStream *file = SDL_IOFromFile(zip, "wb");
    bool ok = file && net_get(url, to_file, file, &s_percent);
    ok = file && SDL_CloseIO(file) && ok;
    if (!ok) {
        const bool stopped = SDL_GetAtomicInt(&s_stop);
        native_log(stopped ? "%s: cancelled" : "%s: can't download it", url);
        SDL_RemovePath(tmp);
        return stopped ? S_T_CANCELLED : S_T_FAILED;
    }
    /* Windows' own tar, by its full path: the zip's in a folder of plain names, whatever the path above it */
    SDL_SetAtomicInt(&s_phase, P_UNPACK);
    GetSystemDirectoryA(tar, MAX_PATH);
    SDL_strlcat(tar, "\\tar.exe", sizeof tar); /* a lone "\t" is a tab */
    const char *args[] = {tar, "-xf", "tool.zip", NULL};
    const int code = process_wait(args, tmp);
    SDL_RemovePath(zip);
    if (code != 0) {
        native_log("%s: tar ended with %d", zip, code);
        return S_T_FAILED;
    }
    return S_T_READY;
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
        if (s_faked && i == VIEWER) {
            continue;
        }
        char path[1300], folder[1200], own[32] = "v0.0.0", tag[32];
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
        if (latest_tag(i, tag, sizeof tag)) { /* without an answer, what was known stays */
            SDL_strlcpy(s_update[i], release_newer(tag, own) ? tag : "", sizeof s_update[i]);
        }
    }
}

/* ponytail: a failed unpacking stays in .tools-tmp, which the next try overwrites; deleted by hand if it never comes */
int tool_install(int i)
{
    char tmp[1200], tag[32], url[512], from[1300], to[1300];
    path_in_root(tmp, sizeof tmp, ".tools-tmp");
    if (!latest_tag(i, tag, sizeof tag)) {
        return S_T_FAILED; /* the log says why */
    }
    SDL_snprintf(url, sizeof url, "https://github.com/%s/releases/download/%s/%s-%s-win64.zip", TOOLS[i].repo, tag, TOOLS[i].folder, tag);
    const int got = fetch_unpack(url, tmp);
    if (got != S_T_READY) {
        return got;
    }
    SDL_snprintf(from, sizeof from, "%s/%s/%s", tmp, TOOLS[i].folder, TOOLS[i].exe);
    if (!exists(from)) {
        native_log("no %s in the zip", from);
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
            return S_T_CLOSE_IT;
        }
        if (!path_move_tree(from, to) || !SDL_RemovePath(from)) {
            native_log("can't move %s over %s: %s", from, to, SDL_GetError());
            return S_T_FAILED;
        }
    } else if (!SDL_RenamePath(from, to)) {
        native_log("can't move %s to %s: %s", from, to, SDL_GetError());
        return S_T_FAILED;
    }
    stamp_path(i, from, sizeof from);
    if (!SDL_SaveFile(from, tag, SDL_strlen(tag))) {
        native_log("can't write %s", from); /* it's offered again, nothing worse */
    }
    SDL_RemovePath(tmp);
    return S_T_READY;
}

int tools_selftest(void)
{
    char out[32] = "";
    int bad = 0;
    bad += !tag_from_location("https://github.com/o/r/releases/tag/v1.2.3", "o/r", out, sizeof out) || SDL_strcmp(out, "v1.2.3");
    bad += tag_from_location("https://github.com/o/r/releases", "o/r", out, sizeof out); /* no release yet */
    bad += tag_from_location("https://github.com/o/other/releases/tag/v1.2.3", "o/r", out, sizeof out);
    bad += tag_from_location("https://github.com/o/r/releases/tag/nightly", "o/r", out, sizeof out);
    return bad;
}
