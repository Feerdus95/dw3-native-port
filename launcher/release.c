/* Asks GitHub whether a newer port is out, in the background. Pre-releases count, as all the port's are, so /releases/latest
   won't do; the public feed lists them, without drafts and without the API's limit of 60 requests an hour per address.
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

bool release_newer(const char *tag, const char *own)
{
    int a[3], b[3];
    return version_parse(tag, a) && version_parse(own, b) && newer(a, b);
}

/* the highest version linked in the feed above `own`, into out, or "". A link in a release's notes is escaped there, so it doesn't match */
static void release_pick(const char *feed, const char *own, char *out, int size)
{
    static const char LINK[] = "href=\"https://github.com/" REPO "/releases/tag/";
    char best[32];
    SDL_strlcpy(best, own, sizeof best);
    out[0] = 0;
    for (const char *p = SDL_strstr(feed, LINK); p; p = SDL_strstr(p, LINK)) {
        p += sizeof LINK - 1;
        const char *end = SDL_strchr(p, '"');
        char tag[32];
        if (end && end - p < (int)sizeof tag) {
            SDL_strlcpy(tag, p, (size_t)(end - p) + 1);
            if (release_newer(tag, best)) {
                SDL_strlcpy(best, tag, sizeof best);
                SDL_strlcpy(out, tag, size);
            }
        }
    }
}

static int SDLCALL check_thread(void *unused)
{
    (void)unused;
    /* ponytail: the feed's newest 10 are plenty, as a newer one is all it looks for */
    char *feed = net_text("https://github.com/" REPO "/releases.atom");
    if (feed) {
        release_pick(feed, LAUNCHER_VERSION, s_tag, sizeof s_tag);
        SDL_free(feed);
    }
    tools_check();
    SDL_SetAtomicInt(&s_state, feed ? FOUND_OR_NOT : FAILED);
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

void release_zip_url(char *out, int size)
{
    SDL_snprintf(out, size, "https://github.com/" REPO "/releases/download/%s/dw2003-native-%s.zip", s_tag, s_tag);
}

int release_finished_manual(void)
{
    const int state = SDL_GetAtomicInt(&s_state);
    if (!s_manual || state == RUNNING) {
        return -1;
    }
    s_manual = false;
    return state == FAILED ? S_CHECK_FAILED : s_tag[0] ? S_RELEASE_D : tool_update_index() >= 0 ? S_T_UPDATE_D : S_UP_TO_DATE;
}

int release_selftest(void)
{
    int bad = 0;
    char out[32];
#define PICK(feed, own, want) (release_pick(feed, own, out, sizeof out), bad += SDL_strcmp(out, want) != 0)
#define LINK_TO(tag) "<link rel=\"alternate\" type=\"text/html\" href=\"https://github.com/" REPO "/releases/tag/" tag "\"/>"
    static const char LIST[] =
        LINK_TO("v0.2.10") LINK_TO("v0.2.9") LINK_TO("nightly") LINK_TO("v3.0.0-rc1")
        "<content>&lt;a href=&quot;https://github.com/" REPO "/releases/tag/v9.0.0&quot;&gt;</content>"
        "<link href=\"https://github.com/someone/else/releases/tag/v8.0.0\"/>";
    PICK(LIST, "v0.2.1", "v0.2.10"); /* odd tags, a link in the notes and other repos are skipped; 10 is above 9 */
    PICK(LIST, "v0.2.10", "");       /* the same version isn't newer */
    PICK(LIST, "v1.0.0", "");        /* a local version above every release */
    PICK(LIST, "dev", "");           /* a development build */
    PICK("not a feed", "v0.1.0", "");
    bad += !release_newer("v0.2.10", "v0.2.9") + release_newer("v0.2.9", "v0.2.9") + release_newer("nightly", "v0.0.0") + release_newer("v1.0.0", "garbage");
    return bad;
#undef LINK_TO
#undef PICK
}
