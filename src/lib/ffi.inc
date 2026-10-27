#if defined(__x86_64__) || defined(__aarch64__)
#define SOLAN_FFI 1
typedef struct { void *fn; char ret; int n; char args[16]; } FfiFn;
typedef int64_t (*FfiI)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, double, double, double, double, double, double, double, double);
typedef double (*FfiD)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, double, double, double, double, double, double, double, double);

NAT(ffi_open) {
  PERM(PM_NATIVE);
  const char *path = (argc > 0 && IS_T(a[0], O_STR)) ? AS_STR(a[0])->chars : NULL;
  void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
  if (!h) { const char *e = dlerror(); FAIL("ffi: تعذّر فتح المكتبة: %s", e ? e : "؟"); }
  *res = NUMV((double)(uintptr_t)h); return true;
}
static bool ffi_call(int argc, Value *a, Value *res) {
  FfiFn *f = (FfiFn *)g_cur_native->ud;
  if (argc != f->n) FAIL("ffi: الدالة تتوقع %d وسائط لكن أُعطيت %d", f->n, argc);
  int64_t iv[6] = {0}; double dv[8] = {0}; int ni = 0, nd = 0;
  for (int k = 0; k < argc; k++) {
    char t = f->args[k];
    if (t == 'd') { NUMA(k); if (nd >= 8) FAIL("ffi: أكثر من 8 وسائط عشرية"); dv[nd++] = a[k].as.n; continue; }
    if (ni >= 6) FAIL("ffi: أكثر من 6 وسائط صحيحة");
    if (t == 's') {
      if (IS_NIL(a[k])) iv[ni++] = 0; else { STRA(k); iv[ni++] = (int64_t)(uintptr_t)AS_STR(a[k])->chars; }
    } else if (a[k].t == V_BOOL) iv[ni++] = a[k].as.b ? 1 : 0;
    else if (IS_NIL(a[k])) iv[ni++] = 0;
    else { NUMA(k); iv[ni++] = (int64_t)a[k].as.n; }
  }
  if (f->ret == 'd') {
    double r = ((FfiD)f->fn)(iv[0], iv[1], iv[2], iv[3], iv[4], iv[5], dv[0], dv[1], dv[2], dv[3], dv[4], dv[5], dv[6], dv[7]);
    *res = NUMV(r); return true;
  }
  int64_t r = ((FfiI)f->fn)(iv[0], iv[1], iv[2], iv[3], iv[4], iv[5], dv[0], dv[1], dv[2], dv[3], dv[4], dv[5], dv[6], dv[7]);
  switch (f->ret) {
    case 'v': *res = NIL; break;
    case 'i': *res = NUMV((double)(int32_t)r); break;
    case 'b': *res = BOOLV((r & 0xff) != 0); break;
    case 's': { const char *p = (const char *)(uintptr_t)r; *res = p ? mkstr(p, (int)strlen(p)) : NIL; break; }
    default: *res = NUMV((double)r); break;
  }
  return true;
}

NAT(ffi_bind) {
  PERM(PM_NATIVE); STRA(1); STRA(2);
  void *h = IS_NUM(a[0]) ? (void *)(uintptr_t)a[0].as.n : dlopen(NULL, RTLD_NOW);
  void *sym = dlsym(h, AS_STR(a[1])->chars);
  if (!sym) FAIL("ffi: الرمز '%s' غير موجود", AS_STR(a[1])->chars);
  const char *sg = AS_STR(a[2])->chars; FfiFn *f = xalloc(sizeof(FfiFn)); memset(f, 0, sizeof *f); f->fn = sym;
  if (!sg[0] || sg[1] != '(' || !strchr("ilpdsbv", sg[0])) { free(f); FAIL("ffi: توقيع غير صالح (مثال: \"d(dd)\")"); }
  f->ret = sg[0]; const char *p = sg + 2;
  while (*p && *p != ')') { if (!strchr("ilpdsb", *p) || f->n >= 14) { free(f); FAIL("ffi: نوع وسيط غير صالح '%c'", *p); } f->args[f->n++] = *p++; }
  if (*p != ')') { free(f); FAIL("ffi: توقيع غير مكتمل"); }
  Native *n = new_native(AS_STR(a[1]), ffi_call, 0, -1); n->ud = f; *res = OBJV(n); return true;
}
NAT(ffi_alloc) { PERM(PM_NATIVE); NUMA(0); if (a[0].as.n < 0 || a[0].as.n > 1e9) FAIL("ffi: حجم غير صالح"); void *p = calloc(1, (size_t)a[0].as.n + 1); if (!p) FAIL("ffi: نفدت الذاكرة"); *res = NUMV((double)(uintptr_t)p); return true; }
NAT(ffi_free) { PERM(PM_NATIVE); NUMA(0); free((void *)(uintptr_t)a[0].as.n); *res = NIL; return true; }
NAT(ffi_read_str) {
  PERM(PM_NATIVE); NUMA(0); const char *p = (const char *)(uintptr_t)a[0].as.n; if (!p) { *res = NIL; return true; }
  int n = (argc > 1 && IS_NUM(a[1])) ? (int)a[1].as.n : (int)strlen(p); *res = mkstr(p, n); return true;
}
NAT(ffi_write_str) { PERM(PM_NATIVE); NUMA(0); STRA(1); memcpy((void *)(uintptr_t)a[0].as.n, AS_STR(a[1])->chars, AS_STR(a[1])->len + 1); *res = NIL; return true; }
#else
#define SOLAN_FFI 0
#endif
