/* The party the player has, read (never written) from the memory card: saves/card1.mcd, the game's own file with its
   three slots, each a raw image of the game state (kb/docs/save_flow.md). The slot last saved to (the file's summary says which) is the one
   shown, else the one played the longest. */
#include "launcher.h"

enum {
    BLOCK = 0x2000, FRAME = 128, SLOT_START = 0x300, SLOT_SIZE = 0x2700, SLOTS = 3, PARTNERS = 8, PARTNER_SIZE = 0x3DC,
    G_TIME = 0x4C, G_PARTY = 0x70, G_PARTNERS = 0x75C, P_UNLOCKED = 4, P_LEVEL = 0xC + 0x1C /* PartnerStats.stats[STAT_LEVEL] */
};

/* the partner records' order (kb/docs/starter_packs.md) and a colour for each one's card */
static const char *const NAMES[PARTNERS] = {"Kotemon", "Kumamon", "Monmon", "Agumon", "Veemon", "Guilmon", "Renamon", "Patamon"};
static const Uint32 COLOURS[PARTNERS] = {0x9A6B3F, 0x6FA8DC, 0xC9B28A, 0xF5A623, 0x4F8FE0, 0xE05555, 0xE8C860, 0xF0A070};

const char *team_name(int partner) { return NAMES[SDL_clamp(partner, 0, PARTNERS - 1)]; }
Uint32 team_colour(int partner) { return COLOURS[SDL_clamp(partner, 0, PARTNERS - 1)]; }

static int s16(const Uint8 *p) { return (Sint16)(p[0] | p[1] << 8); }
static int s32(const Uint8 *p) { return (Sint32)(p[0] | p[1] << 8 | p[2] << 16 | (Uint32)p[3] << 24); }

/* the game's file in the card's directory (frame 1 to 15 name a block each), then its last saved slot (MemCardFile.last, the summary
   after the 0x200 of header and icons, its magic at 4), else the slot with the most play time */
static bool parse(const Uint8 *d, size_t size, Team *t)
{
    int base = -1, best = -1;
    for (int f = 1; f < 16 && base < 0; f++) {
        if ((d[f * FRAME] & 0xF0) == 0x50 && !SDL_strncmp((const char *)d + f * FRAME + 0xA, "BESLES-03936DMW3-EUR", 20)) {
            base = f * BLOCK;
        }
    }
    if (base < 0 || size < (size_t)base + SLOT_START + SLOTS * SLOT_SIZE) {
        return false;
    }
    const int last = size > (size_t)base + 0x208 && !SDL_memcmp(d + base + 0x204, "DMW3", 4) ? d[base + 0x201] : -1;
    for (int slot = 0; slot < SLOTS; slot++) {
        const Uint8 *g = d + base + SLOT_START + slot * SLOT_SIZE;
        const int seconds = s16(g + G_TIME) * 3600 + s16(g + G_TIME + 2) * 60 + s16(g + G_TIME + 4);
        Team here = {0};
        for (int i = 0; i < 3; i++) {
            const int p = s32(g + G_PARTY + 4 * i);
            const Uint8 *partner = g + G_PARTNERS + p * PARTNER_SIZE;
            const int level = p >= 0 && p < PARTNERS ? s16(partner + P_LEVEL) : 0;
            if (level >= 1 && level <= 99 && s32(partner + P_UNLOCKED) != 0) {
                here.partner[here.count] = p;
                here.level[here.count++] = level;
            }
        }
        const int score = slot == last ? 0x7FFFFFFF : seconds;
        if (here.count && score > best) {
            best = score;
            here.hours = s16(g + G_TIME);
            here.minutes = s16(g + G_TIME + 2);
            *t = here;
        }
    }
    return best >= 0;
}

const Team *team_get(void)
{
    static Team team;
    static SDL_Time mtime;
    static bool has;
    static Uint64 checked;
    char path[1200];
    SDL_PathInfo info;
    if (checked && SDL_GetTicks() - checked < 1000) {
        return has ? &team : NULL;
    }
    checked = SDL_GetTicks() | 1;
    SDL_snprintf(path, sizeof path, "%s/../saves/card1.mcd", path_data()); /* as the game finds it */
    if (!SDL_GetPathInfo(path, &info) || info.size != 131072) {
        has = false;
        mtime = 0;
    } else if (info.modify_time != mtime) {
        size_t size = 0;
        Uint8 *data = SDL_LoadFile(path, &size);
        SDL_DateTime dt;
        mtime = info.modify_time;
        has = data && parse(data, size, &team);
        SDL_free(data);
        if (has && SDL_TimeToDateTime(info.modify_time, &dt, true)) {
            SDL_snprintf(team.when, sizeof team.when, "%04d-%02d-%02d %02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute);
        }
    }
    return has ? &team : NULL;
}
