/* The Saves page: the memory card (saves/card1.mcd) as a page. It shows what each slot holds and whether its extra Digimon still
   belong to the save (card.c), and it exports the card, imports one and goes back to the last copy. Every change that replaces
   the card keeps the one it replaces in saves/backups first. The card is only read here, except for those replacements, and
   never while the game runs. */
#include "launcher.h"

enum { PAD = 16, PANEL_Y = 32, LINE = 14 };

/* ---- the files ---- */

static const char *s_root; /* the folder, when the check makes its own */

static void saves_dir(char *out, int size)
{
    if (s_root) {
        SDL_snprintf(out, size, "%s", s_root);
    } else {
        SDL_snprintf(out, size, "%s/../saves", path_data());
    }
}

static void card_path(char *out, int size)
{
    char dir[1200];
    saves_dir(dir, sizeof dir);
    SDL_snprintf(out, size, "%s/card1.mcd", dir);
}

static Card s_card;
static bool s_has; /* the card is there and is this game's */
static SDL_Time s_mtime;
static Uint64 s_checked;

static void refresh(bool force)
{
    char path[1200];
    SDL_PathInfo info;
    if (!force && s_checked && SDL_GetTicks() - s_checked < 1000) {
        return;
    }
    s_checked = SDL_GetTicks() | 1;
    card_path(path, sizeof path);
    if (!SDL_GetPathInfo(path, &info)) {
        s_has = false;
        s_mtime = 0;
    } else if (force || info.modify_time != s_mtime) {
        size_t size = 0;
        Uint8 *data = SDL_LoadFile(path, &size);
        s_mtime = info.modify_time;
        s_has = data && card_parse(data, size, &s_card);
        SDL_free(data);
    }
}

void saves_open(void) { refresh(true); }

static void timestamp(char *out, int size)
{
    SDL_Time now = 0;
    SDL_DateTime dt = {0};
    SDL_GetCurrentTime(&now);
    SDL_TimeToDateTime(now, &dt, true);
    SDL_snprintf(out, size, "%04d%02d%02d-%02d%02d%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
}

/* the card as it is, kept in saves/backups under a name no copy has; true when there was none to keep, or it is kept */
static bool keep_current(void)
{
    char card[1200], dir[1200], name[1300], stamp[24];
    card_path(card, sizeof card);
    if (!SDL_GetPathInfo(card, NULL)) {
        return true;
    }
    saves_dir(dir, sizeof dir);
    SDL_snprintf(name, sizeof name, "%s/backups", dir);
    SDL_CreateDirectory(name);
    timestamp(stamp, sizeof stamp);
    for (int n = 0; n < 100; n++) {
        char suffix[8] = "";
        if (n) {
            SDL_snprintf(suffix, sizeof suffix, "-%d", n);
        }
        SDL_snprintf(name, sizeof name, "%s/backups/card1-%s%s.mcd", dir, stamp, suffix);
        if (!SDL_GetPathInfo(name, NULL)) {
            return SDL_CopyFile(card, name);
        }
    }
    return false;
}

/* the card becomes the file's: copied beside it, then renamed over, so a failure leaves the card as it was */
static bool install(const char *from)
{
    char card[1200], tmp[1210];
    card_path(card, sizeof card);
    SDL_snprintf(tmp, sizeof tmp, "%s.tmp", card);
    return SDL_CopyFile(from, tmp) && SDL_RenamePath(tmp, card);
}

static SDL_EnumerationResult newest(void *ctx, const char *dir, const char *name)
{
    char *best = ctx; /* names are dates: the greatest is the newest */
    (void)dir;
    if (!SDL_strncmp(name, "card1-", 6) && SDL_strcmp(name, best) > 0) {
        SDL_strlcpy(best, name, 128);
    }
    return SDL_ENUM_CONTINUE;
}

static const char *s_message(int id) { return L->s[id]; }

static void import_card(const char *file)
{
    size_t size = 0;
    Uint8 *data = SDL_LoadFile(file, &size);
    Card c;
    const bool ok = data && card_parse(data, size, &c);
    SDL_free(data);
    if (!ok) {
        g_app.note = s_message(S_SV_REJECT);
    } else if (!keep_current() || !install(file)) {
        g_app.note = s_message(S_SV_FAILED);
    } else {
        bool warn = false;
        for (int k = 0; k < SAVE_SLOTS; k++) {
            warn = warn || c.slot[k].forms == F_STALE || c.slot[k].forms == F_BAD;
        }
        g_app.note = s_message(warn ? S_SV_WARN : S_SV_IMPORTED);
    }
    refresh(true);
}

static void restore_last(void)
{
    char dir[1200], best[128] = "", from[1300];
    saves_dir(dir, sizeof dir);
    SDL_snprintf(from, sizeof from, "%s/backups", dir);
    SDL_EnumerateDirectory(from, newest, best);
    if (!best[0]) {
        g_app.note = s_message(S_SV_NO_BACKUP);
        return;
    }
    SDL_snprintf(from, sizeof from, "%s/backups/%s", dir, best);
    g_app.note = s_message(keep_current() && install(from) ? S_SV_RESTORED : S_SV_FAILED);
    refresh(true);
}

/* ---- the dialogs: their answer comes on another thread, and is acted on here ---- */

static SDL_AtomicInt s_dialog; /* 1 asking, 2 answered */
static char s_answer[1200];
static int s_kind;

static void SDLCALL answered(void *unused, const char *const *files, int filter)
{
    (void)unused, (void)filter;
    s_answer[0] = 0;
    if (files && files[0]) {
        SDL_strlcpy(s_answer, files[0], sizeof s_answer);
    }
    SDL_SetAtomicInt(&s_dialog, 2);
    SDL_Event e = {.type = SDL_EVENT_USER};
    SDL_PushEvent(&e);
}

void saves_activate(int id)
{
    static const SDL_DialogFileFilter FILTERS[] = {{"Memory cards (.mcd, .mcr, .bin)", "mcd;mcr;bin"}};
    char card[1200];
    card_path(card, sizeof card);
    if (id == S_SV_FOLDER) {
        char dir[1200];
        saves_dir(dir, sizeof dir);
        SDL_CreateDirectory(dir);
        SDL_OpenURL(dir);
    } else if (game_running()) {
        g_app.note = s_message(S_GAME_RUNNING);
    } else if (id == S_UNDO) {
        restore_last();
    } else if (SDL_GetAtomicInt(&s_dialog) == 0 && id == S_EXPORT && !SDL_GetPathInfo(card, NULL)) {
        g_app.note = s_message(S_SV_NOCARD);
    } else if (SDL_GetAtomicInt(&s_dialog) == 0) {
        s_kind = id;
        SDL_SetAtomicInt(&s_dialog, 1);
        if (id == S_EXPORT) {
            SDL_ShowSaveFileDialog(answered, NULL, g_app.window, FILTERS, 1, "dw3-card.mcd");
        } else {
            SDL_ShowOpenFileDialog(answered, NULL, g_app.window, FILTERS, 1, NULL, false);
        }
    }
}

void saves_poll(void)
{
    char card[1200];
    if (SDL_GetAtomicInt(&s_dialog) != 2) {
        return;
    }
    SDL_SetAtomicInt(&s_dialog, 0);
    if (!s_answer[0]) {
        return;
    }
    card_path(card, sizeof card);
    if (s_kind == S_EXPORT) {
        g_app.note = s_message(SDL_CopyFile(card, s_answer) ? S_SV_EXPORTED : S_SV_FAILED);
    } else if (game_running()) {
        g_app.note = s_message(S_GAME_RUNNING);
    } else {
        import_card(s_answer);
    }
}

bool saves_busy(void) { return SDL_GetAtomicInt(&s_dialog) != 0; }

/* ---- the page ---- */

static void slot_line(const Lang *l, int k, const SlotInfo *s, char *out, int size, Uint32 *colour)
{
    char forms[96];
    int text = S_SV_NONE;
    *colour = C_TEXT;
    if (s->state != SL_OK) {
        SDL_snprintf(out, size, "%s %d   %s", l->s[S_SV_SLOT], k + 1, l->s[s->state == SL_EMPTY ? S_SV_EMPTY : S_SV_DAMAGED]);
        *colour = s->state == SL_EMPTY ? C_SOFT : C_RED;
        return;
    }
    if (s->forms == F_READY) {
        SDL_snprintf(forms, sizeof forms, l->s[S_SV_OK], s->count);
    } else {
        text = s->forms == F_STALE ? S_SV_STALE : s->forms == F_NEWER ? S_SV_NEWER : s->forms == F_BAD ? S_SV_BAD : S_SV_NONE;
        SDL_snprintf(forms, sizeof forms, "%s", l->s[text]);
    }
    *colour = s->forms == F_STALE || s->forms == F_BAD ? C_GOLD : s->forms == F_READY ? C_TEXT : C_SOFT;
    SDL_snprintf(out, size, "%s %d   %d:%02d   %s", l->s[S_SV_SLOT], k + 1, s->hours, s->minutes, forms);
}

bool saves_fits(const Lang *l, int width)
{
    static const int FORMS[] = {F_NONE, F_READY, F_STALE, F_NEWER, F_BAD};
    char line[200];
    Uint32 colour;
    for (int i = 0; i < 5; i++) {
        const SlotInfo worst = {SL_OK, FORMS[i], 66, 999, 59};
        slot_line(l, 2, &worst, line, sizeof line, &colour);
        if (text_width(line, 1) > width) {
            return false;
        }
    }
    return text_width(l->s[S_SV_NOCARD], 1) <= width;
}

void saves_draw(void)
{
    refresh(false);
    panel(PAD, PANEL_Y, SCREEN_W - 2 * PAD, SAVE_SLOTS * LINE + 8);
    if (!s_has) {
        text(PAD + 8, PANEL_Y + 4 + LINE, 1, C_SOFT, L->s[S_SV_NOCARD]);
        return;
    }
    for (int k = 0; k < SAVE_SLOTS; k++) {
        char line[200];
        Uint32 colour;
        slot_line(L, k, &s_card.slot[k], line, sizeof line, &colour);
        text(PAD + 8, PANEL_Y + 4 + k * LINE, 1, colour, line);
    }
}


/* ---- the check: a folder of its own, a card imported over another and the last copy gone back to ---- */

static bool same_file(const char *a, const char *b)
{
    size_t sa = 0, sb = 0;
    Uint8 *da = SDL_LoadFile(a, &sa), *db = SDL_LoadFile(b, &sb);
    const bool same = da && db && sa == sb && !SDL_memcmp(da, db, sa);
    SDL_free(da);
    SDL_free(db);
    return same;
}

int saves_selftest(void)
{
    static Uint8 one[CARD_SIZE], two[CARD_SIZE];
    char root[1100], dir[1200], card[1200], first[1200], second[1200], copy[1300], name[128];
    int bad = card_selftest();
    SDL_snprintf(root, sizeof root, "%scheck-saves", SDL_GetBasePath());
    s_root = root;
    SDL_CreateDirectory(root);
    card_make(one);
    SDL_memcpy(two, one, sizeof two);
    two[CARD_SIZE - 1] ^= 1; /* another card with the same save, told apart by its last byte */
    card_path(card, sizeof card);
    SDL_snprintf(first, sizeof first, "%s/first.mcd", root);
    SDL_snprintf(second, sizeof second, "%s/second.mcd", root);
    SDL_snprintf(dir, sizeof dir, "%s/backups", root);
    bad += !SDL_SaveFile(card, one, sizeof one) || !SDL_SaveFile(first, one, sizeof one) || !SDL_SaveFile(second, two, sizeof two);
    import_card(second); /* the card is the second's, the one it had kept */
    name[0] = 0;
    SDL_EnumerateDirectory(dir, newest, name);
    SDL_snprintf(copy, sizeof copy, "%s/%s", dir, name);
    bad += !same_file(card, second) || !name[0] || !same_file(copy, first);
    restore_last(); /* back to the first, the second kept */
    bad += !same_file(card, first);
    SDL_SaveFile(second, one, 100); /* a file that isn't a card changes nothing */
    import_card(second);
    bad += !same_file(card, first);
    for (int i = 0; i < 8; i++) { /* clean up: the copies, then the folders */
        name[0] = 0;
        SDL_EnumerateDirectory(dir, newest, name);
        SDL_snprintf(copy, sizeof copy, "%s/%s", dir, name);
        if (name[0]) {
            SDL_RemovePath(copy);
        }
    }
    SDL_RemovePath(dir);
    SDL_RemovePath(card);
    SDL_RemovePath(first);
    SDL_RemovePath(second);
    SDL_RemovePath(root);
    s_root = NULL;
    return bad;
}
