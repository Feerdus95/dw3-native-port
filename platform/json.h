/* A small JSON reader (json.c): numbers, strings, booleans, null, arrays and objects. The files it reads are the ones players write and share
   (the mods' manifests, the field editor's and the model viewer's output), so it never trusts them: an error is returned, never a crash.

   What it accepts is JSON, with these extensions: a comma before the closing ] or } (nothing writes it; a hand edit does), a number with a
   leading + or no digit before the point (the rest of a number is JSON's: no 1., 01 or 1e), and a UTF-8 byte order mark at the start. What it refuses: nesting deeper than the limit, a file
   bigger than the limit, a name twice in an object, a number out of range (or so small it would be 0), a string that isn't closed or holds a control character, an escape JSON doesn't have (\x, \a), \u0000 (a string is a C
   string), a lone surrogate, bytes that aren't UTF-8, nan / inf / hexadecimal, and anything after the value but spaces. Strings come out as
   UTF-8. Every allocation is checked. */
#ifndef DW3_JSON_H
#define DW3_JSON_H

typedef struct Json {
    int type; /* 'n' number, 's' string, 'b' true / false (num is 1 / 0), '[' array, '{' object, 0 null */
    double num;
    char *str;          /* a string */
    char *key;          /* an object member's key */
    struct Json *child; /* the first item of an array or object */
    struct Json *next;  /* the next item */
} Json;

/* What a consumer allows: the file's bytes and the depth of arrays and objects inside each other (a root array is depth 1). The depth can
   go down from JSON_MAX_DEPTH, the recursion's safe bound, never up; max_bytes 0 is no limit (json_parse's text is in memory already) */
typedef struct JsonLimits {
    long max_bytes;
    int max_depth;
} JsonLimits;
#define JSON_MAX_DEPTH 64
/* The consumers' limits, each over the biggest real file by a wide margin (a manifest is 3 KB, a stage 2 KB, but the stages' props are 2 MB) */
#define JSON_LIMITS_MANIFEST (&(const JsonLimits){64 << 10, 16}) /* data/mods/digimon/<name>/digimon.json */
#define JSON_LIMITS_STAGE (&(const JsonLimits){4 << 20, 32})     /* the stage mods' json files and characters.json */

/* Where and why a parse stopped; commas counts the trailing commas it let by (also on success) */
typedef struct JsonInfo {
    char msg[120];
    int line, col; /* from 1, of the error */
    int commas;
    int unopened; /* json_load: the file isn't there (or can't be opened), which a reader of optional files doesn't report */
} JsonInfo;

/* A whole document of LEN bytes (any NUL in it is an error): its root, or NULL with INFO's message. LIMITS and INFO may be NULL */
Json *json_parse(const char *text, long len, const JsonLimits *limits, JsonInfo *info);
/* A file, read whole with every failure checked (it can't be opened, is bigger than the limit, can't be read) */
Json *json_load(const char *path, const JsonLimits *limits, JsonInfo *info);
void json_free(Json *j);
Json *json_get(Json *obj, const char *key);
Json *json_at(Json *arr, int i);
int json_len(Json *arr);
/* A number as an int, held at INT_MIN / INT_MAX when it is out of range (a fraction is cut); 0 for what is not a number */
int json_int(Json *j);

#endif
