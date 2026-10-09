/* libsnd: the game's sound banks (VAB: instruments and their samples) and
   their music (SEP: MIDI-like sequences), played on the SPU (spu.c). Our
   own reading of the formats:

   VAB header ("pBAV"): 32-byte header (+0x12 programs with tones, +0x16
   samples, +0x18 master volume, +0x19 pan), 128 programs of 16 bytes (tones,
   volume, priority, mode, pan), then 16 tones of 32 bytes for each program
   that has tones, in program order (volume, pan, centre note, fine tune,
   note range, pitch bend range, ADSR, sample), then 256 sample sizes / 8.

   SEP ("pQES" + 2-byte version): sequences, each with a 13-byte big-endian
   header (id, ticks per quarter, 3-byte tempo in us per quarter, rhythm,
   data size) and MIDI events, each after a variable-length delta in ticks.
   Meta events have no length byte (FF 51 tempo, FF 2F end). Loops are NRPN
   99=20 + data entry (the count, 127 forever) at the start, 99=30 at the end. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"

#define MAX_VABS 16
#define MAX_SEPS 8
#define SEQS_PER_SEP 16

typedef struct {
    uint8_t tones, vol, prior, mode, pan, reserved0;
    uint16_t attr;
    uint32_t reserved1, reserved2;
} ProgAtr;

typedef struct {
    uint8_t prior, mode, vol, pan, center, shift, min, max;
    uint8_t vibW, vibT, porW, porT, pbmin, pbmax, reserved1, reserved2;
    uint16_t adsr1, adsr2;
    int16_t prog, vag;
    int16_t reserved[4];
} VagAtr;

typedef struct {
    int open;
    uint8_t vol, pan;
    const ProgAtr *progs;
    const VagAtr *tones;
    int tone_base[128];   /* each program's first tone */
    uint32_t vag_addr[256]; /* SPU addresses of the samples */
    uint32_t spu_addr, body_size;
} Vab;

typedef struct {
    uint8_t prog, vol, pan;
    int16_t bend; /* -8192..8191 */
} Channel;

typedef struct Seq {
    const uint8_t *data, *pos, *end;
    const uint8_t *loop_pos;
    uint8_t loop_status;
    int loop_count; /* 127: forever */
    int vab;
    int res, tempo; /* ticks per quarter, us per quarter */
    double ticks;   /* elapsed, not yet run */
    uint32_t wait;  /* ticks until the next event */
    uint8_t status; /* running status */
    uint8_t nrpn;   /* the last NRPN number (CC 99) */
    int playing;
    double vol_l, vol_r; /* 0-127 */
    double fade_step;    /* per tick of the sequencer, while fading */
    Channel ch[16];
} Seq;

typedef struct {
    int open;
    Seq seq[SEQS_PER_SEP];
} Sep;

/* What each SPU voice plays, to update or release it */
typedef struct {
    Seq *seq; /* NULL: a key-on of the game's (SsUtKeyOn) */
    int ch, note, vab, prog, tone;
    int vel_l, vel_r; /* 0-127 */
    uint32_t age;
} VoiceOwner;

SAVED static Vab s_vab[MAX_VABS];
SAVED static Sep s_sep[MAX_SEPS];
SAVED static VoiceOwner s_owner[SPU_VOICES];
SAVED static uint32_t s_age;
SAVED static int s_tick_rate = 60;

static uint32_t be(const uint8_t *p, int n) {
    uint32_t v = 0;

    while (n-- > 0) {
        v = v << 8 | *p++;
    }
    return v;
}

/* --- voices --- */

static int alloc_voice(void) {
    int best = 0, best_score = 0x7FFFFFFF, n, env;

    for (n = 0; n < SPU_VOICES; n++) {
        int state = spu_voice_state(n, &env), score;

        if (state == 0) {
            return n;
        }
        /* fading ones first, the quietest; then the oldest note */
        score = state == 1 ? env - 0x10000 : (int)(s_owner[n].age - s_age) & 0x7FFFFFFF;
        if (score < best_score) {
            best_score = score;
            best = n;
        }
    }
    spu_voice_stop(best);
    return best;
}

static const VagAtr *tone_of(int vab, int prog, int tone) {
    return &s_vab[vab].tones[s_vab[vab].tone_base[prog & 127] + tone];
}

/* A pan (0-127, 64 centre) narrows one side, as libsnd's _SsVmSetVol */
static void pan_by(double *l, double *r, int pan) {
    if (pan < 64) {
        *r *= pan / 63.0;
    } else {
        *l *= (127 - pan) / 63.0;
    }
}

/* The voice's volume and pitch from its tone, program, channel and
   sequence. libsnd's volume law: the product of the volumes, then each
   pan in turn, then squared (_SsVmSetVol in the executable) */
static void update_voice(int n) {
    VoiceOwner *o = &s_owner[n];
    Vab *vb = &s_vab[o->vab];
    const ProgAtr *p = &vb->progs[o->prog];
    const VagAtr *t = tone_of(o->vab, o->prog, o->tone);
    double vol = vb->vol / 127.0 * p->vol / 127.0 * t->vol / 127.0, l, r, semis;
    int bend = 0;

    l = vol * o->vel_l / 127.0;
    r = vol * o->vel_r / 127.0;
    if (o->seq != NULL) {
        l *= o->seq->vol_l / 127.0;
        r *= o->seq->vol_r / 127.0;
    }
    pan_by(&l, &r, t->pan);
    pan_by(&l, &r, p->pan);
    if (o->seq != NULL) {
        Channel *c = &o->seq->ch[o->ch];

        l *= c->vol / 127.0;
        r *= c->vol / 127.0;
        pan_by(&l, &r, c->pan);
        bend = c->bend;
    }
    spu_set_volume(n, (int16_t)(l * l * 0x3FFF), (int16_t)(r * r * 0x3FFF));
    semis = o->note - t->center + t->shift / 128.0;
    semis += bend >= 0 ? bend / 8192.0 * t->pbmax : bend / 8192.0 * t->pbmin;
    spu_set_pitch(n, (uint16_t)fmin(0x3FFF, 4096.0 * pow(2.0, semis / 12.0)));
}

static int start_voice(Seq *seq, int ch, int vab, int prog, int tone, int note, int vel_l, int vel_r) {
    const VagAtr *t = tone_of(vab, prog, tone);
    int n = alloc_voice();
    VoiceOwner *o = &s_owner[n];

    o->seq = seq;
    o->ch = ch;
    o->note = note;
    o->vab = vab;
    o->prog = prog;
    o->tone = tone;
    o->vel_l = vel_l;
    o->vel_r = vel_r;
    o->age = s_age++;
    update_voice(n);
    if (g_log & LOG_SND) {
        LOG("keyon v%d seq %p ch %d vab %d prog %d tone %d note %d vel %d vag %d addr %x adsr %04x %04x vol %d pan %d center %d", n, (void *)seq, ch, vab, prog, tone, note, vel_l, t->vag, s_vab[vab].vag_addr[t->vag & 255], t->adsr1, t->adsr2, t->vol, t->pan, t->center);
    }
    spu_set_voice_reverb(n, t->mode & 4); /* the tone's reverb bit */
    spu_key_on(n, s_vab[vab].vag_addr[t->vag & 255], t->adsr1, t->adsr2);
    return n;
}

static int playing(int n) {
    int env;

    return spu_voice_state(n, &env) != 0;
}

/* --- VAB --- */

short SsVabOpenHeadSticky(unsigned char *addr, short id, unsigned long sbaddr) {
    Vab *vb;
    const uint16_t *sizes;
    int ps, p, k = 0, i;
    uint32_t at = 0;

    if (id < 0 || id >= MAX_VABS || memcmp(addr, "pBAV", 4) != 0) {
        return -1;
    }
    vb = &s_vab[id];
    ps = addr[0x12] | addr[0x13] << 8;
    vb->vol = addr[0x18];
    vb->pan = addr[0x19];
    vb->progs = (const ProgAtr *)(addr + 32);
    vb->tones = (const VagAtr *)(addr + 32 + 128 * 16);
    for (p = 0; p < 128; p++) {
        vb->tone_base[p] = k * 16;
        if (vb->progs[p].tones != 0 && k < ps) {
            k++;
        }
    }
    sizes = (const uint16_t *)(addr + 32 + 128 * 16 + ps * 16 * 32);
    for (i = 0; i < 256; i++) {
        vb->vag_addr[i] = sbaddr + at;
        at += sizes[i] * 8u;
    }
    vb->spu_addr = sbaddr;
    vb->body_size = at;
    vb->open = 1;
    return id;
}

short SsVabTransBody(unsigned char *addr, short id) {
    if (id < 0 || id >= MAX_VABS || !s_vab[id].open) {
        return -1;
    }
    spu_write(s_vab[id].spu_addr, addr, s_vab[id].body_size);
    return id;
}

/* 1: the transfer to the SPU is done (it was, at once) */
short SsVabTransCompleted(short mode) {
    return 1;
}

void SsVabClose(short id) {
    int n;

    if (id < 0 || id >= MAX_VABS) {
        return;
    }
    for (n = 0; n < SPU_VOICES; n++) {
        if (s_owner[n].vab == id && playing(n)) {
            spu_voice_stop(n);
        }
    }
    s_vab[id].open = 0;
}

/* --- the game's own notes --- */

short SsUtKeyOn(short vab, short prog, short tone, short note, short fine, short voll, short volr) {
    if (vab < 0 || vab >= MAX_VABS || !s_vab[vab].open || tone >= s_vab[vab].progs[prog & 127].tones) {
        return -1;
    }
    return (short)start_voice(NULL, 0, vab, prog & 127, tone, note, voll, volr);
}

short SsUtKeyOff(short voice, short vab, short prog, short tone, short note) {
    VoiceOwner *o;

    if (voice < 0 || voice >= SPU_VOICES) {
        return -1;
    }
    o = &s_owner[voice];
    if (o->seq == NULL && o->vab == vab && o->prog == prog && o->tone == tone && o->note == note) {
        spu_key_off(voice);
    }
    return 0;
}

void SsUtAllKeyOff(short mode) {
    int n;

    for (n = 0; n < SPU_VOICES; n++) {
        spu_voice_stop(n);
    }
}

/* --- sequences --- */

static void seq_voices_off(Seq *t, int stop) {
    int n;

    for (n = 0; n < SPU_VOICES; n++) {
        if (s_owner[n].seq == t && playing(n)) {
            if (stop) {
                spu_voice_stop(n);
            } else {
                spu_key_off(n);
            }
        }
    }
}

static void seq_update_channel(Seq *t, int ch) {
    int n;

    for (n = 0; n < SPU_VOICES; n++) {
        if (s_owner[n].seq == t && (ch < 0 || s_owner[n].ch == ch) && playing(n)) {
            update_voice(n);
        }
    }
}

static uint32_t read_delta(Seq *t) {
    uint32_t v = 0;

    while (t->pos < t->end) {
        uint8_t b = *t->pos++;

        v = v << 7 | (b & 0x7F);
        if (!(b & 0x80)) {
            break;
        }
    }
    return v;
}

static void note_on(Seq *t, int ch, int note, int vel) {
    Vab *vb = &s_vab[t->vab];
    int prog = t->ch[ch].prog, i;

    if (!vb->open) {
        return;
    }
    for (i = 0; i < vb->progs[prog].tones && i < 16; i++) {
        const VagAtr *tn = tone_of(t->vab, prog, i);

        if (note >= tn->min && note <= tn->max) {
            start_voice(t, ch, t->vab, prog, i, note, vel, vel);
        }
    }
}

static void note_off(Seq *t, int ch, int note) {
    int n;

    for (n = 0; n < SPU_VOICES; n++) {
        VoiceOwner *o = &s_owner[n];
        int env;

        if (o->seq == t && o->ch == ch && o->note == note && spu_voice_state(n, &env) == 2) {
            spu_key_off(n);
        }
    }
}

void SsUtSetReverbType(short type);
void SsUtSetReverbDepth(short l, short r);

static void controller(Seq *t, int ch, int cc, int value) {
    switch (cc) {
    case 6: /* data entry */
        if (t->nrpn == 20) {
            t->loop_count = value;
            t->loop_pos = t->pos;
            t->loop_status = t->status;
        } else if (t->nrpn == 15) {
            SsUtSetReverbType((short)value);
        } else if (t->nrpn == 16) {
            SsUtSetReverbDepth((short)value, (short)value);
        }
        break;
    case 7:
        t->ch[ch].vol = (uint8_t)value;
        seq_update_channel(t, ch);
        break;
    case 10:
        t->ch[ch].pan = (uint8_t)value;
        seq_update_channel(t, ch);
        break;
    case 99:
        t->nrpn = (uint8_t)value;
        if (value == 30 && t->loop_pos != NULL && t->loop_count != 0) {
            if (t->loop_count != 127) {
                t->loop_count--;
            }
            t->pos = t->loop_pos;
            t->status = t->loop_status;
        }
        break;
    }
}

/* Runs one event; 0 at the end of the sequence */
static int event(Seq *t) {
    uint8_t s, a, b;
    int ch;

    if (t->pos >= t->end) {
        return 0;
    }
    if (*t->pos & 0x80) {
        t->status = *t->pos++;
    }
    s = t->status;
    ch = s & 15;
    if (s == 0xFF) {
        a = *t->pos++;
        if (a == 0x51) {
            t->tempo = (int)be(t->pos, 3);
            t->pos += 3;
        } else if (a == 0x2F) {
            return 0;
        }
        return 1;
    }
    a = *t->pos++;
    switch (s & 0xF0) {
    case 0xC0:
        t->ch[ch].prog = a & 127;
        return 1;
    case 0xD0:
        return 1;
    }
    b = *t->pos++;
    switch (s & 0xF0) {
    case 0x90:
        if (b != 0) {
            note_on(t, ch, a, b);
            break;
        }
        /* fall through: velocity 0 is a note off */
    case 0x80:
        note_off(t, ch, a);
        break;
    case 0xB0:
        controller(t, ch, a, b);
        break;
    case 0xE0:
        t->ch[ch].bend = (int16_t)((a | b << 7) - 8192);
        seq_update_channel(t, ch);
        break;
    }
    return 1;
}

/* libsnd keeps all the SEPs' sequences in one table (SsSetTableSize), 16 to
   an access number, and doesn't check the index: the game's system sounds
   play "sequence 27" of their first SEP, which is 11 of the next one */
static Seq *find_seq(short acc, short sep) {
    int i = acc * SEQS_PER_SEP + sep;

    if (acc < 0 || sep < 0 || i >= MAX_SEPS * SEQS_PER_SEP) {
        return NULL;
    }
    acc = (short)(i / SEQS_PER_SEP);
    sep = (short)(i % SEQS_PER_SEP);
    if (!s_sep[acc].open || s_sep[acc].seq[sep].data == NULL) {
        return NULL;
    }
    return &s_sep[acc].seq[sep];
}

short SsSepOpen(unsigned long *addr, short vab, short count) {
    const uint8_t *p = (const uint8_t *)addr;
    int acc, i;
    Sep *sp;

    if (memcmp(p, "pQES", 4) != 0) {
        return -1;
    }
    for (acc = 0; acc < MAX_SEPS && s_sep[acc].open; acc++) {
    }
    if (acc == MAX_SEPS) {
        return -1;
    }
    sp = &s_sep[acc];
    memset(sp, 0, sizeof *sp);
    sp->open = 1;
    p += 6;
    for (i = 0; i < count && i < SEQS_PER_SEP && be(p, 2) == (uint32_t)i; i++) {
        Seq *t = &sp->seq[i];

        t->res = (int)be(p + 2, 2);
        t->tempo = (int)be(p + 4, 3);
        t->data = p + 13;
        t->end = t->data + be(p + 9, 4);
        t->vab = vab;
        t->vol_l = t->vol_r = 127;
        p = t->end;
    }
    return (short)acc;
}

void SsSepClose(short acc) {
    int i;

    if (acc < 0 || acc >= MAX_SEPS || !s_sep[acc].open) {
        return;
    }
    for (i = 0; i < SEQS_PER_SEP; i++) {
        seq_voices_off(&s_sep[acc].seq[i], 1);
    }
    s_sep[acc].open = 0;
}

void SsSepPlay(short acc, short sep, char mode, short count) {
    Seq *t = find_seq(acc, sep);
    int ch;

    if (t == NULL) {
        return;
    }
    t->pos = t->data;
    t->loop_pos = NULL;
    t->loop_count = 0;
    t->status = 0;
    t->nrpn = 0;
    t->ticks = 0;
    t->fade_step = 0;
    for (ch = 0; ch < 16; ch++) {
        t->ch[ch].prog = 0;
        t->ch[ch].vol = 127;
        t->ch[ch].pan = 64;
        t->ch[ch].bend = 0;
    }
    t->wait = read_delta(t);
    t->playing = mode == 1; /* SSPLAY_PLAY; else paused */
}

void SsSepStop(short acc, short sep) {
    Seq *t = find_seq(acc, sep);

    if (t != NULL) {
        t->playing = 0;
        seq_voices_off(t, 0);
    }
}

void SsSepSetVol(short acc, short sep, short l, short r) {
    Seq *t = find_seq(acc, sep);

    if (t != NULL) {
        t->vol_l = l;
        t->vol_r = r;
        t->fade_step = 0;
        seq_update_channel(t, -1);
    }
}

/* Lowers the volume by `by` over `ticks` calls of the sequencer */
void SsSepSetDecrescendo(short acc, short sep, short by, long ticks) {
    Seq *t = find_seq(acc, sep);

    if (t != NULL) {
        t->fade_step = ticks > 0 ? (double)by / ticks : by;
    }
}

/* Called each vsync (SS_NOTICK): the sequences go on by one tick of the
   tick rate, in their own ticks per quarter at their tempo */
void SsSeqCalledTbyT(void) {
    int acc, i;

    for (acc = 0; acc < MAX_SEPS; acc++) {
        for (i = 0; i < SEQS_PER_SEP && s_sep[acc].open; i++) {
            Seq *t = &s_sep[acc].seq[i];

            if (!t->playing) {
                continue;
            }
            if (t->fade_step > 0) {
                t->vol_l = fmax(0, t->vol_l - t->fade_step);
                t->vol_r = fmax(0, t->vol_r - t->fade_step);
                seq_update_channel(t, -1);
            }
            t->ticks += (double)t->res * 1e6 / t->tempo / s_tick_rate;
            while (t->playing && t->ticks >= t->wait) {
                t->ticks -= t->wait;
                if (!event(t)) {
                    t->playing = 0;
                    seq_voices_off(t, 0);
                    break;
                }
                t->wait = read_delta(t);
            }
        }
    }
}

/* --- setup --- */

void SsInit(void) {}
void SsStart2(void) {}
void SsSetTableSize(char *table, short seqs, short tracks) {}

/* SS_NOTICK | n: the game calls SsSeqCalledTbyT n times a second (60 if 0) */
void SsSetTickMode(long mode) {
    static const int RATES[6] = {60, 60, 240, 120, 50, 60};
    int n = (int)(mode & 0xFFF);

    s_tick_rate = n > 5 ? n : RATES[n];
}

void SsSetMVol(short l, short r) {
    spu_set_main_volume((int16_t)(l * 0x3FFF / 127), (int16_t)(r * 0x3FFF / 127));
}

/* The CD's audio (channel 0): its volume */
void SsSetSerialVol(char channel, short l, short r) {
    if (channel == 0) {
        spu_set_cd_volume((int16_t)(l * 0x3FFF / 127), (int16_t)(r * 0x3FFF / 127));
    }
}

void SsSetSerialAttr(char channel, char attr, char mode) {}
/* The reverb presets are libspu's, read from the user's own executable (the
   EU SLES_039.36) rather than carried here: for each of the 10 modes, its
   work area's start (in 8-byte units) at 0x8005C810, and at 0x8005C840 a
   mask word and the 32 registers dAPF1..vRIN (as SpuSetReverbModeParam) */
#define REV_STARTS 0x8005C810u
#define REV_PARAMS 0x8005C840u

void SsUtSetReverbType(short type) {
    static uint8_t exe[0x800 + 0x60000];
    static int loaded;
    uint32_t base, start;
    uint16_t regs[32];
    char path[1100];
    FILE *f;

    if (!loaded) {
        loaded = -1;
        snprintf(path, sizeof path, "%s/SLES_039.36", data_dir());
        if ((f = fopen(path, "rb")) != NULL) {
            loaded = fread(exe, 1, sizeof exe, f) > 0x800 ? 1 : -1;
            fclose(f);
        }
    }
    if (loaded < 0 || type < 0 || type > 9) {
        return;
    }
    memcpy(&base, exe + 0x18, 4); /* the PS-X EXE's load address */
    memcpy(&start, exe + 0x800 + (REV_STARTS - base) + type * 4, 4);
    memcpy(regs, exe + 0x800 + (REV_PARAMS - base) + type * 68 + 4, sizeof regs);
    spu_set_reverb(regs, start * 8);
}

/* 0-127, scaled as libsnd's (d * 0x7FFF / 127) */
void SsUtSetReverbDepth(short l, short r) {
    spu_set_reverb_depth((int16_t)(l * 0x7FFF / 127), (int16_t)(r * 0x7FFF / 127));
}

void SsUtReverbOn(void) {
    spu_set_reverb_on(1);
}

void SsUtReverbOff(void) {
    spu_set_reverb_on(0);
}
