#ifndef PETSCOP_JSON_H
#define PETSCOP_JSON_H
#include <stddef.h>
typedef struct Json Json;
struct Json {
    int type;
    int count;
    double number;
    char *text;
    char **keys;
    Json **items;
};
enum { J_NULL, J_NUMBER, J_STRING, J_ARRAY, J_OBJECT, J_BOOL };
Json *json_load(const char *path);
void json_free(Json *j);
Json *jg(const Json *j, const char *key);
Json *ja(const Json *j, int i);
int ji(const Json *j, int fallback);
double json_number(const Json *j, double fallback);
const char *js(const Json *j);
int jc(const Json *j);
void jvec(const Json *j, double *v, int n);
#endif
