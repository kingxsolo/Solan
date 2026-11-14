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
    case M_TRIML: case M_TRIMR: {
      NEED(0); const char *p = s->chars, *e = p + s->len;
      if (mid == M_TRIML) while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
      else while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) e--;
      *res = mkstr(p, (int)(e - p)); return 0;
    }
    case M_CHRAT: {
      NEED(1); ARGNUM(0); int n = u8_len(s->chars, s->len), i = (int)a[0].as.n;
      if (i < 0) i += n;
      if (i < 0 || i >= n) { rt_error("فهرس الحرف خارج النطاق"); return 1; }
      int bx = u8_off(s->chars, s->len, i); int cs = u8_charsize(s->chars + bx);
      *res = mkstr(s->chars + bx, cs); return 0;
    }
    case M_TITLE: case M_CAPT: {
      NEED(0); char *t = xalloc(s->len + 1); memcpy(t, s->chars, s->len); t[s->len] = 0;
      bool word = mid == M_CAPT, atstart = true;
      for (int i = 0; i < s->len;) {
        int cs = u8_charsize(t + i);
        if (cs == 1) {
          unsigned char c = (unsigned char)t[i];
          if ((c >= 'a' && c <= 'z') && (word || atstart)) { t[i] = (char)(c - 32); atstart = false; }
          else if (c >= 'A' && c <= 'Z') { if (!word && !atstart) t[i] = (char)(c + 32); atstart = false; }
          else if (c == ' ' || c == '-' || c == '_' || c == '\t') atstart = true;
          else atstart = false;
        } else atstart = false;
        i += cs;
      }
      *res = mkstr(t, s->len); free(t); return 0;
    }
    case M_ISALNUM: {
      NEED(0); bool all = s->len > 0;
      for (int i = 0; i < s->len && all;) {
        int cs = u8_charsize(s->chars + i); uint32_t cp;
        u8_decode((const unsigned char *)s->chars + i, &cp);
        bool ok = (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= 0x600 && cp <= 0x6FF);
        if (!ok) all = false;
        i += cs;
      }
      *res = BOOLV(all); return 0;
    }
    case M_ISEMPTY: { NEED(0); *res = BOOLV(s->len == 0); return 0; }
    case M_INDEXOF: { NEEDR(1, 2); ARGSTR(0); Str *t = AS_STR(a[0]); int from = 0; if (argc == 2) { ARGNUM(1); from = (int)a[1].as.n; int ln = u8_len(s->chars, s->len); if (from < 0) from += ln; if (from < 0) from = 0; from = u8_off(s->chars, s->len, from); }
      if (t->len == 0) { *res = NUMV(argc == 2 ? u8_len(s->chars, from < s->len ? from : s->len) : 0); return 0; }
      if (from >= s->len) { *res = NUMV(-1); return 0; }
      const char *f = memmem(s->chars + from, s->len - from, t->chars, t->len);
      *res = NUMV(f ? u8_len(s->chars, (int)(f - s->chars)) : -1); return 0; }
    case M_COUNTVAL: { NEED(1); ARGSTR(0); Str *t = AS_STR(a[0]); int c = 0; if (t->len > 0) { const char *p = s->chars, *end = p + s->len; for (;;) { const char *f = memmem(p, end - p, t->chars, t->len); if (!f) break; c++; p = f + t->len; } } *res = NUMV(c); return 0; }
    case M_TAKE: case M_SKIP: {
      NEED(1); ARGNUM(0); int n = u8_len(s->chars, s->len), k = (int)a[0].as.n; if (k < 0) { rt_error("'%s' تتطلب عددا غير سالب", nm->chars); return 1; }
      if (k > n) k = n; int x = mid == M_TAKE ? 0 : k, y = mid == M_TAKE ? k : n;
      int bx = u8_off(s->chars, s->len, x), by = u8_off(s->chars, s->len, y); *res = mkstr(s->chars + bx, by - bx); return 0;
    }
    case M_PADCENTER: {
      NEEDR(1, 2); ARGNUM(0); int w = (int)a[0].as.n, n = u8_len(s->chars, s->len);
      Str *pad = argc > 1 && IS_T(a[1], O_STR) ? AS_STR(a[1]) : intern_c(" ");
      if (n >= w) { *res = OBJV(s); return 0; }
      int total = w - n, left = total / 2, right = total - left;
      Buf b; buf_init(&b);
      for (int i = 0; i < left; i++) buf_add(&b, pad->chars, pad->len);
      buf_add(&b, s->chars, s->len);
      for (int i = 0; i < right; i++) buf_add(&b, pad->chars, pad->len);
      *res = mkstr(buf_cstr(&b), b.n); free(b.p); return 0;
    }    case M_COUNTMATCH: {
      NEED(1); Str *t = tostr(a[0]); int c = 0;
      if (t->len > 0) { const char *p = s->chars, *end = p + s->len; for (;;) { const char *f = memmem(p, end - p, t->chars, t->len); if (!f) break; c++; p = f + t->len; } }
      *res = NUMV(c); return 0;
    }
    case M_INDICES: {
      NEED(1); Str *t = tostr(a[0]); List *out = new_list(); *res = OBJV(out);
      if (t->len > 0) {
        const char *p = s->chars, *end = p + s->len;
        for (;;) {
          const char *f = memmem(p, end - p, t->chars, t->len);
          if (!f) break;
          varr_push(&out->a, NUMV(u8_len(s->chars, (int)(f - s->chars))));
          p = f + t->len;
        }
      }
      return 0;
    }
    case M_MULTISPLIT: {
      NEED(1); if (!IS_T(a[0], O_LIST)) { rt_error("قسم_متعدد تتطلب قائمة فواصل"); return 1; }
      List *seps = AS_LIST(a[0]);
      List *out = new_list(); *res = OBJV(out);
      const char *p = s->chars, *end = p + s->len;
      while (p < end) {
        const char *best = NULL; int blen = 0;
        for (int i = 0; i < seps->a.n; i++) {
          if (!IS_T(seps->a.v[i], O_STR)) continue;
          Str *sp2 = AS_STR(seps->a.v[i]);
          if (sp2->len == 0) continue;
          const char *f = memmem(p, end - p, sp2->chars, sp2->len);
          if (f && (!best || f < best || (f == best && sp2->len > blen))) { best = f; blen = sp2->len; }
        }
        if (!best) { varr_push(&out->a, mkstr(p, (int)(end - p))); break; }
        if (best > p || out->a.n == 0) varr_push(&out->a, mkstr(p, (int)(best - p)));
        p = best + blen;
        if (p >= end) varr_push(&out->a, mkstr("", 0));
      }
      return 0;
    }
    case M_STRIPDIAC: { // إزالة التشكيل (الحركات العربية + التطويل)
      NEED(0); Buf b; buf_init(&b);
      for (int i = 0; i < s->len;) {
        uint32_t cp; int cs = u8_decode((const unsigned char *)s->chars + i, &cp);
        bool diac = cp == 0x640 || (cp >= 0x64B && cp <= 0x65F) || cp == 0x670;
        if (!diac) buf_add(&b, s->chars + i, cs);
        i += cs;
      }
      *res = mkstr(buf_cstr(&b), b.n); free(b.p); return 0;
    }
    case M_NORMAR: { // تطبيع عربي كامل: إزالة تشكيل + توحيد الألف/الياء/التاء المربوطة
      NEED(0); char tmp[4096];
      if (s->len >= (int)sizeof tmp) { rt_error("النص طويل جدا للطبّع (الحد 4096 بايت)"); return 1; }
      int n = norm_into(s->chars, s->len, tmp);
      *res = OBJV(intern(tmp, n)); return 0;
    }
    case M_ISARABIC: { // هل النص عربي (كل الحروف عربية أو مسافات/أرقام)
      NEED(0); bool any = false, all = true;
      for (int i = 0; i < s->len;) {
        uint32_t cp; int cs = u8_decode((const unsigned char *)s->chars + i, &cp);
        bool ar = (cp >= 0x600 && cp <= 0x6FF) || cp == ' ' || (cp >= '0' && cp <= '9') || cp == '\n' || cp == '\t';
        if (cp == ' ' || cp == '\n' || cp == '\t' || (cp >= '0' && cp <= '9')) { i += cs; continue; }
        if (cp >= 0x600 && cp <= 0x6FF) any = true; else all = false;
        i += cs;
      }
      *res = BOOLV(any && all); return 0;
    }
    case M_TASHKEEL: { // عدد علامات التشكيل
      NEED(0); int c = 0;
      for (int i = 0; i < s->len;) {
        uint32_t cp; int cs = u8_decode((const unsigned char *)s->chars + i, &cp);
        if (cp == 0x640 || (cp >= 0x64B && cp <= 0x65F) || cp == 0x670) c++;
        i += cs;
      }
      *res = NUMV(c); return 0;
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
    case M_ISEMPTY: { NEED(0); *res = BOOLV(l->a.n == 0); return 0; }
    case M_INDEXOF: {
      NEEDR(1, 2); int from = 0; if (argc == 2) { ARGNUM(1); from = (int)a[1].as.n; if (from < 0) from += l->a.n; if (from < 0) from = 0; }
      for (int i = from; i < l->a.n; i++) if (values_equal(l->a.v[i], a[0])) { *res = NUMV(i); return 0; }
      *res = NUMV(-1); return 0;
    }
    case M_COUNTVAL: { NEED(1); int c = 0; for (int i = 0; i < l->a.n; i++) if (values_equal(l->a.v[i], a[0])) c++; *res = NUMV(c); return 0; }
    case M_MINV: case M_MAXV: {
      NEED(0); if (l->a.n == 0) { rt_error("'%s' على قائمة فارغة", nm->chars); return 1; }
      int bi = 0; for (int i = 1; i < l->a.n; i++) { int c = vm_cmp(l->a.v[i], l->a.v[bi]); if (c == 2) { rt_error("'%s': العناصر غير قابلة للمقارنة", nm->chars); return 1; } if (mid == M_MINV ? c < 0 : c > 0) bi = i; }
      *res = l->a.v[bi]; return 0;
    }
    case M_AVG: {
      NEED(0); if (l->a.n == 0) { rt_error("متوسط على قائمة فارغة"); return 1; }
      double s = 0; for (int i = 0; i < l->a.n; i++) { if (!IS_NUM(l->a.v[i])) { rt_error("متوسط: العناصر يجب أن تكون أرقاما"); return 1; } s += l->a.v[i].as.n; }
      *res = NUMV(s / l->a.n); return 0;
    }
    case M_TAKE: case M_SKIP: {
      NEED(1); ARGNUM(0); int k = (int)a[0].as.n; if (k < 0) { rt_error("'%s' تتطلب عددا غير سالب", nm->chars); return 1; }
      if (k > l->a.n) k = l->a.n; int x = mid == M_TAKE ? 0 : k, y = mid == M_TAKE ? k : l->a.n;
      List *o = new_list(); for (int i = x; i < y; i++) varr_push(&o->a, l->a.v[i]); *res = OBJV(o); return 0;
    }
    case M_ZIP: {
      NEED(1); if (!IS_T(a[0], O_LIST)) { rt_error("اقترن تتطلب قائمة"); return 1; }
      List *o = AS_LIST(a[0]), *r = new_list(); int n = l->a.n < o->a.n ? l->a.n : o->a.n;
      for (int i = 0; i < n; i++) { List *p = new_list(); varr_push(&p->a, l->a.v[i]); varr_push(&p->a, o->a.v[i]); varr_push(&r->a, OBJV(p)); }
      *res = OBJV(r); return 0;
    }
    case M_EXTEND: {
      NEED(1); if (!IS_T(a[0], O_LIST)) { rt_error("اضف_الكل يتطلب قائمة"); return 1; }
      List *o = AS_LIST(a[0]); for (int i = 0; i < o->a.n; i++) varr_push(&l->a, o->a.v[i]); *res = OBJV(l); return 0;
    }
    case M_GROUPBY: {
      NEED(1); Map *out = new_map(); push(OBJV(out));
      for (int i = 0; i < l->a.n; i++) {
        Value x = l->a.v[i], r; if (!call_fn(a[0], 1, &x, &r)) return 1;
        Value bucket; List *bl;
        if (dget(&out->d, r, &bucket)) bl = AS_LIST(bucket);
        else { bl = new_list(); dset(&out->d, r, OBJV(bl)); }
        varr_push(&bl->a, x);
      }
      vm.sp--; *res = OBJV(out); return 0;
    }
    case M_COUNTBY: {
      NEED(1); Map *out = new_map(); push(OBJV(out));
      for (int i = 0; i < l->a.n; i++) {
        Value x = l->a.v[i], r; if (!call_fn(a[0], 1, &x, &r)) return 1;
        Value c; double n2 = dget(&out->d, r, &c) ? c.as.n + 1 : 1;
        dset(&out->d, r, NUMV(n2));
      }
      vm.sp--; *res = OBJV(out); return 0;
    }
    case M_CHUNK: {
      NEED(1); ARGNUM(0); int sz = (int)a[0].as.n;
      if (sz < 1) { rt_error("قسّم_قطع: حجم القطعة يجب أن يكون 1 على الأقل"); return 1; }
      List *out = new_list();
      for (int i = 0; i < l->a.n; i += sz) {
        List *p = new_list();
        for (int j = i; j < l->a.n && j < i + sz; j++) varr_push(&p->a, l->a.v[j]);
        varr_push(&out->a, OBJV(p));
      }
      *res = OBJV(out); return 0;
    }
    case M_WINDOWED: {
      NEED(1); ARGNUM(0); int w = (int)a[0].as.n;
      if (w < 1) { rt_error("نوافذ: حجم النافذة يجب أن يكون 1 على الأقل"); return 1; }
      List *out = new_list();
      for (int i = 0; i + w <= l->a.n; i++) {
        List *p = new_list();
        for (int j = i; j < i + w; j++) varr_push(&p->a, l->a.v[j]);
        varr_push(&out->a, OBJV(p));
      }
      *res = OBJV(out); return 0;
    }
    case M_DISTINCTBY: {
      NEED(1); List *out = new_list(); push(OBJV(out)); Dict seen; memset(&seen, 0, sizeof seen);
      for (int i = 0; i < l->a.n; i++) {
        Value x = l->a.v[i], r; if (!call_fn(a[0], 1, &x, &r)) return 1;
        Value t; if (!dget(&seen, r, &t)) { varr_push(&out->a, x); dset(&seen, r, BOOLV(true)); }
      }
      dfree(&seen); vm.sp--; *res = OBJV(out); return 0;
    }
    case M_SORTBY: {
      NEED(1); int n = l->a.n; if (n < 2) { *res = OBJV(l); return 0; }
      Value *keys = xalloc(sizeof(Value) * n);
      for (int i = 0; i < n; i++) { Value x = l->a.v[i]; if (!call_fn(a[0], 1, &x, &keys[i])) { free(keys); return 1; } }
      // فرز إدراجي مستقر يرتب المؤشرات حسب المفاتيح
      int *idx = xalloc(sizeof(int) * n);
      for (int i = 0; i < n; i++) idx[i] = i;
      Value *kv = xalloc(sizeof(Value) * n);
      memcpy(kv, keys, sizeof(Value) * n);
      for (int i = 1; i < n; i++) {
        int ti = idx[i]; Value tk = kv[i]; int j = i - 1;
        while (j >= 0) { int c; if (!cmp_vals(kv[j], tk, NIL, &c)) { free(keys); free(idx); free(kv); return 1; } if (c > 0) { idx[j + 1] = idx[j]; kv[j + 1] = kv[j]; j--; } else break; }
        idx[j + 1] = ti; kv[j + 1] = tk;
      }
      Value *nv = xalloc(sizeof(Value) * n);
      for (int i = 0; i < n; i++) nv[i] = l->a.v[idx[i]];
      memcpy(l->a.v, nv, sizeof(Value) * n);
      free(keys); free(idx); free(kv); free(nv);
      *res = OBJV(l); return 0;
    }
    case M_TAKEWHILE: case M_DROPWHILE: {
      NEED(1); List *out = new_list(); push(OBJV(out));
      int cut = l->a.n;
      for (int i = 0; i < l->a.n; i++) {
        Value x = l->a.v[i], r; if (!call_fn(a[0], 1, &x, &r)) return 1;
        if (falsey(r)) { cut = i; break; }
        cut = i + 1;
      }
      int x = mid == M_TAKEWHILE ? 0 : cut, y = mid == M_TAKEWHILE ? cut : l->a.n;
      for (int i = x; i < y; i++) varr_push(&out->a, l->a.v[i]);
      vm.sp--; *res = OBJV(out); return 0;
    }
    case M_FLATDEEP: {
      NEEDR(0, 1); int dmax = argc > 0 ? (int)a[0].as.n : -1; if (dmax < 0) dmax = 1 << 30;
      List *out = new_list(); push(OBJV(out));
      // تنفيذ تكراري بكومة أعماق: كل عنصر يُعالج مع عمقه المتبقي
      typedef struct { Value v; int d; } FlatItem;
      int wcap = l->a.n > 0 ? l->a.n : 16;
      FlatItem *work = xalloc(sizeof(FlatItem) * wcap); int wn = 0;
      for (int i = l->a.n - 1; i >= 0; i--) { work[wn].v = l->a.v[i]; work[wn].d = dmax; wn++; }
      while (wn > 0) {
        FlatItem it = work[--wn];
        if (IS_T(it.v, O_LIST) && it.d > 0) {
          List *s = AS_LIST(it.v);
          if (wn + s->a.n > wcap) { wcap = wn + s->a.n; work = xrealloc(work, sizeof(FlatItem) * wcap); }
          for (int j = s->a.n - 1; j >= 0; j--) { work[wn].v = s->a.v[j]; work[wn].d = it.d - 1; wn++; }
        } else varr_push(&out->a, it.v);
      }
      free(work); vm.sp--; *res = OBJV(out); return 0;
    }
    case M_DEDUPCONSEC: {
      NEED(0); List *out = new_list();
      for (int i = 0; i < l->a.n; i++) if (i == 0 || !values_equal(l->a.v[i], l->a.v[i - 1])) varr_push(&out->a, l->a.v[i]);
      *res = OBJV(out); return 0;
    }
    case M_ZIPWITH: {
      NEED(2); if (!IS_T(a[0], O_LIST)) { rt_error("اقترن_مع تتطلب قائمة ثانية"); return 1; }
      List *o = AS_LIST(a[0]); List *out = new_list(); push(OBJV(out));
      int n = l->a.n < o->a.n ? l->a.n : o->a.n;
      for (int i = 0; i < n; i++) {
        Value argv[2] = {l->a.v[i], o->a.v[i]}, r;
        if (!call_fn(a[1], 2, argv, &r)) return 1;
        varr_push(&out->a, r);
      }
      vm.sp--; *res = OBJV(out); return 0;
    }
    case M_FOLDL: {
      NEED(2); Value acc = a[1];
      for (int i = 0; i < l->a.n; i++) { Value args[2] = {acc, l->a.v[i]}; if (!call_fn(a[0], 2, args, &acc)) return 1; }
      *res = acc; return 0;
    }
    case M_SCANL: {
      NEED(2); Value acc = a[1]; List *out = new_list(); push(OBJV(out)); varr_push(&out->a, acc);
      for (int i = 0; i < l->a.n; i++) { Value args[2] = {acc, l->a.v[i]}; if (!call_fn(a[0], 2, args, &acc)) return 1; varr_push(&out->a, acc); }
      vm.sp--; *res = OBJV(out); return 0;
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
    case M_ISEMPTY: { NEED(0); *res = BOOLV(m->d.live == 0); return 0; }
    case M_SETDEFAULT: {
      NEED(2); Value t; if (!dget(&m->d, a[0], &t)) dset(&m->d, a[0], a[1]);
      return dget(&m->d, a[0], &t) ? (*res = t, 0) : 0;
    }
    case M_MERGE2: {
      NEED(1); if (!IS_T(a[0], O_MAP)) { rt_error("دمج_جديد يتطلب قاموسا"); return 1; }
      Map *o = new_map(); for (int i = 0; i < m->d.n; i++) if (m->d.e[i].k.t != V_EMPTY) dset(&o->d, m->d.e[i].k, m->d.e[i].v);
      Map *s = AS_MAP(a[0]); for (int i = 0; i < s->d.n; i++) if (s->d.e[i].k.t != V_EMPTY) dset(&o->d, s->d.e[i].k, s->d.e[i].v);
      *res = OBJV(o); return 0;
    }
    case M_MAPVALS: {
      NEED(1); Map *out = new_map(); push(OBJV(out));
      for (int i = 0; i < m->d.n; i++) {
        if (m->d.e[i].k.t == V_EMPTY) continue;
        Value r; if (!call_fn(a[0], 1, &m->d.e[i].v, &r)) return 1;
        dset(&out->d, m->d.e[i].k, r);
      }
      vm.sp--; *res = OBJV(out); return 0;
    }
    case M_FILTERKEYS: {
      NEED(1); Map *out = new_map(); push(OBJV(out));
      for (int i = 0; i < m->d.n; i++) {
        if (m->d.e[i].k.t == V_EMPTY) continue;
        Value x = m->d.e[i].k, r; if (!call_fn(a[0], 1, &x, &r)) return 1;
        if (!falsey(r)) dset(&out->d, m->d.e[i].k, m->d.e[i].v);
      }
      vm.sp--; *res = OBJV(out); return 0;
    }
    case M_INVERT: {
      NEED(0); Map *out = new_map();
      for (int i = 0; i < m->d.n; i++) if (m->d.e[i].k.t != V_EMPTY) dset(&out->d, m->d.e[i].v, m->d.e[i].k);
      *res = OBJV(out); return 0;
    }
    case M_DEEPGET: {
      NEEDR(1, 32); Value cur = OBJV(m);
      for (int i = 0; i < argc; i++) {
        if (IS_T(cur, O_MAP)) { Value t; if (dget(&AS_MAP(cur)->d, a[i], &t)) cur = t; else { cur = NIL; break; } }
        else if (IS_T(cur, O_LIST)) { Value out; int gi = get_index(cur, a[i], &out); if (gi == 0) cur = out; else { cur = NIL; break; } }
        else { cur = NIL; break; }
      }
      *res = cur; return 0;
    }
    case M_DEEPSET: {
      NEED(2); // اضبط_عميق(مسار_قائمة, قيمة)
      if (!IS_T(a[0], O_LIST)) { rt_error("اضبط_عميق تتطلب قائمة مسار"); return 1; }
      List *path = AS_LIST(a[0]);
      if (path->a.n == 0) { rt_error("اضبط_عميق: المسار فارغ"); return 1; }
      Value cur = OBJV(m);
      for (int i = 0; i < path->a.n - 1; i++) {
        Value nxt;
        if (IS_T(cur, O_MAP)) {
          if (dget(&AS_MAP(cur)->d, path->a.v[i], &nxt)) cur = nxt;
          else { Map *nm = new_map(); nxt = OBJV(nm); dset(&AS_MAP(cur)->d, path->a.v[i], nxt); cur = nxt; }
        }
        else if (IS_T(cur, O_LIST)) {
          if (get_index(cur, path->a.v[i], &nxt)) { rt_error("اضبط_عميق: فهرس قائمة غير صالح في المسار"); return 1; }
        }
        else { rt_error("اضبط_عميق: العنصر الأوسط يجب أن يكون قاموسا أو قائمة"); return 1; }
      }
      if (IS_T(cur, O_MAP)) dset(&AS_MAP(cur)->d, path->a.v[path->a.n - 1], a[1]);
      else if (IS_T(cur, O_LIST)) { if (set_index(cur, path->a.v[path->a.n - 1], a[1])) { rt_error("اضبط_عميق: فهرس قائمة غير صالح"); return 1; } }
      else { rt_error("اضبط_عميق: العنصر الأخير يجب أن يكون قاموسا أو قائمة"); return 1; }
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

// طرق الأرقام المدمجة (تُستدعى كـ 5.عدد_الى_حروف())
static int num_method(Value r, Str *nm, int argc, Value *a, Value *res) {
  if (!IS_NUM(r)) return 2;
  Value idv; if (!dget(&vm.mid, OBJV(nm), &idv)) return 2; int mid = (int)idv.as.n;
  double x = r.as.n;
  switch (mid) {
    case M_NUMTOWORDS: {
      if (argc != 0) { rt_error("عدد_الى_حروف لا تأخذ وسائط"); return 1; }
      if (x < 0 || x != floor(x) || x >= 1000000) { rt_error("عدد_الى_حروف تتطلب رقما صحيحا من 0 إلى 999999"); return 1; }
      static const char *ones[] = {"صفر","واحد","اثنان","ثلاثة","أربعة","خمسة","ستة","سبعة","ثمانية","تسعة","عشرة","أحد عشر","اثنا عشر","ثلاثة عشر","أربعة عشر","خمسة عشر","ستة عشر","سبعة عشر","ثمانية عشر","تسعة عشر"};
      static const char *tens[] = {"","","عشرون","ثلاثون","أربعون","خمسون","ستون","سبعون","ثمانون","تسعون"};
      static const char *hundreds[] = {"","مئة","مئتان","ثلاثمئة","أربعمئة","خمسمئة","ستمئة","سبعمئة","ثمانمئة","تسعمئة"};
      int n = (int)x; Buf b; buf_init(&b);
      if (n < 20) buf_fmt(&b, "%s", ones[n]);
      else if (n < 100) { int t = n / 10, o = n % 10; if (o) buf_fmt(&b, "%s و%s", ones[o], tens[t]); else buf_fmt(&b, "%s", tens[t]); }
      else {
        int h = n / 100, rr = n % 100;
        buf_fmt(&b, "%s", hundreds[h]);
        if (rr) { if (rr < 20) buf_fmt(&b, " و%s", ones[rr]); else { int t = rr / 10, o = rr % 10; if (o) buf_fmt(&b, " و%s و%s", ones[o], tens[t]); else buf_fmt(&b, " و%s", tens[t]); } }
      }
      *res = mkstr(buf_cstr(&b), b.n); free(b.p); return 0;
    }
    case M_ISARABIC: { rt_error("هل_عربي طريقة نصوص فقط"); return 1; }
    case M_LEN: *res = NUMV(x == floor(x) && fabs(x) < 1e15 ? (double)(int)x : x); return 0;
    case M_ABS: *res = NUMV(x < 0 ? -x : x); return 0;
    case M_FLOORC: *res = NUMV(floor(x)); return 0;
    case M_CEILC: *res = NUMV(ceil(x)); return 0;
    case M_ROUNDC: *res = NUMV(round(x)); return 0;
    case M_SQRTC: if (x < 0) { rt_error("الجذر التربيعي لرقم سالب"); return 1; } *res = NUMV(sqrt(x)); return 0;
    case M_ISINTC: *res = BOOLV(x == floor(x) && fabs(x) < 9007199254740992.0); return 0;
    case M_ISNANC: *res = BOOLV(x != x); return 0;
    case M_ISFINTC: { double inf = x == (1.0/0.0) || x == -(1.0/0.0); *res = BOOLV(inf || x != x); return 0; }
    case M_TRUNC: *res = NUMV(trunc(x)); return 0;
    case M_TODIGITS: { // تحويل الرقم إلى أرقام عربية-هندية
      char tmp[64]; char ar[192]; int an = 0;
      snprintf(tmp, sizeof tmp, "%g", x);
      for (int i = 0; tmp[i]; i++) {
        char c = tmp[i];
        if (c >= '0' && c <= '9') { // U+0660..U+0669 = 0xD9 0xA0..0xA9
          ar[an++] = (char)0xD9; ar[an++] = (char)(0xA0 + (c - '0'));
        } else if (c == '.') { ar[an++] = (char)0xD9; ar[an++] = (char)0xAB; } // فاصلة عربية U+066B
        else if (c == '-') { ar[an++] = (char)0xD8; ar[an++] = (char)0x80; }  // سالب U+2212... استخدم الشرطة العربية U+066C؟ نستخدم '-'
        else ar[an++] = c;
      }
      *res = mkstr(ar, an); return 0;
    }
    default: return 2;
  }
}
