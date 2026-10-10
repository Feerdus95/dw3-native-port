/* The field editor's changes to a stage: data/mods/stages/<stage>.json,
   applied after the stage's setup (native_apply_stage_mod, from FIELDSTG's
   startField). The file has the stage dump's form (stagedump.c), with only
   the parts the editor changed: "objects", "slots", "actors" (and "images"
   for them), "start" and "startDir", "places" (the battles), "music" and
   "soundBank". Each one present replaces the stage's own table; the tables
   are built here, as the game reads them (stage_tables.h), and live until
   the next field. The map itself (its tiles and logic maps) is a file the
   editor writes to data/mods/<fid>.bin, which files.c already prefers. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "stage_tables.h"

extern uint8_t GAME[]; /* the game's GameState */
#define GAME_PLACE 0x44 /* GAME.place, u16: the place that picks a stage's battles */

/* --- a small JSON reader: numbers, strings, arrays and objects --- */

typedef struct Json {
    int type; /* 'n' number, 's' string, '[' array, '{' object, 0 null */
    double num;
    char *str;           /* a string, or an object member's key */
    struct Json *child;  /* the first item of an array or object */
    struct Json *next;   /* the next item */
} Json;

static Json *json_value(const char **p);

static void json_ws(const char **p) {
    while (**p == ' ' || **p == '\n' || **p == '\r' || **p == '\t') (*p)++;
}

static void json_free(Json *j) {
    while (j != NULL) {
        Json *next = j->next;
        json_free(j->child);
        free(j->str);
        free(j);
        j = next;
    }
}

static char *json_string(const char **p) {
    const char *s = ++*p;
    size_t n = 0;
    char *out;

    size_t k = 0;

    while (s[n] != 0 && s[n] != '"') n += s[n] == '\\' && s[n + 1] != 0 ? 2 : 1;
    out = malloc(n + 1);
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\') i++; /* \" and \\ as they are */
        out[k++] = s[i];
    }
    out[k] = 0;
    *p = s + n + (s[n] == '"');
    return out;
}

static Json *json_value(const char **p) {
    Json *j = calloc(1, sizeof(Json));

    json_ws(p);
    if (**p == '[' || **p == '{') {
        char close = **p == '[' ? ']' : '}';
        Json **tail = &j->child;

        j->type = **p;
        (*p)++;
        for (;;) {
            char *key = NULL;
            Json *v;

            json_ws(p);
            if (**p == close) {
                (*p)++;
                break;
            }
            if (j->type == '{') {
                if (**p != '"') goto bad;
                key = json_string(p);
                json_ws(p);
                if (**p != ':') {
                    free(key);
                    goto bad;
                }
                (*p)++;
            }
            if ((v = json_value(p)) == NULL) {
                free(key);
                goto bad;
            }
            v->str = v->type == 's' ? v->str : key;
            if (v->type == 's' && key != NULL) free(key); /* keys of string members aren't needed */
            *tail = v;
            tail = &v->next;
            json_ws(p);
            if (**p == ',') (*p)++;
            else if (**p != close) goto bad;
        }
        return j;
    }
    if (**p == '"') {
        j->type = 's';
        j->str = json_string(p);
        return j;
    }
    if (strncmp(*p, "null", 4) == 0) {
        *p += 4;
        return j;
    }
    {
        char *end;
        j->num = strtod(*p, &end);
        if (end == *p) goto bad;
        j->type = 'n';
        *p = end;
        return j;
    }
bad:
    json_free(j);
    return NULL;
}

static Json *json_get(Json *obj, const char *key) {
    for (Json *c = obj != NULL ? obj->child : NULL; c != NULL; c = c->next) {
        if (c->type != 's' && c->str != NULL && strcmp(c->str, key) == 0) return c;
    }
    return NULL;
}

static Json *json_at(Json *arr, int i) {
    Json *c = arr != NULL ? arr->child : NULL;
    while (c != NULL && i-- > 0) c = c->next;
    return c;
}

static int json_len(Json *arr) {
    int n = 0;
    for (Json *c = arr != NULL ? arr->child : NULL; c != NULL; c = c->next) n++;
    return n;
}

static int json_int(Json *j) { return j != NULL && j->type == 'n' ? (int)j->num : 0; }

/* --- the tables --- */

/* What the last applied mod allocated: freed when the next field starts */
static void *s_blocks[4096];
static int s_count;

static void *alloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (s_count < (int)(sizeof s_blocks / sizeof *s_blocks)) s_blocks[s_count++] = p;
    return p;
}

/* (code, value) pairs up to CODES_END (0xFFFF), or NULL for none */
static uint16_t *codes(Json *list) {
    int n = json_len(list);
    uint16_t *c;

    if (n == 0) return NULL;
    c = alloc((n * 2 + 1) * sizeof(uint16_t));
    for (int i = 0; i < n; i++) {
        c[i * 2] = (uint16_t)json_int(json_at(json_at(list, i), 0));
        c[i * 2 + 1] = (uint16_t)json_int(json_at(json_at(list, i), 1));
    }
    c[n * 2] = 0xFFFF;
    return c;
}

static void apply(FieldState *st, Json *mod) {
    Json *j;

    if ((j = json_get(mod, "objects")) != NULL) {
        int n = json_len(j);
        StageTile *t = alloc((n + 1) * sizeof(StageTile));
        for (int i = 0; i < n; i++) {
            Json *o = json_at(j, i);
            int v[14];
            for (int k = 0; k < 14; k++) v[k] = json_int(json_at(o, k));
            t[i] = (StageTile){(uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3], (uint8_t)v[4],
                               (uint8_t)v[5], (uint8_t)v[6], (uint8_t)v[7], (uint8_t)v[8], (uint8_t)v[9],
                               (int16_t)v[10], (int16_t)v[11], (int16_t)v[12], (int16_t)v[13]};
            if (t[i].margin == 0) t[i].margin = 1; /* margin 0 ends the table */
        }
        st->objects = t; /* the last one zeroed: margin 0 */
    }
    if ((j = json_get(mod, "slots")) != NULL) {
        int n = json_len(j);
        StageSlot *s = alloc((n + 1) * sizeof(StageSlot));
        for (int i = 0; i < n; i++) {
            uint16_t v[12];
            for (int k = 0; k < 12; k++) v[k] = (uint16_t)json_int(json_at(json_at(j, i), k));
            memcpy(&s[i], v, sizeof v);
        }
        st->slots = s; /* type 0 ends it */
    }
    if ((j = json_get(mod, "images")) != NULL) {
        int n = json_len(j);
        ActorImage *m = alloc((n + 1) * sizeof(ActorImage));
        for (int i = 0; i < n; i++) {
            int16_t v[8];
            for (int k = 0; k < 8; k++) v[k] = (int16_t)json_int(json_at(json_at(j, i), k));
            memcpy(&m[i], v, sizeof v);
        }
        st->images = m;
    }
    if ((j = json_get(mod, "actors")) != NULL) {
        int n = json_len(j);
        FieldActorEntry **list = alloc((n + 1) * sizeof(FieldActorEntry *));
        for (int i = 0; i < n; i++) {
            Json *a = json_at(j, i), *talks = json_get(a, "talks");
            int nt = json_len(talks);
            FieldActorEntry *e = alloc(sizeof(FieldActorEntry));
            FieldTalk *t = alloc((nt + 1) * sizeof(FieldTalk));

            e->conditions = codes(json_get(a, "conditions"));
            e->id = (int16_t)json_int(json_get(a, "id"));
            e->image = (int16_t)json_int(json_get(a, "image"));
            e->x = (int16_t)json_int(json_get(a, "x"));
            e->y = (int16_t)json_int(json_get(a, "y"));
            e->dir = (int16_t)json_int(json_get(a, "dir"));
            for (int k = 0; k < nt; k++) {
                Json *tk = json_at(talks, k);
                t[k].conditions = codes(json_get(tk, "conditions"));
                t[k].actions = codes(json_get(tk, "actions"));
                t[k].entry = json_int(json_get(tk, "entry"));
            }
            /* the talks end at the first without conditions: the game reads on
               until one, so a list whose last has some gets a silent one */
            if (nt == 0 || t[nt - 1].conditions != NULL) t[nt].entry = nt ? t[nt - 1].entry : 0;
            e->talks = t;
            list[i] = e;
        }
        st->actors = list; /* NULL ends it */
    }
    if ((j = json_get(mod, "start")) != NULL) {
        st->startX = json_int(json_at(j, 0));
        st->startY = json_int(json_at(j, 1));
    }
    if ((j = json_get(mod, "startDir")) != NULL) st->startDir = json_int(j);
    if ((j = json_get(mod, "music")) != NULL) st->music = json_int(j);
    if ((j = json_get(mod, "soundBank")) != NULL) st->soundBank = json_int(j);
    if ((j = json_get(mod, "places")) != NULL) {
        int n = json_len(j);
        FieldBattles *list = alloc((n + 1) * sizeof(FieldBattles));
        for (int i = 0; i < n; i++) {
            Json *p = json_at(j, i), *areas = json_get(p, "areas");
            list[i].serial = json_int(json_get(p, "serial"));
            list[i].id = json_int(json_get(p, "id"));
            for (int a = 0; a < 4; a++) {
                Json *ar = json_at(areas, a), *battles = json_get(ar, "battles");
                BattleList *l = alloc(sizeof(BattleList));
                l->count = json_int(json_get(ar, "rate"));
                for (int k = 0; k < 8; k++) {
                    Json *b = json_at(battles, k);
                    Battle *t = alloc(sizeof(Battle));
                    t->encounter = json_int(json_at(b, 0));
                    t->stage = json_int(json_at(b, 1));
                    t->music = json_int(json_at(b, 2));
                    l->battles[k] = t;
                }
                list[i].battles[a] = l;
            }
        }
        /* one place is the stage's; several, the one of GAME.place (FieldState.findBattles) */
        if (n == 1) st->battles = &list[0];
        else if (n > 1) st->battles = st->findBattles(list, *(uint16_t *)(GAME + GAME_PLACE));
        else st->battles = NULL;
    }
}

const StageFuncs *native_current_stage(void) {
    for (const StageFuncs *s = native_stage_list; s->name != NULL; s++) {
        if (s->start == FIELDSTG_state.stageInit) return s;
    }
    return NULL;
}

void native_apply_stage_mod(void) {
    const StageFuncs *stage = native_current_stage();
    char path[1200];
    FILE *f;
    long size;
    char *text;
    const char *p;
    Json *mod;

    for (int i = 0; i < s_count; i++) free(s_blocks[i]);
    s_count = 0;
    if (stage == NULL) return;
    snprintf(path, sizeof path, "%s/mods/stages/%s.json", data_dir(), stage->name);
    if ((f = fopen(path, "rb")) == NULL) return;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = malloc(size + 1);
    text[fread(text, 1, size, f)] = 0;
    fclose(f);
    p = text;
    mod = json_value(&p);
    free(text);
    if (mod == NULL || mod->type != '{') {
        LOG("stage mod %s: not a JSON object, left out", path);
        json_free(mod);
        return;
    }
    apply(&FIELDSTG_state, mod);
    json_free(mod);
    LOG("stage mod: %s", path);
}
