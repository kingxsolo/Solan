static Str *tostr(Value v) {
  if (IS_T(v, O_STR)) return AS_STR(v);
  if (!g_digits && IS_NUM(v) && v.as.n == floor(v.as.n) && fabs(v.as.n) < 1e15) { char t[32]; int n = snprintf(t, sizeof t, "%lld", (long long)v.as.n); return intern(t, n); }
  Buf b; buf_init(&b); repr(v, &b, 0, false);
  Str *s = intern(buf_cstr(&b), b.n); free(b.p); return s;
}

static bool falsey(Value v) {
  switch (v.t) {
    case V_NIL: case V_EMPTY: return true;
    case V_BOOL: return !v.as.b;
    case V_NUM: return v.as.n == 0;
    default: break;
  }
  switch (v.as.o->t) {
    case O_STR: return ((Str *)v.as.o)->len == 0;
    case O_LIST: return ((List *)v.as.o)->a.n == 0;
    case O_MAP: return ((Map *)v.as.o)->d.live == 0;
    default: return false;
  }
}
static bool values_equal_d(Value a, Value b, int depth) {
  if (a.t != b.t) return false;
  if (a.t == V_OBJ) {
    Obj *x = a.as.o, *y = b.as.o;
    if (x == y) return true;
    if (x->t != y->t) return keq(a, b);
    if (x->t == O_LIST) {
      if (depth > 200) return false;
      List *p = (List *)x, *q = (List *)y; if (p->a.n != q->a.n) return false;
      for (int i = 0; i < p->a.n; i++) if (!values_equal_d(p->a.v[i], q->a.v[i], depth + 1)) return false;
      return true;
    }
    if (x->t == O_MAP) {
      if (depth > 200) return false;
      Dict *p = &((Map *)x)->d, *q = &((Map *)y)->d; if (p->live != q->live) return false;
      for (int i = 0; i < p->n; i++) {
        if (p->e[i].k.t == V_EMPTY) continue;
        Value ov; if (!dget(q, p->e[i].k, &ov) || !values_equal_d(p->e[i].v, ov, depth + 1)) return false;
      }
      return true;
    }
  }
  return keq(a, b);
}
static bool values_equal(Value a, Value b) { return values_equal_d(a, b, 0); }

static int cur_line(void) {
  if (vm.fcount == 0) return 0;
  Frame *f = &vm.frames[vm.fcount - 1];
  int off = (int)(f->ip - f->clo->f->ch.code) - 1;
  if (off < 0) off = 0;
  if (off >= f->clo->f->ch.n) off = f->clo->f->ch.n - 1;
  return off >= 0 ? f->clo->f->ch.lines[off] : 0;
}

static void rt_error(const char *fmt, ...) {
  char msg[600]; va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
  char full[700]; int ln = cur_line();
  if (ln > 0) snprintf(full, sizeof full, "%s (سطر %d)", msg, ln); else snprintf(full, sizeof full, "%s", msg);
  vm.exc = OBJV(intern_c(full));
}
#define FAIL(...) do { rt_error(__VA_ARGS__); return false; } while (0)
static void capture_trace(void) {
  Buf b; buf_init(&b);
  for (int i = vm.fcount - 1; i >= 0; i--) {
    Frame *f = &vm.frames[i]; Fun *fn = f->clo->f;
    int off = (int)(f->ip - fn->ch.code) - 1; if (off < 0) off = 0; if (off >= fn->ch.n) off = fn->ch.n - 1;
    int ln = off >= 0 ? fn->ch.lines[off] : 0;
    buf_fmt(&b, "  في %s (%s:%d)\n", fn->name ? fn->name->chars : "<الملف>", fn->file ? fn->file->chars : "?", ln);
    if (i == vm.fcount - 1) { snprintf(vm.tr_file, sizeof vm.tr_file, "%s", fn->file ? fn->file->chars : ""); vm.tr_line = ln; }
  }
  free(vm.trace); vm.trace = b.p ? b.p : NULL;
}
