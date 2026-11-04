/* The player's disc, read once into data/: extract.c, in both the game (--extract) and the launcher. */
#pragma once

enum { E_NONE, E_OPEN, E_GAME, E_BAD, E_WRITE, E_READ, E_TRUNCATED, E_CUE, E_SPACE, E_CANCELLED };

/* `progress` (if any) hears how far, in percent, and returns 0 to stop. E_NONE when the game is in `data`,
   else what went wrong, with `data` as it was. */
int native_extract(const char *image, const char *data, int (*progress)(int percent));
/* Only data/names.txt, for a game read before the extraction wrote it. The same results. */
int native_disc_names(const char *image, const char *data, int (*progress)(int percent));
const char *native_extract_error(int e); /* in English, for the log */
