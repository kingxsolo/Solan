#include "../../include/solan.h"

_Static_assert(sizeof(Value) == sizeof(SolanValue), "SolanValue يجب أن يطابق حجم Value");
_Static_assert(_Alignof(Value) <= _Alignof(SolanValue), "محاذاة SolanValue");

#define TO_V(sv) (*(const Value *)&(sv))
static inline SolanValue to_sv(Value v) { SolanValue s; memcpy(&s, &v, sizeof s); return s; }

static char g_capi_err[1024];
static bool g_capi_ready = false;
static Map *g_capi_globals = NULL;

typedef struct { SolanNativeFn fn; void *user; } AbiFn;

static bool abi_trampoline(int argc, Value *a, Value *res) {
  AbiFn *af = (AbiFn *)g_cur_native->ud;
  SolanValue out = to_sv(NIL);
  vm.exc = NIL;
  int rc = af->fn(argc, (const SolanValue *)a, &out, af->user);
  if (rc != 0) { if (IS_NIL(vm.exc)) rt_error("فشلت الدالة الأصلية '%s'", g_cur_native->name->chars); return false; }
  *res = TO_V(out); return true;
}
static Native *abi_make_native(const char *name, SolanNativeFn fn, int mn, int mx, void *user) {
  AbiFn *af = xalloc(sizeof *af); af->fn = fn; af->user = user;
  Native *n = new_native(normstr(name), abi_trampoline, mn, mx); n->ud = af; return n;
}

SOLAN_API int solan_abi_version(void) { return SOLAN_ABI_VERSION; }

SOLAN_API int solan_init(void) {
  if (g_capi_ready) return 0;
  setvbuf(stdout, NULL, _IOLBF, 0); os_ignore_sigpipe();
  vm_init();
  g_capi_globals = new_map(); pin((Obj *)g_capi_globals);
  dset(&g_capi_globals->d, KN("هو_الرئيسي"), BOOLV(true));
  List *al = new_list(); g_args = OBJV(al); pin((Obj *)al);
  g_capi_ready = true; return 0;
}
SOLAN_API const char *solan_last_error(void) { return g_capi_err; }

static void capi_capture_error(void) {
  Str *s = tostr(vm.exc); snprintf(g_capi_err, sizeof g_capi_err, "%s", s->chars); reset_vm_state();
}
SOLAN_API int solan_eval(const char *source, const char *name) {
  if (solan_init()) return 1;
  g_capi_err[0] = 0;
  Fun *fn = compile(source, name ? name : "<solan_eval>", true);
  if (!fn) { snprintf(g_capi_err, sizeof g_capi_err, "%s", P.errmsg); return 65; }
  dset(&g_capi_globals->d, KN("اسم_الملف"), mkstr(name ? name : "<solan_eval>", (int)strlen(name ? name : "<solan_eval>")));
  if (!run_fun(fn, g_capi_globals)) { capi_capture_error(); return 70; }
  return 0;
}
SOLAN_API int solan_eval_file(const char *path) {
  char *src = read_file(path, NULL);
  if (!src) { snprintf(g_capi_err, sizeof g_capi_err, "تعذّر فتح الملف '%s'", path); return 66; }
  int rc = solan_eval(src, path); free(src); return rc;
}
SOLAN_API int solan_call(const char *global_name, int argc, const SolanValue *argv, SolanValue *result) {
  if (solan_init()) return 1;
  g_capi_err[0] = 0; Value callee;
  Str *nm = normstr(global_name);
  if (!dget(&g_capi_globals->d, OBJV(nm), &callee) && !dget(&vm.builtins, OBJV(nm), &callee)) {
    snprintf(g_capi_err, sizeof g_capi_err, "الدالة '%s' غير معرّفة", global_name); return 1;
  }
  vm.exc = NIL; push(callee); for (int i = 0; i < argc; i++) push(TO_V(argv[i]));
  if (!call_sync(argc)) { capi_capture_error(); return 70; }
  Value r = peek(0); vm.sp--;
  if (result) *result = to_sv(r);
  return 0;
}

SOLAN_API int solan_register_function(const char *name, SolanNativeFn fn, int mn, int mx, void *user) {
  if (solan_init()) return 1;
  Native *n = abi_make_native(name, fn, mn, mx, user);
  dset(&vm.builtins, OBJV(normstr(name)), OBJV(n)); return 0;
}
SOLAN_API int solan_module_add_function(SolanModule *m, const char *name, SolanNativeFn fn, int mn, int mx, void *user) {
  Native *n = abi_make_native(name, fn, mn, mx, user);
  dset(&((Map *)m)->d, OBJV(normstr(name)), OBJV(n)); return 0;
}
SOLAN_API int solan_module_add_value(SolanModule *m, const char *name, SolanValue v) {
  dset(&((Map *)m)->d, OBJV(normstr(name)), TO_V(v)); return 0;
}

static bool load_native_module(const char *file, Value *out) {
  void *h = dlopen(file, RTLD_NOW | RTLD_LOCAL);
  if (!h) { rt_error("تعذّر تحميل المكتبة الأصلية '%s': %s", file, dlerror()); return false; }
  int (*init)(SolanModule *) = NULL; *(void **)&init = dlsym(h, "solan_module_init");
  if (!init) { rt_error("المكتبة '%s' لا تصدّر solan_module_init", file); dlclose(h); return false; }
  Map *mod = new_map(); mod->module = true; pin((Obj *)mod);
  if (init((SolanModule *)mod) != 0) { if (IS_NIL(vm.exc)) rt_error("فشل تهيئة المكتبة الأصلية '%s'", file); return false; }
  *out = OBJV(mod); return true;
}

SOLAN_API SolanValue solan_nil(void) { return to_sv(NIL); }
SOLAN_API SolanValue solan_bool(int b) { return to_sv(BOOLV(b != 0)); }
SOLAN_API SolanValue solan_number(double n) { return to_sv(NUMV(n)); }
SOLAN_API SolanValue solan_string_n(const char *s, size_t n) { return to_sv(mkstr(s, (int)n)); }
SOLAN_API SolanValue solan_string(const char *s) { return solan_string_n(s, strlen(s)); }
SOLAN_API int solan_is_nil(SolanValue v) { return IS_NIL(TO_V(v)); }
SOLAN_API int solan_is_bool(SolanValue v) { return TO_V(v).t == V_BOOL; }
SOLAN_API int solan_is_number(SolanValue v) { return IS_NUM(TO_V(v)); }
SOLAN_API int solan_is_string(SolanValue v) { return IS_T(TO_V(v), O_STR); }
SOLAN_API int solan_is_list(SolanValue v) { return IS_T(TO_V(v), O_LIST); }
SOLAN_API int solan_is_map(SolanValue v) { return IS_T(TO_V(v), O_MAP); }
SOLAN_API int solan_to_bool(SolanValue v) { return !falsey(TO_V(v)); }
SOLAN_API double solan_to_number(SolanValue v) { return IS_NUM(TO_V(v)) ? TO_V(v).as.n : 0; }
SOLAN_API const char *solan_to_cstr(SolanValue v, size_t *len) {
  if (!IS_T(TO_V(v), O_STR)) { if (len) *len = 0; return NULL; }
  Str *s = AS_STR(TO_V(v)); if (len) *len = (size_t)s->len; return s->chars;
}

SOLAN_API SolanValue solan_list_new(void) { return to_sv(OBJV(new_list())); }
SOLAN_API size_t solan_list_len(SolanValue l) { return IS_T(TO_V(l), O_LIST) ? (size_t)AS_LIST(TO_V(l))->a.n : 0; }
SOLAN_API SolanValue solan_list_get(SolanValue l, size_t i) {
  if (!IS_T(TO_V(l), O_LIST) || i >= (size_t)AS_LIST(TO_V(l))->a.n) return to_sv(NIL);
  return to_sv(AS_LIST(TO_V(l))->a.v[i]);
}
SOLAN_API int solan_list_push(SolanValue l, SolanValue item) {
  if (!IS_T(TO_V(l), O_LIST)) return 1; varr_push(&AS_LIST(TO_V(l))->a, TO_V(item)); return 0;
}
SOLAN_API SolanValue solan_map_new(void) { return to_sv(OBJV(new_map())); }
SOLAN_API int solan_map_set(SolanValue m, const char *key, SolanValue v) {
  if (!IS_T(TO_V(m), O_MAP)) return 1; dset(&AS_MAP(TO_V(m))->d, OBJV(intern_c(key)), TO_V(v)); return 0;
}
SOLAN_API int solan_map_get(SolanValue m, const char *key, SolanValue *out) {
  Value v; if (!IS_T(TO_V(m), O_MAP) || !dget(&AS_MAP(TO_V(m))->d, OBJV(intern_c(key)), &v)) return 0;
  if (out) *out = to_sv(v); return 1;
}

SOLAN_API int solan_throw(const char *fmt, ...) {
  char msg[600]; va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
  rt_error("%s", msg); return 1;
}
SOLAN_API void solan_pin(SolanValue v) { if (TO_V(v).t == V_OBJ) pin(TO_V(v).as.o); }
SOLAN_API void solan_unpin(SolanValue v) {
  if (TO_V(v).t != V_OBJ) return; VArr *a = &vm.pins->a;
  for (int i = a->n - 1; i >= 0; i--) if (a->v[i].t == V_OBJ && a->v[i].as.o == TO_V(v).as.o) { memmove(&a->v[i], &a->v[i + 1], sizeof(Value) * (a->n - i - 1)); a->n--; return; }
}
