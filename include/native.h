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

/* Overlay variables back to their initial values: the mode overlays (sub 0)
   or the stages and WFIGHTMN/TS (sub 1) (platform/memory.c) */
void native_reset_overlays(int sub);

/* printf to stderr, for probes (platform/main.c) */
void native_log(const char *fmt, ...);

#endif /* NATIVE_H */
