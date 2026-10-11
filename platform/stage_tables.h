/* The game's field tables, mirrored by their offsets (dw3_decomp's
   include/field_map.h): the port is 32-bit, as the PSX, so they are laid out
   the same. For the stage dump (stagedump.c) and the field editor's changes
   (stagemods.c). */
#ifndef STAGE_TABLES_H
#define STAGE_TABLES_H
#include <stdint.h>

typedef struct {
    uint8_t visible, anim, margin, depth, frame, cycle, cycleFirst, cycleLast, cycleDelay, clutRow;
    int16_t x, y, sortY, cycleTime;
} StageTile;

typedef struct {
    uint16_t conditions[2][2];
    uint16_t type, arg, x, y, dir, hideAnim, place, placeArg;
} StageSlot;

typedef struct {
    uint16_t *conditions, *actions;
    int32_t entry;
} FieldTalk;

typedef struct {
    uint16_t *conditions;
    FieldTalk *talks;
    int16_t id, image, x, y, dir;
} FieldActorEntry;

typedef struct {
    int16_t x, y, imageX, imageY, u, v, clutX, clutY;
} ActorImage;

typedef struct {
    int32_t encounter, stage, music;
} Battle;

typedef struct {
    int32_t count;
    Battle *battles[8];
} BattleList;

typedef struct {
    int32_t serial, id, unk8;
    BattleList *battles[4];
} FieldBattles;

typedef struct {
    int32_t id;
    int16_t *script;
    int32_t text;
    void *start, *end;
} FieldEvent;

typedef struct {
    int32_t stageFile;
    void *stageInit;
    int32_t mapFile, sheetEntry;
    StageTile *objects;
    StageSlot *slots;
    int32_t imageEntry, imageFile;
    FieldBattles *battles;
    FieldEvent *events;
    ActorImage *images;
    int32_t startX, startY, startDir;
    uint8_t spriteColor[4];
    int32_t soundBank, music, textFile, eventText;
    FieldActorEntry **actors;
    uint8_t rest[0x80 - 0x50 - 4];
    FieldBattles *(*findBattles)(FieldBattles *list, int32_t id);
} FieldState;

typedef struct {
    int32_t files[8];
} FieldMap;

typedef struct {
    int32_t mode, file;
    void *init;
} StageEntry;

typedef struct {
    const char *name;
    void (*setup)(void);
    void *start;
} StageFuncs;

extern FieldState FIELDSTG_state;
extern FieldMap FIELDSTG_map;
extern StageEntry FIELDSTG_stages[], FIELDSTG_euStages[];
/* the banner's names of a mode: strings of text files STAREA and STNAME */
typedef struct {
    uint8_t area, place;
    int16_t mode;
} AreaName;
extern AreaName FIELDSTG_areaNames[];
/* each character's (Actor.key1) animation file entry, fid << 16 | entry, and width */
extern int32_t FIELDSTG_fileEntries[];
extern uint8_t FIELDSTG_actorWidths[];
#define CHARACTERS 406 /* the European FIELDSTG_fileEntries' length */
extern const StageFuncs native_stage_list[]; /* build.py's stage_list.c, up to name NULL */
void native_reset_overlays(int sub);

/* An event script's words, its end (0) included: each command's length by
   its kind and sub (FIELDSTG_runEvent) */
int native_script_length(const int16_t *script);

/* the stage whose start is FIELDSTG_state.stageInit, or NULL */
const StageFuncs *native_current_stage(void);

#endif
