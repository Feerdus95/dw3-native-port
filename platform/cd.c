/* libcd: the game reads files through readFile (patched to read data/files
   at once), so the drive itself only has to answer "done". The one thing it
   still reads from the "disc" is a movie's stream (CdRead2, movie.c). */
#include <string.h>
#include "platform.h"

void movie_stream_start(int pos); /* movie.c */
void movie_stream_stop(void);

static int s_loc; /* the last CdlSetloc */

typedef void (*CdlCB)(uint8_t, uint8_t *);

static CdlCB s_sync_cb, s_ready_cb;

int CdInit(void) {
    return 1;
}

int CdSetDebug(int level) {
    (void)level;
    return 0;
}

int CdControl(uint8_t com, uint8_t *param, uint8_t *result) {
    if (com == 0x02 && param != NULL) { /* CdlSetloc */
        memcpy(&s_loc, param, 4);
    } else if (com == 0x09) { /* CdlPause */
        movie_stream_stop();
    }
    return 1;
}

/* Streams from the last CdlSetloc (the movies) */
int CdRead2(long mode) {
    movie_stream_start(s_loc);
    return 1;
}

int CdControlB(uint8_t com, uint8_t *param, uint8_t *result) {
    return CdControl(com, param, result);
}

int CdControlF(uint8_t com, uint8_t *param) {
    return CdControl(com, param, NULL);
}

CdlCB CdSyncCallback(CdlCB f) {
    CdlCB old = s_sync_cb;

    s_sync_cb = f;
    return old;
}

CdlCB CdReadyCallback(CdlCB f) {
    CdlCB old = s_ready_cb;

    s_ready_cb = f;
    return old;
}

/* CdlLOC: the game only hands it back to CdControl, so natively it holds
   the sector as an int: the fake disc positions (fid * FILE_SECTOR_SPAN)
   don't fit its BCD minutes */
void *CdIntToPos(int i, void *p) {
    memcpy(p, &i, 4);
    return p;
}

int CdPosToInt(void *p) {
    int i;

    memcpy(&i, p, 4);
    return i;
}
