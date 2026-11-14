/* DirectStorage (Microsoft, MIT) as a way to read the game's files: dstorage.dll is loaded at run time from beside the exe, and without it
   the reader thread's plain reads stay. The header is C++ only, so the few calls used are declared by hand; their sizes are checked in
   the static asserts below against what the header gives (sizeof DSTORAGE_REQUEST 104, DSTORAGE_CONFIGURATION1 32, offsets as written).
   Kept apart from files.c: windows.h and platform.h clash. */
#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN64
/* the layouts below are the 64-bit ones: the 32-bit build keeps the reader thread's plain reads */
int ds_ready(int buffered) { (void)buffered; return 0; }
int ds_start(const char *path, long long offset, unsigned bytes, void *dst) { (void)path, (void)offset, (void)bytes, (void)dst; return 0; }
int ds_wait(void) { return 0; }
#else
typedef struct { UINT64 a, b; } DsOptions; /* compression none, source a file, destination memory: all zeros */
typedef struct {
    DsOptions options;
    struct { void *file; UINT64 offset; UINT32 size; } source;
    struct { void *buffer; UINT32 size; } dest; /* the destination is a union of 40 bytes: memory is its first 16 */
    char pad[40 - 16];
    UINT32 uncompressed;
    UINT64 tag;
    const char *name;
} DsRequest;
typedef struct {
    UINT64 source_type; /* 0: files */
    UINT16 capacity;
    INT8 priority;
    const char *name;
    void *device;
} DsQueueDesc;
typedef struct {
    UINT32 submit_threads;
    INT32 cpu_decompression_threads;
    BOOL force_mapping, disable_bypassio, disable_telemetry, disable_metacommand, disable_gpu_decompression, force_buffering;
} DsConfig1;

/* the layout is not the C++ header's if these fail: the member names are DsRequest's, but its offsets are the header's */
_Static_assert(sizeof(DsRequest) == 104 && offsetof(DsRequest, dest) == 40 && offsetof(DsRequest, uncompressed) == 80 && offsetof(DsRequest, name) == 96, "DSTORAGE_REQUEST");
_Static_assert(sizeof(DsConfig1) == 32 && offsetof(DsConfig1, disable_telemetry) == 16 && offsetof(DsConfig1, force_buffering) == 28, "DSTORAGE_CONFIGURATION1");
_Static_assert(sizeof(DsQueueDesc) == 32 && offsetof(DsQueueDesc, name) == 16 && offsetof(DsQueueDesc, device) == 24, "DSTORAGE_QUEUE_DESC");

typedef struct ComUnk { HRESULT(STDMETHODCALLTYPE *qi)(void *, const GUID *, void **); ULONG(STDMETHODCALLTYPE *add)(void *); ULONG(STDMETHODCALLTYPE *release)(void *); } ComUnk;
typedef struct {
    ComUnk u;
    HRESULT(STDMETHODCALLTYPE *create_queue)(void *, const DsQueueDesc *, const GUID *, void **);
    HRESULT(STDMETHODCALLTYPE *open_file)(void *, const WCHAR *, const GUID *, void **);
    HRESULT(STDMETHODCALLTYPE *create_status)(void *, UINT32, const char *, const GUID *, void **);
} FactoryVt;
typedef struct {
    ComUnk u;
    void(STDMETHODCALLTYPE *enqueue_request)(void *, const DsRequest *);
    void(STDMETHODCALLTYPE *enqueue_status)(void *, void *, UINT32);
    void(STDMETHODCALLTYPE *enqueue_signal)(void *, void *, UINT64);
    void(STDMETHODCALLTYPE *submit)(void *);
} QueueVt;
typedef struct {
    ComUnk u;
    unsigned char(STDMETHODCALLTYPE *is_complete)(void *, UINT32);
    HRESULT(STDMETHODCALLTYPE *get_hresult)(void *, UINT32);
} StatusVt;
typedef struct { const void *vt; } Com;

static const GUID IID_FACTORY = {0x6924ea0c, 0xc3cd, 0x4826, {0xb1, 0x0a, 0xf6, 0x4f, 0x4e, 0xd9, 0x27, 0xc1}};
static const GUID IID_FILE = {0x5de95e7b, 0x955a, 0x4868, {0xa7, 0x3c, 0x24, 0x3b, 0x29, 0xf4, 0xb8, 0xda}};
static const GUID IID_STATUS = {0x82397587, 0x7cd5, 0x453b, {0xa0, 0x2e, 0x31, 0x37, 0x9b, 0xd6, 0x46, 0x56}};
static const GUID IID_QUEUE = {0xcfdbd83f, 0x9e06, 0x4fda, {0x8e, 0xa5, 0x69, 0x04, 0x21, 0x37, 0xf4, 0x9b}};

#define VT(p, T) ((const T *)((Com *)(p))->vt)

static Com *s_factory, *s_queue, *s_status;
static Com *s_file; /* the file of the request in flight */
static int s_state; /* 0 not tried, 1 ready, -1 not there */

/* 1 when dstorage.dll is beside the exe and a queue came up. `buffered`: the OS file cache stays on, for a hard disk (DW3_DS_BUFFERED) */
int ds_ready(int buffered)
{
    char path[MAX_PATH];
    HMODULE dll;
    HRESULT(WINAPI * get_factory)(const GUID *, void **);
    HRESULT(WINAPI * set_config)(const DsConfig1 *);
    DsConfig1 cfg = {0};
    DsQueueDesc desc = {0, 128, 0, "dw3 files", NULL}; /* 128: the least a queue may hold */
    HRESULT hq, hs;
    char *slash;

    if (s_state != 0) {
        return s_state > 0;
    }
    s_state = -1;
    slash = path + GetModuleFileNameA(NULL, path, sizeof path);
    while (slash > path && slash[-1] != '\\' && slash[-1] != '/') {
        slash--;
    }
    strcpy(slash, "dstorage.dll");
    dll = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    get_factory = dll ? (void *)GetProcAddress(dll, "DStorageGetFactory") : NULL;
    set_config = dll ? (void *)GetProcAddress(dll, "DStorageSetConfiguration1") : NULL;
    if (get_factory == NULL || set_config == NULL) {
        fprintf(stderr, "dstorage: no dstorage.dll beside the exe (%s)\n", path);
        return 0;
    }
    cfg.disable_telemetry = TRUE; /* the runtime reports usage to Microsoft unless told not to */
    cfg.disable_bypassio = cfg.force_buffering = buffered;
    if (FAILED(set_config(&cfg)) || FAILED(get_factory(&IID_FACTORY, (void **)&s_factory))) {
        fprintf(stderr, "dstorage: the factory didn't come up\n");
        return 0;
    }
    hq = VT(s_factory, FactoryVt)->create_queue(s_factory, &desc, &IID_QUEUE, (void **)&s_queue);
    hs = FAILED(hq) ? hq : VT(s_factory, FactoryVt)->create_status(s_factory, 1, "dw3 status", &IID_STATUS, (void **)&s_status);
    if (FAILED(hs)) {
        fprintf(stderr, "dstorage: no queue (%08lx)\n", (unsigned long)hs);
        return 0;
    }
    fprintf(stderr, "dstorage: ready%s\n", buffered ? " (OS file cache on)" : "");
    s_state = 1;
    return 1;
}

/* asks for BYTES from OFFSET of the file at PATH (UTF-8) into DST: 1 when it is queued, ds_wait says when it is read */
int ds_start(const char *path, long long offset, unsigned bytes, void *dst)
{
    WCHAR w[1300];
    DsRequest r = {0};

    if (s_state <= 0 || MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1300) == 0 ||
        FAILED(VT(s_factory, FactoryVt)->open_file(s_factory, w, &IID_FILE, (void **)&s_file))) {
        return 0;
    }
    r.source.file = s_file;
    r.source.offset = (UINT64)offset;
    r.source.size = bytes;
    r.dest.buffer = dst;
    r.dest.size = bytes;
    r.name = "dw3 read";
    VT(s_queue, QueueVt)->enqueue_request(s_queue, &r);
    VT(s_queue, QueueVt)->enqueue_status(s_queue, s_status, 0);
    VT(s_queue, QueueVt)->submit(s_queue);
    return 1;
}

/* blocks until the request is read (the reader thread's); 1 when it went well */
int ds_wait(void)
{
    HRESULT hr;

    while (!VT(s_status, StatusVt)->is_complete(s_status, 0)) {
        Sleep(0); /* ponytail: spins a core while a read is in flight; an event (IDStorageQueue1::EnqueueSetEvent) if it ever matters */
    }
    hr = VT(s_status, StatusVt)->get_hresult(s_status, 0);
    if (FAILED(hr)) {
        fprintf(stderr, "dstorage: a read failed (%08lx)\n", (unsigned long)hr);
    }
    VT(s_file, ComUnk)->release(s_file);
    s_file = NULL;
    return SUCCEEDED(hr);
}

#endif
