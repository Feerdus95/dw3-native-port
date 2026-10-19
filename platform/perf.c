/* The battle's performance meter (F10, or perf = 1 in native.ini): only while a
   fight runs. A corner graph of the last frames, stacked by where the time
   went, and every frame of the fight in perf/battle_<time>.csv, so a drop can
   be found afterwards by its frame. Times are CPU (the game's code, the
   drawing of the frame, the swap, the wait for the next vsync) and, from the
   card's own timer queries (gl.c), the GPU's (the scene's batches, the props).
   The GPU's come a few frames late and are put with the frame they arrive in. */
#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"

#define RING 480
#define BINS 400 /* the histogram of the fight's frames: 0.5 ms each */

typedef struct {
    float interval, game, draw, props, present, wait, gpu, gpu_props, pload, pgl, pflush, ptex, spoly, bright, pdraw, soft, soft_n;
    int late;
} PerfRec;

int g_perf;
static PerfRec s_ring[RING];
static int s_count; /* frames recorded in this fight */
static double s_acc[PF_N], s_began[PF_N];
static double s_prev_enter, s_budget = 20;
static FILE *s_csv;
static int s_hist[BINS], s_late;
static double s_max, s_sum;
static int s_notes; /* perf_note's lines in this fight's csv */

/* A line about the frame being made, into the csv as a comment: "# <frame>: ..." */
void perf_note(const char *fmt, ...) {
    char line[256];
    va_list ap;

    if (s_csv == NULL || s_notes >= 3000) {
        return;
    }
    va_start(ap, fmt);
    SDL_vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fprintf(s_csv, "# %d: %s\n", s_count, line);
    s_notes++;
}

static double now_ms(void) {
    return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

int perf_on(void) {
    return g_perf && gte_in_battle();
}

void perf_begin(int s) {
    if (perf_on()) {
        s_began[s] = now_ms();
    }
}

void perf_end(int s) {
    if (s_began[s] > 0) {
        s_acc[s] += now_ms() - s_began[s];
        s_began[s] = 0;
    }
}

void perf_gpu(int s, double ms) {
    s_acc[s] += ms;
}

/* The fight is over (or the meter off): the csv closed with what the fight came to */
static void stop(void) {
    if (s_count > 0) {
        int i, seen = 0, p99 = 0;

        for (i = 0; i < BINS; i++) {
            seen += s_hist[i];
            if (seen >= s_count * 0.99 && !p99) {
                p99 = i + 1;
            }
        }
        LOG("perf: fight of %d frames: average %.1f ms, 99%% under %.1f ms, worst %.1f ms, %d late", s_count, s_sum / s_count, p99 * 0.5, s_max, s_late);
        if (s_csv != NULL) {
            fprintf(s_csv, "# frames %d, average %.2f ms, 99%% under %.1f ms, worst %.2f ms, late %d\n", s_count, s_sum / s_count, p99 * 0.5, s_max, s_late);
        }
    }
    if (s_csv != NULL) {
        fclose(s_csv);
        s_csv = NULL;
    }
    s_count = s_late = 0, s_max = s_sum = 0;
    memset(s_hist, 0, sizeof s_hist);
}

void perf_reset(void) {
    s_prev_enter = 0;
    memset(s_acc, 0, sizeof s_acc);
    memset(s_began, 0, sizeof s_began);
}

static void open_csv(void) {
    char path[1200];

    SDL_snprintf(path, sizeof path, "%sperf", SDL_GetBasePath());
    SDL_CreateDirectory(path);
    SDL_snprintf(path, sizeof path, "%sperf/battle_%llu.csv", SDL_GetBasePath(), (unsigned long long)SDL_GetTicks());
    s_csv = fopen(path, "w");
    if (s_csv != NULL) {
        fprintf(s_csv, "# budget %.2f ms; late: the frame took over 1.2 budgets; gpu times arrive ~3 frames late\n", s_budget);
        fprintf(s_csv, "frame,interval_ms,game_ms,draw_ms,props_cpu_ms,present_ms,wait_ms,gpu_scene_ms,gpu_props_ms,late,props_load_ms,props_gl_ms,props_flush_ms,props_tex_ms,stage_polys,stage_bright,props_drawn,soft_ms,soft_prims\n");
        s_notes = 0;
        LOG("perf: %s", path);
    }
}

/* At each vsync, before it waits: the frame that just ended is closed. BUDGET is the vsync's period in ms */
void perf_enter(double budget) {
    double t = now_ms();

    s_budget = budget;
    if (!perf_on() || g_ff_speed > 1) {
        stop();
        perf_reset();
        return;
    }
    if (s_prev_enter > 0) {
        PerfRec r;
        double ms;

        r.interval = (float)(t - s_prev_enter);
        r.draw = (float)s_acc[PF_DRAW], r.props = (float)s_acc[PF_PROPS], r.present = (float)s_acc[PF_PRESENT], r.wait = (float)s_acc[PF_WAIT];
        r.gpu = (float)s_acc[PF_GPU], r.gpu_props = (float)s_acc[PF_GPU_PROPS];
        r.pload = (float)s_acc[PF_PLOAD], r.pgl = (float)s_acc[PF_PGL], r.pflush = (float)s_acc[PF_PFLUSH], r.ptex = (float)s_acc[PF_PTEX];
        r.spoly = (float)s_acc[PF_SPOLY], r.bright = (float)s_acc[PF_BRIGHT], r.pdraw = (float)s_acc[PF_PDRAW];
        r.soft = (float)s_acc[PF_SOFT], r.soft_n = (float)s_acc[PF_SOFT_N];
        r.game = (float)SDL_max(r.interval - r.draw - r.present - r.wait, 0.0);
        r.late = r.interval > budget * 1.2;
        s_ring[s_count % RING] = r;
        if (s_csv == NULL && s_count == 0) {
            open_csv();
        }
        if (s_csv != NULL) {
            fprintf(s_csv, "%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%.2f,%.2f,%.2f,%.2f,%.0f,%.0f,%.0f,%.2f,%.0f\n", s_count, r.interval, r.game, r.draw, r.props, r.present, r.wait, r.gpu, r.gpu_props, r.late, r.pload, r.pgl, r.pflush, r.ptex, r.spoly, r.bright, r.pdraw, r.soft, r.soft_n);
        }
        ms = r.interval;
        s_hist[SDL_clamp((int)(ms * 2), 0, BINS - 1)]++;
        s_sum += ms, s_late += r.late;
        if (ms > s_max) {
            s_max = ms;
        }
        s_count++;
    }
    s_prev_enter = t;
    memset(s_acc, 0, sizeof s_acc);
}

/* The corner graph over the picture, in the menu's 320x240 units: a column a frame, stacked game / drawing /
   swap from the bottom, the vsync's budget as a line, a late frame with a red cap */
void perf_draw(SDL_Renderer *r) {
    enum { W = 160, GH = 36, TEXT = 5 };
    float x = 4, h = GH + TEXT * 10 + 8, y = 240 - 4 - h, base = y + 4 + GH, k = GH / (2.0f * (float)s_budget);
    int n = SDL_min(s_count, W), i, j;
    double sum = 0, worst = 0, g = 0, d = 0, p = 0, gp = 0, gs = 0, w = 0;
    char text[48];
    SDL_FRect bg = {x, y, W + 8, h};

    if (s_count < 2) {
        return;
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 170);
    SDL_RenderFillRect(r, &bg);
    for (i = 0; i < n; i++) {
        const PerfRec *q = &s_ring[(s_count - n + i) % RING];
        float cx = x + 4 + (W - n) + i, top = base;
        const float parts[3] = {q->game, q->draw, q->present};
        static const Uint8 COL[3][3] = {{70, 130, 230}, {90, 200, 110}, {150, 150, 160}};

        for (j = 0; j < 3; j++) {
            float hh = SDL_max(SDL_min(parts[j] * k, GH - (base - top)), 0.0f);
            SDL_FRect b = {cx, top - hh, 1, hh};

            SDL_SetRenderDrawColor(r, COL[j][0], COL[j][1], COL[j][2], 255);
            SDL_RenderFillRect(r, &b);
            top -= hh;
        }
        if (q->late) {
            SDL_FRect b = {cx, base - GH, 1, 2};

            SDL_SetRenderDrawColor(r, 255, 70, 70, 255);
            SDL_RenderFillRect(r, &b);
        }
        sum += q->interval, g += q->game, d += q->draw, p += q->props, gp += q->gpu_props, gs += q->gpu, w += q->wait;
        if (q->interval > worst) {
            worst = q->interval;
        }
    }
    SDL_SetRenderDrawColor(r, 255, 255, 255, 200);
    SDL_RenderLine(r, x + 4, base - (float)s_budget * k, x + 4 + W, base - (float)s_budget * k);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_snprintf(text, sizeof text, "%.1f fps  %.1f ms  max %.0f", 1000.0 / (sum / n), sum / n, worst);
    SDL_RenderDebugText(r, x + 4, base + 3, text);
    SDL_snprintf(text, sizeof text, "game %.1f  draw %.1f", g / n, d / n);
    SDL_RenderDebugText(r, x + 4, base + 13, text);
    SDL_snprintf(text, sizeof text, "props cpu %.1f gpu %.1f", p / n, gp / n);
    SDL_RenderDebugText(r, x + 4, base + 23, text);
    SDL_snprintf(text, sizeof text, "gpu %.1f  idle %.1f", (gs + gp) / n, w / n);
    SDL_RenderDebugText(r, x + 4, base + 33, text);
    SDL_snprintf(text, sizeof text, "late %d of %d", s_late, s_count);
    SDL_RenderDebugText(r, x + 4, base + 43, text);
}
