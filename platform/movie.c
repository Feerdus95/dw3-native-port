/* The movies: libcd's streaming (St*) and libpress's MDEC (DecDCT*), our own.

   A movie is data/files/<fid>.str (whole 2336-byte sectors, extract.c). Once CdRead2 starts it, the "disc" reads 150 sectors a
   second (double speed) on each vsync (movie_vblank): video sectors (a
   32-byte StHEADER, then 2016 bytes of the frame) build up frames, and XA
   audio sectors are decoded and sent to the SPU's CD input. StGetNext waits
   for the next whole frame, as the drive made the game wait.

   The MDEC part: DecDCTvlc2 turns a frame's bitstream (version 2: 10-bit DC,
   MPEG-1 AC codes) into the MDEC's run-level halfwords; DecDCTin takes them
   and DecDCTout decodes 16x16 macroblocks (Cr, Cb, Y0-Y3, column by column)
   into 24-bit RGB, calling the game's callback after each slice as the DMA
   did. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"

void native_wait_vblank(void);

#define RAW_SECTOR 2336
#define FRAME_SECTOR 2016 /* frame bytes in a video sector */
#define MAX_FRAME (32 * FRAME_SECTOR)
#define QUEUE 3
#define SPAN 0x10000 /* NATIVE_FILE_SECTOR_SPAN */

typedef struct {
    uint16_t id, type, secCount, nSectors;
    uint32_t frameCount, frameSize;
    uint16_t width, height;
    uint32_t dummy1, dummy2;
    uint8_t loc[4];
} StHEADER;

typedef struct {
    StHEADER header;
    uint32_t data[MAX_FRAME / 4];
} Frame;

uint32_t StCdIntrFlag; /* a variable in libcd, which the player polls */

static FILE *s_file;
static double s_due; /* sectors the drive owes */
static int s_ended = 1;
static Frame s_queue[QUEUE + 1]; /* QUEUE ready + one being built */
static int s_head, s_ready;
static StHEADER s_end_header = {0x0160, 0x8001, 0, 1, 0x7FFFFFFF, 0, 16, 16};
static uint32_t s_dummy[64];

/* --- XA audio --- */

static int16_t s_xa_hist[2][2];
static double s_xa_phase; /* between the last two input frames, 0-1 */
static int16_t s_xa_last[2];

/* To 44.1 kHz, linearly, and on to the SPU's CD input */
static void xa_out(const int16_t *lr, int frames, int rate) {
    static int16_t out[8192][2];
    double step = (double)rate / AUDIO_RATE;
    int n = 0, i;

    for (i = 0; i < frames; i++) {
        while (s_xa_phase < 1.0 && n < 8192) {
            out[n][0] = (int16_t)(s_xa_last[0] + (lr[i * 2] - s_xa_last[0]) * s_xa_phase);
            out[n][1] = (int16_t)(s_xa_last[1] + (lr[i * 2 + 1] - s_xa_last[1]) * s_xa_phase);
            n++;
            s_xa_phase += step;
        }
        s_xa_phase -= 1.0;
        s_xa_last[0] = lr[i * 2];
        s_xa_last[1] = lr[i * 2 + 1];
    }
    spu_cd_push(&out[0][0], n);
}

/* An XA audio sector: 18 groups of 128 bytes, each with 8 units of 28 4-bit
   samples (or 4 of 8-bit) and their filters and shifts; in stereo the even
   units are the left channel and the odd ones the right */
static void xa_sector(const uint8_t *sub, const uint8_t *data) {
    static const int K0[4] = {0, 60, 115, 98}, K1[4] = {0, 0, -52, -55};
    static int16_t pcm[18 * 8 * 28][2];
    int stereo = sub[3] & 1, rate = sub[3] & 4 ? 18900 : 37800, bits8 = sub[3] & 16;
    int units = bits8 ? 4 : 8, per_group = units * 28 / (stereo ? 2 : 1), g, u, i;

    for (g = 0; g < 18; g++) {
        const uint8_t *grp = data + g * 128;

        for (u = 0; u < units; u++) {
            int hdr = grp[4 + u], shift = hdr & 15, filter = hdr >> 4 & 3, ch = stereo ? u & 1 : 0;
            int16_t *h = s_xa_hist[ch], *o = pcm[g * per_group + (stereo ? u >> 1 : u) * 28];

            if (shift > 12) {
                shift = 9;
            }
            for (i = 0; i < 28; i++) {
                int32_t s;

                if (bits8) {
                    s = (int16_t)(grp[16 + i * 4 + u] << 8) >> shift;
                } else {
                    s = (int16_t)((grp[16 + i * 4 + (u >> 1)] >> (u & 1) * 4 & 15) << 12) >> shift;
                }
                s += (h[0] * K0[filter] + h[1] * K1[filter] + 32) >> 6;
                s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
                h[1] = h[0];
                h[0] = (int16_t)s;
                if (stereo) {
                    o[i * 2 + ch] = (int16_t)s;
                } else {
                    o[i * 2] = o[i * 2 + 1] = (int16_t)s;
                }
            }
        }
    }
    xa_out(&pcm[0][0], 18 * per_group, rate);
}

/* --- the stream --- */

int movie_playing(void) {
    return s_file != NULL;
}

void movie_stream_stop(void) {
    if (s_file != NULL) {
        fclose(s_file);
        s_file = NULL;
    }
    s_ready = 0;
    s_ended = 1;
    spu_cd_clear();
}

static int s_cooked; /* the movie is a .bin: 2048-byte sectors, no subheaders */

void movie_stream_start(int pos) {
    char path[1200];

    movie_stream_stop();
    snprintf(path, sizeof path, "%s/files/%04d.str", data_dir(), pos / SPAN);
    s_file = fopen(path, "rb");
    s_cooked = s_file == NULL;
    if (s_cooked) {
        /* from a plain .iso: the 2048-byte copy, its pictures without the sound */
        snprintf(path, sizeof path, "%s/files/%04d.bin", data_dir(), pos / SPAN);
        if ((s_file = fopen(path, "rb")) == NULL) {
            LOG("no movie %s", path);
            return;
        }
        log_once("movies without sound: the disc was read from a plain .iso");
    }
    LOGC(LOG_CD, "cd: movie %04d from sector %d", pos / SPAN, pos % SPAN);
    fseek(s_file, (long)(pos % SPAN) * (s_cooked ? 2048 : RAW_SECTOR), SEEK_SET);
    s_due = 0;
    s_ended = 0;
    s_head = 0;
    memset(s_xa_hist, 0, sizeof s_xa_hist);
    s_xa_phase = 0;
}

/* One sector off the disc; 0 at the end */
static int read_sector(void) {
    static uint8_t raw[RAW_SECTOR];
    const StHEADER *h = (const StHEADER *)(raw + 8);
    Frame *f = &s_queue[(s_head + s_ready) % (QUEUE + 1)];

    if (s_cooked) {
        /* no subheader: a video sector shows by its header, the audio's are lost */
        raw[2] = 0;
        if (fread(raw + 8, 2048, 1, s_file) != 1) {
            return 0;
        }
    } else if (fread(raw, RAW_SECTOR, 1, s_file) != 1) {
        return 0;
    }
    if (raw[2] & 4) {
        xa_sector(raw, raw + 8);
    } else if (!(raw[2] & 0x20) && h->id == 0x0160 && h->secCount < MAX_FRAME / FRAME_SECTOR) {
        memcpy((uint8_t *)f->data + h->secCount * FRAME_SECTOR, raw + 8 + 32, FRAME_SECTOR);
        if (h->secCount == h->nSectors - 1) {
            f->header = *h;
            s_ready++;
        }
    }
    return 1;
}

/* Each vsync: the drive reads on, unless the frames it read wait unused */
void movie_vblank(int rate) {
    if (s_file == NULL || s_ended) {
        return;
    }
    s_due += 150.0 / rate;
    while (s_due >= 1.0 && s_ready < QUEUE) {
        s_due -= 1.0;
        if (!read_sector()) {
            s_ended = 1;
            break;
        }
    }
    if (s_due > 1.0) {
        s_due = 1.0;
    }
}

void StSetRing(uint32_t *ring, uint32_t size) {}
void StSetStream(uint32_t mode, uint32_t start, uint32_t end, void (*func1)(void), void (*func2)(void)) {}
void StCdInterrupt(void) {}

void StUnSetRing(void) {
    movie_stream_stop();
}

/* 0 and the next frame (its data and its last sector's header); after the
   last one, a header past any end frame, so the player stops */
uint32_t StGetNext(uint32_t **addr, uint32_t **header) {
    while (s_ready == 0 && s_file != NULL && !s_ended) {
        native_wait_vblank();
    }
    if (s_ready == 0) {
        *addr = s_dummy;
        *header = (uint32_t *)&s_end_header;
        return 0;
    }
    *addr = s_queue[s_head].data;
    *header = (uint32_t *)&s_queue[s_head].header;
    return 0;
}

uint32_t StFreeRing(uint32_t *base) {
    if (s_ready > 0 && base == s_queue[s_head].data) {
        s_head = (s_head + 1) % (QUEUE + 1);
        s_ready--;
    }
    return 0;
}

/* --- MDEC --- */

typedef struct {
    uint16_t code; /* the bits, without the sign */
    uint8_t len, run, level;
} Vlc;

/* MPEG-1's DCT coefficient codes (not first coefficient), sign bit after */
static const Vlc VLC[] = {
    {0x3, 2, 0, 1},    {0x3, 3, 1, 1},    {0x4, 4, 0, 2},    {0x5, 4, 2, 1},    {0x5, 5, 0, 3},
    {0x7, 5, 3, 1},    {0x6, 5, 4, 1},    {0x6, 6, 1, 2},    {0x7, 6, 5, 1},    {0x5, 6, 6, 1},
    {0x4, 6, 7, 1},    {0x6, 7, 0, 4},    {0x4, 7, 2, 2},    {0x7, 7, 8, 1},    {0x5, 7, 9, 1},
    {0x26, 8, 0, 5},   {0x21, 8, 0, 6},   {0x25, 8, 1, 3},   {0x24, 8, 3, 2},   {0x27, 8, 10, 1},
    {0x23, 8, 11, 1},  {0x22, 8, 12, 1},  {0x20, 8, 13, 1},  {0xA, 10, 0, 7},   {0xC, 10, 1, 4},
    {0xB, 10, 2, 3},   {0xF, 10, 4, 2},   {0x9, 10, 5, 2},   {0xE, 10, 14, 1},  {0xD, 10, 15, 1},
    {0x8, 10, 16, 1},  {0x1D, 12, 0, 8},  {0x18, 12, 0, 9},  {0x13, 12, 0, 10}, {0x10, 12, 0, 11},
    {0x1B, 12, 1, 5},  {0x14, 12, 2, 4},  {0x1C, 12, 3, 3},  {0x12, 12, 4, 3},  {0x1E, 12, 6, 2},
    {0x15, 12, 7, 2},  {0x11, 12, 8, 2},  {0x1F, 12, 17, 1}, {0x1A, 12, 18, 1}, {0x19, 12, 19, 1},
    {0x17, 12, 20, 1}, {0x16, 12, 21, 1}, {0x1A, 13, 0, 12}, {0x19, 13, 0, 13}, {0x18, 13, 0, 14},
    {0x17, 13, 0, 15}, {0x16, 13, 1, 6},  {0x15, 13, 1, 7},  {0x14, 13, 2, 5},  {0x13, 13, 3, 4},
    {0x12, 13, 5, 3},  {0x11, 13, 9, 2},  {0x10, 13, 10, 2}, {0x1F, 13, 22, 1}, {0x1E, 13, 23, 1},
    {0x1D, 13, 24, 1}, {0x1C, 13, 25, 1}, {0x1B, 13, 26, 1}, {0x1F, 14, 0, 16}, {0x1E, 14, 0, 17},
    {0x1D, 14, 0, 18}, {0x1C, 14, 0, 19}, {0x1B, 14, 0, 20}, {0x1A, 14, 0, 21}, {0x19, 14, 0, 22},
    {0x18, 14, 0, 23}, {0x17, 14, 0, 24}, {0x16, 14, 0, 25}, {0x15, 14, 0, 26}, {0x14, 14, 0, 27},
    {0x13, 14, 0, 28}, {0x12, 14, 0, 29}, {0x11, 14, 0, 30}, {0x10, 14, 0, 31}, {0x18, 15, 0, 32},
    {0x17, 15, 0, 33}, {0x16, 15, 0, 34}, {0x15, 15, 0, 35}, {0x14, 15, 0, 36}, {0x13, 15, 0, 37},
    {0x12, 15, 0, 38}, {0x11, 15, 0, 39}, {0x10, 15, 0, 40}, {0x1F, 15, 1, 8},  {0x1E, 15, 1, 9},
    {0x1D, 15, 1, 10}, {0x1C, 15, 1, 11}, {0x1B, 15, 1, 12}, {0x1A, 15, 1, 13}, {0x19, 15, 1, 14},
    {0x13, 16, 1, 15}, {0x12, 16, 1, 16}, {0x11, 16, 1, 17}, {0x10, 16, 1, 18}, {0x14, 16, 6, 3},
    {0x1A, 16, 11, 2}, {0x19, 16, 12, 2}, {0x18, 16, 13, 2}, {0x17, 16, 14, 2}, {0x16, 16, 15, 2},
    {0x15, 16, 16, 2}, {0x1F, 16, 27, 1}, {0x1E, 16, 28, 1}, {0x1D, 16, 29, 1}, {0x1C, 16, 30, 1},
    {0x1B, 16, 31, 1},
};

/* The first 16 bits of a code -> its VLC index, -1 if none */
static int16_t s_vlc_lut[1 << 16];
static int s_vlc_built;

static void build_vlc(void) {
    size_t i;
    int j;

    memset(s_vlc_lut, -1, sizeof s_vlc_lut);
    for (i = 0; i < sizeof VLC / sizeof VLC[0]; i++) {
        int shift = 16 - VLC[i].len, base = VLC[i].code << shift;

        for (j = 0; j < 1 << shift; j++) {
            s_vlc_lut[base | j] = (int16_t)i;
        }
    }
    s_vlc_built = 1;
}

/* Bits off little-endian halfwords, most significant first */
typedef struct {
    const uint16_t *p, *end;
    uint32_t bits;
    int count;
} Bits;

static uint32_t peek(Bits *b, int n) {
    while (b->count < n) {
        b->bits |= (uint32_t)(b->p < b->end ? *b->p++ : 0) << (16 - b->count);
        b->count += 16;
    }
    return b->bits >> (32 - n);
}

static void skip(Bits *b, int n) {
    b->bits <<= n;
    b->count -= n;
}

static uint32_t get(Bits *b, int n) {
    uint32_t v = peek(b, n);

    skip(b, n);
    return v;
}

/* A frame's bitstream (8-byte header: RLE words, 0x3800, qscale, version 2)
   to the MDEC's halfwords, after a word with their count */
int DecDCTvlc2(uint32_t *bs, uint32_t *buf, uint16_t *table) {
    const uint16_t *in = (const uint16_t *)bs;
    uint16_t *out = (uint16_t *)(buf + 1);
    int total = in[0] * 2, q = in[2], n = 0;
    Bits b = {in + 4, in + MAX_FRAME / 2, 0, 0};

    if (!s_vlc_built) {
        build_vlc();
    }
    if (in[3] != 2) {
        log_once("movie: a frame not of version 2");
    }
    while (n < total) {
        /* a block: the DC, then AC codes up to the end of block (10) */
        out[n++] = (uint16_t)(q << 10 | get(&b, 10));
        while (n < total) {
            uint32_t c = peek(&b, 16);
            int i;

            if (c >> 14 == 2) {
                skip(&b, 2);
                break;
            }
            if (c >> 10 == 1) { /* escape: 6-bit run, 10-bit level */
                skip(&b, 6);
                i = (int)get(&b, 6) << 10;
                out[n++] = (uint16_t)(i | get(&b, 10));
                continue;
            }
            i = s_vlc_lut[c];
            if (i < 0) {
                /* zeroes: the frame's padding after its last macroblock */
                if (c != 0) {
                    log_once("movie: bad VLC code");
                }
                while (n < total) {
                    out[n++] = 0xFE00;
                }
                break;
            }
            skip(&b, VLC[i].len);
            out[n++] = (uint16_t)(VLC[i].run << 10 | ((get(&b, 1) ? -VLC[i].level : VLC[i].level) & 0x3FF));
        }
        if (n < total) {
            out[n++] = 0xFE00;
        }
    }
    buf[0] = (uint32_t)n;
    return 0;
}

static const uint8_t ZIGZAG[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

/* libpress's quantisation table (MPEG-1's intra one, 2 for the DC), by row */
static const uint8_t QUANT[64] = {
    2,  16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34,
    34, 38, 22, 22, 26, 27, 29, 34, 37, 40, 22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32,
    35, 40, 48, 58, 26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83,
};

static const uint16_t *s_rle, *s_rle_end;
static int s_rgb24;
static void (*s_out_cb)(void);
static float s_cos[8][8]; /* C(u)/2 cos((2x+1)u pi/16) */

static int sext10(int v) {
    return (v & 0x3FF) - (v & 0x200 ? 0x400 : 0);
}

/* One 8x8 block off the RLE, to pixels around 0 (-128..127) */
static void block(float out[64]) {
    float coef[64] = {0}, tmp[64];
    int k = 0, q, x, y, u;

    while (s_rle < s_rle_end && *s_rle == 0xFE00) {
        s_rle++; /* padding where a DC would be */
    }
    if (s_rle >= s_rle_end) {
        memset(out, 0, sizeof(float) * 64);
        return;
    }
    q = *s_rle >> 10;
    coef[0] = (float)(sext10(*s_rle++) * QUANT[0]);
    while (s_rle < s_rle_end) {
        uint16_t w = *s_rle++;

        if (w == 0xFE00) {
            break;
        }
        k += (w >> 10) + 1;
        if (k > 63) {
            break;
        }
        coef[ZIGZAG[k]] = (float)((sext10(w) * QUANT[ZIGZAG[k]] * q + 4) / 8);
    }
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            float s = 0;

            for (u = 0; u < 8; u++) {
                s += coef[y * 8 + u] * s_cos[x][u];
            }
            tmp[y * 8 + x] = s;
        }
    }
    for (x = 0; x < 8; x++) {
        for (y = 0; y < 8; y++) {
            float s = 0;

            for (u = 0; u < 8; u++) {
                s += tmp[u * 8 + x] * s_cos[y][u];
            }
            out[y * 8 + x] = s;
        }
    }
}

static uint8_t clamp8(float v) {
    v += 128.5f;
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

/* One macroblock to 16 rows of 16 pixels, of 3 (or 2) bytes */
static void macroblock(uint8_t *dst) {
    float cr[64], cb[64], y[4][64];
    int i, px, py;

    block(cr);
    block(cb);
    for (i = 0; i < 4; i++) {
        block(y[i]);
    }
    for (py = 0; py < 16; py++) {
        for (px = 0; px < 16; px++) {
            float l = y[(py >> 3) * 2 + (px >> 3)][(py & 7) * 8 + (px & 7)];
            float r = cr[(py >> 1) * 8 + (px >> 1)], b = cb[(py >> 1) * 8 + (px >> 1)];
            uint8_t R = clamp8(l + 1.402f * r), G = clamp8(l - 0.3437f * b - 0.7143f * r), B = clamp8(l + 1.772f * b);

            if (s_rgb24) {
                uint8_t *o = dst + (py * 16 + px) * 3;

                o[0] = R, o[1] = G, o[2] = B;
            } else {
                ((uint16_t *)dst)[py * 16 + px] = (uint16_t)(R >> 3 | (G >> 3) << 5 | (B >> 3) << 10);
            }
        }
    }
}

void DecDCTReset(int mode) {
    int x, u;

    for (x = 0; x < 8; x++) {
        for (u = 0; u < 8; u++) {
            s_cos[x][u] = (float)((u == 0 ? sqrt(0.5) : 1.0) / 2 * cos((2 * x + 1) * u * 3.14159265358979 / 16));
        }
    }
}

void DecDCTvlcBuild(uint16_t *table) {}

/* The MDEC takes a frame's halfwords; bit 0 of mode: 24-bit output */
void DecDCTin(uint32_t *buf, int mode) {
    s_rle = (const uint16_t *)(buf + 1);
    s_rle_end = s_rle + buf[0];
    s_rgb24 = mode & 1;
}

/* `size` words of pixels, whole macroblocks, then the callback (the DMA's
   end) that asks for the next slice: run as a loop, not as recursion */
void DecDCTout(uint32_t *buf, int size) {
    static int busy, pending, s_size;
    static uint32_t *s_buf;
    int mb, bytes;

    s_buf = buf;
    s_size = size;
    pending = 1;
    if (busy) {
        return;
    }
    busy = 1;
    while (pending) {
        pending = 0;
        bytes = s_rgb24 ? 16 * 16 * 3 : 16 * 16 * 2;
        for (mb = 0; mb < s_size * 4 / bytes; mb++) {
            macroblock((uint8_t *)s_buf + mb * bytes);
        }
        if (s_out_cb != NULL) {
            s_out_cb();
        }
    }
    busy = 0;
}

int DecDCToutCallback(void (*func)(void)) {
    s_out_cb = func;
    return 0;
}
