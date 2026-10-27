typedef struct { int neg, n; uint32_t *d; } Big;
static void big_trim(Big *b) { while (b->n > 0 && b->d[b->n - 1] == 0) b->n--; if (b->n == 0) b->neg = 0; }
static Big big_new(int n) { Big b; b.neg = 0; b.n = n; b.d = xalloc(sizeof(uint32_t) * (n ? n : 1)); memset(b.d, 0, sizeof(uint32_t) * (n ? n : 1)); return b; }
static bool big_parse(Value v, Big *b) {
  char tmp[64]; const char *s; int len;
  if (IS_NUM(v)) { snprintf(tmp, sizeof tmp, "%.0f", v.as.n); s = tmp; len = (int)strlen(tmp); }
  else if (IS_T(v, O_STR)) { s = AS_STR(v)->chars; len = AS_STR(v)->len; } else return false;
  int neg = 0, i = 0; if (len && (s[0] == '-' || s[0] == '+')) { neg = s[0] == '-'; i = 1; }
  while (i < len && s[i] == '0') i++;
  int nd = len - i; for (int k = i; k < len; k++) if (!isdigit((unsigned char)s[k])) return false;
  if (len - (neg || s[0] == '+' ? 1 : 0) == 0) return false;
  *b = big_new((nd + 8) / 9); int pos = 0;
  for (int e = len; e > i; e -= 9) { int st = e - 9 < i ? i : e - 9; uint32_t x = 0; for (int k = st; k < e; k++) x = x * 10 + (s[k] - '0'); b->d[pos++] = x; }
  b->n = pos; b->neg = neg; big_trim(b); return true;
}
static Value big_val(Big *b) {
  Buf o; buf_init(&o); if (b->n == 0) buf_addc(&o, '0'); else { if (b->neg) buf_addc(&o, '-'); buf_fmt(&o, "%u", b->d[b->n - 1]); for (int i = b->n - 2; i >= 0; i--) buf_fmt(&o, "%09u", b->d[i]); }
  Value v = mkstr(buf_cstr(&o), o.n); free(o.p); return v;
}
static int bmag_cmp(const Big *a, const Big *b) { if (a->n != b->n) return a->n < b->n ? -1 : 1; for (int i = a->n - 1; i >= 0; i--) if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1; return 0; }
static Big bmag_add(const Big *a, const Big *b) {
  int n = (a->n > b->n ? a->n : b->n) + 1; Big r = big_new(n); uint64_t c = 0;
  for (int i = 0; i < n; i++) { uint64_t s = c + (i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0); r.d[i] = (uint32_t)(s % BBASE); c = s / BBASE; }
  big_trim(&r); return r;
}
static Big bmag_sub(const Big *a, const Big *b) {
  Big r = big_new(a->n); int64_t c = 0;
  for (int i = 0; i < a->n; i++) { int64_t s = (int64_t)a->d[i] - c - (i < b->n ? b->d[i] : 0); if (s < 0) { s += BBASE; c = 1; } else c = 0; r.d[i] = (uint32_t)s; }
  big_trim(&r); return r;
}
static Big bmag_mul(const Big *a, const Big *b) {
  if (!a->n || !b->n) return big_new(0);
  Big r = big_new(a->n + b->n);
  for (int i = 0; i < a->n; i++) { uint64_t c = 0; for (int j = 0; j < b->n || c; j++) { uint64_t cur = r.d[i + j] + c + (j < b->n ? (uint64_t)a->d[i] * b->d[j] : 0); r.d[i + j] = (uint32_t)(cur % BBASE); c = cur / BBASE; } }
  big_trim(&r); return r;
}
static Big bmag_mulsmall(const Big *a, uint32_t m) {
  Big r = big_new(a->n + 1); uint64_t c = 0;
  for (int i = 0; i < a->n; i++) { uint64_t cur = (uint64_t)a->d[i] * m + c; r.d[i] = (uint32_t)(cur % BBASE); c = cur / BBASE; }
  r.d[a->n] = (uint32_t)c; big_trim(&r); return r;
}
static void bmag_divmod(const Big *a, const Big *b, Big *q, Big *r) {
  *q = big_new(a->n); *r = big_new(0);
  for (int i = a->n - 1; i >= 0; i--) {
    r->d = xrealloc(r->d, sizeof(uint32_t) * (r->n + 2)); memmove(r->d + 1, r->d, sizeof(uint32_t) * r->n); r->d[0] = a->d[i]; r->n++; big_trim(r);
    uint32_t lo = 0, hi = BBASE - 1;
    while (lo < hi) { uint32_t mid = lo + (hi - lo + 1) / 2; Big t = bmag_mulsmall(b, mid); int c = bmag_cmp(&t, r); free(t.d); if (c <= 0) lo = mid; else hi = mid - 1; }
    q->d[i] = lo;
    if (lo) { Big t = bmag_mulsmall(b, lo); Big nr = bmag_sub(r, &t); free(t.d); free(r->d); *r = nr; }
  }
  big_trim(q); big_trim(r);
}
static Big big_add(const Big *a, const Big *b, int subtract) {
  int bn = subtract ? !b->neg : b->neg; Big r;
  if (a->neg == bn) { r = bmag_add(a, b); r.neg = a->neg; }
  else { int c = bmag_cmp(a, b); if (c == 0) return big_new(0); if (c > 0) { r = bmag_sub(a, b); r.neg = a->neg; } else { r = bmag_sub(b, a); r.neg = bn; } }
  big_trim(&r); return r;
}
#define BIG2(fn_body) \
  Big x, y; if (!big_parse(a[0], &x)) FAIL("القيمة الأولى ليست عددا صحيحا"); if (!big_parse(a[1], &y)) { free(x.d); FAIL("القيمة الثانية ليست عددا صحيحا"); } \
  fn_body
NAT(bg_add) { BIG2(Big r = big_add(&x, &y, 0); *res = big_val(&r); free(r.d); free(x.d); free(y.d); return true;) }
NAT(bg_sub) { BIG2(Big r = big_add(&x, &y, 1); *res = big_val(&r); free(r.d); free(x.d); free(y.d); return true;) }
NAT(bg_mul) { BIG2(Big r = bmag_mul(&x, &y); r.neg = r.n ? x.neg ^ y.neg : 0; *res = big_val(&r); free(r.d); free(x.d); free(y.d); return true;) }
static bool bg_div(Value *a, Value *res, bool wantq) {
  Big x, y; if (!big_parse(a[0], &x)) FAIL("القيمة الأولى ليست عددا صحيحا"); if (!big_parse(a[1], &y)) { free(x.d); FAIL("القيمة الثانية ليست عددا صحيحا"); }
  if (y.n == 0) { free(x.d); free(y.d); FAIL("القسمة على صفر"); }
  Big q, r; bmag_divmod(&x, &y, &q, &r); q.neg = q.n ? x.neg ^ y.neg : 0; r.neg = r.n ? x.neg : 0;
  *res = big_val(wantq ? &q : &r); free(q.d); free(r.d); free(x.d); free(y.d); return true;
}
NAT(bg_div_) { return bg_div(a, res, true); }
NAT(bg_mod_) { return bg_div(a, res, false); }
NAT(bg_pow) {
  Big base; if (!big_parse(a[0], &base)) FAIL("القاعدة ليست عددا صحيحا"); NUMA(1); long e = (long)a[1].as.n; if (e < 0 || e > 100000) { free(base.d); FAIL("الأس خارج النطاق"); }
  Big r = big_new(1); r.d[0] = 1; r.n = 1; int neg = base.neg && (e & 1); base.neg = 0;
  while (e) { if (e & 1) { Big t = bmag_mul(&r, &base); free(r.d); r = t; } e >>= 1; if (e) { Big t = bmag_mul(&base, &base); free(base.d); base = t; } }
  r.neg = r.n ? neg : 0; *res = big_val(&r); free(r.d); free(base.d); return true;
}
NAT(bg_cmp) { BIG2(int c; if (x.neg != y.neg) c = x.neg ? -1 : 1; else { c = bmag_cmp(&x, &y); if (x.neg) c = -c; } *res = NUMV(c); free(x.d); free(y.d); return true;) }
NAT(bg_fact) {
  NUMA(0); long n = (long)a[0].as.n; if (n < 0 || n > 20000) FAIL("مضروب: خارج النطاق");
  Big r = big_new(1); r.d[0] = 1; r.n = 1;
  for (long i = 2; i <= n; i++) { Big t = bmag_mulsmall(&r, (uint32_t)i); free(r.d); r = t; }
  *res = big_val(&r); free(r.d); return true;
}
NAT(bg_num) { Big x; if (!big_parse(a[0], &x)) FAIL("ليست عددا صحيحا"); double d = 0; for (int i = x.n - 1; i >= 0; i--) d = d * BBASE + x.d[i]; if (x.neg) d = -d; *res = NUMV(d); free(x.d); return true; }

NAT(t_date) {
  time_t t = argc > 0 && IS_NUM(a[0]) ? (time_t)a[0].as.n : (time_t)now_sec(); bool utc = argc > 1 && !falsey(a[1]); struct tm tmv;
  if (utc) gmtime_r(&t, &tmv); else localtime_r(&t, &tmv);
  Map *m = new_map();
  dset(&m->d, KX("سنة"), NUMV(tmv.tm_year + 1900)); dset(&m->d, KN("شهر"), NUMV(tmv.tm_mon + 1)); dset(&m->d, KN("يوم"), NUMV(tmv.tm_mday));
  dset(&m->d, KX("ساعة"), NUMV(tmv.tm_hour)); dset(&m->d, KX("دقيقة"), NUMV(tmv.tm_min)); dset(&m->d, KX("ثانية"), NUMV(tmv.tm_sec));
  dset(&m->d, KN("اسبوع"), NUMV(tmv.tm_wday)); dset(&m->d, KX("يوم_السنة"), NUMV(tmv.tm_yday + 1)); *res = OBJV(m); return true;
}
static int dget_int(Dict *d, const char *k, int def) { Value v; if (mget(d, KDYN(k), &v) && IS_NUM(v)) return (int)v.as.n; return def; }
NAT(t_mk) {
  if (!IS_T(a[0], O_MAP)) FAIL("من_تاريخ تتطلب قاموسا"); Dict *d = &AS_MAP(a[0])->d; struct tm tmv; memset(&tmv, 0, sizeof tmv);
  tmv.tm_year = dget_int(d, "سنة", 1970) - 1900; tmv.tm_mon = dget_int(d, "شهر", 1) - 1; tmv.tm_mday = dget_int(d, "يوم", 1);
  tmv.tm_hour = dget_int(d, "ساعة", 0); tmv.tm_min = dget_int(d, "دقيقة", 0); tmv.tm_sec = dget_int(d, "ثانية", 0); tmv.tm_isdst = -1;
  *res = NUMV((double)(argc > 1 && !falsey(a[1]) ? timegm(&tmv) : mktime(&tmv))); return true;
}
NAT(t_parse) {
  STRA(0); STRA(1); struct tm tmv; memset(&tmv, 0, sizeof tmv); tmv.tm_isdst = -1;
  const char *e = solan_strptime(AS_STR(a[0])->chars, AS_STR(a[1])->chars, &tmv); if (!e) { *res = NIL; return true; }
  *res = NUMV((double)(argc > 2 && !falsey(a[2]) ? timegm(&tmv) : mktime(&tmv))); return true;
}
