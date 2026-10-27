#ifndef SOLAN_H
#define SOLAN_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SOLAN_ABI_VERSION 1
#define SOLAN_API __attribute__((visibility("default")))

typedef struct { uint64_t _opaque[2]; } SolanValue;
typedef struct SolanModule SolanModule;

typedef int (*SolanNativeFn)(int argc, const SolanValue *argv, SolanValue *result, void *user);

SOLAN_API int         solan_abi_version(void);
SOLAN_API int         solan_init(void);
SOLAN_API int         solan_eval(const char *source, const char *name);
SOLAN_API int         solan_eval_file(const char *path);
SOLAN_API const char *solan_last_error(void);
SOLAN_API int         solan_call(const char *global_name, int argc, const SolanValue *argv, SolanValue *result);

SOLAN_API int solan_register_function(const char *name, SolanNativeFn fn, int min_args, int max_args, void *user);

SOLAN_API int solan_module_add_function(SolanModule *m, const char *name, SolanNativeFn fn, int min_args, int max_args, void *user);
SOLAN_API int solan_module_add_value(SolanModule *m, const char *name, SolanValue v);

SOLAN_API SolanValue solan_nil(void);
SOLAN_API SolanValue solan_bool(int b);
SOLAN_API SolanValue solan_number(double n);
SOLAN_API SolanValue solan_string(const char *utf8);
SOLAN_API SolanValue solan_string_n(const char *utf8, size_t len);
SOLAN_API int        solan_is_nil(SolanValue v);
SOLAN_API int        solan_is_bool(SolanValue v);
SOLAN_API int        solan_is_number(SolanValue v);
SOLAN_API int        solan_is_string(SolanValue v);
SOLAN_API int        solan_is_list(SolanValue v);
SOLAN_API int        solan_is_map(SolanValue v);
SOLAN_API int        solan_to_bool(SolanValue v);
SOLAN_API double     solan_to_number(SolanValue v);
SOLAN_API const char *solan_to_cstr(SolanValue v, size_t *len);

SOLAN_API SolanValue solan_list_new(void);
SOLAN_API size_t     solan_list_len(SolanValue list);
SOLAN_API SolanValue solan_list_get(SolanValue list, size_t index);
SOLAN_API int        solan_list_push(SolanValue list, SolanValue item);
SOLAN_API SolanValue solan_map_new(void);
SOLAN_API int        solan_map_set(SolanValue map, const char *key, SolanValue v);
SOLAN_API int        solan_map_get(SolanValue map, const char *key, SolanValue *out);

SOLAN_API int  solan_throw(const char *fmt, ...);
SOLAN_API void solan_pin(SolanValue v);
SOLAN_API void solan_unpin(SolanValue v);

#ifdef __cplusplus
}
#endif
#endif
