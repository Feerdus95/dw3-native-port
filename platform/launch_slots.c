/* Prepare player script 9/13 launch slots from the owner's extracted disc.
   The release contains only offsets and expected values, never game archives.
   User data/mods overrides take precedence over these generated copies. */
#define _WIN32_WINNT 0x0A00
#include <SDL3/SDL.h>
#include <windows.h>
#include <bcrypt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "launch_slots.h"
#include "launch_manifest.h"

const char *data_dir(void);
void native_log(const char *fmt, ...);

static int s_ready;

int native_launch_archives_ready(void) {
    return s_ready;
}

static int16_t word(const uint8_t *bytes, uint32_t at) {
    return (int16_t)(bytes[at] | ((uint16_t)bytes[at + 1] << 8));
}

static void put_word(uint8_t *bytes, uint32_t at, int16_t value) {
    bytes[at] = (uint8_t)value;
    bytes[at + 1] = (uint8_t)((uint16_t)value >> 8);
}

static int mark(uint8_t *bytes, const NativeLaunchScript *script,
                uint32_t at, int opcode, int mode, int original) {
    uint32_t operand = at + (mode < 0 ? 2 : 4);
    if (at < script->start || operand + 2 > script->end ||
        word(bytes, at) != opcode ||
        (mode >= 0 && word(bytes, at + 2) != mode) ||
        word(bytes, operand) != original) return 0;
    put_word(bytes, operand, (int16_t)-original);
    return 1;
}

static int edit_script(uint8_t *bytes, size_t size, const NativeLaunchScript *script) {
    uint32_t first, last, wait_at;
    uint8_t wait_bytes[4];
    int i;

    if (script->start >= script->end || script->end > size ||
        script->spawn_count < 1 || script->spawn_count > NATIVE_LAUNCH_SPAWNS ||
        !mark(bytes, script, script->model_preload, 6, 1, script->model) ||
        !mark(bytes, script, script->sprite_preload, 3, 1, script->sprite)) return 0;
    first = script->spawns[0].model_offset;
    for (i = 0; i < script->spawn_count; i++) {
        const NativeLaunchSpawn *spawn = &script->spawns[i];
        if (spawn->model_offset != first + 30u * i ||
            spawn->sprite_offset != spawn->model_offset + 18 ||
            !mark(bytes, script, spawn->model_offset, 6, 0, script->model) ||
            !mark(bytes, script, spawn->sprite_offset, 3, 0, script->sprite)) return 0;
    }
    if (!mark(bytes, script, script->sound_offset, 10, -1, script->sound) ||
        script->sound_offset + 6 > script->end ||
        word(bytes, script->sound_offset + 4) != script->duration) return 0;
    last = script->spawns[script->spawn_count - 1].sprite_offset;
    wait_at = script->wait_offset;
    if (wait_at == NATIVE_NO_WAIT) {
        return script->spawn_count == 1 && script->sound_offset == first + 30;
    }
    if (first < script->start || wait_at + 4 != script->sound_offset ||
        wait_at - first != 30u * script->spawn_count ||
        last + 12 != wait_at ||
        word(bytes, wait_at) != 11 ||
        word(bytes, wait_at + 2) != script->wait_frames ||
        script->wait_frames <= 0) return 0;

    memcpy(wait_bytes, bytes + wait_at, sizeof wait_bytes);
    memmove(bytes + first + 4, bytes + first, wait_at - first);
    memcpy(bytes + first, wait_bytes, sizeof wait_bytes);
    put_word(bytes, first + 2, (int16_t)-script->wait_frames);
    return 1;
}

static uint8_t *read_original(int file_id, size_t *size) {
    char path[1200];
    FILE *file;
    long length;
    uint8_t *bytes;

    snprintf(path, sizeof path, "%s/files/%04d.bin", data_dir(), file_id);
    file = fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        length > 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0 ||
        (bytes = malloc((size_t)length)) == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return bytes;
}

/* Recheck three megabytes on startup so new builds and interrupted writes
   repair themselves; unchanged files are not written again. */
static int write_if_changed(int file_id, const uint8_t *bytes, size_t size) {
    char path[1200], temp[1210];
    uint8_t chunk[4096];
    FILE *file;
    size_t offset = 0;
    int same = 1;

    snprintf(path, sizeof path, "%s/generated/%04d.bin", data_dir(), file_id);
    file = fopen(path, "rb");
    if (file != NULL) {
        while (offset < size) {
            size_t count = size - offset < sizeof chunk ? size - offset : sizeof chunk;
            if (fread(chunk, 1, count, file) != count ||
                memcmp(chunk, bytes + offset, count) != 0) {
                same = 0;
                break;
            }
            offset += count;
        }
        if (same && fgetc(file) != EOF) same = 0;
        fclose(file);
        if (same) return 1;
    }
    snprintf(temp, sizeof temp, "%s.tmp", path);
    file = fopen(temp, "wb");
    if (file == NULL) return 0;
    size_t written = fwrite(bytes, 1, size, file);
    int closed = fclose(file);
    if (written != size || closed != 0) {
        SDL_RemovePath(temp);
        return 0;
    }
    if (!SDL_RenamePath(temp, path)) {
        SDL_RemovePath(temp);
        return 0;
    }
    return 1;
}

int native_prepare_launch_archives(void) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    char dir[1200];
    int i;

    s_ready = 0;
    snprintf(dir, sizeof dir, "%s/generated", data_dir());
    if (!SDL_CreateDirectory(dir) ||
        BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0) {
        native_log("technique archives: cannot initialize local generation");
        return 0;
    }
    for (i = 0; i < NATIVE_LAUNCH_ARCHIVE_COUNT; i++) {
        const NativeLaunchArchive *entry = &NATIVE_LAUNCH_ARCHIVES[i];
        uint8_t digest[32];
        size_t size = 0;
        uint8_t *bytes = read_original(entry->file_id, &size);
        int good = bytes != NULL &&
                   BCryptHash(algorithm, NULL, 0, bytes, (ULONG)size,
                              digest, sizeof digest) >= 0 &&
                   memcmp(digest, entry->source_sha256, sizeof digest) == 0 &&
                   edit_script(bytes, size, &entry->scripts[0]) &&
                   edit_script(bytes, size, &entry->scripts[1]) &&
                   write_if_changed(entry->file_id, bytes, size);
        free(bytes);
        if (!good) {
            native_log("technique archives: could not prepare file %04d from this disc",
                       entry->file_id);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return 0;
        }
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    s_ready = 1;
    return 1;
}
