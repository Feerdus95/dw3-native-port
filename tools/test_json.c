/* The JSON reader (platform/json.c): what it takes, what it refuses and why. Run from native/ (with SDL3.dll beside the exe):
   i686-w64-mingw32-clang -Iplatform -I<SDL>/include tools/test_json.c platform/json.c -L<SDL>/lib -lSDL3 -o build/test_json.exe
   build/test_json.exe */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "json.h"

static int s_checks;
static char s_dir[512];

static Json *parse(const char *text, const JsonLimits *lim, JsonInfo *info) {
    return json_parse(text, (long)strlen(text), lim, info);
}

#define OK(text) do { JsonInfo i; Json *j = parse(text, NULL, &i); s_checks++; if (j == NULL) printf("should parse: %s (%s)\n", text, i.msg); assert(j != NULL); json_free(j); } while (0)
#define BAD(text, part) do { JsonInfo i; Json *j = parse(text, NULL, &i); s_checks++; if (j != NULL || strstr(i.msg, part) == NULL) printf("should fail with \"%s\": %s (%s)\n", part, text, j ? "parsed" : i.msg); assert(j == NULL && strstr(i.msg, part) != NULL); } while (0)

/* n arrays inside each other */
static char *nested(int n) {
    char *s = malloc((size_t)n * 2 + 1);

    memset(s, '[', (size_t)n), memset(s + n, ']', (size_t)n), s[2 * n] = 0;
    return s;
}

static void put(const char *name, const void *data, size_t size) {
    char p[800];

    SDL_snprintf(p, sizeof p, "%s/%s", s_dir, name);
    assert(SDL_SaveFile(p, data, size));
}

static const char *path(const char *name) {
    static char p[800];

    SDL_snprintf(p, sizeof p, "%s/%s", s_dir, name);
    return p;
}

int main(void) {
    JsonInfo info;
    Json *j;

    SDL_snprintf(s_dir, sizeof s_dir, "%sdw3_test_json", SDL_GetBasePath());
    SDL_RemovePath(s_dir);
    assert(SDL_CreateDirectory(s_dir));

    /* what is JSON */
    j = parse("{\"a\": [1, -2.5, 3e2, true, false, null, \"x\"], \"b\": {\"c\": \"d\"}}", NULL, &info);
    assert(j != NULL && j->type == '{' && json_len(json_get(j, "a")) == 7);
    assert(json_int(json_at(json_get(j, "a"), 1)) == -2 && json_at(json_get(j, "a"), 2)->num == 300);
    assert(json_at(json_get(j, "a"), 3)->type == 'b' && json_at(json_get(j, "a"), 3)->num == 1 && json_at(json_get(j, "a"), 4)->num == 0);
    assert(json_at(json_get(j, "a"), 5)->type == 0 && strcmp(json_at(json_get(j, "a"), 6)->str, "x") == 0);
    assert(strcmp(json_get(json_get(j, "b"), "c")->str, "d") == 0 && json_get(j, "nope") == NULL);
    json_free(j);
    OK(" [ ] "); OK("{}"); OK("\"a\""); OK("12"); OK("null"); OK("true"); OK("[1,[2,[3]]]"); OK("\t[\r\n1\n]\n");
    OK("[1e5, 1E-2, 0.5, -0, +1, .5]"); /* a leading + or point: kept as it was */

    /* strings: escapes, UTF-8, surrogates */
    j = parse("[\"a\\n\\t\\\"\\\\\\/\\b\\f\\r\", \"\\u00e9\", \"\\ud83d\\ude00\", \"\xC3\xA9\xE2\x82\xAC\", \"\\u0041\"]", NULL, &info);
    assert(j != NULL);
    assert(strcmp(json_at(j, 0)->str, "a\n\t\"\\/\b\f\r") == 0);
    assert(strcmp(json_at(j, 1)->str, "\xC3\xA9") == 0);
    assert(strcmp(json_at(j, 2)->str, "\xF0\x9F\x98\x80") == 0);
    assert(strcmp(json_at(j, 3)->str, "\xC3\xA9\xE2\x82\xAC") == 0);
    assert(strcmp(json_at(j, 4)->str, "A") == 0);
    json_free(j);
    s_checks += 6;

    /* trailing commas pass and are counted; a byte order mark is skipped */
    j = parse("{\"a\": [1, 2,], \"b\": 3,}", NULL, &info);
    assert(j != NULL && info.commas == 2);
    json_free(j);
    j = parse("\xEF\xBB\xBF{\"a\": 1}", NULL, &info);
    assert(j != NULL && json_int(json_get(j, "a")) == 1 && info.commas == 0);
    json_free(j);
    s_checks += 4;

    /* what isn't */
    BAD("", "ends"); BAD("   ", "ends"); BAD("[1, 2", "ends inside"); BAD("{\"a\": [1, 2", "ends inside"); BAD("{\"a\"", "':'"); BAD("{\"a\":", "ends");
    BAD("\"abc", "isn't closed"); BAD("[\"abc\\\"]", "isn't closed"); BAD("[\"a\nb\"]", "control character"); BAD("[\"a\tb\"]", "control character");
    BAD("[\"\\x41\"]", "escape JSON doesn't have (\\x)"); BAD("[\"\\a\"]", "escape JSON doesn't have (\\a)"); BAD("[\"\\u0000\"]", "\\u0000");
    BAD("[\"\\ud83d\"]", "high surrogate"); BAD("[\"\\ud83d x\"]", "high surrogate"); BAD("[\"\\ud83d\\u0041\"]", "high surrogate"); BAD("[\"\\ude00\"]", "low surrogate");
    BAD("[\"\\u12\"]", "\\u escape"); BAD("[\"\\u12zz\"]", "\\u escape");
    BAD("[\"\\uZ000\"]", "\\u escape"); BAD("[\"\\u0Z00\"]", "\\u escape"); BAD("[\"\\u00Z0\"]", "\\u escape"); BAD("[\"\\u000Z\"]", "\\u escape"); /* each position (once signed overflow) */
    BAD("[\"\\u\xFF\xFF\xFF\xFF\"]", "\\u escape"); BAD("[\"\\ud83d\\uZZZZ\"]", "high surrogate");
    BAD("[\"\xC0\xAF\"]", "UTF-8"); BAD("[\"\xE0\x80\x80\"]", "UTF-8"); BAD("[\"\xED\xA0\x80\"]", "UTF-8"); BAD("[\"\xF4\x90\x80\x80\"]", "UTF-8");
    BAD("[\"\xFF\"]", "UTF-8"); BAD("[\"\xC3\"]", "UTF-8"); BAD("[\"\xC3(\"]", "UTF-8");
    BAD("[nan]", "unknown word"); BAD("[NaN]", "unexpected"); BAD("[inf]", "unexpected"); BAD("[Infinity]", "unexpected"); BAD("[0x10]", "','"); BAD("[1e999]", "out of range");
    BAD("[-]", "malformed"); BAD("[1-2]", "malformed"); BAD("[1.2.3]", "malformed"); BAD("[nullx]", "unknown word"); BAD("[tru]", "unknown word"); BAD("[True]", "unexpected");
    BAD("{} x", "after the value"); BAD("{} {}", "after the value"); BAD("[1] 2", "after the value"); BAD("[1 2]", "','"); BAD("{\"a\" 1}", "':'"); BAD("{1: 2}", "name"); BAD("[,]", "unexpected"); BAD("[1,,2]", "unexpected");
    BAD("[01]", "malformed"); BAD("[-01]", "malformed"); BAD("[1.]", "malformed"); BAD("[1e]", "malformed"); BAD("[1e+]", "malformed"); BAD("[+-1]", "malformed");
    BAD("[+]", "malformed"); BAD("[.]", "malformed"); BAD("[-.5e]", "malformed"); BAD("[1e-999]", "out of range"); BAD("[1e999]", "out of range");
    BAD("{\"id\": 472, \"id\": 473}", "\"id\" is in the object twice"); BAD("[{\"a\": 1, \"b\": 2, \"a\": 3}]", "twice");
    OK("[0, -0, 0.5, 10, 1e0, 1E+2, 2.5e-3, -7.25]"); OK("{\"a\": {\"id\": 1}, \"b\": {\"id\": 2}}"); OK("[{\"id\": 1}, {\"id\": 2}]"); /* a name twice is the one object's */
    BAD("[1}", "','"); BAD("{\"a\": 1]", "','");
    {
        static const char nul[] = "[1,\0 2]";

        j = json_parse(nul, sizeof nul - 1, NULL, &info);
        assert(j == NULL);
        s_checks++;
    }

    /* where it stopped */
    j = parse("{\n  \"a\": ?}", NULL, &info);
    assert(j == NULL && info.line == 2 && info.col == 8);
    s_checks++;

    /* depth: the limit of the consumer, and the recursion's own */
    {
        JsonLimits lim = {0, 16};
        char *s;

        s = nested(16), j = parse(s, &lim, &info), assert(j != NULL), json_free(j), free(s);
        s = nested(17), j = parse(s, &lim, &info), assert(j == NULL && strstr(info.msg, "nested more than 16") != NULL), free(s);
        lim.max_depth = 1000; /* a consumer can't ask for more than the bound */
        s = nested(JSON_MAX_DEPTH), j = parse(s, &lim, &info), assert(j != NULL), json_free(j), free(s);
        s = nested(JSON_MAX_DEPTH + 1), j = parse(s, &lim, &info), assert(j == NULL), free(s);
        s = nested(200000), j = parse(s, NULL, &info), assert(j == NULL && strstr(info.msg, "nested more than 64") != NULL), free(s); /* no stack to spare */
        s_checks += 5;
    }

    /* bytes: the text's, and a file's */
    {
        JsonLimits lim = {10, 8};

        j = parse("[1,2,3,4,5,6]", &lim, &info);
        assert(j == NULL && strstr(info.msg, "limit is 10") != NULL);
        j = parse("[1,2,3,4]", &lim, &info);
        assert(j != NULL), json_free(j);
        put("big.json", "[1,2,3,4,5,6,7,8,9]", 19);
        j = json_load(path("big.json"), &lim, &info);
        assert(j == NULL && !info.unopened && strstr(info.msg, "19 bytes, the limit is 10") != NULL);
        j = json_load(path("big.json"), NULL, &info);
        assert(j != NULL && json_len(j) == 9), json_free(j);
        j = json_load(path("missing.json"), &lim, &info);
        assert(j == NULL && info.unopened);
        put("short.json", "{\"a\": [1, 2", 11);
        j = json_load(path("short.json"), NULL, &info);
        assert(j == NULL && !info.unopened && strstr(info.msg, "ends inside") != NULL);
        put("empty.json", "", 0);
        j = json_load(path("empty.json"), NULL, &info);
        assert(j == NULL && !info.unopened);
        s_checks += 8;
    }

    /* a file of the props' size (over 1 MiB, 10,000 entries): read under the limits made for it, refused under the manifest's */
    {
        size_t cap = 4 << 20, n = 0;
        char *s = malloc(cap);

        n += (size_t)snprintf(s + n, cap - n, "{\"props\": [\n");
        for (int i = 0; i < 10000; i++) {
            n += (size_t)snprintf(s + n, cap - n, "%s{\"model\": \"tree%04d.obj\", \"x\": %d.5, \"y\": -%d, \"z\": [1, 2, 3], \"facing\": 1}", i ? ",\n" : "", i, i, i * 3);
            n += (size_t)snprintf(s + n, cap - n, "                                                  ");
        }
        n += (size_t)snprintf(s + n, cap - n, "]}\n");
        assert(n > (1 << 20) + (100 << 10));
        put("props.json", s, n);
        j = json_load(path("props.json"), &(const JsonLimits){32 << 20, 16}, &info);
        assert(j != NULL && json_len(json_get(j, "props")) == 10000);
        json_free(j);
        j = json_load(path("props.json"), JSON_LIMITS_MANIFEST, &info);
        assert(j == NULL && strstr(info.msg, "the limit is 65536") != NULL);
        j = json_load(path("props.json"), JSON_LIMITS_STAGE, &info); /* the stage limits (4 MiB) take it as well */
        assert(j != NULL), json_free(j);
        free(s);
        s_checks += 3;
    }

    /* json_int: out of the int's range it holds at the end, never an undefined cast */
    j = parse("[2147483647, 2147483648, -2147483648, -2147483649, 1e300, -1e300, 5.9, -5.9, \"x\"]", NULL, &info);
    assert(j != NULL && json_int(json_at(j, 0)) == 2147483647 && json_int(json_at(j, 1)) == 2147483647 && json_int(json_at(j, 2)) == (-2147483647 - 1));
    assert(json_int(json_at(j, 3)) == (-2147483647 - 1) && json_int(json_at(j, 4)) == 2147483647 && json_int(json_at(j, 5)) == (-2147483647 - 1));
    assert(json_int(json_at(j, 6)) == 5 && json_int(json_at(j, 7)) == -5 && json_int(json_at(j, 8)) == 0 && json_int(NULL) == 0);
    json_free(j);
    s_checks += 8;

    SDL_RemovePath(s_dir);
    printf("ok, %d checks\n", s_checks);
    return 0;
}
