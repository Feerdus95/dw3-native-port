/* The Digimon the port adds to a partner (BladeGarurumon, and the ones after),
   kept past the game's 44 and saved in the memory card's own file: its last 0x800
   bytes, which the game never reads or writes (kb/docs/save_flow.md, 0x7800), one
   area of 680 bytes a save slot. Nothing in the game's own sections changes, so the
   card is still a standard one and its checksums hold; a PSX or a port without
   this just doesn't see the forms.

   An area is   "DW3X"  u8 version  u8 flags  u16 count  u32 crc32(records)  [version 2: u32 crc32(the slot's save)]
                count x { u8 partner, u16 form, u8 level, u24 exp, u24 skills }
   Version 2 ties the area to the save it was written with: a save written since by something that doesn't know
   the areas (the original game, an emulator, an imported card) leaves the area STALE, and its forms aren't loaded.

   The game's state in this file is the store (the forms of the game being played,
   SAVED: a savestate has them); the card is where it is kept. A load stages its
   slot's area, and commits it once the slot is in GAME (native_forms_commit); a save
   writes the store to its slot's area when the slot's data is. The pure part
   (encode, decode, the store) has no card or SDL in it: tools/test_forms.c runs it.

   Migration: a save from before the areas has its BladeGarurumon in the partners'
   pad bytes (main/game/partner.c's old storage): a load with no area for a form
   reads it from there (native_extra_loaded), so the first save writes it as a record.
   The pad bytes are left as they were. */
#include <stdint.h>
#include <string.h>
#include "platform.h"
#include "../include/native_forms.h"

#define AREA_VERSION 2
#define AREA_HEADER_V1 12
#define AREA_HEADER 16
#define RECORD_SIZE 10
#define SLOT_COUNT 3
#define AREA_SIZE 680 /* 3 areas in the 0x800 of the tail */

typedef struct Forms {
    NativeForm f[NATIVE_FORM_MAX];
    int n;
} Forms;

#define AREA_BLANK NATIVE_AREA_BLANK
#define AREA_OK NATIVE_AREA_OK
#define AREA_BAD NATIVE_AREA_BAD
#define AREA_NEWER NATIVE_AREA_NEWER
#define AREA_FOREIGN NATIVE_AREA_FOREIGN
#define AREA_STALE NATIVE_AREA_STALE

SAVED static Forms s_store;
static Forms s_stage;
static int s_staged;

static uint32_t crc32(const uint8_t *p, int n) {
    uint32_t c = 0xFFFFFFFFu;
    int i, k;

    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) {
            c = c >> 1 ^ (0xEDB88320u & -(c & 1));
        }
    }
    return ~c;
}

static int all_same(const uint8_t *p, int n) {
    int i;

    for (i = 1; i < n; i++) {
        if (p[i] != p[0]) {
            return 0;
        }
    }
    return p[0] == 0x00 || p[0] == 0xFF;
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

/* what identifies a slot's save, for the areas of version 2 */
uint32_t native_slot_crc(const unsigned char *save) {
    return crc32(save, NATIVE_SAVE_BYTES);
}

/* AREA's records into OUT (which it may leave half filled when it isn't OK). SLOT_CRC is the slot's save now; a version 1
   area has nothing to compare it with and is taken as it is (the next write makes it version 2) */
static int area_decode(const uint8_t *a, uint32_t slot_crc, Forms *out) {
    int n, i, header;
    uint32_t crc;

    out->n = 0;
    if (memcmp(a, "DW3X", 4) != 0) {
        return all_same(a, AREA_SIZE) ? AREA_BLANK : AREA_FOREIGN;
    }
    if (a[4] > AREA_VERSION) {
        return AREA_NEWER;
    }
    n = a[6] | a[7] << 8;
    crc = get32(a + 8);
    header = a[4] == 1 ? AREA_HEADER_V1 : AREA_HEADER;
    if (a[4] == 0 || n > NATIVE_FORM_MAX || crc32(a + header, n * RECORD_SIZE) != crc) {
        return AREA_BAD;
    }
    for (i = 0; i < n; i++) {
        const uint8_t *r = a + header + i * RECORD_SIZE;
        NativeForm *f = &out->f[i];

        f->partner = r[0];
        f->id = (uint16_t)(r[1] | r[2] << 8);
        f->level = r[3];
        f->exp = (uint32_t)r[4] | (uint32_t)r[5] << 8 | (uint32_t)r[6] << 16;
        f->skills = (uint32_t)r[7] | (uint32_t)r[8] << 8 | (uint32_t)r[9] << 16;
    }
    out->n = n;
    return a[4] >= 2 && get32(a + 12) != slot_crc ? AREA_STALE : AREA_OK;
}

static void area_encode(uint8_t *a, const Forms *in, uint32_t slot_crc) {
    int i;
    uint32_t crc;

    memset(a, 0xFF, AREA_SIZE);
    memcpy(a, "DW3X", 4);
    a[4] = AREA_VERSION;
    a[5] = 0;
    a[6] = (uint8_t)in->n;
    a[7] = (uint8_t)(in->n >> 8);
    for (i = 0; i < in->n; i++) {
        uint8_t *r = a + AREA_HEADER + i * RECORD_SIZE;
        const NativeForm *f = &in->f[i];

        r[0] = f->partner;
        r[1] = (uint8_t)f->id, r[2] = (uint8_t)(f->id >> 8);
        r[3] = f->level;
        r[4] = (uint8_t)f->exp, r[5] = (uint8_t)(f->exp >> 8), r[6] = (uint8_t)(f->exp >> 16);
        r[7] = (uint8_t)f->skills, r[8] = (uint8_t)(f->skills >> 8), r[9] = (uint8_t)(f->skills >> 16);
    }
    crc = crc32(a + AREA_HEADER, in->n * RECORD_SIZE);
    put32(a + 8, crc);
    put32(a + 12, slot_crc);
}

/* --- the store --- */

static int find(const Forms *s, int partner, int id) {
    int i;

    for (i = 0; i < s->n; i++) {
        if (s->f[i].partner == partner && s->f[i].id == id) {
            return i;
        }
    }
    return -1;
}

int native_form_get(int partner, int id, NativeForm *out) {
    int i = find(&s_store, partner, id);

    if (i >= 0 && out != NULL) {
        *out = s_store.f[i];
    }
    return i >= 0;
}

int native_form_set(const NativeForm *f) {
    int i = find(&s_store, f->partner, f->id);

    if (i < 0) {
        if (s_store.n >= NATIVE_FORM_MAX) {
            return 0;
        }
        i = s_store.n++;
    }
    s_store.f[i] = *f;
    s_store.f[i].exp &= 0xFFFFFF;
    s_store.f[i].skills &= 0xFFFFFF;
    return 1;
}

int native_form_list(int partner, unsigned short *ids, int max) {
    int i, n = 0;

    for (i = 0; i < s_store.n && n < max; i++) {
        if (s_store.f[i].partner == partner) {
            ids[n++] = s_store.f[i].id;
        }
    }
    return n;
}

/* A form kept the old way (BladeGarurumon in the partner's pad bytes, before the areas) as the slot just loaded has it: made the
   game's when it has no record, or got further than the record (a save played in an older port since); 1 when it was */
int native_form_merge_legacy(const NativeForm *old) {
    NativeForm now;

    if (native_form_get(old->partner, old->id, &now) && (now.level > old->level || (now.level == old->level && now.exp >= old->exp))) {
        return 0;
    }
    return native_form_set(old);
}

/* a new game: none */
void native_forms_reset(void) {
    s_store.n = 0;
    s_staged = 0;
}

/* --- the card: TAIL is the file's last 0x800 bytes --- */

/* What slot SLOT's area is, staging its records for the load that's reading the slot */
int native_forms_stage(const unsigned char *tail, int slot, uint32_t slot_crc) {
    int state = area_decode(tail + slot * AREA_SIZE, slot_crc, &s_stage);

    s_staged = 1;
    if (state != AREA_OK) {
        s_stage.n = 0;
    }
    return state;
}

/* the load's slot is in GAME: its forms are the game's */
void native_forms_commit(void) {
    if (s_staged) {
        s_store = s_stage;
        s_staged = 0;
    }
}

/* the area to write for slot SLOT into TAIL: 0 written; -1 when what's there isn't ours (a newer version, or other data)
   and is left as it is */
int native_forms_write(unsigned char *tail, int slot, uint32_t slot_crc) {
    Forms old;
    int state = area_decode(tail + slot * AREA_SIZE, slot_crc, &old);

    if (state == AREA_NEWER || state == AREA_FOREIGN) {
        return -1;
    }
    area_encode(tail + slot * AREA_SIZE, &s_store, slot_crc);
    return 0;
}

/* whether slot SLOT's area is one a write would change its state of (for the card's backup before the first one) */
int native_forms_area_state(const unsigned char *tail, int slot, uint32_t slot_crc) {
    Forms tmp;

    return area_decode(tail + slot * AREA_SIZE, slot_crc, &tmp);
}
