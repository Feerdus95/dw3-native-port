/* Asks GitHub whether a newer port is out, in the background. Pre-releases count, as all the port's are; drafts don't.
   Installs nothing: the player gets the release's page. Without network nothing changes. */
#include "launcher.h"

#ifndef LAUNCHER_VERSION
#define LAUNCHER_VERSION "dev"
#endif
#define REPO "Feerdus95/dw3-native-port"

enum { IDLE, RUNNING, FOUND_OR_NOT, FAILED };
static SDL_AtomicInt s_state;
static char s_tag[32];
static bool s_manual;

/* exactly v1.2.3: it goes into a URL, and anything else isn't a release of ours */
static bool version_parse(const char *s, int v[3])
{
    int used = 0;
    return SDL_sscanf(s, "v%d.%d.%d%n", &v[0], &v[1], &v[2], &used) == 3 && used > 0 && s[used] == 0 && SDL_strchr(s, '+') == NULL && SDL_strchr(s, ' ') == NULL;
}

static bool newer(const int a[3], const int b[3])
{
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) {
            return a[i] > b[i];
        }
    }
    return false;
}

/* the highest non-draft version in the API's list above `own`, into out, or "" */
static void pick(const char *json, const char *own, char *out, int size)
{
    static const char TAG[] = "\"tag_name\":\"", DRAFT[] = "\"draft\":true";
    int best[3], v[3];
    out[0] = 0;
    if (!version_parse(own, best)) {
        return; /* a development build isn't offered anything */
    }
    for (const char *p = SDL_strstr(json, TAG); p; ) {
        const char *start = p + sizeof TAG - 1, *end = SDL_strchr(start, '"'), *next = SDL_strstr(start, TAG);
        const char *draft = SDL_strstr(start, DRAFT);
        char tag[32];
        if (end && end - start < (int)sizeof tag) {
            SDL_strlcpy(tag, start, (size_t)(end - start) + 1);
            if (version_parse(tag, v) && newer(v, best) && !(draft && (!next || draft < next))) {
                SDL_memcpy(best, v, sizeof best);
                SDL_strlcpy(out, tag, size);
            }
        }
        p = next;
    }
}

static int SDLCALL check_thread(void *unused)
{
    (void)unused;
    /* ponytail: one page of 30, as the newest come first; pages if the port ever has 30 newer drafts */
    char *json = net_text("https://api.github.com/repos/" REPO "/releases?per_page=30");
    if (json) {
        pick(json, LAUNCHER_VERSION, s_tag, sizeof s_tag);
        SDL_free(json);
    }
    SDL_SetAtomicInt(&s_state, json ? FOUND_OR_NOT : FAILED);
    SDL_Event e = {.type = SDL_EVENT_USER};
    SDL_PushEvent(&e);
    return 0;
}

void release_start(bool manual)
{
    if (SDL_GetAtomicInt(&s_state) != RUNNING) {
        s_manual = manual;
        SDL_SetAtomicInt(&s_state, RUNNING);
        SDL_Thread *t = SDL_CreateThread(check_thread, "release", NULL);
        if (t) {
            SDL_DetachThread(t);
        } else {
            SDL_SetAtomicInt(&s_state, FAILED);
        }
    }
}

void release_fake(const char *tag) { SDL_strlcpy(s_tag, tag, sizeof s_tag); }

const char *release_tag(void) { return SDL_GetAtomicInt(&s_state) != RUNNING && s_tag[0] ? s_tag : NULL; }

bool release_busy(void) { return SDL_GetAtomicInt(&s_state) == RUNNING; }

void release_url(char *out, int size) { SDL_snprintf(out, size, "https://github.com/" REPO "/releases/tag/%s", s_tag); }

int release_finished_manual(void)
{
    const int state = SDL_GetAtomicInt(&s_state);
    if (!s_manual || state == RUNNING) {
        return -1;
    }
    s_manual = false;
    return state == FAILED ? S_CHECK_FAILED : s_tag[0] ? S_RELEASE_D : S_UP_TO_DATE;
}

int release_selftest(void)
{
    int bad = 0;
    char out[32];
#define PICK(json, own, want) (pick(json, own, out, sizeof out), bad += SDL_strcmp(out, want) != 0)
    static const char LIST[] =
        "[{\"tag_name\":\"v0.9.0\",\"name\":\"x\",\"draft\":true,\"prerelease\":false},"
        "{\"tag_name\":\"v0.2.10\",\"draft\":false,\"prerelease\":true},"
        "{\"tag_name\":\"v0.2.9\",\"draft\":false},"
        "{\"tag_name\":\"nightly\",\"draft\":false},"
        "{\"tag_name\":\"v3.0.0-rc1\",\"draft\":false}]";
    PICK(LIST, "v0.2.1", "v0.2.10"); /* the draft and the odd tags are skipped; 10 is above 9 */
    PICK(LIST, "v0.2.10", "");       /* the same version isn't newer */
    PICK(LIST, "v1.0.0", "");        /* a local version above every release */
    PICK(LIST, "dev", "");           /* a development build */
    PICK("not json", "v0.1.0", "");
    return bad;
#undef PICK
}
