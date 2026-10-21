/* The extra forms' store and their card areas (platform/forms.c), without a card or a game. Run from build/:
   i686-w64-mingw32-clang -I<SDL>/include -I../platform ../tools/test_forms.c ../platform/forms.c -o test_forms.exe && ./test_forms.exe */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "../include/native_forms.h"

#define TAIL 0x800
#define AREA 680

static NativeForm form(int partner, int id, int level, unsigned exp, unsigned skills) {
    NativeForm f = {(unsigned char)partner, (unsigned short)id, (unsigned char)level, exp, skills};
    return f;
}

int main(void) {
    unsigned char tail[TAIL], copy[TAIL];
    NativeForm f, g;
    unsigned short ids[8];
    int i;

    /* a card from before the areas: its tail is 0xFF, or 0 in a card the port made */
    memset(tail, 0xFF, TAIL);
    for (i = 0; i < 3; i++) assert(native_forms_area_state(tail, i) == NATIVE_AREA_BLANK);
    memset(tail, 0, TAIL);
    for (i = 0; i < 3; i++) assert(native_forms_area_state(tail, i) == NATIVE_AREA_BLANK);
    memset(tail, 0xFF, TAIL);

    /* the store */
    native_forms_reset();
    assert(!native_form_get(2, 471, &g));
    f = form(2, 471, 33, 0x12345678, 0xAB1234); /* 24 bits of each are kept */
    assert(native_form_set(&f));
    assert(native_form_get(2, 471, &g) && g.level == 33 && g.exp == 0x345678 && g.skills == 0xAB1234);
    f = form(2, 471, 34, 5, 6);
    assert(native_form_set(&f)); /* replaces, doesn't add */
    f = form(2, 472, 1, 0, 0);
    assert(native_form_set(&f));
    f = form(5, 471, 7, 70, 3);
    assert(native_form_set(&f));
    assert(native_form_list(2, ids, 8) == 2 && ids[0] == 471 && ids[1] == 472);
    assert(native_form_list(5, ids, 8) == 1 && ids[0] == 471);
    assert(native_form_list(0, ids, 8) == 0);
    assert(native_form_get(2, 471, &g) && g.level == 34);

    /* writing slot 1's area leaves the others alone, and reads back */
    assert(native_forms_write(tail, 1) == 0);
    for (i = 0; i < AREA; i++) assert(tail[i] == 0xFF && tail[2 * AREA + i] == 0xFF);
    assert(native_forms_area_state(tail, 1) == NATIVE_AREA_OK);
    native_forms_reset();
    assert(native_forms_stage(tail, 1) == NATIVE_AREA_OK);
    assert(!native_form_get(2, 471, &g)); /* staged, not the game's yet */
    native_forms_commit();
    assert(native_form_get(2, 471, &g) && g.level == 34 && g.exp == 5 && g.skills == 6);
    assert(native_form_get(5, 471, &g) && g.level == 7 && g.exp == 70);
    assert(native_form_list(2, ids, 8) == 2);

    /* writing it again changes nothing */
    memcpy(copy, tail, TAIL);
    assert(native_forms_write(tail, 1) == 0);
    assert(memcmp(copy, tail, TAIL) == 0);

    /* a slot with no area stages none, and a commit then clears the store (the loaded slot has no forms) */
    assert(native_forms_stage(tail, 0) == NATIVE_AREA_BLANK);
    native_forms_commit();
    assert(!native_form_get(2, 471, &g));

    /* a damaged area: its records are ignored, the card's bytes aren't touched until a save rewrites it */
    memcpy(copy, tail, TAIL);
    tail[AREA + 20] ^= 0x55;
    assert(native_forms_area_state(tail, 1) == NATIVE_AREA_BAD);
    assert(native_forms_stage(tail, 1) == NATIVE_AREA_BAD);
    native_forms_commit();
    assert(!native_form_get(2, 471, &g));
    assert(native_forms_write(tail, 1) == 0); /* the corrupt one is ours: rewritten */
    assert(native_forms_area_state(tail, 1) == NATIVE_AREA_OK);
    memcpy(tail, copy, TAIL);
    tail[AREA + 6] = 0xFF, tail[AREA + 7] = 0xFF; /* a count past the area */
    assert(native_forms_area_state(tail, 1) == NATIVE_AREA_BAD);
    memcpy(tail, copy, TAIL);

    /* a newer version's area is not read or written */
    tail[AREA + 4] = 2;
    memcpy(copy, tail, TAIL);
    assert(native_forms_area_state(tail, 1) == NATIVE_AREA_NEWER);
    assert(native_forms_stage(tail, 1) == NATIVE_AREA_NEWER);
    native_forms_commit();
    assert(native_forms_write(tail, 1) == -1);
    assert(memcmp(copy, tail, TAIL) == 0);

    /* other data in the tail isn't ours to overwrite */
    memset(tail, 0xFF, TAIL);
    tail[10] = 0x42;
    assert(native_forms_area_state(tail, 0) == NATIVE_AREA_FOREIGN);
    memcpy(copy, tail, TAIL);
    assert(native_forms_write(tail, 0) == -1 && memcmp(copy, tail, TAIL) == 0);
    assert(native_forms_write(tail, 1) == 0); /* the other slots are free */

    /* the store holds its limit, and a full area round trips */
    native_forms_reset();
    for (i = 0; i < NATIVE_FORM_MAX; i++) {
        f = form(i % 8, 400 + i / 8, i + 1, i * 1000, i);
        assert(native_form_set(&f));
    }
    f = form(0, 999, 1, 0, 0);
    assert(!native_form_set(&f));
    memset(tail, 0xFF, TAIL);
    assert(native_forms_write(tail, 2) == 0);
    native_forms_reset();
    assert(native_forms_stage(tail, 2) == NATIVE_AREA_OK);
    native_forms_commit();
    for (i = 0; i < NATIVE_FORM_MAX; i++) {
        assert(native_form_get(i % 8, 400 + i / 8, &g) && g.level == i + 1 && g.exp == (unsigned)i * 1000 && g.skills == (unsigned)i);
    }
    assert(12 + NATIVE_FORM_MAX * 10 <= AREA); /* the area holds them all */

    printf("test_forms: ok\n");
    return 0;
}
