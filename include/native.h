/* Native port: what the patched game sources (native/patches) call in the
   platform layer. Force-included into every game file (-include native.h). */
#ifndef NATIVE_H
#define NATIVE_H
#include "native_forms.h"

/* The heap arena (platform/memory.c); the PSX had 0x800AB800-0x801FF000, ~1.3 MB.
   Bigger on purpose: room for mods and expansions. The file cache, which on the
   PSX freed old files only when an allocation failed, also evicts when its
   slots run out (patches/src/engine/file/file_cache.c) */
#ifndef NATIVE_HEAP_SIZE /* build.py --sanitize takes 4 MB, to fit its bigger code under 16 MB */
#define NATIVE_HEAP_SIZE (6 * 1024 * 1024)
#endif

/* A PSX heap address the game hard-codes, at the same offset in the arena */
#define NATIVE_PSX_HEAP_BASE 0x800AB800
#define NATIVE_PSX_HEAP(addr) ((u32)((u8 *)HEAP_BASE + ((addr) - NATIVE_PSX_HEAP_BASE)))

/* getFileSector's fake disc position of a file: fid * span + sector */
#define NATIVE_FILE_SECTOR_SPAN 0x10000

int native_file_exists(int fid);
int native_file_sectors(int fid);
void native_read_file(int fid, int sector, int count, void *dst);

/* Waits for the next vsync and runs what an interrupt would have (platform/etc.c) */
void native_wait_vblank(void);
/* 0 for a vsync fast forward adds (the game's ticks between frames shown) */
int native_real_vsync(void);
int native_real_time(void); /* the vsyncs shown, as the game counts time (etc.c) */
extern int g_fix_equip_stacking; /* fix_equip_stacking = 1: Search and Counter Crest work beside special weapons (mods.c; FIGHTSTG stats.c patch) */
extern int g_technique_visuals; /* technique_visuals = 1: mapped player technique effects (mods.c; WFIGHTMN techs.c patch) */
extern int g_battle_form_name; /* battle_form_name = 1: the battle HUD names the partner's current form (mods.c; STFIGHT hud.c patch) */
extern int g_menu_wrap; /* menu_wrap = 1: a menu's cursor goes round at its ends (mods.c; the menus' patches) */
/* A list's cursor that went nowhere at its first or last row (old == now: the game's clamp held it)
   goes to the other end. dir: -1 up, 1 down; last: the last row */
static inline int native_wrap_list(int old, int now, int dir, int last) {
    if (g_menu_wrap && old == now && last > 0) {
        return dir < 0 && now == 0 ? last : dir > 0 && now == last ? 0 : now;
    }
    return now;
}
/* The same for a page of two columns (the item lists): from the top row up to the column's last row,
   from its last row down to the top. Elsewhere the game's own move stands */
static inline int native_wrap_grid(int old, int now, int first, int last, int dir) {
    int col = (old - first) & 1, bottom = ((last - first) & 1) == col ? last : last - 1;

    if (!g_menu_wrap) {
        return now;
    }
    if (bottom < first) {
        bottom = first;
    }
    if (dir < 0 && old - first < 2) {
        return bottom;
    }
    return dir > 0 && old == bottom ? (first + col <= last ? first + col : first) : now;
}
extern int g_ff_menu_speed; /* ff_menu_speed = 1: a held button repeats at its normal pace in fast forward (pad/port.c) */

/* Overlay variables back to their initial values: the mode overlays (sub 0)
   or the stages and WFIGHTMN/TS (sub 1) (platform/memory.c) */
void native_reset_overlays(int sub);

/* After a stage's setup: the field editor's changes to its tables, from
   data/mods/stages/<stage>.json (platform/stagemods.c) */
void native_apply_stage_mod(void);
/* a new field of the field editor's for a mode no stage has: 1 once
   FIELDSTG_state has its template's code (FIELDSTG's pickStage) */
int native_pick_new_field(int mode);
/* the banner's names of a new field's mode, 1 if it is one */
int native_field_names(int mode, int *area, int *place);
/* --event ID: the event FIELDSTG starts once the field runs, then 0 */
extern int g_field_event;
/* a character of the field editor's, past the game's table (CHARACTERS):
   1 with its animation file entry and width (data/mods/characters.json) */
int native_character(int id, int *file, int *width);

/* The port's own options (native.ini, the options menu), mods of the game
   that were byte patches for the PSX:
   fast text: the text boxes take X held as well as pressed, outside battles
   (FIGHTSTG, 6) and card games (CARDGAME, 7), where X confirms commands;
   held a third of a second first, so a tap shows the page whole as the
   game's own press does, and the next goes on (mods.c).
   Based on Dmw2003FastText by markisha64 (MIT) */
extern int g_fast_text;
int native_long_held(int held);
#define NATIVE_TEXT_BUTTONS()                                                                  \
    (PAD.getPressed(0) |                                                                       \
     (g_fast_text && OVERLAY_LOADER.mode != 6 && OVERLAY_LOADER.mode != 7 ? native_long_held(PAD.getHeld(0)) : 0))
#define NATIVE_TEXT_PRESSED(button) ((NATIVE_TEXT_BUTTONS() >> PAD.getButtonBit(0, button)) & 1)

/* Extra starter packs (platform/packs.c): DRAGON, TACTICAL and MYSTIC, 3-5
   after the game's three. The choice offers them while g_extra_packs is on;
   a game started with one plays as the pack it takes after in the lobby
   (NATIVE_PACK_SCENE: 1, Powerful), with its own partners there */
extern int g_extra_packs;
extern const int NATIVE_EXTRA_PACKS[3][3]; /* their partners */
/* their texts, a text file as the game's in its language (LANGUAGE): 1-3 the
   names, 4-6 the labels D-F, 7-9 the packs' texts, 10 Veemon's, 11 its
   greeting, 12-13 the word of the intro and the one replacing it; and the
   port's other texts: 14 fast travel's question */
const void *native_packs_text(int language);
/* The journal's picture of a field (data/postcards, tools/postcards.py): whether there is one. It is drawn by a
   primitive of its own, three words: 0x1B000000 | mode, the place (y << 16 | x) and the size (h << 16 | w) */
int native_postcard(int mode);
void native_quit(void); /* the field menu's Quit: saves the options menu's changes, then exits as closing the window does */
#define NATIVE_PACK_SCENE(pack) ((pack) >= 3 ? 1 : (pack))

/* BladeGarurumon as a partner's Digimon: model 471 and the texts of its
   signature (made by platform/bladegaru.c), DIGIMON_DATA[53] made its own
   and its signature Solar Laser a technique of its own, 444
   (native_bladegarurumon_setup, main/game/digimon.c), a 45th entry past
   each partner's 44 (main/game/partner.c) that partners learn while
   g_bladegarurumon is on (stfgtrep/growth.c) */
#define NATIVE_BLADEGARURUMON 471
#define NATIVE_BLADEGARURUMON_DATA 53
#define NATIVE_SOLAR_LASER 444 /* its signature: a technique past the game's 443 */
extern int g_bladegarurumon;
void native_bladegarurumon_setup(void);
/* Full EXP Share: battle-end eligibility and the option frozen for this report
   (platform/mods.c; WFIGHTMN and STFGTREP patches). */
extern int g_full_exp_share;
extern int g_skip_press_start; /* skip_press_start = 1: the title has no PRESS START (STDWTITL menu.c) */
extern int g_heal_on_levelup; /* heal_on_levelup = 1: a partner that levels up is healed (STFGTREP growth.c) */
extern int g_save_anywhere;
extern int g_save_from_menu;
extern int g_full_exp_eligible[3];
extern int g_full_exp_report;
/* Digimon mods (platform/digimon_mods.c): a folder data/mods/digimon/<name>/digimon.json makes one more Digimon a partner can digivolve into
   (Flamedramon is the first). Its model is the file FID of data/mods; the mod is on only while that file is there and "enabled" isn't 0.
   A mod's id is its own (472 on, one number a mod, the number of its form in a save), and from it the rest follows: its DIGIMON_DATA entry
   (55 + k, k = id - 472), its name's string (251 + k: nameId is a byte) and its seven techniques (445 + 7k: six of its own, the pair's
   fusion last). Up to NATIVE_MAX_MODS at once; the fields are in the README's Digimon mods */
#define NATIVE_MAX_MODS 4
#define NATIVE_MOD_FIRST_ID 472
#define NATIVE_MOD_DATA 55
#define NATIVE_MOD_NAME 251
#define NATIVE_MOD_TECH 445
#define NATIVE_DATA_COUNT (NATIVE_MOD_DATA + NATIVE_MAX_MODS) /* DIGIMON_DATA: the game's 55, then the mods' */
#define NATIVE_TECH_COUNT (NATIVE_MOD_TECH - 1 + 7 * NATIVE_MAX_MODS) /* TECHS: the game's 443, Solar Laser, then the mods' */
#define NATIVE_EXTRA_FORMS (1 + NATIVE_MAX_MODS) /* BladeGarurumon and the mods' */
typedef struct NativeModTech {
    int from, mp, power, element_power, element, script; /* the technique it is made from (sprites, sounds), its own cost, power, element, script (0: the model's) */
    char name[24];
    char help[5][100]; /* English, French, Italian, German, Spanish: no accents, the glyphs of those are the game's to find */
} NativeModTech;
typedef struct NativeMod {
    int id, slot, name_id, tech, fid;
    int like;        /* a model whose partner record gives the cameras and face (ExVeemon's, 259) */
    int template;    /* the DIGIMON_DATA entry it starts from: the growth tables of that Digimon */
    int hp_permille, mp_permille;
    int signatures;  /* how many of its last skills are signatures */
    int tired;       /* its eyes stay shut in the weak idle */
    int portrait_dz; /* the portrait cameras move this far along the facing (the model's head is level where the record's leans back) */
    int subs[3];     /* its skeleton, motions and effects in its file */
    int stats[5], resists[7], status[5], learn[6], known[5];
    int needs[4];    /* learnt with DIGIMON_DATA entries needs[0] and needs[2] (1-based) at levels needs[1] and needs[3] */
    NativeModTech techs[6];
    int pair[2], pair_element, pair_element_power, pair_shift; /* a pair that fuses into it (none while 0): both ids, the fusion's element, how much farther the second flies */
    int dna_scripts[3], dna_motions[3]; /* the fusion's finale: a script of its file and its motion, one of three at random */
    int face_n;
    unsigned char face[20]; /* its eyes (the record's face table: part, frames) when the record's own would paint over it */
    int fx[16][2], fx_n; /* effect id -> a file of data/mods, for its techniques' own models */
    char name[24], folder[40];
} NativeMod;
/* What the loader made of each folder of data/mods/digimon, in the order of their names (native_mods_report) */
enum { NATIVE_MOD_LOADED, NATIVE_MOD_DISABLED, NATIVE_MOD_REJECTED, NATIVE_MOD_SKIPPED /* no digimon.json in it */ };
#define NATIVE_MOD_REPORTS 256 /* the report's room: the folders past it are read all the same, only left out of it (dropped) */
typedef struct NativeModReport {
    char folder[64], name[24];
    int id, fid, status;
    char reason[192];   /* why it is not loaded: the first thing wrong with it, or what it clashes with */
    char warnings[192]; /* what is unusual in it, separated by "; " */
} NativeModReport;
#define NATIVE_MOD_SCHEMA 1 /* the manifest's "schema_version" this port reads; a manifest without one is 1 */
void native_mods_load(void);
const NativeModReport *native_mods_report(int *count, int *dropped);
const NativeMod *native_mod_at(int k);       /* mod k (id - 472) or NULL */
const NativeMod *native_mod_by_id(int id);   /* by form id, or NULL */
const NativeMod *native_mod_for_effect(int id, int *fid); /* the mod with an effect model of its own */
void native_mods_setup(void);                /* the game's main/game/digimon.c (patched): DIGIMON_DATA and TECHS */
/* an extra form's id by position (0 is BladeGarurumon, then the mods'), 0 where there is none */
static int __attribute__((unused)) native_form_id(int i) {
    const NativeMod *m = i > 0 ? native_mod_at(i - 1) : 0;
    return i == 0 ? NATIVE_BLADEGARURUMON : m != 0 ? m->id : 0;
}
/* the DIGIMON_DATA entry of an extra form's id, or -1 */
static int __attribute__((unused)) native_form_slot(int id) {
    const NativeMod *m = id == NATIVE_BLADEGARURUMON ? 0 : native_mod_by_id(id);
    return id == NATIVE_BLADEGARURUMON ? NATIVE_BLADEGARURUMON_DATA : m != 0 ? m->slot : -1;
}
/* how many of a form's last skills are signatures (SKILL_LAST) */
static int __attribute__((unused)) native_form_signatures(int id) {
    const NativeMod *m = native_mod_by_id(id);
    return m != 0 ? m->signatures : 1;
}
/* Pair techniques (the Tag menu) past the game's one pairPartner/pairTech a form has. A pair that a mod fuses into (its "pair") gets
   the mod's fusion technique, tech + 6, in the menu (both pay it) */
extern int g_dna_other;
int native_dna_begin(int pair_tech, int active); /* the other partner's id when PAIR_TECH is a fusion (g_dna_other's), else 0 */
short *native_dna_script(const short *pc, int other, unsigned rnd);
int native_pair_tech(int active, int partner);
/* an effect model of the port's (Flaming Rocket's rocket, 450, and the dome of flames, 451: data/mods/7101.bin, 7102.bin, made by
   dw2-extractor) as FIGHTSTG_effectModels' entry: 1 with its model's file and index and its motions' (fightstg/effect_model.c) */
int native_effect_model(int id, int *file, int *motionFile);
/* ... and what Digital Card Battle's fire needs besides (452-458): the VRAM page its image goes to, and its texture scrolling (SUG_scrollTexture), begun
   when the model is made and ticked while it runs; every other effect model is left as it is */
int native_effect_texture_x(int id);
void native_effect_scroll_begin(int id);
void native_effect_scroll(int id, int x, int y);
void native_vram_roll(int x, int y, int w, int h, int dx);
extern int g_give_form; /* --give ID: the form is given to every partner on a load (a development aid) */
/* listPartnerEntries into room for max ids, BladeGarurumon included when there is */
void controls_shoulder_text(int bit, char *out, int size); /* the label of L1 (10) / R1 (11) for the keys or pad in use, as the game's text */
int native_list_partner_entries(int partner, unsigned short *out, int max);

/* Fast travel (fast_travel, the options menu): X on a visited area of the
   map screen (ststatus/map_screen.c) asks, and yes fades to its field,
   through one of the game's doors into it, and only from where the player
   could walk out to another area now. The ways out: every exit, warp and
   launch out of the European stages, with the mode it is in, read before
   the game runs (platform/mods.c) */
typedef struct NativeExit {
    short from, to, x, y, dir; /* to: the mode it leaves for; x, y in pixels there */
    unsigned short place, placeArg, conditions[2][2];
    short door; /* an exit (SLOT_EXIT), what fast travel arrives through */
} NativeExit;
extern int g_fast_travel;
/* Objective on map (map_objective, the options menu): the map screen blinks
   the area where the story's next step happens */
extern int g_map_objective;
/* Journal (journal, the options menu): a last row of the field menu, a list
   of the story's entries in the item list's panels (ststatus/item_list.c) */
extern int g_journal;
void native_fast_travel_setup(void);
const NativeExit *native_exits(int *count);
/* where MODE's stage starts the player (x, y in 256ths of a pixel), 0 without one */
int native_stage_start(int mode, int *x, int *y, int *dir);

/* Soft shadows (soft_shadows, the options menu): the field's actors' shadow
   subtracted from the ground rather than a grey disc over it, and on the
   card (gl.c) a smooth ellipse rather than the 32x16 sprite's pixels. The
   flag rides in the sprite's height, a bit the PSX GPU never reads */
extern int g_soft_shadows;
#define NATIVE_SOFT_SHADOW 0x8000

/* Battle shadows (battle_shadows, the options menu): each fighter's model
   cast on the ground (fightstg/fightstg.c), drawn by the graphics card alone
   as GP0 1C triangles (flat, 4 words: the code, three points), which the PSX
   GPU doesn't have. They need Sub-pixel 3D and the Depth buffer */
extern int g_battle_shadows;

/* Effects' own images (platform/effects.c): PNGs in data/mods/effects/<id>/
   (a critical hit's in critical/ there; a stage's in rookie/, champion/,
   ultimate/ or mega/, with a critical/ of its own) drawn in a battle's 2D
   effect's place, as their effect.json times them, by the graphics card alone
   as GP0 1D packets (2 words: the code with the images << 12 | the tick, the
   effect's point). Returns its images, -1 without, and their ticks. STAGE is
   native_fighter_stage's: without images, the nearest lower stage's play */
int native_effect_images(int effect, int critical, int stage, int *ticks);
/* --render-sounds (platform/soundrender.c), run by main.c's boot after it starts the sound: the battle sound index I of
   the list or -1, a sound's capture (begin, a frame of the SPU's mix at a time while 1, end writes its WAV) and done exits */
extern int g_render_sounds;
int native_render_next(int i);
void native_render_begin(int index);
int native_render_frame(void);
void native_render_end(int index);
void native_render_done(void);
/* 1 when the effect (for that critical hit and stage) has a sound of its own that plays (g_custom_sounds on): the attack script
   leaves the game's hit sound out then */
int native_effect_has_sound(int effect, int critical, int stage);
/* the sound library's WAV plays in place of the game's battle sound INDEX (data/mods/sounds): 1 if so, and the game's is left out */
int native_sound_replace(int index);
/* the words of fighter FIGHTER's battle script INDEX from data/mods/scripts when a mod has a valid one and Custom scripts is on, else NULL */
const short *native_script_mod(int fighter, int index);
/* FIGHTER's stage in data/mods/effects/stages.json: 1 rookie, 2 champion,
   3 ultimate, 4 mega, 0 for one it doesn't list */
int native_fighter_stage(int fighter);
/* the layer whose ordering table is drawn (main/gfx/display.c's drawFrame),
   for widescreen battles' menus (platform/gpu_draw.c) */
extern int g_draw_layer;
/* the next ordering table drawn belongs to a menu against the left (0) or
   right (320) edge, in a widescreen battle (the cursor's bar, fightstg/cursor.c) */
void native_wide_side(int edge);
/* the vsyncs so far, a new value for each frame (platform/etc.c) */
unsigned native_frame(void);

/* printf to stderr, for probes (platform/main.c) */
void native_log(const char *fmt, ...);

/* Player technique visuals. Omitted values retain the original script value. */
enum {
    NATIVE_VIS_PROJECTILE,
    NATIVE_VIS_LAUNCH_SPRITE,
    NATIVE_VIS_LAUNCH_SOUND,
    NATIVE_VIS_IMPACT_SPRITE,
    NATIVE_VIS_IMPACT_SOUND,
    NATIVE_VIS_STAGE,
    NATIVE_VIS_LAUNCH_SOUND_DURATION,
    NATIVE_VIS_LAUNCH_TIMING,
};
#define NATIVE_LAUNCH_ORIGINAL 0
#define NATIVE_LAUNCH_ALIGNED 1
int native_visual_load(void);
int native_visual_get(int technique, int field, int original);
int native_visual_archive_enabled(int archive); /* selected archive contains prepared launch slots */

#endif /* NATIVE_H */
