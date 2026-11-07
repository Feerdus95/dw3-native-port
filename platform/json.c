/* The JSON reader (json.h) */
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json.h"

typedef struct {
    const char *start, *p, *end;
    int max_depth, commas;
    JsonInfo *info;
    int failed;
} P;

static Json *fail(P *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static Json *fail(P *s, const char *fmt, ...) {
    __builtin_va_list ap;

    if (!s->failed) {
        int line = 1, col = 1;

        for (const char *c = s->start; c < s->p && c < s->end; c++) {
            if (*c == '\n') {
                line++, col = 1;
            } else {
                col++;
            }
        }
        __builtin_va_start(ap, fmt);
        vsnprintf(s->info->msg, sizeof s->info->msg, fmt, ap);
        __builtin_va_end(ap);
        s->info->line = line, s->info->col = col;
        s->failed = 1;
    }
    return NULL;
}

void json_free(Json *j) {
    while (j != NULL) {
        Json *next = j->next;

        json_free(j->child);
        free(j->str);
        free(j->key);
        free(j);
        j = next;
    }
}

static void skip_ws(P *s) {
    while (s->p < s->end && (*s->p == ' ' || *s->p == '\n' || *s->p == '\r' || *s->p == '\t')) {
        s->p++;
    }
}

static Json *node(P *s) {
    Json *j = calloc(1, sizeof *j);

    return j != NULL ? j : fail(s, "out of memory");
}

static int hex4(const char *p) {
    int v = 0;

    for (int i = 0; i < 4; i++) {
        int c = (unsigned char)p[i];

        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;

        if (d < 0) {
            return -1;
        }
        v = v * 16 + d;
    }
    return v;
}

static char *put_utf8(char *w, unsigned cp) {
    if (cp < 0x80) {
        *w++ = (char)cp;
    } else if (cp < 0x800) {
        *w++ = (char)(0xC0 | cp >> 6), *w++ = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        *w++ = (char)(0xE0 | cp >> 12), *w++ = (char)(0x80 | (cp >> 6 & 0x3F)), *w++ = (char)(0x80 | (cp & 0x3F));
    } else {
        *w++ = (char)(0xF0 | cp >> 18), *w++ = (char)(0x80 | (cp >> 12 & 0x3F)), *w++ = (char)(0x80 | (cp >> 6 & 0x3F)), *w++ = (char)(0x80 | (cp & 0x3F));
    }
    return w;
}

/* The string at s->p (its opening quote), decoded into UTF-8 in a new buffer; s->p behind its closing quote */
static char *string(P *s) {
    const char *q = s->p + 1, *e = q, *r;
    char *out, *w;

    while (e < s->end && *e != '"') {
        if ((unsigned char)*e < 0x20) {
            s->p = e;
            fail(s, "a control character in a string (0x%02X)", (unsigned char)*e);
            return NULL;
        }
        e += *e == '\\' ? 2 : 1;
    }
    if (e >= s->end) {
        s->p = s->end;
        fail(s, "a string isn't closed");
        return NULL;
    }
    if ((out = malloc((size_t)(e - q) + 1)) == NULL) {
        fail(s, "out of memory");
        return NULL;
    }
    for (r = q, w = out; r < e;) {
        unsigned char c = (unsigned char)*r;

        if (c == '\\') {
            r++;
            switch (*r) {
            case '"': case '\\': case '/': *w++ = *r++; break;
            case 'b': *w++ = '\b', r++; break;
            case 'f': *w++ = '\f', r++; break;
            case 'n': *w++ = '\n', r++; break;
            case 'r': *w++ = '\r', r++; break;
            case 't': *w++ = '\t', r++; break;
            case 'u': {
                int cp = r + 4 < e + 0 ? hex4(r + 1) : -1;

                if (cp < 0) {
                    s->p = r;
                    fail(s, "a bad \\u escape");
                    goto bad;
                }
                r += 5;
                if (cp >= 0xD800 && cp <= 0xDBFF) { /* a high surrogate: its low one follows as another \u */
                    int lo = r + 5 < e + 0 && r[0] == '\\' && r[1] == 'u' ? hex4(r + 2) : -1;

                    if (lo < 0xDC00 || lo > 0xDFFF) {
                        s->p = r;
                        fail(s, "a high surrogate without its low one");
                        goto bad;
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    r += 6;
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    s->p = r;
                    fail(s, "a low surrogate on its own");
                    goto bad;
                }
                if (cp == 0) {
                    s->p = r;
                    fail(s, "\\u0000 isn't allowed (the strings are C strings)");
                    goto bad;
                }
                w = put_utf8(w, (unsigned)cp);
                break;
            }
            default:
                s->p = r;
                fail(s, "an escape JSON doesn't have (\\%c)", *r >= 0x20 && *r < 0x7F ? *r : '?');
                goto bad;
            }
        } else if (c < 0x80) {
            *w++ = (char)c, r++;
        } else {
            int n = c >= 0xC2 && c <= 0xDF ? 2 : c >= 0xE0 && c <= 0xEF ? 3 : c >= 0xF0 && c <= 0xF4 ? 4 : 0, ok = n > 0 && r + n <= e;

            for (int i = 1; ok && i < n; i++) {
                ok = ((unsigned char)r[i] & 0xC0) == 0x80;
            }
            if (ok && n > 1) { /* no overlong forms, no surrogates, nothing past U+10FFFF */
                unsigned char b = (unsigned char)r[1];

                ok = !(c == 0xE0 && b < 0xA0) && !(c == 0xED && b > 0x9F) && !(c == 0xF0 && b < 0x90) && !(c == 0xF4 && b > 0x8F);
            }
            if (!ok) {
                s->p = r;
                fail(s, "bytes that aren't UTF-8");
                goto bad;
            }
            memcpy(w, r, (size_t)n), w += n, r += n;
        }
    }
    *w = 0;
    s->p = e + 1;
    return out;
bad:
    free(out);
    return NULL;
}

static Json *value(P *s, int depth);

static Json *container(P *s, int depth) {
    char open = *s->p, close = open == '[' ? ']' : '}';
    Json *j, **tail;
    int after_comma = 0;

    if (depth + 1 > s->max_depth) {
        return fail(s, "nested more than %d levels", s->max_depth);
    }
    if ((j = node(s)) == NULL) {
        return NULL;
    }
    tail = &j->child;
    j->type = open;
    s->p++;
    for (;;) {
        char *key = NULL;
        Json *v;

        skip_ws(s);
        if (s->p >= s->end) {
            fail(s, "the file ends inside a '%c'", open);
            goto bad;
        }
        if (*s->p == close) {
            s->commas += after_comma;
            s->p++;
            return j;
        }
        if (open == '{') {
            if (*s->p != '"') {
                fail(s, "a member's name (a string) was expected");
                goto bad;
            }
            if ((key = string(s)) == NULL) {
                goto bad;
            }
            skip_ws(s);
            if (s->p >= s->end || *s->p != ':') {
                free(key);
                fail(s, "a ':' was expected after the name");
                goto bad;
            }
            s->p++;
            for (Json *o = j->child; o != NULL; o = o->next) { /* two readers would pick different ones: neither is picked */
                if (strcmp(o->key, key) == 0) {
                    fail(s, "the name \"%.40s\" is in the object twice", key);
                    free(key);
                    goto bad;
                }
            }
        }
        if ((v = value(s, depth + 1)) == NULL) {
            free(key);
            goto bad;
        }
        v->key = key;
        *tail = v;
        tail = &v->next;
        skip_ws(s);
        if (s->p < s->end && *s->p == ',') {
            s->p++;
            after_comma = 1;
        } else if (s->p < s->end && *s->p == close) {
            after_comma = 0;
        } else {
            fail(s, s->p >= s->end ? "the file ends inside a '%c'" : "a ',' or '%c' was expected", s->p >= s->end ? open : close);
            goto bad;
        }
    }
bad:
    json_free(j);
    return NULL;
}

static Json *keyword(P *s) {
    static const struct { const char *word; int type, num; } W[] = {{"true", 'b', 1}, {"false", 'b', 0}, {"null", 0, 0}};
    Json *j;

    for (int i = 0; i < 3; i++) {
        size_t n = strlen(W[i].word);

        if ((size_t)(s->end - s->p) >= n && memcmp(s->p, W[i].word, n) == 0 &&
            (s->p + n == s->end || !(((s->p[n] | 0x20) >= 'a' && (s->p[n] | 0x20) <= 'z') || (s->p[n] >= '0' && s->p[n] <= '9') || s->p[n] == '_'))) {
            if ((j = node(s)) == NULL) {
                return NULL;
            }
            j->type = W[i].type, j->num = W[i].num;
            s->p += n;
            return j;
        }
    }
    return fail(s, "an unknown word");
}

/* The number's grammar: JSON's (-?int frac? exp?, no leading zero), with a leading + and a missing int digit before the point as the
   extensions json.h names. "1.", "01", "-01", "1e", "+-1" and the like are not numbers */
static int number_form(const char *p, size_t n) {
    const char *e = p + n;
    int digits = 0;

    if (p < e && (*p == '+' || *p == '-')) {
        p++;
    }
    if (p < e && *p == '0' && p + 1 < e && p[1] >= '0' && p[1] <= '9') {
        return 0;
    }
    while (p < e && *p >= '0' && *p <= '9') {
        p++, digits++;
    }
    if (p < e && *p == '.') {
        int frac = 0;

        for (p++; p < e && *p >= '0' && *p <= '9'; p++) {
            frac++;
        }
        if (frac == 0) {
            return 0;
        }
        digits += frac;
    }
    if (digits == 0) {
        return 0;
    }
    if (p < e && (*p == 'e' || *p == 'E')) {
        int ex = 0;

        p++;
        if (p < e && (*p == '+' || *p == '-')) {
            p++;
        }
        for (; p < e && *p >= '0' && *p <= '9'; p++) {
            ex++;
        }
        if (ex == 0) {
            return 0;
        }
    }
    return p == e;
}

static Json *number(P *s) {
    char buf[64], *end;
    const char *q = s->p;
    size_t n;
    Json *j;

    while (q < s->end && strchr("+-.0123456789eE", *q) != NULL && *q != 0) {
        q++;
    }
    n = (size_t)(q - s->p);
    if (n > 0 && !number_form(s->p, n)) {
        return fail(s, "a malformed number");
    }
    if (n == 0) {
        unsigned char c = (unsigned char)*s->p;

        return c >= 0x20 && c < 0x7F ? fail(s, "unexpected '%c'", c) : fail(s, "unexpected byte 0x%02X", c);
    }
    if (n >= sizeof buf) {
        return fail(s, "a number too long");
    }
    memcpy(buf, s->p, n), buf[n] = 0;
    j = NULL;
    {
        double v;

        errno = 0;
        v = strtod(buf, &end);
        if (end != buf + n) {
            return fail(s, "a malformed number");
        }
        if (!isfinite(v) || errno == ERANGE) { /* too big, or so small it would come out 0 */
            return fail(s, "a number out of range");
        }
        if ((j = node(s)) == NULL) {
            return NULL;
        }
        j->type = 'n', j->num = v;
    }
    s->p = q;
    return j;
}

static Json *value(P *s, int depth) {
    Json *j;

    skip_ws(s);
    if (s->p >= s->end) {
        return fail(s, "the file ends where a value was expected");
    }
    switch (*s->p) {
    case '[': case '{': return container(s, depth);
    case '"':
        if ((j = node(s)) == NULL) {
            return NULL;
        }
        j->type = 's';
        if ((j->str = string(s)) == NULL) {
            free(j);
            return NULL;
        }
        return j;
    case 't': case 'f': case 'n': return keyword(s);
    default: return number(s);
    }
}

static void start(P *s, const char *text, long len, const JsonLimits *limits, JsonInfo *info, JsonInfo *spare) {
    memset(s, 0, sizeof *s);
    s->info = info != NULL ? info : spare;
    memset(s->info, 0, sizeof *s->info);
    s->start = s->p = text, s->end = text + len;
    s->max_depth = limits != NULL && limits->max_depth > 0 && limits->max_depth < JSON_MAX_DEPTH ? limits->max_depth : JSON_MAX_DEPTH;
    if (len >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0) {
        s->p += 3;
    }
}

Json *json_parse(const char *text, long len, const JsonLimits *limits, JsonInfo *info) {
    JsonInfo spare;
    P s;
    Json *j;

    start(&s, text, len, limits, info, &spare);
    if (limits != NULL && limits->max_bytes > 0 && len > limits->max_bytes) {
        return fail(&s, "%ld bytes, the limit is %ld", len, limits->max_bytes);
    }
    if ((j = value(&s, 0)) == NULL) {
        return NULL;
    }
    skip_ws(&s);
    if (s.p < s.end) {
        json_free(j);
        return fail(&s, "something after the value");
    }
    s.info->commas = s.commas;
    return j;
}

Json *json_load(const char *path, const JsonLimits *limits, JsonInfo *info) {
    JsonInfo spare;
    P s;
    FILE *f = fopen(path, "rb");
    long size;
    char *text;
    Json *j;

    start(&s, "", 0, limits, info, &spare);
    if (f == NULL) {
        s.info->unopened = 1;
        return fail(&s, "the file can't be opened");
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return fail(&s, "the file's size can't be read");
    }
    if (limits != NULL && limits->max_bytes > 0 && size > limits->max_bytes) {
        fclose(f);
        return fail(&s, "%ld bytes, the limit is %ld", size, limits->max_bytes);
    }
    if ((text = malloc((size_t)size + 1)) == NULL) {
        fclose(f);
        return fail(&s, "out of memory (%ld bytes)", size);
    }
    if (fread(text, 1, (size_t)size, f) != (size_t)size) {
        free(text);
        fclose(f);
        return fail(&s, "the file can't be read");
    }
    fclose(f);
    text[size] = 0;
    j = json_parse(text, size, limits, info);
    free(text);
    return j;
}

Json *json_get(Json *obj, const char *key) {
    for (Json *c = obj != NULL ? obj->child : NULL; c != NULL; c = c->next) {
        if (c->key != NULL && strcmp(c->key, key) == 0) {
            return c;
        }
    }
    return NULL;
}

Json *json_at(Json *arr, int i) {
    Json *c = arr != NULL ? arr->child : NULL;

    while (c != NULL && i-- > 0) {
        c = c->next;
    }
    return c;
}

int json_len(Json *arr) {
    int n = 0;

    for (Json *c = arr != NULL ? arr->child : NULL; c != NULL; c = c->next) {
        n++;
    }
    return n;
}

int json_int(Json *j) {
    if (j == NULL || j->type != 'n') {
        return 0;
    }
    return j->num >= (double)INT_MAX ? INT_MAX : j->num <= (double)INT_MIN ? INT_MIN : (int)j->num; /* a cast out of range is undefined */
}
