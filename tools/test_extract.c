/* tools/test_extract.py's program: test_extract IMAGE DATA [cancel-at-percent | names]; the exit code is native_extract's or native_disc_names's. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extract.h"

static int s_cancel_at = -1;

void native_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static int progress(int percent) { return s_cancel_at < 0 || percent < s_cancel_at; }

int main(int argc, char **argv) {
    if (argc > 3 && !strcmp(argv[3], "names")) {
        return native_disc_names(argv[1], argv[2], progress);
    }
    if (argc > 3) {
        s_cancel_at = atoi(argv[3]);
    }
    return native_extract(argv[1], argv[2], progress);
}
