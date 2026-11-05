/* The forms the port adds to a partner (platform/forms.c): kept in the game being
   played and saved in the memory card file's unused tail, one area a save slot.
   Included by native.h (the game's) and the platform's own files. */
#ifndef NATIVE_FORMS_H
#define NATIVE_FORMS_H

#define NATIVE_SAVE_BYTES 0x26C4 /* a slot's save, the EU's GAME_SAVE_SIZE: platform/memcard.c has no game headers */
#define NATIVE_FORM_MAX 66 /* records an area holds: 8 partners' 8 forms and a few over */

typedef struct NativeForm {
    unsigned char partner;
    unsigned short id; /* the form's id as the game has it (471 BladeGarurumon) */
    unsigned char level;
    unsigned exp;    /* 24 bits */
    unsigned skills; /* 24 bits: the form's skills, 3 bits each (known and marked, as the game's flags) */
} NativeForm;

/* whether the partner has the form (1), and its record in OUT */
int native_form_get(int partner, int id, NativeForm *out);
/* adds the form or replaces its record; 0 when the store is full */
int native_form_set(const NativeForm *f);
/* the ids of the partner's forms, up to MAX; how many */
int native_form_list(int partner, unsigned short *ids, int max);
/* the old way's form (the partner's pad bytes) as a slot just loaded has it: 1 when it became the game's */
int native_form_merge_legacy(const NativeForm *old);
/* a new game has none */
void native_forms_reset(void);
/* a load has put its slot into GAME: the slot's forms are the game's. Also main/game/partner.c's native_extra_loaded,
   which then reads a save from before the areas */
void native_forms_commit(void);

/* main/game/partner.c: a slot is in GAME (stgmcard/saves.c calls it after a load) */
void native_extra_loaded(void);

/* the card's side (platform/memcard.c): TAIL is the file's last 0x800 bytes */
/* SLOT_CRC is native_slot_crc of the slot's save as it is on the card now; an area written with a different one is STALE: a
   save was written to the slot since by something that doesn't know the areas, so its forms belong to another game */
unsigned native_slot_crc(const unsigned char *save);
int native_forms_stage(const unsigned char *tail, int slot, unsigned slot_crc);
int native_forms_write(unsigned char *tail, int slot, unsigned slot_crc);
int native_forms_area_state(const unsigned char *tail, int slot, unsigned slot_crc);
enum { NATIVE_AREA_BLANK, NATIVE_AREA_OK, NATIVE_AREA_BAD, NATIVE_AREA_NEWER, NATIVE_AREA_FOREIGN, NATIVE_AREA_STALE };

#endif
