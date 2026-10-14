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
   greeting, 12-13 the word of the intro and the one replacing it */
const void *native_packs_text(int language);
#define NATIVE_PACK_SCENE(pack) ((pack) >= 3 ? 1 : (pack))

/* BladeGarurumon as a partner's Digimon: model 471 (made by
   platform/bladegaru.c), DIGIMON_DATA[53] and technique 443 made its own
   (native_bladegarurumon_setup, main/game/digimon.c), a 45th entry past each
   partner's 44 (main/game/partner.c) that partners learn while
   g_bladegarurumon is on (stfgtrep/growth.c) */
#define NATIVE_BLADEGARURUMON 471
#define NATIVE_BLADEGARURUMON_DATA 53
extern int g_bladegarurumon;
void native_bladegarurumon_setup(void);
/* listPartnerEntries into room for max ids, BladeGarurumon included when there is */
int native_list_partner_entries(int partner, unsigned short *out, int max);

/* printf to stderr, for probes (platform/main.c) */
void native_log(const char *fmt, ...);

#endif /* NATIVE_H */
