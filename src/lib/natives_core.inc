#define NAT(name) static bool name(int argc, Value *a, Value *res)
#define NUMA(i) do { if (!IS_NUM(a[i])) FAIL("المعامل %d يجب أن يكون رقما", (i) + 1); } while (0)
#define SAFE() PERM(PM_EXEC)
#define PERM(bit) do { if (!perm_has(bit)) FAIL("الصلاحية غير ممنوحة: %s (شغّل بـ --اسمح=%s)", perm_name(bit), perm_name(bit)); } while (0)
#define PERMP(bit, path) do { if (!path_allowed(bit, path)) FAIL("الصلاحية غير ممنوحة لهذا المسار: %s (%s)", perm_name(bit), path); } while (0)
#define STRA(i) do { if (!IS_T(a[i], O_STR)) FAIL("المعامل %d يجب أن يكون نصا", (i) + 1); } while (0)

static const char *perm_name(int bit) {
  switch (bit) { case PM_READ: return "قراءة"; case PM_WRITE: return "كتابة"; case PM_NET: return "شبكة"; case PM_EXEC: return "تنفيذ"; case PM_ENV: return "بيئة"; default: return "اصلي"; }
}
NAT(n_print) {
  for (int i = 0; i < argc; i++) { if (i) fputc(' ', stdout); Str *s = tostr(a[i]); fwrite(s->chars, 1, s->len, stdout); }
  fputc('\n', stdout); *res = NIL; return true;
}
NAT(n_write) {
  for (int i = 0; i < argc; i++) { if (i) fputc(' ', stdout); Str *s = tostr(a[i]); fwrite(s->chars, 1, s->len, stdout); }
  fflush(stdout); *res = NIL; return true;
}
NAT(n_input) {
  if (argc > 0) { Str *s = tostr(a[0]); fwrite(s->chars, 1, s->len, stdout); fflush(stdout); }
  char *line = NULL; size_t cap = 0; ssize_t n = getline(&line, &cap, stdin);
  if (n < 0) { free(line); *res = NIL; return true; }
  while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
  *res = mkstr(line, (int)n); free(line); return true;
}
NAT(n_len) {
  Value v = a[0];
  if (IS_T(v, O_STR)) *res = NUMV(u8_len(AS_STR(v)->chars, AS_STR(v)->len));
  else if (IS_T(v, O_LIST)) *res = NUMV(AS_LIST(v)->a.n);
  else if (IS_T(v, O_MAP)) *res = NUMV(AS_MAP(v)->d.live);
  else FAIL("طول: نوع غير مدعوم '%s'", type_name(v));
  return true;
}
NAT(n_type) { *res = OBJV(intern_c(type_name(a[0]))); return true; }
NAT(n_str) { *res = OBJV(tostr(a[0])); return true; }
NAT(n_num) {
  Value v = a[0];
  if (IS_NUM(v)) { *res = v; return true; }
  if (v.t == V_BOOL) { *res = NUMV(v.as.b ? 1 : 0); return true; }
  if (!IS_T(v, O_STR)) { *res = NIL; return true; }
  Str *s = AS_STR(v); char *t = xalloc(s->len + 1); int o = 0;
  for (int i = 0; i < s->len;) {
    uint32_t cp; int l = u8_decode((const unsigned char *)s->chars + i, &cp); i += l;
    if (cp == 0x66B) t[o++] = '.'; else if (cp == 0x60C || cp == 0x66C) continue; else if (cp < 0x80 || is_digit_cp(cp)) t[o++] = (char)norm_cp(cp); else { t[o++] = '?'; }
  }
  t[o] = 0; char *end; double d = strtod(t, &end); while (*end == ' ' || *end == '\n' || *end == '\t' || *end == '\r') end++;
  bool ok = o > 0 && *end == 0 && end != t; free(t);
  *res = ok ? NUMV(d) : NIL; return true;
}
NAT(n_range) {
  double s = 0, e = 0, st = 1;
  for (int i = 0; i < argc; i++) NUMA(i);
  if (argc == 1) e = a[0].as.n; else { s = a[0].as.n; e = a[1].as.n; if (argc > 2) st = a[2].as.n; }
  if (st == 0) FAIL("مدى: الخطوة لا يمكن أن تكون صفرا");
  if (fabs((e - s) / st) > 1e7) FAIL("مدى: النطاق كبير جدا");
  List *l = new_list();
  if (st > 0) for (double x = s; x < e; x += st) varr_push(&l->a, NUMV(x)); else for (double x = s; x > e; x += st) varr_push(&l->a, NUMV(x));
  *res = OBJV(l); return true;
}
NAT(n_range_count) {
  for (int i = 0; i < 3; i++) NUMA(i);
  double s = a[0].as.n, e = a[1].as.n, st = a[2].as.n;
  if (st == 0) FAIL("مدى: الخطوة لا يمكن أن تكون صفرا");
  double n = ceil((e - s) / st); if (!(n > 0)) n = 0;
  *res = NUMV(n); return true;
}
NAT(n_assert) {
  if (falsey(a[0])) { if (argc > 1) { vm.exc = OBJV(tostr(a[1])); return false; } FAIL("فشل التأكيد"); }
  *res = NIL; return true;
}
NAT(n_chr) { NUMA(0); char t[4]; *res = mkstr(t, u8_encode((uint32_t)a[0].as.n, t)); return true; }
NAT(n_ord) { STRA(0); if (AS_STR(a[0])->len == 0) { *res = NIL; return true; } uint32_t cp; u8_decode((const unsigned char *)AS_STR(a[0])->chars, &cp); *res = NUMV(cp); return true; }
NAT(n_isa) {
  *res = BOOLV(false); if (!IS_T(a[0], O_INST) || !IS_T(a[1], O_CLS)) return true;
  for (Class *c = AS_INST(a[0])->cls; c; c = c->super) if (c == AS_CLS(a[1])) { *res = BOOLV(true); break; }
  return true;
}
NAT(n_is_str) { *res = BOOLV(IS_T(a[0], O_STR)); return true; }
NAT(n_is_num) { *res = BOOLV(IS_NUM(a[0])); return true; }
NAT(n_is_list) { *res = BOOLV(IS_T(a[0], O_LIST)); return true; }
NAT(n_is_map) { *res = BOOLV(IS_T(a[0], O_MAP)); return true; }
NAT(n_is_nil) { *res = BOOLV(IS_NIL(a[0])); return true; }
NAT(n_is_fn) { *res = BOOLV(IS_T(a[0], O_CLO) || IS_T(a[0], O_NAT) || IS_T(a[0], O_BOUND)); return true; }
NAT(n_has) {
  Value t; *res = BOOLV(false);
  if (IS_T(a[0], O_INST)) { STRA(1); *res = BOOLV(dget(&AS_INST(a[0])->fields, a[1], &t) || dget(&AS_INST(a[0])->cls->methods, a[1], &t)); }
  return true;
}

#define MATH1(nm, fn) NAT(nm) { NUMA(0); *res = NUMV(fn(a[0].as.n)); return true; }
MATH1(m_sqrt, sqrt) MATH1(m_sin, sin) MATH1(m_cos, cos) MATH1(m_tan, tan) MATH1(m_floor, floor) MATH1(m_ceil, ceil)
MATH1(m_abs, fabs) MATH1(m_round, round) MATH1(m_exp, exp) MATH1(m_asin, asin) MATH1(m_acos, acos) MATH1(m_atan, atan) MATH1(m_trunc, trunc)
NAT(m_pow) { NUMA(0); NUMA(1); *res = NUMV(sol_pow(a[0].as.n, a[1].as.n)); return true; }
NAT(m_log) { NUMA(0); if (argc > 1) { NUMA(1); *res = NUMV(log(a[0].as.n) / log(a[1].as.n)); } else *res = NUMV(log(a[0].as.n)); return true; }
NAT(m_log10) { NUMA(0); *res = NUMV(log10(a[0].as.n)); return true; }
static bool minmax(int argc, Value *a, Value *res, bool mx) {
  Value *v = a; int n = argc;
  if (argc == 1 && IS_T(a[0], O_LIST)) { v = AS_LIST(a[0])->a.v; n = AS_LIST(a[0])->a.n; }
  if (n == 0) { *res = NIL; return true; }
  double best = 0;
  for (int i = 0; i < n; i++) { if (!IS_NUM(v[i])) FAIL("اكبر/اصغر: يجب أن تكون القيم أرقاما"); if (i == 0 || (mx ? v[i].as.n > best : v[i].as.n < best)) best = v[i].as.n; }
  *res = NUMV(best); return true;
}
NAT(m_max) { return minmax(argc, a, res, true); }
NAT(m_min) { return minmax(argc, a, res, false); }
NAT(m_atan2) { NUMA(0); NUMA(1); *res = NUMV(atan2(a[0].as.n, a[1].as.n)); return true; }

static uint64_t rng_state = 88172645463325252ULL;
static uint64_t rng_next(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17; return rng_state; }
static double rng_f(void) { return (rng_next() >> 11) * (1.0 / 9007199254740992.0); }
NAT(r_num) { *res = NUMV(rng_f()); return true; }
NAT(r_int) { NUMA(0); NUMA(1); double lo = a[0].as.n, hi = a[1].as.n; if (hi < lo) FAIL("عشوائي.صحيح: الحد الأعلى أصغر من الأدنى"); *res = NUMV(floor(lo + rng_f() * (hi - lo + 1))); return true; }
NAT(r_pick) { if (!IS_T(a[0], O_LIST)) FAIL("اختر تتطلب قائمة"); List *l = AS_LIST(a[0]); if (!l->a.n) { *res = NIL; return true; } *res = l->a.v[(int)(rng_f() * l->a.n)]; return true; }
NAT(r_shuffle) { if (!IS_T(a[0], O_LIST)) FAIL("اخلط تتطلب قائمة"); List *l = AS_LIST(a[0]); for (int i = l->a.n - 1; i > 0; i--) { int j = (int)(rng_f() * (i + 1)); Value t = l->a.v[i]; l->a.v[i] = l->a.v[j]; l->a.v[j] = t; } *res = a[0]; return true; }
NAT(r_seed) { NUMA(0); rng_state = (uint64_t)a[0].as.n * 2654435761ULL + 88172645463325252ULL; *res = NIL; return true; }

static double now_sec(void) { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
NAT(t_now) { *res = NUMV(now_sec()); return true; }
NAT(t_clock) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); *res = NUMV(ts.tv_sec + ts.tv_nsec / 1e9); return true; }
NAT(t_sleep) { NUMA(0); fflush(stdout); struct timespec ts; double s = a[0].as.n; if (s < 0) s = 0; ts.tv_sec = (time_t)s; ts.tv_nsec = (long)((s - floor(s)) * 1e9); nanosleep(&ts, NULL); *res = NIL; return true; }
NAT(t_format) {
  STRA(0); time_t t = argc > 1 && IS_NUM(a[1]) ? (time_t)a[1].as.n : (time_t)now_sec(); struct tm tmv; localtime_r(&t, &tmv);
  char buf[512]; size_t n = strftime(buf, sizeof buf, AS_STR(a[0])->chars, &tmv); *res = mkstr(buf, (int)n); return true;
}

static Value g_args;
NAT(s_env) { PERM(PM_ENV); STRA(0); const char *v = getenv(AS_STR(a[0])->chars); *res = v ? mkstr(v, (int)strlen(v)) : (argc > 1 ? a[1] : NIL); return true; }
NAT(s_setenv) { STRA(0); Str *v = tostr(a[1]); setenv(AS_STR(a[0])->chars, v->chars, 1); *res = NIL; return true; }
NAT(s_tty) { *res = BOOLV(isatty(1) && !getenv("NO_COLOR")); return true; }
NAT(s_loadenv) {
  const char *path = argc > 0 && IS_T(a[0], O_STR) ? AS_STR(a[0])->chars : ".env"; char *src = read_file(path, NULL);
  if (!src) { *res = NUMV(0); return true; } int n = 0;
  for (char *line = strtok(src, "\n"); line; line = strtok(NULL, "\n")) {
    while (*line == ' ' || *line == '\t') line++; if (*line == '#' || !*line) continue; if (!strncmp(line, "export ", 7)) line += 7;
    char *eq = strchr(line, '='); if (!eq) continue; *eq = 0; char *k = line, *v = eq + 1;
    for (char *e = k + strlen(k); e > k && (e[-1] == ' ' || e[-1] == '\t'); ) *--e = 0;
    while (*v == ' ' || *v == '\t') v++; size_t vl = strlen(v); while (vl && (v[vl - 1] == '\r' || v[vl - 1] == ' ')) v[--vl] = 0;
    if (vl >= 2 && (v[0] == '"' || v[0] == '\'') && v[vl - 1] == v[0]) { v[vl - 1] = 0; v++; }
    { const char *cur = getenv(k); if (!cur || !*cur) setenv(k, v, 1); } n++;
  }
  free(src); *res = NUMV(n); return true;
}
NAT(s_exit) { fflush(stdout); exit(argc > 0 && IS_NUM(a[0]) ? (int)a[0].as.n : 0); return true; }
NAT(s_exec) {
  SAFE();
  STRA(0); fflush(stdout); FILE *p = popen(AS_STR(a[0])->chars, "r"); if (!p) FAIL("تعذّر تنفيذ الأمر");
  Buf b; buf_init(&b); char tmp[4096]; size_t n; while ((n = fread(tmp, 1, sizeof tmp, p)) > 0) buf_add(&b, tmp, (int)n);
  pclose(p); *res = mkstr(buf_cstr(&b), b.n); free(b.p); return true;
}
NAT(s_run) { SAFE(); STRA(0); fflush(stdout); int r = system(AS_STR(a[0])->chars); *res = NUMV(WIFEXITED(r) ? WEXITSTATUS(r) : r); return true; }
NAT(s_cwd) { char b[PATH_MAX]; if (!getcwd(b, sizeof b)) FAIL("تعذّر قراءة المسار"); *res = mkstr(b, (int)strlen(b)); return true; }
NAT(s_platform) {
  Map *m = new_map(); push(OBJV(m));
  dset(&m->d, KN("نظام"), OBJV(intern_c(os_name()))); dset(&m->d, KX("معمارية"), OBJV(intern_c(os_arch())));
  dset(&m->d, KN("jit"), BOOLV(SOLAN_JIT)); dset(&m->d, KN("فرع"), BOOLV(SOLAN_FORK)); dset(&m->d, KN("فاصل"), OBJV(intern_c(SOLAN_WIN ? "\\" : "/")));
  dset(&m->d, KX("نسخة"), OBJV(intern_c(SOLAN_VERSION)));

  dset(&m->d, KN("ضغط_مضمن"), BOOLV(1));
#ifdef SOLAN_SQLITE_EMBED
  dset(&m->d, KN("sqlite_مضمن"), BOOLV(1));
#else
  dset(&m->d, KN("sqlite_مضمن"), BOOLV(0));
#endif
  vm.sp--; *res = OBJV(m); return true;
}

NAT(f_read) {
  STRA(0); PERMP(PM_READ, AS_STR(a[0])->chars); size_t n; char *s = read_file(AS_STR(a[0])->chars, &n);
  if (!s) FAIL("تعذّرت قراءة الملف '%s'", AS_STR(a[0])->chars);
  *res = mkstr(s, (int)n); free(s); return true;
}
static bool write_common(Value *a, Value *res, const char *mode) {
  if (!IS_T(a[0], O_STR)) FAIL("المسار يجب أن يكون نصا");
  PERMP(PM_WRITE, AS_STR(a[0])->chars);
  Str *t = tostr(a[1]); FILE *f = fopen(AS_STR(a[0])->chars, mode); if (!f) FAIL("تعذّرت الكتابة في '%s'", AS_STR(a[0])->chars);
  fwrite(t->chars, 1, t->len, f); fclose(f); *res = BOOLV(true); return true;
}
NAT(f_write) { return write_common(a, res, "wb"); }
NAT(f_append) { return write_common(a, res, "ab"); }
NAT(f_exists) { STRA(0); PERMP(PM_READ, AS_STR(a[0])->chars); sstat_t st; *res = BOOLV(sstat(AS_STR(a[0])->chars, &st) == 0); return true; }
NAT(f_remove) { STRA(0); PERMP(PM_WRITE, AS_STR(a[0])->chars); *res = BOOLV(remove(AS_STR(a[0])->chars) == 0); return true; }
NAT(f_mkdir) { STRA(0); PERMP(PM_WRITE, AS_STR(a[0])->chars); *res = BOOLV(mkdir(AS_STR(a[0])->chars, 0755) == 0); return true; }
NAT(f_list) {
  STRA(0); PERMP(PM_READ, AS_STR(a[0])->chars); DIR *d = opendir(AS_STR(a[0])->chars); if (!d) FAIL("تعذّر فتح المجلد '%s'", AS_STR(a[0])->chars);
  List *l = new_list(); struct dirent *e;
  while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; varr_push(&l->a, mkstr(e->d_name, (int)strlen(e->d_name))); }
  closedir(d); *res = OBJV(l); return true;
}

static void json_str(Buf *b, Str *s) {
  buf_addc(b, '"');
  for (int i = 0; i < s->len; i++) {
    unsigned char c = s->chars[i];
    switch (c) {
      case '"': buf_add(b, "\\\"", 2); break; case '\\': buf_add(b, "\\\\", 2); break; case '\n': buf_add(b, "\\n", 2); break;
      case '\r': buf_add(b, "\\r", 2); break; case '\t': buf_add(b, "\\t", 2); break;
      default: if (c < 0x20) buf_fmt(b, "\\u%04x", c); else buf_addc(b, c);
    }
  }
  buf_addc(b, '"');
}
