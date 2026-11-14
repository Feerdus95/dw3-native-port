/* perf test (DW3_COLD): a disc read straight from the disk, as DirectStorage's is, past Windows' file cache (files.c). Kept apart: windows.h and platform.h clash */
#include <windows.h>
#include <malloc.h>
#include <string.h>
#define SECTOR 2048

void native_read_cold(const char *path, int sector, int count, void *dst) {
    wchar_t w[1300];
    HANDLE h;
    size_t got = 0;

    MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1300);
    h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        LONGLONG at = (LONGLONG)sector * SECTOR, start = at & ~4095LL;
        DWORD head = (DWORD)(at - start), len = (DWORD)((head + (DWORD)count * SECTOR + 4095) & ~4095u), n = 0;
        unsigned char *buf = _aligned_malloc(len, 4096);
        LARGE_INTEGER pos;

        pos.QuadPart = start;
        if (buf != NULL && SetFilePointerEx(h, pos, NULL, FILE_BEGIN) && ReadFile(h, buf, len, &n, NULL) && n > head) {
            got = n - head < (size_t)count * SECTOR ? n - head : (size_t)count * SECTOR;
            memcpy(dst, buf + head, got);
        }
        _aligned_free(buf);
        CloseHandle(h);
    }
    memset((char *)dst + got, 0, (size_t)count * SECTOR - got);
}
