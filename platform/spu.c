/* The SPU: 512 KB of sound RAM and 24 voices that play its ADPCM samples
   through an ADSR envelope, mixed at 44.1 kHz with the CD's audio (XA).
   Our own model of the chip, without its registers: libsnd (snd.c) and the
   movie player (movie.c) call it directly. */
#include <stdlib.h>
#include <string.h>
#include "platform.h"

#define RAM_SIZE (512 * 1024)
#define CD_FIFO 32768 /* stereo frames of CD audio waiting to be mixed */

enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct {
    uint32_t addr, loop; /* bytes: the block playing, where its loop goes */
    uint32_t pos;        /* into the block's samples, 12-bit fraction */
    uint16_t pitch;      /* 0x1000: 44.1 kHz */
    uint16_t adsr1, adsr2;
    int16_t vol_l, vol_r; /* 0-0x3FFF */
    int phase;            /* ENV_* */
    int32_t env;          /* 0-0x7FFF */
    int env_wait;         /* samples until the next envelope step */
    int16_t buf[28];      /* the decoded block */
    int16_t hist[3];      /* the previous block's last three samples, oldest first */
    int16_t h0, h1;       /* ADPCM filter history */
    int reverb;           /* feeds the reverb unit (the VAB tone's mode bit 2) */
} Voice;

static uint8_t s_ram[RAM_SIZE];
static Voice s_voice[SPU_VOICES];
static int16_t s_main_l = 0x3FFF, s_main_r = 0x3FFF, s_cd_l = 0x3FFF, s_cd_r = 0x3FFF;
static int16_t s_cd[CD_FIFO][2];
static int s_cd_head, s_cd_count;

/* The chip's 4-point interpolation weights (psx-spx, "4-Point Gaussian
   Interpolation"; hardware data, not code) */
static const int16_t GAUSS[512] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3,
    3, 4, 4, 5, 5, 6, 7, 7, 8, 9, 9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 21, 22, 24, 25, 27, 28, 30, 32, 33, 35, 37,
    39, 41, 44, 46, 48, 51, 53, 56, 58, 61, 64, 67, 70, 73, 77, 80,
    84, 87, 91, 95, 99, 103, 107, 111, 116, 120, 125, 130, 135, 140, 145, 150,
    156, 161, 167, 173, 179, 186, 192, 199, 205, 212, 219, 227, 234, 242, 250, 257,
    266, 274, 283, 291, 300, 309, 319, 328, 338, 348, 358, 369, 379, 390, 401, 412,
    424, 436, 448, 460, 473, 485, 498, 512, 525, 539, 553, 567, 582, 597, 612, 627,
    643, 659, 675, 692, 708, 726, 743, 761, 779, 797, 816, 835, 854, 874, 894, 914,
    935, 956, 977, 999, 1020, 1043, 1066, 1089, 1112, 1136, 1160, 1184, 1209, 1234, 1260, 1286,
    1312, 1339, 1366, 1394, 1422, 1450, 1479, 1508, 1537, 1567, 1598, 1628, 1660, 1691, 1723, 1756,
    1789, 1822, 1856, 1890, 1924, 1959, 1995, 2031, 2067, 2104, 2141, 2179, 2217, 2256, 2295, 2334,
    2374, 2415, 2456, 2497, 2539, 2582, 2624, 2668, 2712, 2756, 2801, 2846, 2892, 2938, 2985, 3032,
    3079, 3128, 3176, 3225, 3275, 3325, 3376, 3427, 3479, 3531, 3584, 3637, 3691, 3745, 3799, 3855,
    3910, 3967, 4023, 4081, 4138, 4197, 4255, 4315, 4374, 4435, 4495, 4557, 4619, 4681, 4744, 4807,
    4871, 4935, 5000, 5065, 5131, 5197, 5264, 5332, 5399, 5468, 5536, 5606, 5676, 5746, 5817, 5888,
    5959, 6032, 6104, 6177, 6251, 6325, 6400, 6475, 6550, 6626, 6702, 6779, 6856, 6934, 7012, 7091,
    7170, 7249, 7329, 7409, 7490, 7571, 7653, 7735, 7817, 7900, 7983, 8066, 8150, 8234, 8319, 8404,
    8489, 8575, 8661, 8748, 8834, 8922, 9009, 9097, 9185, 9273, 9362, 9451, 9541, 9630, 9720, 9811,
    9901, 9992, 10083, 10174, 10266, 10358, 10450, 10542, 10635, 10727, 10820, 10913, 11007, 11100, 11194, 11288,
    11382, 11476, 11571, 11665, 11760, 11855, 11950, 12045, 12140, 12236, 12331, 12427, 12522, 12618, 12714, 12809,
    12905, 13001, 13097, 13193, 13289, 13385, 13481, 13577, 13673, 13769, 13865, 13961, 14056, 14152, 14248, 14343,
    14439, 14534, 14630, 14725, 14820, 14915, 15010, 15104, 15199, 15293, 15387, 15481, 15575, 15669, 15762, 15855,
    15948, 16041, 16133, 16226, 16317, 16409, 16500, 16592, 16682, 16773, 16863, 16953, 17042, 17131, 17220, 17308,
    17396, 17484, 17571, 17658, 17744, 17830, 17916, 18001, 18086, 18170, 18254, 18337, 18420, 18502, 18584, 18665,
    18746, 18826, 18905, 18985, 19063, 19141, 19219, 19295, 19372, 19447, 19522, 19597, 19671, 19744, 19816, 19888,
    19959, 20030, 20100, 20169, 20238, 20306, 20373, 20439, 20505, 20570, 20634, 20698, 20760, 20822, 20884, 20944,
    21004, 21063, 21121, 21178, 21235, 21290, 21345, 21399, 21452, 21505, 21556, 21607, 21657, 21706, 21754, 21801,
    21848, 21893, 21938, 21982, 22025, 22066, 22107, 22148, 22187, 22225, 22262, 22299, 22334, 22369, 22402, 22435,
    22467, 22498, 22527, 22556, 22584, 22611, 22637, 22662, 22686, 22709, 22731, 22752, 22772, 22791, 22809, 22826,
    22842, 22857, 22872, 22885, 22897, 22908, 22918, 22927, 22935, 22942, 22948, 22953, 22957, 22960, 22962, 22963,
};

static const int F0[5] = {0, 60, 115, 98, 122};
static const int F1[5] = {0, 0, -52, -55, -60};

static int16_t clamp16(int32_t v) {
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

/* The 28 samples of the 16-byte block at v->addr */
static void decode_block(Voice *v) {
    const uint8_t *b = s_ram + v->addr;
    int shift = b[0] & 15, filter = b[0] >> 4 & 7, i;

    if (shift > 12) {
        shift = 9; /* what the chip does with 13-15 */
    }
    if (filter > 4) {
        filter = 4;
    }
    if (b[1] & 4) {
        v->loop = v->addr; /* loop start */
    }
    for (i = 0; i < 28; i++) {
        int32_t s = (int16_t)((b[2 + i / 2] >> (i & 1) * 4 & 15) << 12) >> shift;

        s += (v->h0 * F0[filter] + v->h1 * F1[filter] + 32) >> 6;
        v->h1 = v->h0;
        v->h0 = clamp16(s);
        v->buf[i] = v->h0;
    }
}

/* On to the next block: the loop's at a loop end, which silences the voice
   unless it repeats */
static void next_block(Voice *v) {
    uint8_t flags = s_ram[v->addr + 1];

    if (flags & 1) {
        v->addr = v->loop;
        if (!(flags & 2)) {
            v->phase = ENV_OFF;
            v->env = 0;
        }
    } else {
        v->addr = (v->addr + 16) & (RAM_SIZE - 1);
    }
    decode_block(v);
}

/* One sample's step of the envelope (the ADSR rates of the chip) */
static void envelope(Voice *v) {
    int expo, dec, shift, step, cycles;

    if (v->env_wait > 0) {
        v->env_wait--;
        return;
    }
    switch (v->phase) {
    case ENV_ATTACK:
        expo = v->adsr1 >> 15, dec = 0, shift = v->adsr1 >> 10 & 31, step = 7 - (v->adsr1 >> 8 & 3);
        break;
    case ENV_DECAY:
        expo = 1, dec = 1, shift = v->adsr1 >> 4 & 15, step = -8;
        break;
    case ENV_SUSTAIN:
        expo = v->adsr2 >> 15, dec = v->adsr2 >> 14 & 1, shift = v->adsr2 >> 8 & 31;
        step = dec ? -8 + (v->adsr2 >> 6 & 3) : 7 - (v->adsr2 >> 6 & 3);
        break;
    case ENV_RELEASE:
        expo = v->adsr2 >> 5 & 1, dec = 1, shift = v->adsr2 & 31, step = -8;
        break;
    default:
        return;
    }
    cycles = 1 << (shift > 11 ? shift - 11 : 0);
    step <<= shift < 11 ? 11 - shift : 0;
    if (expo && !dec && v->env > 0x6000) {
        cycles *= 4;
    }
    if (expo && dec) {
        step = step * v->env >> 15;
    }
    v->env += step;
    v->env = v->env < 0 ? 0 : v->env > 0x7FFF ? 0x7FFF : v->env;
    v->env_wait = cycles - 1;
    if (v->phase == ENV_ATTACK && v->env >= 0x7FFF) {
        v->phase = ENV_DECAY;
    } else if (v->phase == ENV_DECAY && v->env <= ((v->adsr1 & 15) + 1) * 0x800) {
        v->phase = ENV_SUSTAIN;
    } else if (v->phase == ENV_RELEASE && v->env == 0) {
        v->phase = ENV_OFF;
    }
}

void spu_write(uint32_t addr, const void *src, uint32_t size) {
    if (addr < RAM_SIZE) {
        memcpy(s_ram + addr, src, addr + size > RAM_SIZE ? RAM_SIZE - addr : size);
    }
}

void spu_key_on(int n, uint32_t addr, uint16_t adsr1, uint16_t adsr2) {
    Voice *v = &s_voice[n];

    v->addr = v->loop = addr & (RAM_SIZE - 1) & ~15u;
    v->adsr1 = adsr1;
    v->adsr2 = adsr2;
    v->pos = 0;
    v->h0 = v->h1 = 0;
    v->hist[0] = v->hist[1] = v->hist[2] = 0;
    v->env = 0;
    v->env_wait = 0;
    v->phase = ENV_ATTACK;
    decode_block(v);
}

void spu_key_off(int n) {
    if (s_voice[n].phase != ENV_OFF) {
        s_voice[n].phase = ENV_RELEASE;
        s_voice[n].env_wait = 0;
    }
}

/* At once, without a release */
void spu_voice_stop(int n) {
    s_voice[n].phase = ENV_OFF;
    s_voice[n].env = 0;
}

void spu_set_pitch(int n, uint16_t pitch) {
    s_voice[n].pitch = pitch > 0x3FFF ? 0x3FFF : pitch;
}

void spu_set_volume(int n, int16_t l, int16_t r) {
    s_voice[n].vol_l = l;
    s_voice[n].vol_r = r;
}

/* 0: silent; 1: keyed off, fading; 2: playing. The envelope level in *env */
int spu_voice_state(int n, int *env) {
    *env = s_voice[n].env;
    return s_voice[n].phase == ENV_OFF ? 0 : s_voice[n].phase == ENV_RELEASE ? 1 : 2;
}

void spu_set_main_volume(int16_t l, int16_t r) {
    s_main_l = l;
    s_main_r = r;
}

void spu_set_cd_volume(int16_t l, int16_t r) {
    s_cd_l = l;
    s_cd_r = r;
}

/* CD audio at 44.1 kHz, queued to be mixed */
void spu_cd_push(const int16_t *lr, int frames) {
    static FILE *dump;

    if (dump == NULL && getenv("DW3_XAWAV") != NULL) {
        dump = fopen(getenv("DW3_XAWAV"), "wb");
    }
    if (dump != NULL) {
        fwrite(lr, 4, frames, dump);
    }
    while (frames-- > 0 && s_cd_count < CD_FIFO) {
        int i = (s_cd_head + s_cd_count++) % CD_FIFO;

        s_cd[i][0] = *lr++;
        s_cd[i][1] = *lr++;
    }
}

int spu_cd_queued(void) {
    return s_cd_count;
}

void spu_cd_clear(void) {
    s_cd_count = 0;
}

/* The voice's decoded sample k of the block, k = -3..27 (before 0: the
   previous block's) */
static int sample(const Voice *v, int k) {
    return k >= 0 ? v->buf[k] : v->hist[3 + k];
}

/* --- the reverb unit (psx-spx, "SPU Reverb Formula") --- */

enum {
    dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2,
    mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF,
    mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2,
    vLIN, vRIN, REVERB_REGS
};

static uint16_t s_rv[REVERB_REGS]; /* the preset's registers */
static uint32_t s_rv_base = RAM_SIZE, s_rv_addr = RAM_SIZE; /* work area, bytes */
static int s_rv_on, s_rv_phase;
static int16_t s_rv_depth_l, s_rv_depth_r;

/* The 39-tap FIR the chip resamples the reverb's input and output with
   (psx-spx, "Reverb Buffer Resampling"; hardware data). Its taps sum to
   0x7FFE: the input keeps its level, the zero-stuffed output takes x2 */
#define FIR_TAPS 39
static const int16_t FIR[FIR_TAPS] = {
    -0x0001, 0, 0x0002, 0, -0x000A, 0, 0x0023, 0, -0x0067, 0, 0x010A, 0, -0x0268, 0, 0x0534, 0,
    -0x0B90, 0, 0x2806, 0x4000, 0x2806, 0, -0x0B90, 0, 0x0534, 0, -0x0268, 0, 0x010A, 0, -0x0067, 0,
    0x0023, 0, -0x000A, 0, 0x0002, 0, -0x0001,
};
/* per side: the last FIR_TAPS inputs and outputs, twice over so a window is
   contiguous at any position */
static int16_t s_rv_in[2][FIR_TAPS * 2], s_rv_out[2][FIR_TAPS * 2];
static int s_rv_pos;

/* A register as a signed volume, or (for the d and m ones) a byte offset */
#define RV(r) ((int32_t)(int16_t)s_rv[r])
#define RO(r) ((int32_t)s_rv[r] * 8)

static int16_t *rv_at(int32_t off) {
    uint32_t span = RAM_SIZE - s_rv_base;
    int32_t a = (int32_t)(s_rv_addr - s_rv_base) + off;

    a %= (int32_t)span;
    if (a < 0) {
        a += span;
    }
    return (int16_t *)(s_ram + s_rv_base + (a & ~1));
}

static int32_t rv_mul(int32_t a, int32_t b) {
    return a * b >> 15;
}

/* A write to the work area; while the reverb is off (ATTR bit 7) the chip
   reads there instead */
static void rv_write(int32_t off, int32_t v) {
    if (s_rv_on) {
        *rv_at(off) = clamp16(v);
    }
}

/* The reflections' write: (in + [d]*vWALL - [m-1]) * vIIR + [m-1]; with vIIR at
   -0x8000 the chip negates what it writes (psx-spx, "Bug") */
static void rv_reflect(int32_t m, int32_t d, int32_t in) {
    int32_t t = *rv_at(m - 2), v = rv_mul(in + rv_mul(*rv_at(d), RV(vWALL)) - t, RV(vIIR)) + t;

    rv_write(m, RV(vIIR) == -0x8000 ? -v : v);
}

/* One side's step at 22.05 kHz (the chip does the left one sample, the right
   the next): its input from the FIR, its output out of the two all-pass
   filters, times the depth */
static int32_t reverb_side(int right, int32_t in) {
    int32_t lin = rv_mul(in, RV(right ? vRIN : vLIN)), out, t;

    if (!right) {
        rv_reflect(RO(mLSAME), RO(dLSAME), lin);
        rv_reflect(RO(mLDIFF), RO(dRDIFF), lin);
        out = rv_mul(RV(vCOMB1), *rv_at(RO(mLCOMB1))) + rv_mul(RV(vCOMB2), *rv_at(RO(mLCOMB2))) +
              rv_mul(RV(vCOMB3), *rv_at(RO(mLCOMB3))) + rv_mul(RV(vCOMB4), *rv_at(RO(mLCOMB4)));
    } else {
        rv_reflect(RO(mRSAME), RO(dRSAME), lin);
        rv_reflect(RO(mRDIFF), RO(dLDIFF), lin);
        out = rv_mul(RV(vCOMB1), *rv_at(RO(mRCOMB1))) + rv_mul(RV(vCOMB2), *rv_at(RO(mRCOMB2))) +
              rv_mul(RV(vCOMB3), *rv_at(RO(mRCOMB3))) + rv_mul(RV(vCOMB4), *rv_at(RO(mRCOMB4)));
    }
    t = *rv_at(RO(right ? mRAPF1 : mLAPF1) - RO(dAPF1));
    out = clamp16(out - rv_mul(RV(vAPF1), t));
    rv_write(RO(right ? mRAPF1 : mLAPF1), out);
    out = clamp16(rv_mul(out, RV(vAPF1)) + t);
    t = *rv_at(RO(right ? mRAPF2 : mLAPF2) - RO(dAPF2));
    out = clamp16(out - rv_mul(RV(vAPF2), t));
    rv_write(RO(right ? mRAPF2 : mLAPF2), out);
    out = clamp16(rv_mul(out, RV(vAPF2)) + t);
    return rv_mul(out, right ? s_rv_depth_r : s_rv_depth_l);
}

static int32_t fir(const int16_t *w, int shift) {
    int32_t sum = 0;
    int i;

    for (i = 0; i < FIR_TAPS; i++) {
        sum += FIR[i] * w[i];
    }
    return sum >> shift;
}

/* The reverb at 44.1 kHz: the input into both FIR windows; each sample one
   side steps (left, then right and the buffer moves on) and the other side's
   output gets a zero; the output is the FIR over the zero-stuffed stream */
static void reverb(int16_t in_l, int16_t in_r, int32_t *out_l, int32_t *out_r) {
    int side, p;

    if (s_rv_base >= RAM_SIZE) {
        *out_l = *out_r = 0;
        return;
    }
    p = s_rv_pos;
    s_rv_in[0][p] = s_rv_in[0][p + FIR_TAPS] = in_l;
    s_rv_in[1][p] = s_rv_in[1][p + FIR_TAPS] = in_r;
    side = s_rv_phase;
    s_rv_out[side][p] = s_rv_out[side][p + FIR_TAPS] =
        clamp16(reverb_side(side, clamp16(fir(&s_rv_in[side][p + 1], 15))));
    s_rv_out[!side][p] = s_rv_out[!side][p + FIR_TAPS] = 0;
    if (side) {
        s_rv_addr = (s_rv_addr + 2) & (RAM_SIZE - 2);
        if (s_rv_addr < s_rv_base) {
            s_rv_addr = s_rv_base;
        }
    }
    s_rv_phase ^= 1;
    s_rv_pos = (p + 1) % FIR_TAPS;
    *out_l = clamp16(fir(&s_rv_out[0][s_rv_pos], 14));
    *out_r = clamp16(fir(&s_rv_out[1][s_rv_pos], 14));
}

/* A preset (libspu's register set) and its work area's start, in bytes; the
   work area is cleared, as SpuSetReverbModeParam does */
void spu_set_reverb(const uint16_t regs[32], uint32_t base) {
    memcpy(s_rv, regs, sizeof s_rv);
    s_rv_base = base < RAM_SIZE ? base & ~1u : RAM_SIZE;
    s_rv_addr = s_rv_base;
    if (s_rv_base < RAM_SIZE) {
        memset(s_ram + s_rv_base, 0, RAM_SIZE - s_rv_base);
    }
    memset(s_rv_in, 0, sizeof s_rv_in);
    memset(s_rv_out, 0, sizeof s_rv_out);
}

void spu_set_reverb_on(int on) {
    s_rv_on = on;
}

/* The reverb's output volume (EVOL), 0-0x7FFF */
void spu_set_reverb_depth(int16_t l, int16_t r) {
    s_rv_depth_l = l;
    s_rv_depth_r = r;
}

void spu_set_voice_reverb(int n, int on) {
    s_voice[n].reverb = on;
}

/* `frames` stereo samples at 44.1 kHz: the voices through the chip's
   Gaussian interpolation, the reverb unit on those that feed it, and the CD */
void spu_render(int16_t *out, int frames) {
    int f, n;

    for (f = 0; f < frames; f++) {
        int32_t l = 0, r = 0, rl = 0, rr = 0;

        for (n = 0; n < SPU_VOICES; n++) {
            Voice *v = &s_voice[n];
            int i, g, s, vl, vr;

            if (v->phase == ENV_OFF) {
                continue;
            }
            i = v->pos >> 12;
            g = v->pos >> 4 & 0xFF;
            s = (GAUSS[0x0FF - g] * sample(v, i - 3) >> 15) + (GAUSS[0x1FF - g] * sample(v, i - 2) >> 15) +
                (GAUSS[0x100 + g] * sample(v, i - 1) >> 15) + (GAUSS[g] * sample(v, i) >> 15);
            s = s * v->env >> 15;
            vl = s * v->vol_l >> 14;
            vr = s * v->vol_r >> 14;
            l += vl;
            r += vr;
            if (v->reverb) {
                rl += vl;
                rr += vr;
            }
            envelope(v);
            v->pos += v->pitch;
            while (v->pos >> 12 >= 28 && v->phase != ENV_OFF) {
                v->hist[0] = v->buf[25];
                v->hist[1] = v->buf[26];
                v->hist[2] = v->buf[27];
                v->pos -= 28 << 12;
                next_block(v);
            }
        }
        reverb(clamp16(rl), clamp16(rr), &rl, &rr);
        l = (l + rl) * s_main_l >> 14;
        r = (r + rr) * s_main_r >> 14;
        if (s_cd_count > 0) {
            l += s_cd[s_cd_head][0] * s_cd_l >> 14;
            r += s_cd[s_cd_head][1] * s_cd_r >> 14;
            s_cd_head = (s_cd_head + 1) % CD_FIFO;
            s_cd_count--;
        }
        out[f * 2] = clamp16(l);
        out[f * 2 + 1] = clamp16(r);
    }
}
