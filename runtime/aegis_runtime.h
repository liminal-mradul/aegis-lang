#pragma once
#include <stdint.h>

/* ── String ─────────────────────── */
typedef struct AegisStr   AegisStr;
AegisStr* aegis_str_new       (const char*);
void      aegis_str_retain    (AegisStr*);
void      aegis_str_release   (AegisStr*);
AegisStr* aegis_str_concat    (AegisStr*, AegisStr*);
AegisStr* aegis_str_concat_cstr(AegisStr*, const char*);
int64_t   aegis_str_len       (AegisStr*);
int64_t   aegis_str_eq        (AegisStr*, AegisStr*);
AegisStr* aegis_str_substr    (AegisStr*, int64_t, int64_t);
int64_t   aegis_str_index_of  (AegisStr*, AegisStr*);
AegisStr* aegis_str_upper     (AegisStr*);
AegisStr* aegis_str_lower     (AegisStr*);
AegisStr* aegis_str_trim      (AegisStr*);
int64_t   aegis_str_starts_with(AegisStr*, AegisStr*);
int64_t   aegis_str_ends_with (AegisStr*, AegisStr*);

/* ── Type conversion ─────────────── */
AegisStr* aegis_int_to_str    (int64_t);
AegisStr* aegis_float_to_str  (double);
AegisStr* aegis_bool_to_str   (int64_t);
int64_t   aegis_str_to_int    (AegisStr*);
double    aegis_str_to_float  (AegisStr*);
int64_t   aegis_float_to_int  (double);
double    aegis_int_to_float  (int64_t);

/* ── List ────────────────────────── */
typedef struct AegisList  AegisList;
AegisList* aegis_list_new    (void);
void       aegis_list_retain (AegisList*);
void       aegis_list_release(AegisList*);
void       aegis_list_push   (AegisList*, int64_t);
int64_t    aegis_list_pop    (AegisList*);
int64_t    aegis_list_get    (AegisList*, int64_t);
void       aegis_list_set    (AegisList*, int64_t, int64_t);
int64_t    aegis_list_len    (AegisList*);
AegisList* aegis_range       (int64_t start, int64_t end);
AegisList* aegis_range_step  (int64_t start, int64_t end, int64_t step);

/* ── Map ─────────────────────────── */
typedef struct AegisMap   AegisMap;
AegisMap* aegis_map_new    (void);
void      aegis_map_retain (AegisMap*);
void      aegis_map_release(AegisMap*);
void      aegis_map_set    (AegisMap*, const char*, int64_t);
int64_t   aegis_map_get    (AegisMap*, const char*);
int64_t   aegis_map_has    (AegisMap*, const char*);

/* ── Object ──────────────────────── */
typedef struct AegisObject AegisObject;
AegisObject* aegis_obj_new    (uint64_t class_id, uint64_t field_count);
void         aegis_obj_retain (AegisObject*);
void         aegis_obj_release(AegisObject*);
int64_t      aegis_obj_get    (AegisObject*, uint64_t idx);
void         aegis_obj_set    (AegisObject*, uint64_t idx, int64_t val);

/* ── Print ───────────────────────── */
void aegis_print_int  (int64_t);
void aegis_print_float(double);
void aegis_print_bool (int64_t);
void aegis_print_str  (AegisStr*);
void aegis_print_cstr (const char*);
void aegis_print      (int64_t val, int tag);

/* ── Channel ─────────────────────── */
typedef struct AegisChannel AegisChannel;
AegisChannel* aegis_channel_new (void);
void          aegis_channel_send(AegisChannel*, int64_t);
int64_t       aegis_channel_recv(AegisChannel*);

/* ── Math ────────────────────────── */
double  aegis_math_sqrt (double);
double  aegis_math_pow  (double, double);
double  aegis_math_floor(double);
double  aegis_math_ceil (double);
double  aegis_math_abs_f(double);
int64_t aegis_math_abs_i(int64_t);
double  aegis_math_sin  (double);
double  aegis_math_cos  (double);
double  aegis_math_log  (double);
double  aegis_math_log2 (double);
double  aegis_math_pi   (void);

/* ── I/O ─────────────────────────── */
AegisStr* aegis_io_readline(void);
void      aegis_io_write   (AegisStr*);
void      aegis_io_writeln (AegisStr*);

/* ── Memory / Safety ─────────────── */
void* aegis_own_alloc(uint64_t bytes);
void  aegis_own_free (void*);
void  aegis_assert   (int64_t cond, const char* msg);
void  aegis_panic    (const char* msg);

/* ── Crypto (stubs) ──────────────── */
AegisStr* aegis_crypto_sha256(AegisStr*);
/* ── Call depth guard ── */
void aegis_call_enter(void);
void aegis_call_leave(void);
