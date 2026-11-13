/* The prepared launch slots (platform/launch_slots.c): for each of the 44 archives' two scripts, a script laid out as the manifest
   says is edited as it should be (operands negated, the wait moved before the first projectile), and one that differs from it is
   refused. The manifest is made by tools/generate_launch_manifest.py. Run by tools/run_tests.py. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "launch_slots.c"

const char *data_dir(void) { return "."; }
void native_log(const char *fmt, ...) {}

static void put(uint8_t *b, uint32_t at, int v) { put_word(b, at, (int16_t)v); }

/* the words of SCRIPT the editor looks at, as the original files have them */
static void lay_out(uint8_t *b, const NativeLaunchScript *s) {
    int i;

    put(b, s->model_preload, 6), put(b, s->model_preload + 2, 1), put(b, s->model_preload + 4, s->model);
    put(b, s->sprite_preload, 3), put(b, s->sprite_preload + 2, 1), put(b, s->sprite_preload + 4, s->sprite);
    for (i = 0; i < s->spawn_count; i++) {
        put(b, s->spawns[i].model_offset, 6), put(b, s->spawns[i].model_offset + 2, 0), put(b, s->spawns[i].model_offset + 4, s->model);
        put(b, s->spawns[i].sprite_offset, 3), put(b, s->spawns[i].sprite_offset + 2, 0), put(b, s->spawns[i].sprite_offset + 4, s->sprite);
    }
    put(b, s->sound_offset, 10), put(b, s->sound_offset + 2, s->sound), put(b, s->sound_offset + 4, s->duration);
    if (s->wait_offset != NATIVE_NO_WAIT) put(b, s->wait_offset, 11), put(b, s->wait_offset + 2, s->wait_frames);
}

static void check(const NativeLaunchScript *s, int fid, int which) {
    size_t size = s->end + 16;
    uint8_t *b = calloc(size, 1), *orig;
    int i;

    lay_out(b, s);
    orig = malloc(size);
    memcpy(orig, b, size);
    if (!edit_script(b, size, s)) {
        fprintf(stderr, "file %d script %d: refused\n", fid, which);
        exit(1);
    }
    assert(word(b, s->model_preload + 4) == -s->model && word(b, s->sprite_preload + 4) == -s->sprite);
    assert(word(b, s->sound_offset + 2) == -s->sound);
    if (s->wait_offset != NATIVE_NO_WAIT) {
        uint32_t first = s->spawns[0].model_offset;

        assert(word(b, first) == 11 && word(b, first + 2) == -s->wait_frames); /* the wait comes first, as a marker */
        for (i = 0; i < s->spawn_count; i++) {
            assert(word(b, first + 4 + 30u * i + 4) == -s->model && word(b, first + 4 + 30u * i + 22) == -s->sprite);
        }
        assert(word(b, s->wait_offset + 2) != s->wait_frames); /* the old wait's place now holds a projectile's words */
    } else {
        assert(word(b, s->spawns[0].model_offset + 4) == -s->model);
    }
    /* a script whose model is not the expected one is refused, untouched */
    memcpy(b, orig, size);
    put(b, s->model_preload + 4, s->model + 1);
    assert(!edit_script(b, size, s));
    free(b);
    free(orig);
}

int main(void) {
    int i;

    assert(NATIVE_LAUNCH_ARCHIVE_COUNT == 44);
    for (i = 0; i < NATIVE_LAUNCH_ARCHIVE_COUNT; i++) {
        const NativeLaunchArchive *a = &NATIVE_LAUNCH_ARCHIVES[i];

        assert(native_launch_archive_listed(a->file_id) && native_launch_archive_matches(a->file_id << 16 | a->archive_entry));
        check(&a->scripts[0], a->file_id, 9);
        check(&a->scripts[1], a->file_id, 13);
    }
    assert(!native_launch_archive_listed(0) && !native_launch_archive_matches(0));
    printf("ok\n");
    return 0;
}
