/* dw2003 --dump-stages DIR: runs each stage's setup (WSTAGnnn_setupStage,
   linked in) on a cleared FIELDSTG_state and writes what it set to
   DIR/wstagNNN.json, for the field editor: the map's files, the objects,
   slots, characters, battles and events (stage_tables.h). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"
#include "stage_tables.h"


/* The place list a stage's setup searched (FieldState.findBattles), which
   is dumped whole: GAME.place is 0 here */
static FieldBattles *s_places;
static FieldBattles *(*s_findBattles)(FieldBattles *list, int32_t id);

static FieldBattles *record_places(FieldBattles *list, int32_t id) {
    s_places = list;
    return s_findBattles(list, id);
}

static void codes(FILE *f, const uint16_t *p) {
    fputc('[', f);
    for (int i = 0; p != NULL && p[i] != 0xFFFF && i < 64; i += 2) {
        fprintf(f, "%s[%u,%u]", i ? "," : "", p[i], p[i + 1]);
    }
    fputc(']', f);
}

static void places(FILE *f, FieldBattles *b, int list) {
    fputc('[', f);
    for (int i = 0; b != NULL && i < (list ? 30 : 1); i++, b++) {
        if (list && (b->serial <= 0 || b->serial > 448 || b->battles[0] == NULL)) {
            break;
        }
        fprintf(f, "%s{\"serial\":%d,\"id\":%d,\"areas\":[", i ? "," : "", b->serial, b->id);
        for (int a = 0; a < 4; a++) {
            BattleList *l = b->battles[a];
            fprintf(f, "%s{\"rate\":%d,\"battles\":[", a ? "," : "", l ? l->count : 0);
            for (int k = 0; l != NULL && k < 8; k++) {
                Battle *t = l->battles[k];
                fprintf(f, "%s[%d,%d,%d]", k ? "," : "", t ? t->encounter : 0, t ? t->stage : 0, t ? t->music : 0);
            }
            fputs("]}", f);
        }
        fputs("]}", f);
    }
    fputc(']', f);
}

/* [mode, overlay file, area name, place name] of each mode the stage runs */
static void modes(FILE *f, const StageEntry *e, void *start) {
    int n = 0;

    for (; e->mode != 0; e++) {
        if (e->init == start) {
            int area = 0, place = 0;

            for (const AreaName *a = FIELDSTG_areaNames; a->mode != 0; a++) {
                if (a->mode == e->mode) {
                    area = a->area, place = a->place;
                    break;
                }
            }
            fprintf(f, "%s[%d,%d,%d,%d]", n++ ? "," : "", e->mode, e->file, area, place);
        }
    }
}

static void dump(FILE *f, const StageFuncs *s) {
    FieldState *st = &FIELDSTG_state;
    int images = 6; /* the field's two, the player's and the partners' (FIELDSTG_createActor's 0 to 3) */

    native_reset_overlays(1);
    memset(st, 0, 0x64);
    memset(&FIELDSTG_map, 0, sizeof FIELDSTG_map);
    s_places = NULL;
    s_findBattles = st->findBattles;
    st->findBattles = record_places;
    s->setup();
    st->findBattles = s_findBattles;

    fprintf(f, "{\"name\":\"%s\",\"modes\":[", s->name);
    modes(f, FIELDSTG_stages, s->start);
    fputs("],\"euModes\":[", f);
    modes(f, FIELDSTG_euStages, s->start);
    fprintf(f, "],\n\"mapFile\":%d,\"sheetEntry\":%d,\"imageEntry\":%d,\"imageFile\":%d,\"textFile\":%d,"
               "\"soundBank\":%d,\"music\":%d,\"start\":[%d,%d],\"startDir\":%d,\"spriteColor\":[%u,%u,%u,%u],\n"
               "\"mapFiles\":[",
            st->mapFile, st->sheetEntry, st->imageEntry, st->imageFile, st->textFile, st->soundBank, st->music,
            st->startX, st->startY, st->startDir, st->spriteColor[0], st->spriteColor[1], st->spriteColor[2],
            st->spriteColor[3]);
    for (int i = 0; i < 8; i++) {
        fprintf(f, "%s%d", i ? "," : "", FIELDSTG_map.files[i]);
    }

    /* the objects end with margin 0 */
    fputs("],\n\"objects\":[", f);
    for (StageTile *o = st->objects; o != NULL && o->margin != 0; o++) {
        fprintf(f, "%s\n[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%d,%d,%d]", o == st->objects ? "" : ",", o->visible,
                o->anim, o->margin, o->depth, o->frame, o->cycle, o->cycleFirst, o->cycleLast, o->cycleDelay,
                o->clutRow, o->x, o->y, o->sortY, o->cycleTime);
    }

    /* the slots end with type 0 */
    fputs("],\n\"slots\":[", f);
    for (StageSlot *s = st->slots; s != NULL && s->type != 0; s++) {
        fprintf(f, "%s\n[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]", s == st->slots ? "" : ",", s->conditions[0][0],
                s->conditions[0][1], s->conditions[1][0], s->conditions[1][1], s->type, s->arg, s->x, s->y, s->dir,
                s->hideAnim, s->place, s->placeArg);
    }

    /* the characters, up to NULL; their talks up to the first without conditions */
    fputs("],\n\"actors\":[", f);
    for (FieldActorEntry **a = st->actors; a != NULL && *a != NULL; a++) {
        FieldActorEntry *e = *a;

        fprintf(f, "%s\n{\"id\":%d,\"image\":%d,\"x\":%d,\"y\":%d,\"dir\":%d,\"conditions\":", a == st->actors ? "" : ",",
                e->id, e->image, e->x, e->y, e->dir);
        codes(f, e->conditions);
        fputs(",\"talks\":[", f);
        for (FieldTalk *t = e->talks; t != NULL; t++) {
            fprintf(f, "%s{\"entry\":%d,\"conditions\":", t == e->talks ? "" : ",", t->entry);
            codes(f, t->conditions);
            fputs(",\"actions\":", f);
            codes(f, t->actions);
            fputc('}', f);
            if (t->conditions == NULL) {
                break;
            }
        }
        fputs("]}", f);
        /* FIELDSTG_createActor takes images.actors[image + 2] */
        if (e->image + 3 > images) {
            images = e->image + 3;
        }
    }

    /* the images: the field's two, then the characters' as far as they use, the party's first */
    fputs("],\n\"images\":[", f);
    for (int i = 0; st->images != NULL && i < images; i++) {
        ActorImage *m = &st->images[i];
        fprintf(f, "%s[%d,%d,%d,%d,%d,%d,%d,%d]", i ? "," : "", m->x, m->y, m->imageX, m->imageY, m->u, m->v,
                m->clutX, m->clutY);
    }

    fputs("],\n\"places\":", f);
    places(f, s_places != NULL ? s_places : st->battles, s_places != NULL);

    /* the events end with id -1: each one's script (the words FIELDSTG_runEvent
       reads, up to its end) and whether code starts or ends it */
    fputs(",\n\"events\":[", f);
    for (FieldEvent *e = st->events; e != NULL && e->id != -1; e++) {
        fprintf(f, "%s\n{\"id\":%d,\"text\":%d,\"start\":%d,\"end\":%d,\"script\":", e == st->events ? "" : ",", e->id,
                e->text, e->start != NULL, e->end != NULL);
        if (e->script == NULL) {
            fputs("null}", f);
            continue;
        }
        int n = native_script_length(e->script);
        for (int i = 0; i < n; i++) fprintf(f, "%s%d", i ? "," : "[", e->script[i]);
        fputs("]}", f);
    }
    fputs("]}\n", f);
}

int stage_dump(const char *dir) {
    int n = 0;

    SDL_CreateDirectory(dir);
    for (const StageFuncs *s = native_stage_list; s->name != NULL; s++) {
        char path[1200];
        FILE *f;

        snprintf(path, sizeof path, "%s/%s.json", dir, s->name);
        if ((f = fopen(path, "w")) == NULL) {
            LOG("can't write %s", path);
            return 1;
        }
        dump(f, s);
        fclose(f);
        n++;
    }
    native_reset_overlays(1);

    /* the characters, for their sprites */
    char path[1200];
    FILE *f;
    snprintf(path, sizeof path, "%s/characters.json", dir);
    if ((f = fopen(path, "w")) == NULL) {
        LOG("can't write %s", path);
        return 1;
    }
    fputs("{\"animFiles\":[", f);
    for (int i = 0; i < CHARACTERS; i++) {
        fprintf(f, "%s%d", i ? "," : "", FIELDSTG_fileEntries[i]);
    }
    fputs("],\n\"widths\":[", f);
    for (int i = 0; i < CHARACTERS; i++) {
        fprintf(f, "%s%u", i ? "," : "", FIELDSTG_actorWidths[i]);
    }
    fputs("]}\n", f);
    fclose(f);
    LOG("%d stages dumped to %s", n, dir);
    return 0;
}
