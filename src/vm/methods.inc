#define NEED(n) do { if (argc != (n)) { rt_error("'%s' تتطلب %d وسائط", nm->chars, (n)); return 1; } } while (0)
#define NEEDR(lo, hi) do { if (argc < (lo) || argc > (hi)) { rt_error("عدد وسائط '%s' غير صحيح", nm->chars); return 1; } } while (0)
#define ARGSTR(i) do { if (!IS_T(a[i], O_STR)) { rt_error("'%s' تتطلب نصا", nm->chars); return 1; } } while (0)
#define ARGNUM(i) do { if (!IS_NUM(a[i])) { rt_error("'%s' تتطلب رقما", nm->chars); return 1; } } while (0)
static Value mkstr(const char *s, int n) { return OBJV(intern(s, n)); }
static void slice_bounds(int len, Value *a, int argc, int *s, int *e) {
  double x = argc > 0 && IS_NUM(a[0]) ? a[0].as.n : 0, y = argc > 1 && IS_NUM(a[1]) ? a[1].as.n : len;
  if (x < 0) x += len; if (y < 0) y += len; if (x < 0) x = 0; if (y > len) y = len; if (x > len) x = len; if (y < x) y = x;
  *s = (int)x; *e = (int)y;
}
static bool cmp_vals(Value a, Value b, Value cmp, int *r) {
  if (IS_NIL(cmp)) {
    int c = vm_cmp(a, b); if (c == 2) return false; *r = c; return true;
  }
  Value argv[2] = {a, b}, out;
  if (!call_fn(cmp, 2, argv, &out)) return false;
  if (!IS_NUM(out)) { rt_error("دالة المقارنة يجب أن تعيد رقما"); return false; }
  *r = out.as.n < 0 ? -1 : out.as.n > 0 ? 1 : 0; return true;
}
static bool msort(Value *v, Value *tmp, int n, Value cmp) {
  if (n < 2) return true;
  int m = n / 2; if (!msort(v, tmp, m, cmp) || !msort(v + m, tmp, n - m, cmp)) return false;
  int i = 0, j = m, k = 0;
  while (i < m && j < n) { int r; if (!cmp_vals(v[j], v[i], cmp, &r)) return false; if (r < 0) tmp[k++] = v[j++]; else tmp[k++] = v[i++]; }
  while (i < m) tmp[k++] = v[i++]; while (j < n) tmp[k++] = v[j++];
  memcpy(v, tmp, sizeof(Value) * n); return true;
}
static bool do_format(Str *f, int argc, Value *args, Value *res);
static int str_method(Str *s, int mid, Str *nm, int argc, Value *a, Value *res) {
  switch (mid) {
    case M_LEN: *res = NUMV(u8_len(s->chars, s->len)); return 0;
    case M_SPLIT: {
      NEEDR(0, 1); List *l = new_list(); *res = OBJV(l);
      if (argc == 0) {
        const char *p = s->chars, *end = p + s->len;
        while (p < end) {
          while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
          const char *q = p; while (q < end && !(*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
          if (q > p) varr_push(&l->a, mkstr(p, (int)(q - p))); p = q;
        }
        return 0;
      }
      ARGSTR(0); Str *sep = AS_STR(a[0]);
      if (sep->len == 0) { for (int i = 0; i < s->len;) { int cs = u8_charsize(s->chars + i); varr_push(&l->a, mkstr(s->chars + i, cs)); i += cs; } return 0; }
      const char *p = s->chars, *end = p + s->len;
      for (;;) {
        const char *f = memmem(p, end - p, sep->chars, sep->len);
        if (!f) { varr_push(&l->a, mkstr(p, (int)(end - p))); break; }
        varr_push(&l->a, mkstr(p, (int)(f - p))); p = f + sep->len;
      }
      return 0;
    }
    case M_LINES: {
      NEED(0); List *l = new_list(); *res = OBJV(l); const char *p = s->chars, *end = p + s->len;
      while (p < end) { const char *q = memchr(p, '\n', end - p); int n = q ? (int)(q - p) : (int)(end - p); int m = n; if (m > 0 && p[m - 1] == '\r') m--; varr_push(&l->a, mkstr(p, m)); if (!q) break; p = q + 1; }
      return 0;
    }
    case M_TRIM: {
      NEED(0); const char *p = s->chars, *e = p + s->len;
      while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
      while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) e--;
      *res = mkstr(p, (int)(e - p)); return 0;
    }
    case M_UPPER: case M_LOWER: {
      NEED(0); char *t = xalloc(s->len + 1);
      for (int i = 0; i < s->len; i++) { unsigned char c = s->chars[i]; t[i] = mid == M_UPPER ? ((c >= 'a' && c <= 'z') ? c - 32 : c) : ((c >= 'A' && c <= 'Z') ? c + 32 : c); }
      *res = mkstr(t, s->len); free(t); return 0;
    }
    case M_STARTS: { NEED(1); ARGSTR(0); Str *t = AS_STR(a[0]); *res = BOOLV(t->len <= s->len && memcmp(s->chars, t->chars, t->len) == 0); return 0; }
    case M_ENDS: { NEED(1); ARGSTR(0); Str *t = AS_STR(a[0]); *res = BOOLV(t->len <= s->len && memcmp(s->chars + s->len - t->len, t->chars, t->len) == 0); return 0; }
    case M_HAS: { NEED(1); ARGSTR(0); Str *t = AS_STR(a[0]); *res = BOOLV(t->len == 0 || memmem(s->chars, s->len, t->chars, t->len) != NULL); return 0; }
    case M_REPLACE: {
      NEED(2); ARGSTR(0); ARGSTR(1); Str *o = AS_STR(a[0]), *n = AS_STR(a[1]);
      if (o->len == 0) { *res = OBJV(s); return 0; }
      Buf b; buf_init(&b); const char *p = s->chars, *end = p + s->len;
      for (;;) { const char *f = memmem(p, end - p, o->chars, o->len); if (!f) { buf_add(&b, p, (int)(end - p)); break; } buf_add(&b, p, (int)(f - p)); buf_add(&b, n->chars, n->len); p = f + o->len; }
      *res = mkstr(buf_cstr(&b), b.n); free(b.p); return 0;
    }
    case M_SLICE: {
      NEEDR(1, 2); int n = u8_len(s->chars, s->len), x, y; slice_bounds(n, a, argc, &x, &y);
      int bx = u8_off(s->chars, s->len, x), by = u8_off(s->chars, s->len, y); *res = mkstr(s->chars + bx, by - bx); return 0;
    }
    case M_FIND: {
      NEEDR(1, 1); ARGSTR(0); Str *t = AS_STR(a[0]); const char *f = memmem(s->chars, s->len, t->chars, t->len);
      *res = NUMV(f ? u8_len(s->chars, (int)(f - s->chars)) : -1); return 0;
    }
    case M_REPEAT: { NEED(1); ARGNUM(0); double nd = a[0].as.n; if (!(nd > 0)) { *res = mkstr("", 0); return 0; } if (nd != floor(nd)) { rt_error("كرر تتطلب عددا صحيحا"); return 1; } if (s->len > 0 && nd * (double)s->len > 536870912.0) { rt_error("النص الناتج كبير جدا (الحد الأقصى 512 ميغابايت)"); return 1; } int n = (int)nd; Buf b; buf_init(&b); for (int i = 0; i < n; i++) buf_add(&b, s->chars, s->len); *res = mkstr(buf_cstr(&b), b.n); free(b.p); return 0; }
    case M_REVERSE: {
      NEED(0); char *t = xalloc(s->len + 1); int o = s->len;
      for (int i = 0; i < s->len;) { int cs = u8_charsize(s->chars + i); o -= cs; memcpy(t + o, s->chars + i, cs); i += cs; }
      *res = mkstr(t, s->len); free(t); return 0;
    }
    case M_CHARS: { NEED(0); List *l = new_list(); *res = OBJV(l); for (int i = 0; i < s->len;) { int cs = u8_charsize(s->chars + i); varr_push(&l->a, mkstr(s->chars + i, cs)); i += cs; } return 0; }
    case M_FORMAT: return do_format(s, argc, a, res) ? 0 : 1;
    case M_PADL: case M_PADR: {
      NEEDR(1, 2); ARGNUM(0); int w = (int)a[0].as.n, n = u8_len(s->chars, s->len);
      Str *pad = argc > 1 && IS_T(a[1], O_STR) ? AS_STR(a[1]) : intern_c(" "); Buf b; buf_init(&b);
      if (mid == M_PADR) buf_add(&b, s->chars, s->len);
      for (int i = n; i < w; i++) buf_add(&b, pad->chars, pad->len);
      if (mid == M_PADL) buf_add(&b, s->chars, s->len);
      *res = mkstr(buf_cstr(&b), b.n); free(b.p); return 0;
    }
    default: return 2;
  }
}
static int list_method(List *l, int mid, Str *nm, int argc, Value *a, Value *res) {
  switch (mid) {
    case M_LEN: *res = NUMV(l->a.n); return 0;
    case M_PUSH: for (int i = 0; i < argc; i++) varr_push(&l->a, a[i]); *res = OBJV(l); return 0;
    case M_INSERT: {
      NEED(2); ARGNUM(0); int i = (int)a[0].as.n; if (i < 0) i += l->a.n; if (i < 0 || i > l->a.n) { rt_error("فهرس الإدراج خارج النطاق"); return 1; }
      varr_push(&l->a, NIL); memmove(l->a.v + i + 1, l->a.v + i, sizeof(Value) * (l->a.n - 1 - i)); l->a.v[i] = a[1]; *res = OBJV(l); return 0;
    }
    case M_REMOVE: {
      NEED(1); int i; if (!to_index(a[0], l->a.n, &i)) { rt_error("فهرس الحذف غير صالح"); return 1; }
      *res = l->a.v[i]; memmove(l->a.v + i, l->a.v + i + 1, sizeof(Value) * (l->a.n - i - 1)); l->a.n--; return 0;
    }
    case M_POP: { NEED(0); if (l->a.n == 0) { *res = NIL; return 0; } *res = l->a.v[--l->a.n]; return 0; }
    case M_JOIN: {
      NEEDR(0, 1); Str *sep = argc > 0 && IS_T(a[0], O_STR) ? AS_STR(a[0]) : intern_c(""); Buf b; buf_init(&b);
      for (int i = 0; i < l->a.n; i++) { if (i) buf_add(&b, sep->chars, sep->len); Str *t = tostr(l->a.v[i]); buf_add(&b, t->chars, t->len); }
      *res = mkstr(buf_cstr(&b), b.n); free(b.p); return 0;
    }
    case M_REVERSE: { NEED(0); for (int i = 0, j = l->a.n - 1; i < j; i++, j--) { Value t = l->a.v[i]; l->a.v[i] = l->a.v[j]; l->a.v[j] = t; } *res = OBJV(l); return 0; }
    case M_SORT: {
      NEEDR(0, 1); int n = l->a.n; Value cmp = argc ? a[0] : NIL; if (n < 2) { *res = OBJV(l); return 0; }
      Value *cp = xalloc(sizeof(Value) * n), *tmp = xalloc(sizeof(Value) * n); memcpy(cp, l->a.v, sizeof(Value) * n);
      bool ok = msort(cp, tmp, n, cmp);
      if (ok && l->a.n == n) memcpy(l->a.v, cp, sizeof(Value) * n);
      free(cp); free(tmp); if (!ok) return 1; *res = OBJV(l); return 0;
    }
    case M_MAP: case M_FILTER: {
      NEED(1); List *out = new_list(); push(OBJV(out));
      for (int i = 0; i < l->a.n; i++) {
        Value x = l->a.v[i], r; if (!call_fn(a[0], 1, &x, &r)) return 1;
        if (mid == M_MAP) varr_push(&out->a, r); else if (!falsey(r)) varr_push(&out->a, x);
      }
      vm.sp--; *res = OBJV(out); return 0;
    }
    case M_REDUCE: {
      NEEDR(1, 2); int i = 0; Value acc; if (argc == 2) acc = a[1]; else { if (l->a.n == 0) { rt_error("اطوي على قائمة فارغة بدون قيمة ابتدائية"); return 1; } acc = l->a.v[0]; i = 1; }
      for (; i < l->a.n; i++) { Value args[2] = {acc, l->a.v[i]}; if (!call_fn(a[0], 2, args, &acc)) return 1; }
      *res = acc; return 0;
    }
    case M_EACH: { NEED(1); for (int i = 0; i < l->a.n; i++) { Value x = l->a.v[i], r; if (!call_fn(a[0], 1, &x, &r)) return 1; } *res = NIL; return 0; }
    case M_FINDF: { NEED(1); *res = NIL; for (int i = 0; i < l->a.n; i++) { Value x = l->a.v[i], r; if (!call_fn(a[0], 1, &x, &r)) return 1; if (!falsey(r)) { *res = x; break; } } return 0; }
    case M_ANY: case M_ALL: {
      NEED(1); bool any = mid == M_ANY; bool out = !any;
      for (int i = 0; i < l->a.n; i++) { Value x = l->a.v[i], r; if (!call_fn(a[0], 1, &x, &r)) return 1; if (any && !falsey(r)) { out = true; break; } if (!any && falsey(r)) { out = false; break; } }
      *res = BOOLV(out); return 0;
    }
    case M_COPY: { NEED(0); List *o = new_list(); for (int i = 0; i < l->a.n; i++) varr_push(&o->a, l->a.v[i]); *res = OBJV(o); return 0; }
    case M_CLEAR: NEED(0); l->a.n = 0; *res = OBJV(l); return 0;
    case M_SUM: {
      NEED(0); double s = 0; for (int i = 0; i < l->a.n; i++) { if (!IS_NUM(l->a.v[i])) { rt_error("مجموع: العناصر يجب أن تكون أرقاما"); return 1; } s += l->a.v[i].as.n; }
      *res = NUMV(s); return 0;
    }
    case M_FIND: { NEED(1); *res = NUMV(-1); for (int i = 0; i < l->a.n; i++) if (values_equal(l->a.v[i], a[0])) { *res = NUMV(i); break; } return 0; }
    case M_HAS: { NEED(1); bool r; if (vm_in(a[0], OBJV(l), &r)) return 1; *res = BOOLV(r); return 0; }
    case M_GET: { NEEDR(1, 2); int gi; *res = to_index(a[0], l->a.n, &gi) ? l->a.v[gi] : (argc == 2 ? a[1] : NIL); return 0; }
    case M_FIRST: NEED(0); *res = l->a.n ? l->a.v[0] : NIL; return 0;
    case M_LAST: NEED(0); *res = l->a.n ? l->a.v[l->a.n - 1] : NIL; return 0;
    case M_FLATTEN: {
      NEED(0); List *o = new_list(); push(OBJV(o));
      for (int i = 0; i < l->a.n; i++) {
        Value x = l->a.v[i];
        if (IS_T(x, O_LIST)) { List *s = AS_LIST(x); for (int j = 0; j < s->a.n; j++) varr_push(&o->a, s->a.v[j]); } else varr_push(&o->a, x);
      }
      vm.sp--; *res = OBJV(o); return 0;
    }
    case M_UNIQUE: {
      NEED(0); List *o = new_list(); push(OBJV(o)); Dict seen; memset(&seen, 0, sizeof seen);
      for (int i = 0; i < l->a.n; i++) {
        Value x = l->a.v[i], t; bool dup;
        if (IS_T(x, O_LIST)) { dup = false; for (int j = 0; j < o->a.n; j++) if (values_equal(o->a.v[j], x)) { dup = true; break; } }
        else dup = dget(&seen, x, &t);
        if (!dup) { varr_push(&o->a, x); dset(&seen, x, BOOLV(true)); }
      }
      dfree(&seen); vm.sp--; *res = OBJV(o); return 0;
    }
    case M_SLICE: {
      NEEDR(1, 2); int x, y; slice_bounds(l->a.n, a, argc, &x, &y); List *o = new_list();
      for (int i = x; i < y; i++) varr_push(&o->a, l->a.v[i]); *res = OBJV(o); return 0;
    }
    default: return 2;
  }
}
static int map_method(Map *m, int mid, Str *nm, int argc, Value *a, Value *res) {
  switch (mid) {
    case M_LEN: *res = NUMV(m->d.live); return 0;
    case M_KEYS: case M_VALUES: case M_ITEMS: {
      NEED(0); List *l = new_list(); *res = OBJV(l);
      for (int i = 0; i < m->d.n; i++) {
        if (m->d.e[i].k.t == V_EMPTY) continue;
        if (mid == M_KEYS) varr_push(&l->a, m->d.e[i].k); else if (mid == M_VALUES) varr_push(&l->a, m->d.e[i].v);
        else { List *p = new_list(); varr_push(&p->a, m->d.e[i].k); varr_push(&p->a, m->d.e[i].v); varr_push(&l->a, OBJV(p)); }
      }
      return 0;
    }
    case M_HAS: { NEED(1); Value t; *res = BOOLV(dget(&m->d, a[0], &t)); return 0; }
    case M_GET: { NEEDR(1, 2); Value t; *res = dget(&m->d, a[0], &t) ? t : (argc == 2 ? a[1] : NIL); return 0; }
    case M_REMOVE: { NEED(1); Value old; if (ddel(&m->d, a[0], &old)) *res = old; else *res = NIL; return 0; }
    case M_COPY: { NEED(0); Map *o = new_map(); for (int i = 0; i < m->d.n; i++) if (m->d.e[i].k.t != V_EMPTY) dset(&o->d, m->d.e[i].k, m->d.e[i].v); *res = OBJV(o); return 0; }
    case M_CLEAR: NEED(0); dfree(&m->d); *res = OBJV(m); return 0;
    case M_MERGE: {
      NEED(1); if (!IS_T(a[0], O_MAP)) { rt_error("دمج يتطلب قاموسا"); return 1; }
      Map *o = AS_MAP(a[0]); for (int i = 0; i < o->d.n; i++) if (o->d.e[i].k.t != V_EMPTY) dset(&m->d, o->d.e[i].k, o->d.e[i].v);
      *res = OBJV(m); return 0;
    }
    default: return 2;
  }
}
static int builtin_method(Value r, Str *nm, int argc, Value *a, Value *res) {
  Value idv; if (!dget(&vm.mid, OBJV(nm), &idv)) return 2; int mid = (int)idv.as.n;
  if (IS_T(r, O_STR)) return str_method(AS_STR(r), mid, nm, argc, a, res);
  if (IS_T(r, O_LIST)) return list_method(AS_LIST(r), mid, nm, argc, a, res);
  if (IS_T(r, O_MAP)) return map_method(AS_MAP(r), mid, nm, argc, a, res);
  return 2;
}
