#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define AEGIS_FMT_INT  "%lld\n"
#define AEGIS_FMT_STR  "%s\n"
#define AEGIS_FMT_FLT  "%.6g\n"

typedef struct AegisStr { char* data; uint64_t len; uint64_t refcount; } AegisStr;

AegisStr* aegis_str_new(const char* s) {
    AegisStr* r = (AegisStr*)malloc(sizeof(AegisStr));
    size_t n = strlen(s);
    r->data = (char*)malloc(n+1); memcpy(r->data, s, n+1);
    r->len = n; r->refcount = 1; return r;
}
AegisStr* aegis_str_new_len(const char* s, size_t n) {
    AegisStr* r = (AegisStr*)malloc(sizeof(AegisStr));
    r->data = (char*)malloc(n+1); memcpy(r->data, s, n); r->data[n]='\0';
    r->len = n; r->refcount = 1; return r;
}
void aegis_str_retain (AegisStr* s) { if (s) s->refcount++; }
void aegis_str_release(AegisStr* s) { if (s && --s->refcount == 0) { free(s->data); free(s); } }

AegisStr* aegis_str_concat(AegisStr* a, AegisStr* b) {
    if (!a) return b;
    if (!b) return a;
    size_t n = a->len + b->len;
    AegisStr* r = (AegisStr*)malloc(sizeof(AegisStr));
    r->data = (char*)malloc(n+1);
    memcpy(r->data, a->data, a->len);
    memcpy(r->data + a->len, b->data, b->len);
    r->data[n] = '\0'; r->len = n; r->refcount = 1; return r;
}
AegisStr* aegis_str_concat_cstr(AegisStr* a, const char* b) {
    AegisStr* t = aegis_str_new(b);
    AegisStr* r = aegis_str_concat(a, t);
    aegis_str_release(t); return r;
}
int64_t aegis_str_len(AegisStr* s) { return s ? (int64_t)s->len : 0; }

int64_t aegis_str_eq(AegisStr* a, AegisStr* b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    return (a->len == b->len) && (memcmp(a->data, b->data, a->len) == 0);
}

AegisStr* aegis_int_to_str(int64_t n) {
    char buf[32]; snprintf(buf, sizeof(buf), "%lld", (long long)n);
    return aegis_str_new(buf);
}
AegisStr* aegis_float_to_str(double f) {
    char buf[64]; snprintf(buf, sizeof(buf), "%.6g", f);
    return aegis_str_new(buf);
}
AegisStr* aegis_bool_to_str(int64_t b) { return aegis_str_new(b ? "true" : "false"); }
int64_t   aegis_str_to_int  (AegisStr* s) { return s ? (int64_t)atoll(s->data) : 0; }
double    aegis_str_to_float(AegisStr* s) { return s ? atof(s->data) : 0.0; }
int64_t   aegis_float_to_int(double f)    { return (int64_t)f; }
double    aegis_int_to_float(int64_t i)   { return (double)i; }

AegisStr* aegis_str_substr(AegisStr* s, int64_t a, int64_t b) {
    if (!s) return aegis_str_new("");
    if (a < 0) a = 0;
    if (b > (int64_t)s->len) b = (int64_t)s->len;
    if (a >= b) return aegis_str_new("");
    return aegis_str_new_len(s->data + a, (size_t)(b - a));
}
int64_t aegis_str_index_of(AegisStr* h, AegisStr* n) {
    if (!h || !n) return -1;
    char* p = strstr(h->data, n->data);
    return p ? (int64_t)(p - h->data) : -1;
}
AegisStr* aegis_str_upper(AegisStr* s) {
    if (!s) return aegis_str_new("");
    AegisStr* r = aegis_str_new(s->data);
    for (uint64_t i = 0; i < r->len; i++)
        if (r->data[i] >= 'a' && r->data[i] <= 'z') r->data[i] -= 32;
    return r;
}
AegisStr* aegis_str_lower(AegisStr* s) {
    if (!s) return aegis_str_new("");
    AegisStr* r = aegis_str_new(s->data);
    for (uint64_t i = 0; i < r->len; i++)
        if (r->data[i] >= 'A' && r->data[i] <= 'Z') r->data[i] += 32;
    return r;
}
AegisStr* aegis_str_trim(AegisStr* s) {
    if (!s) return aegis_str_new("");
    const char* p = s->data;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    const char* e = s->data + s->len - 1;
    while (e > p && (*e == ' ' || *e == '\t' || *e == '\n' || *e == '\r')) e--;
    return aegis_str_new_len(p, (size_t)(e - p + 1));
}
int64_t aegis_str_starts_with(AegisStr* s, AegisStr* p) {
    if (!s || !p) return 0;
    return strncmp(s->data, p->data, p->len) == 0;
}
int64_t aegis_str_ends_with(AegisStr* s, AegisStr* p) {
    if (!s || !p || p->len > s->len) return 0;
    return strcmp(s->data + s->len - p->len, p->data) == 0;
}

typedef struct AegisList { int64_t* data; uint64_t len, cap, refcount; } AegisList;
AegisList* aegis_list_new(void) {
    AegisList* l = (AegisList*)malloc(sizeof(AegisList));
    l->cap = 8; l->len = 0;
    l->data = (int64_t*)malloc(8 * sizeof(int64_t)); l->refcount = 1; return l;
}
void    aegis_list_retain (AegisList* l) { if (l) l->refcount++; }
void    aegis_list_release(AegisList* l) { if (l && --l->refcount == 0) { free(l->data); free(l); } }
void    aegis_list_push(AegisList* l, int64_t v) {
    if (l->len == l->cap) {
        l->cap *= 2;
        l->data = (int64_t*)realloc(l->data, l->cap * sizeof(int64_t));
    }
    l->data[l->len++] = v;
}
int64_t aegis_list_pop(AegisList* l) { return (!l || l->len == 0) ? 0 : l->data[--l->len]; }
int64_t aegis_list_get(AegisList* l, int64_t i) {
    if (!l) return 0;
    if (i < 0) i = (int64_t)l->len + i;
    if (i < 0 || (uint64_t)i >= l->len) { fprintf(stderr, "[Aegis] index out of bounds\n"); exit(1); }
    return l->data[i];
}
void aegis_list_set(AegisList* l, int64_t i, int64_t v) {
    if (!l) return;
    if (i < 0) i = (int64_t)l->len + i;
    if (i < 0 || (uint64_t)i >= l->len) { fprintf(stderr, "[Aegis] index out of bounds\n"); exit(1); }
    l->data[i] = v;
}
int64_t    aegis_list_len(AegisList* l) { return l ? (int64_t)l->len : 0; }
AegisList* aegis_range(int64_t a, int64_t b) {
    AegisList* l = aegis_list_new();
    for (int64_t i = a; i < b; i++) aegis_list_push(l, i);
    return l;
}
AegisList* aegis_range_step(int64_t a, int64_t b, int64_t s) {
    AegisList* l = aegis_list_new();
    if (s > 0) { for (int64_t i = a; i < b; i += s) aegis_list_push(l, i); }
    else        { for (int64_t i = a; i > b; i += s) aegis_list_push(l, i); }
    return l;
}

#define MAP_BUCKETS 64
typedef struct MapEntry { char* key; int64_t val; struct MapEntry* next; } MapEntry;
typedef struct AegisMap { MapEntry* buckets[MAP_BUCKETS]; uint64_t refcount; } AegisMap;
static unsigned map_hash(const char* k) {
    unsigned h = 5381; while (*k) h = h*33 ^ (unsigned char)*k++; return h % MAP_BUCKETS;
}
AegisMap* aegis_map_new(void) {
    AegisMap* m = (AegisMap*)calloc(1, sizeof(AegisMap)); m->refcount = 1; return m;
}
void aegis_map_set(AegisMap* m, const char* k, int64_t v) {
    unsigned h = map_hash(k);
    for (MapEntry* e = m->buckets[h]; e; e = e->next)
        if (strcmp(e->key, k) == 0) { e->val = v; return; }
    MapEntry* e = (MapEntry*)malloc(sizeof(MapEntry));
    e->key = strdup(k); e->val = v; e->next = m->buckets[h]; m->buckets[h] = e;
}
int64_t aegis_map_get(AegisMap* m, const char* k) {
    unsigned h = map_hash(k);
    for (MapEntry* e = m->buckets[h]; e; e = e->next)
        if (strcmp(e->key, k) == 0) return e->val;
    return 0;
}
int64_t aegis_map_has(AegisMap* m, const char* k) {
    unsigned h = map_hash(k);
    for (MapEntry* e = m->buckets[h]; e; e = e->next)
        if (strcmp(e->key, k) == 0) return 1;
    return 0;
}

typedef struct AegisObject { uint64_t class_id, field_count, refcount; int64_t fields[]; } AegisObject;
AegisObject* aegis_obj_new(uint64_t cid, uint64_t fc) {
    AegisObject* o = (AegisObject*)calloc(1, sizeof(AegisObject) + fc * sizeof(int64_t));
    o->class_id = cid; o->field_count = fc; o->refcount = 1; return o;
}
void    aegis_obj_retain (AegisObject* o) { if (o) o->refcount++; }
void    aegis_obj_release(AegisObject* o) { if (o && --o->refcount == 0) free(o); }
int64_t aegis_obj_get(AegisObject* o, uint64_t i) { return (!o || i >= o->field_count) ? 0 : o->fields[i]; }
void    aegis_obj_set(AegisObject* o, uint64_t i, int64_t v) { if (o && i < o->field_count) o->fields[i] = v; }

void aegis_print_int  (int64_t n)   { printf(AEGIS_FMT_INT, (long long)n); }
void aegis_print_float(double f)    { printf(AEGIS_FMT_FLT, f); }
void aegis_print_bool (int64_t b)   { puts(b ? "true" : "false"); }
void aegis_print_str  (AegisStr* s) { if (s) printf(AEGIS_FMT_STR, s->data); else puts("null"); }
void aegis_print_cstr (const char* s) { printf(AEGIS_FMT_STR, s ? s : "null"); }
void aegis_print(int64_t v, int tag) {
    switch (tag) {
        case 0: puts("null"); break;
        case 1: printf("%lld\n", (long long)v); break;
        case 2: { double f; memcpy(&f, &v, 8); printf("%.6g\n", f); } break;
        case 3: puts(v ? "true" : "false"); break;
        case 4: aegis_print_str((AegisStr*)(uintptr_t)v); break;
        default: printf("<obj:%lld>\n", (long long)v);
    }
}

typedef struct AegisChannel { int64_t* buf; uint64_t head, tail, cap, refcount; } AegisChannel;
AegisChannel* aegis_channel_new(void) {
    AegisChannel* c = (AegisChannel*)calloc(1, sizeof(AegisChannel));
    c->cap = 16; c->buf = (int64_t*)malloc(16 * sizeof(int64_t)); c->refcount = 1; return c;
}
void aegis_channel_send(AegisChannel* c, int64_t v) {
    if (!c) return;
    if (c->tail - c->head == c->cap) {
        uint64_t n = c->tail - c->head; c->cap *= 2;
        int64_t* nb = (int64_t*)malloc(c->cap * sizeof(int64_t));
        for (uint64_t i = 0; i < n; i++) nb[i] = c->buf[(c->head + i) % (c->cap / 2)];
        free(c->buf); c->buf = nb; c->head = 0; c->tail = n;
    }
    c->buf[c->tail++ % c->cap] = v;
}
int64_t aegis_channel_recv(AegisChannel* c) {
    return (!c || c->head == c->tail) ? 0 : c->buf[c->head++ % c->cap];
}

double  aegis_math_sqrt (double x)           { return sqrt(x); }
double  aegis_math_pow  (double b, double e) { return pow(b, e); }
double  aegis_math_floor(double x)           { return floor(x); }
double  aegis_math_ceil (double x)           { return ceil(x); }
double  aegis_math_abs_f(double x)           { return fabs(x); }
int64_t aegis_math_abs_i(int64_t x)          { return x < 0 ? -x : x; }
double  aegis_math_sin  (double x)           { return sin(x); }
double  aegis_math_cos  (double x)           { return cos(x); }
double  aegis_math_log  (double x)           { return log(x); }
double  aegis_math_log2 (double x)           { return log2(x); }
double  aegis_math_pi   (void)               { return 3.14159265358979323846; }

void* aegis_own_alloc(uint64_t n) { return malloc(n); }
void  aegis_own_free (void* p)    { free(p); }

void aegis_assert(int64_t cond, const char* msg) {
    if (!cond) { fprintf(stderr, "[Aegis] Assert failed: %s\n", msg ? msg : ""); exit(1); }
}
void aegis_panic(const char* msg) {
    fprintf(stderr, "[Aegis] Panic: %s\n", msg ? msg : ""); exit(1);
}

AegisStr* aegis_io_readline(void) {
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return aegis_str_new("");
    size_t n = strlen(buf);
    if (n > 0 && buf[n-1] == '\n') buf[--n] = '\0';
    return aegis_str_new_len(buf, n);
}
void aegis_io_write  (AegisStr* s) { if (s) fputs(s->data, stdout); }
void aegis_io_writeln(AegisStr* s) { if (s) puts(s->data); else puts(""); }

AegisStr* aegis_crypto_sha256(AegisStr* s) { (void)s; return aegis_str_new("<sha256:stub>"); }

/* ── Call depth guard ── */
static int64_t aegis_call_depth_ = 0;
#define AEGIS_MAX_CALL_DEPTH 500

void aegis_call_enter(void) {
    if (++aegis_call_depth_ > AEGIS_MAX_CALL_DEPTH) {
        fprintf(stderr, "[Aegis] Stack overflow: maximum call depth (%d) exceeded\n",
                AEGIS_MAX_CALL_DEPTH);
        exit(1);
    }
}
void aegis_call_leave(void) {
    --aegis_call_depth_;
}
