#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct { const char *p; int depth; int failed; } Parser;
static void ws(Parser *p) { while (isspace((unsigned char)*p->p)) ++p->p; }
static char *string_parse(Parser *p)
{
    const char *start;
    char *s;
    char *out;
    int c;
    unsigned int u;
    int i;
    if (*p->p != '"') { p->failed = 1; return NULL; }
    start = ++p->p;
    s = (char *)malloc(strlen(start) + 1);
    if (!s) { p->failed = 1; return NULL; }
    out = s;
    while (*p->p && *p->p != '"') {
        c = (unsigned char)*p->p++;
        if (c == '\\') {
            c = (unsigned char)*p->p++;
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c == 'b') c = '\b';
            else if (c == 'f') c = '\f';
            else if (c == 'u') {
                u = 0;
                for (i = 0; i < 4; ++i) {
                    c = (unsigned char)*p->p++;
                    if (!isxdigit(c)) { p->failed = 1; break; }
                    u = u * 16 + (unsigned int)(isdigit(c) ? c - '0' : tolower(c) - 'a' + 10);
                }
                if (u < 128) *out++ = (char)u;
                else if (u < 2048) {
                    *out++ = (char)(192 | (u >> 6)); *out++ = (char)(128 | (u & 63));
                } else {
                    *out++ = (char)(224 | (u >> 12));
                    *out++ = (char)(128 | ((u >> 6) & 63)); *out++ = (char)(128 | (u & 63));
                }
                continue;
            } else if (c != '\\' && c != '/' && c != '"') p->failed = 1;
        }
        if (!c) { p->failed = 1; break; }
        *out++ = (char)c;
    }
    if (*p->p == '"') ++p->p; else p->failed = 1;
    *out = 0;
    out = (char *)realloc(s, (size_t)(out - s) + 1);
    return out ? out : s;
}
static Json *parse(Parser *p)
{
    Json *j;
    Json *child;
    char *key;
    char *end;
    int object;
    char close;
    ws(p);
    if (++p->depth > 96) { p->failed = 1; --p->depth; return NULL; }
    j = (Json *)calloc(1, sizeof(Json));
    if (!j) { p->failed = 1; --p->depth; return NULL; }
    if (*p->p == '"') { j->type = J_STRING; j->text = string_parse(p); }
    else if (*p->p == '[' || *p->p == '{') {
        object = *p->p == '{'; close = object ? '}' : ']'; ++p->p;
        j->type = object ? J_OBJECT : J_ARRAY;
        ws(p);
        while (*p->p && *p->p != close && !p->failed) {
            key = NULL;
            if (object) { key = string_parse(p); ws(p); if (*p->p++ != ':') p->failed = 1; }
            child = parse(p);
            j->items = (Json **)realloc(j->items, (size_t)(j->count + 1) * sizeof(Json *));
            if (!j->items) { free(key); json_free(child); p->failed = 1; break; }
            if (object) {
                j->keys = (char **)realloc(j->keys, (size_t)(j->count + 1) * sizeof(char *));
                if (!j->keys) { free(key); json_free(child); p->failed = 1; break; }
                j->keys[j->count] = key;
            }
            j->items[j->count++] = child;
            ws(p);
            if (*p->p != ',') break;
            ++p->p; ws(p);
        }
        if (*p->p == close) ++p->p; else p->failed = 1;
    } else if (!strncmp(p->p, "true", 4)) { j->type = J_BOOL; j->number = 1; p->p += 4; }
    else if (!strncmp(p->p, "false", 5)) { j->type = J_BOOL; p->p += 5; }
    else if (!strncmp(p->p, "null", 4)) { p->p += 4; }
    else {
        j->type = J_NUMBER; j->number = strtod(p->p, &end);
        if (end == p->p) p->failed = 1;
        p->p = end;
    }
    --p->depth;
    return j;
}
Json *json_load(const char *path)
{
    FILE *f;
    long length;
    char *data;
    Parser p;
    Json *j;
    f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END); length = ftell(f); rewind(f);
    if (length < 0 || length > 67108864L) { fclose(f); return NULL; }
    data = (char *)malloc((size_t)length + 1);
    if (!data) { fclose(f); return NULL; }
    if (fread(data, 1, (size_t)length, f) != (size_t)length) { fclose(f); free(data); return NULL; }
    fclose(f); data[length] = 0;
    p.p = data; p.depth = 0; p.failed = 0;
    j = parse(&p); ws(&p);
    if (p.failed || *p.p) { fprintf(stderr, "Invalid JSON: %s\n", path); json_free(j); j = NULL; }
    free(data);
    return j;
}
void json_free(Json *j)
{
    int i;
    if (!j) return;
    for (i = 0; i < j->count; ++i) { json_free(j->items[i]); if (j->keys) free(j->keys[i]); }
    free(j->keys); free(j->items); free(j->text); free(j);
}
Json *jg(const Json *j, const char *key)
{
    int i;
    if (!j || j->type != J_OBJECT) return NULL;
    for (i = 0; i < j->count; ++i) if (!strcmp(j->keys[i], key)) return j->items[i];
    return NULL;
}
Json *ja(const Json *j, int i) { return j && i >= 0 && i < j->count ? j->items[i] : NULL; }
int ji(const Json *j, int fallback) { return j ? (int)j->number : fallback; }
double json_number(const Json *j, double fallback) { return j ? j->number : fallback; }
const char *js(const Json *j) { return j && j->text ? j->text : ""; }
int jc(const Json *j) { return j ? j->count : 0; }
void jvec(const Json *j, double *v, int n) { int i; for (i = 0; i < n; ++i) if (ja(j, i)) v[i] = json_number(ja(j, i), v[i]); }
