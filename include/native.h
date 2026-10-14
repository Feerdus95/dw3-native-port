/* Native port: what the patched game sources (native/patches) call in the
   platform layer. Force-included into every game file (-include native.h). */
#ifndef NATIVE_H
#define NATIVE_H

/* The heap arena (platform/memory.c); the PSX had 0x800AB800-0x801FF000, ~1.3 MB.
   Bigger on purpose: room for mods and expansions. The file cache, which on the
   PSX freed old files only when an allocation failed, also evicts when its
   slots run out (patches/src/main/file/file_cache.c) */
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
   (FIGHTSTG, 6) and card games (CARDGAME, 7), where X confirms commands.
   Based on Dmw2003FastText by markisha64 (MIT) */
extern int g_fast_text;
#define NATIVE_TEXT_BUTTONS()                                                                  \
    (PAD.getPressed(0) |                                                                       \
     (g_fast_text && OVERLAY_LOADER.mode != 6 && OVERLAY_LOADER.mode != 7 ? PAD.getHeld(0) : 0))
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
/* listPartnerEntries into room for max ids, BladeGarurumon included when there is */
int native_list_partner_entries(int partner, unsigned short *out, int max);

/* Fast travel (fast_travel, the options menu): X on a visited area of the
   map screen (ststatus/map_screen.c) takes the player there as the menu
   closes, through one of the game's doors into it. The doors: every exit
   (SLOT_EXIT) of the European stages, with the mode it is in, read before
   the game runs (platform/mods.c) */
typedef struct NativeExit {
    short from, to, x, y, dir; /* to: the mode it leaves for; x, y in pixels there */
    unsigned short place, placeArg, conditions[2][2];
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

#endif /* NATIVE_H */
