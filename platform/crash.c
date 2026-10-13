/* Windows crash report (separate from platform.h: windows.h has its own RECT) */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
/* A crash prints the faulting address and the frame-pointer chain (the game
   is built -O0); `llvm-addr2line -fe dw2003.exe <addr>...` names them. The
   same goes to crash.txt next to the exe, for a bug report */
static LONG WINAPI on_crash(EXCEPTION_POINTERS *e) {
    uint32_t *fp = (uint32_t *)e->ContextRecord->Ebp;
    char report[512], path[MAX_PATH + 16];
    int n, i;
    FILE *f;

    n = snprintf(report, sizeof report, "CRASH %08lx at %08lx (access %08lx)\nstack:", e->ExceptionRecord->ExceptionCode,
                 (unsigned long)e->ContextRecord->Eip, (unsigned long)e->ExceptionRecord->ExceptionInformation[1]);
    for (i = 0; i < 24 && fp != NULL && !IsBadReadPtr(fp, 8); i++, fp = (uint32_t *)fp[0]) {
        n += snprintf(report + n, sizeof report - n, " %08x", fp[1]);
    }
    fprintf(stderr, "%s\n", report);
    fflush(stderr);
    GetModuleFileNameA(NULL, path, MAX_PATH);
    strcpy(strrchr(path, '\\') + 1, "crash.txt");
    if ((f = fopen(path, "a")) != NULL) {
        fprintf(f, "%s\n", report);
        fclose(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void install_crash_handler(void) {
    SetUnhandledExceptionFilter(on_crash);
}
