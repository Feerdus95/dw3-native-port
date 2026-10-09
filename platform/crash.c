/* Windows crash report (separate from platform.h: windows.h has its own RECT) */
#include <stdint.h>
#include <stdio.h>
#include <windows.h>
/* A crash prints the faulting address and the frame-pointer chain (the game
   is built -O0); `llvm-addr2line -fe dw2003.exe <addr>...` names them */
static LONG WINAPI on_crash(EXCEPTION_POINTERS *e) {
    uint32_t *fp = (uint32_t *)e->ContextRecord->Ebp;
    int i;

    fprintf(stderr, "CRASH %08lx at %08lx (access %08lx)\nstack:", e->ExceptionRecord->ExceptionCode,
            (unsigned long)e->ContextRecord->Eip, (unsigned long)e->ExceptionRecord->ExceptionInformation[1]);
    for (i = 0; i < 24 && fp != NULL && !IsBadReadPtr(fp, 8); i++, fp = (uint32_t *)fp[0]) {
        fprintf(stderr, " %08x", fp[1]);
    }
    fputc('\n', stderr);
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

void install_crash_handler(void) {
    SetUnhandledExceptionFilter(on_crash);
}
