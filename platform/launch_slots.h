#ifndef NATIVE_LAUNCH_SLOTS_H
#define NATIVE_LAUNCH_SLOTS_H

#include <stdint.h>

#define NATIVE_LAUNCH_SPAWNS 3
#define NATIVE_NO_WAIT UINT32_MAX

typedef struct {
    uint32_t model_offset;
    uint32_t sprite_offset;
} NativeLaunchSpawn;

typedef struct {
    uint32_t start, end;
    uint32_t model_preload, sprite_preload, sound_offset, wait_offset;
    int16_t model, sprite, sound, duration, wait_frames;
    uint8_t spawn_count;
    NativeLaunchSpawn spawns[NATIVE_LAUNCH_SPAWNS];
} NativeLaunchScript;

typedef struct {
    uint16_t file_id;
    uint8_t source_sha256[32];
    NativeLaunchScript scripts[2]; /* indices 9 and 13 */
} NativeLaunchArchive;

int native_prepare_launch_archives(void);
int native_launch_archives_ready(void);

#endif
