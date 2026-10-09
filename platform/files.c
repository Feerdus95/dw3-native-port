/* The game's files: data/files/<fid>.bin from the user's disc
   (tools/extract_iso.py), read whole and at once. A mod's file in
   data/mods/<fid>.bin wins. Sectors stay 2048 bytes, the game's unit. */
#include <stdlib.h>
#include <string.h>
#include "platform.h"

#define SECTOR 2048
#define MAX_FILES 4096

static long s_size[MAX_FILES]; /* bytes; 0 unknown, -1 missing */

static FILE *open_file(int fid) {
    char path[1200];
    FILE *f;

    snprintf(path, sizeof path, "%s/mods/%04d.bin", data_dir(), fid);
    f = fopen(path, "rb");
    if (f == NULL) {
        snprintf(path, sizeof path, "%s/files/%04d.bin", data_dir(), fid);
        f = fopen(path, "rb");
    }
    return f;
}

static long file_size(int fid) {
    FILE *f;

    if (fid < 0 || fid >= MAX_FILES) {
        return -1;
    }
    if (s_size[fid] == 0) {
        f = open_file(fid);
        if (f == NULL) {
            s_size[fid] = -1;
        } else {
            fseek(f, 0, SEEK_END);
            s_size[fid] = ftell(f);
            fclose(f);
        }
    }
    return s_size[fid];
}

int native_file_exists(int fid) {
    return file_size(fid) > 0;
}

int native_file_sectors(int fid) {
    long size = file_size(fid);

    return size > 0 ? (int)((size + SECTOR - 1) / SECTOR) : 0;
}

/* `count` sectors from sector `sector`; past the end reads as zeroes */
void native_read_file(int fid, int sector, int count, void *dst) {
    FILE *f = open_file(fid);
    size_t got = 0;

    if (f == NULL) {
        LOG("files: no file %d", fid);
    } else {
        fseek(f, (long)sector * SECTOR, SEEK_SET);
        got = fread(dst, 1, (size_t)count * SECTOR, f);
        fclose(f);
    }
    memset((char *)dst + got, 0, (size_t)count * SECTOR - got);
}
