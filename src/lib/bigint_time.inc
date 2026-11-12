typedef struct { int neg, n; uint32_t *d; } Big;
static void big_trim(Big *b) { while (b->n > 0 && b->d[b->n - 1] == 0) b->n--; if (b->n == 0) b->neg = 0; }
static Big big_new(int n) { Big b; b.neg = 0; b.n = n > 0 ? n : 0; b.d = xalloc(sizeof(uint32_t) * (n > 0 ? n : 1)); memset(b.d, 0, sizeof(uint32_t) * (n > 0 ? n : 1)); return b; }
static Big big_copy(const Big *a) { Big r = big_new(a->n); r.neg = a->neg; if (a->n) memcpy(r.d, a->d, sizeof(uint32_t) * a->n); return r; }
static bool big_parse(Value v, Big *b) {
  char tmp[512]; const char *s; int len;
  if (IS_NUM(v)) { int sl = snprintf(tmp, sizeof tmp, "%.0f", v.as.n); if (sl < 0 || sl >= (int)sizeof tmp) return false; s = tmp; len = sl; }
  else if (IS_T(v, O_STR)) { s = AS_STR(v)->chars; len = AS_STR(v)->len; } else return false;
  int neg = 0, i = 0;
  if (len && (s[0] == '-' || s[0] == '+')) { neg = s[0] == '-'; i = 1; }
  for (int k = i; k < len; k++) if (!isdigit((unsigned char)s[k])) return false;
  if (len - (i ? 1 : 0) < 1) return false;
  while (i < len && s[i] == '0') i++;
  int nd = len - i;
  if (nd <= 0) { *b = big_new(0); return true; }
  Big r = big_new(nd / 9 + 2); r.n = 0;
  int e = i + ((nd - 1) % 9 + 1), pos = i;
  for (;;) {
    uint32_t x = 0;
    for (int k = pos; k < e; k++) x = x * 10 + (uint32_t)(s[k] - '0');
    uint64_t c = x;
    for (int k = 0; k < r.n; k++) { uint64_t cur = (uint64_t)r.d[k] * 1000000000u + c; r.d[k] = (uint32_t)cur; c = cur >> 32; }
    r.d[r.n++] = (uint32_t)c;
    if (e >= len) break;
    pos = e; e += 9; if (e > len) e = len;
  }
  r.neg = neg; big_trim(&r); *b = r; return true;
}
static Value big_val(Big *b) {
  if (b->n == 0) return mkstr("0", 1);
  int n = b->n, rc = 0, rcap = (int)(n * 32 / 29) + 2;
  uint32_t *w = xalloc(sizeof(uint32_t) * n), *rem = xalloc(sizeof(uint32_t) * rcap);
  memcpy(w, b->d, sizeof(uint32_t) * n);
  while (n > 0) {
    uint64_t r = 0;
    for (int i = n - 1; i >= 0; i--) { uint64_t cur = (r << 32) | w[i]; w[i] = (uint32_t)(cur / 1000000000u); r = cur % 1000000000u; }
    rem[rc++] = (uint32_t)r;
    while (n > 0 && w[n - 1] == 0) n--;
  }
  free(w);
  Buf o; buf_init(&o); if (b->neg) buf_addc(&o, '-');
  char t[16]; snprintf(t, sizeof t, "%u", rem[rc - 1]); buf_add(&o, t, (int)strlen(t));
  for (int i = rc - 2; i >= 0; i--) { snprintf(t, sizeof t, "%09u", rem[i]); buf_add(&o, t, 9); }
  free(rem);
  Value v = mkstr(buf_cstr(&o), o.n); free(o.p); return v;
}
static int bmag_cmp(const Big *a, const Big *b) { if (a->n != b->n) return a->n < b->n ? -1 : 1; for (int i = a->n - 1; i >= 0; i--) if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1; return 0; }
static Big bmag_add(const Big *a, const Big *b) {
  int n = (a->n > b->n ? a->n : b->n) + 1; Big r = big_new(n); uint64_t c = 0;
  for (int i = 0; i < n; i++) { uint64_t s = c + (i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0); r.d[i] = (uint32_t)s; c = s >> 32; }
  big_trim(&r); return r;
}
static Big bmag_sub(const Big *a, const Big *b) {
  Big r = big_new(a->n); int64_t c = 0;
  for (int i = 0; i < a->n; i++) { int64_t s = (int64_t)a->d[i] - c - (i < b->n ? b->d[i] : 0); if (s < 0) { s += (int64_t)1 << 32; c = 1; } else c = 0; r.d[i] = (uint32_t)s; }
  big_trim(&r); return r;
}
static Big bmag_mul(const Big *a, const Big *b) {
  if (!a->n || !b->n) return big_new(0);
  Big r = big_new(a->n + b->n);
  for (int i = 0; i < a->n; i++) {
    uint64_t c = 0, av = a->d[i];
    for (int j = 0; j < b->n; j++) {
#if defined(__SIZEOF_INT128__)
      unsigned __int128 cur = (unsigned __int128)r.d[i + j] + av * b->d[j] + c;
      r.d[i + j] = (uint32_t)cur; c = (uint64_t)(cur >> 32);
#else
      uint64_t p = av * b->d[j] + c;
      uint64_t t = (uint64_t)r.d[i + j] + (uint32_t)p;
      r.d[i + j] = (uint32_t)t; c = (p >> 32) + (t >> 32);
#endif
    }
    r.d[i + b->n] = (uint32_t)c;
  }
  big_trim(&r); return r;
}
static Big bmag_mulsmall(const Big *a, uint32_t m) {
  Big r = big_new(a->n + 1); uint64_t c = 0;
  for (int i = 0; i < a->n; i++) { uint64_t cur = (uint64_t)a->d[i] * m + c; r.d[i] = (uint32_t)cur; c = cur >> 32; }
  r.d[a->n] = (uint32_t)c; big_trim(&r); return r;
}
static Big big_shl(const Big *a, int s) {
  Big r = big_new(a->n + 1);
  if (s == 0) { if (a->n) memcpy(r.d, a->d, sizeof(uint32_t) * a->n); }
  else for (int i = 0; i < a->n; i++) r.d[i] = (uint32_t)(((uint64_t)a->d[i] << s) | (i ? (a->d[i - 1] >> (32 - s)) : 0));
  if (s && a->n) r.d[a->n] = a->d[a->n - 1] >> (32 - s);
  return r;
}
static void bmag_divmod(const Big *a, const Big *b, Big *q, Big *r) {
  if (b->n == 0) { *q = big_new(0); *r = big_copy(a); return; }
  if (bmag_cmp(a, b) < 0) { *q = big_new(0); *r = big_copy(a); return; }
  if (b->n == 1) {
    uint32_t d = b->d[0]; Big qq = big_new(a->n); uint64_t rem = 0;
    for (int i = a->n - 1; i >= 0; i--) { uint64_t cur = (rem << 32) | a->d[i]; qq.d[i] = (uint32_t)(cur / d); rem = cur % d; }
    big_trim(&qq); *r = big_new(1); r->d[0] = (uint32_t)rem; big_trim(r); *q = qq; return;
  }
  int s = __builtin_clz(b->d[b->n - 1]);
  Big u = big_shl(a, s), v = big_shl(b, s);
  big_trim(&v);
  int m = a->n - b->n;
  Big qq = big_new(m + 1);
  uint32_t vn1 = v.d[v.n - 1], vn2 = v.n >= 2 ? v.d[v.n - 2] : 0;
  for (int j = m; j >= 0; j--) {
    uint64_t num = ((uint64_t)u.d[j + v.n] << 32) | u.d[j + v.n - 1];
    uint64_t qhat = num / vn1, rhat = num % vn1;
    uint32_t un2 = j + v.n >= 2 ? u.d[j + v.n - 2] : 0;
    while (qhat >= ((uint64_t)1 << 32) || qhat * vn2 > (((uint64_t)rhat << 32) | un2)) {
      qhat--; rhat += vn1;
      if (rhat >= ((uint64_t)1 << 32)) break;
    }
    int64_t k = 0;
    for (int i = 0; i < v.n; i++) {
      uint64_t p = qhat * v.d[i];
      int64_t t = (int64_t)u.d[i + j] - k - (int64_t)(p & 0xFFFFFFFFull);
      u.d[i + j] = (uint32_t)t;
      k = (int64_t)(p >> 32) - (t >> 32);
    }
    int64_t t = (int64_t)u.d[j + v.n] - k;
    u.d[j + v.n] = (uint32_t)t;
    if (t >> 32) {
      qhat--;
      uint64_t k2 = 0;
      for (int i = 0; i < v.n; i++) { uint64_t t2 = (uint64_t)u.d[i + j] + v.d[i] + k2; u.d[i + j] = (uint32_t)t2; k2 = t2 >> 32; }
      u.d[j + v.n] = (uint32_t)((uint64_t)u.d[j + v.n] + k2);
    }
    qq.d[j] = (uint32_t)qhat;
  }
  Big rr = big_new(v.n);
  for (int i = 0; i < v.n; i++) { uint64_t hi = u.d[i + 1]; rr.d[i] = (uint32_t)(((u.d[i] >> s) | (hi << (32 - s))) & 0xFFFFFFFFull); }
  big_trim(&rr);
  big_trim(&qq);
  free(u.d); free(v.d);
  *q = qq; *r = rr;
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
NAT(bg_num) { Big x; if (!big_parse(a[0], &x)) FAIL("ليست عددا صحيحا"); double d = 0; for (int i = x.n - 1; i >= 0; i--) d = d * 4294967296.0 + x.d[i]; if (x.neg) d = -d; *res = NUMV(d); free(x.d); return true; }
static Big mont_mul(const Big *a, const Big *b, const Big *n, uint32_t ninv, int L) {
  uint32_t *t = xalloc(sizeof(uint32_t) * (L + 2));
  memset(t, 0, sizeof(uint32_t) * (L + 2));
  for (int i = 0; i < L; i++) {
    uint64_t c = 0;
    uint32_t av = i < a->n ? a->d[i] : 0;
    if (av) {
      for (int j = 0; j < L; j++) {
        uint32_t bv = j < b->n ? b->d[j] : 0;
#if defined(__SIZEOF_INT128__)
        unsigned __int128 cur = (unsigned __int128)t[j] + (uint64_t)av * bv + c;
        t[j] = (uint32_t)cur; c = (uint64_t)(cur >> 32);
#else
        uint64_t p = (uint64_t)av * bv + c;
        uint64_t t2 = (uint64_t)t[j] + (uint32_t)p;
        t[j] = (uint32_t)t2; c = (p >> 32) + (t2 >> 32);
#endif
      }
    }
#if defined(__SIZEOF_INT128__)
    unsigned __int128 cur = (unsigned __int128)t[L] + c;
    t[L] = (uint32_t)cur; t[L + 1] = (uint32_t)(cur >> 32);
#else
    uint64_t t2 = (uint64_t)t[L] + (uint32_t)c;
    t[L] = (uint32_t)t2; t[L + 1] = (uint32_t)(t2 >> 32) + (uint32_t)(c >> 32);
#endif
    uint32_t m = t[0] * ninv;
    uint64_t c2 = ((uint64_t)m * n->d[0] + t[0]) >> 32;
    for (int j = 1; j < L; j++) {
#if defined(__SIZEOF_INT128__)
      unsigned __int128 cur = (unsigned __int128)t[j] + (uint64_t)m * n->d[j] + c2;
      t[j - 1] = (uint32_t)cur; c2 = (uint64_t)(cur >> 32);
#else
      uint64_t p = (uint64_t)m * n->d[j] + c2;
      uint64_t t2 = (uint64_t)t[j] + (uint32_t)p;
      t[j - 1] = (uint32_t)t2; c2 = (p >> 32) + (t2 >> 32);
#endif
    }
#if defined(__SIZEOF_INT128__)
    unsigned __int128 cur2 = (unsigned __int128)t[L] + c2;
    t[L - 1] = (uint32_t)cur2;
    t[L] = t[L + 1] + (uint32_t)(cur2 >> 32);
#else
    uint64_t t3 = (uint64_t)t[L] + (uint32_t)c2;
    t[L - 1] = (uint32_t)t3;
    t[L] = t[L + 1] + (uint32_t)(t3 >> 32) + (uint32_t)(c2 >> 32);
#endif
  }
  int sub = t[L] != 0;
  if (!sub) for (int i = L - 1; i >= 0; i--) { if (t[i] != n->d[i]) { sub = t[i] > n->d[i]; break; } }
  Big out = big_new(L);
  if (sub) {
    int64_t k = 0;
    for (int i = 0; i < L; i++) { int64_t v = (int64_t)t[i] - n->d[i] - k; if (v < 0) { v += (int64_t)1 << 32; k = 1; } else k = 0; out.d[i] = (uint32_t)v; }
  } else if (L) memcpy(out.d, t, sizeof(uint32_t) * L);
  free(t);
  big_trim(&out); return out;
}
static Big big_modpow(const Big *base, const Big *exp, const Big *mod) {
  if (mod->n == 1 && mod->d[0] == 1) return big_new(0);
  Big bm, bq;
  bmag_divmod(base, mod, &bq, &bm); free(bq.d);
  Big acc;
  if (mod->d[0] & 1) {
    int L = mod->n;
    Big R2 = big_new(2 * L + 1); R2.n = 2 * L + 1; R2.d[2 * L] = 1;
    Big q2, r2; bmag_divmod(&R2, mod, &q2, &r2); free(q2.d); free(R2.d);
    uint32_t n0 = mod->d[0], inv = 1;
    for (int i = 0; i < 5; i++) inv *= 2u - n0 * inv;
    uint32_t ninv = 0u - inv;
    Big one = big_new(1); one.d[0] = 1; one.n = 1;
    acc = mont_mul(&one, &r2, mod, ninv, L);
    Big arep = mont_mul(&bm, &r2, mod, ninv, L);
    free(r2.d);
    int started = 0;
    for (int i = exp->n - 1; i >= 0; i--) {
      uint32_t w = exp->d[i];
      for (int k = 31; k >= 0; k--) {
        int bit = (int)((w >> k) & 1);
        if (!started) { if (bit) started = 1; else continue; }
        Big t = mont_mul(&acc, &acc, mod, ninv, L); free(acc.d); acc = t;
        if (bit) { Big t2 = mont_mul(&acc, &arep, mod, ninv, L); free(acc.d); acc = t2; }
      }
    }
    free(arep.d);
    Big t = mont_mul(&acc, &one, mod, ninv, L);
    free(acc.d); free(one.d);
    acc = t;
  } else {
    acc = big_new(1); acc.d[0] = 1; acc.n = 1;
    int started = 0;
    for (int i = exp->n - 1; i >= 0; i--) {
      uint32_t w = exp->d[i];
      for (int k = 31; k >= 0; k--) {
        int bit = (int)((w >> k) & 1);
        if (!started) { if (bit) started = 1; else continue; }
        Big t = bmag_mul(&acc, &acc); Big q3, r3; bmag_divmod(&t, mod, &q3, &r3); free(t.d); free(q3.d); free(acc.d); acc = r3;
        if (bit) { Big t2 = bmag_mul(&acc, &bm); Big q4, r4; bmag_divmod(&t2, mod, &q4, &r4); free(t2.d); free(q4.d); free(acc.d); acc = r4; }
      }
    }
  }
  free(bm.d);
  if (base->neg && acc.n && (exp->d[0] & 1)) { Big t = bmag_sub(mod, &acc); free(acc.d); acc = t; }
  return acc;
}
static bool big_from_hex(const char *s, int len, Big *b) {
  int i = 0, neg = 0;
  if (len && (s[0] == '-' || s[0] == '+')) { neg = s[0] == '-'; i = 1; }
  while (i < len && s[i] == '0') i++;
  for (int k = i; k < len; k++) if (hexv(s[k]) < 0) return false;
  int nd = len - i;
  if (nd <= 0) { *b = big_new(0); return true; }
  Big r = big_new(nd / 8 + 1);
  int pos = 0;
  for (int e = len; e > i; ) {
    int st = e - 8 > i ? e - 8 : i;
    uint32_t x = 0;
    for (int k = st; k < e; k++) x = (x << 4) | (uint32_t)hexv(s[k]);
    r.d[pos++] = x;
    e = st;
  }
  r.n = pos; r.neg = neg; big_trim(&r); *b = r; return true;
}
static Value big_hex_val(Big *b, long minlen) {
  if (minlen < 1) minlen = 1; if (minlen > (1 << 20)) minlen = 1 << 20;
  Buf o; buf_init(&o);
  int total = b->n > 0 ? (32 - __builtin_clz(b->d[b->n - 1]) + 3) / 4 + 8 * (b->n - 1) : 1;
  for (int i = total; i < minlen; i++) buf_addc(&o, '0');
  if (b->n == 0) buf_addc(&o, '0');
  else {
    char t[16];
    snprintf(t, sizeof t, "%x", b->d[b->n - 1]); buf_add(&o, t, (int)strlen(t));
    for (int i = b->n - 2; i >= 0; i--) { snprintf(t, sizeof t, "%08x", b->d[i]); buf_add(&o, t, 8); }
  }
  if (b->neg) {
    Buf o2; buf_init(&o2); buf_addc(&o2, '-'); buf_add(&o2, o.p, o.n); free(o.p);
    Value v = mkstr(buf_cstr(&o2), o2.n); free(o2.p); return v;
  }
  Value v = mkstr(buf_cstr(&o), o.n); free(o.p); return v;
}
static void big_from_bytes(const char *s, int len, Big *b) {
  int st = 0; while (st < len && (unsigned char)s[st] == 0) st++;
  int nd = len - st;
  if (nd <= 0) { *b = big_new(0); return; }
  Big r = big_new(nd / 4 + 1);
  int pos = 0;
  for (int e = len; e > st; ) {
    int k0 = e - 4 > st ? e - 4 : st;
    uint32_t x = 0;
    for (int k = k0; k < e; k++) x = (x << 8) | (uint32_t)(unsigned char)s[k];
    r.d[pos++] = x;
    e = k0;
  }
  r.n = pos; big_trim(&r); *b = r;
}
static Value big_bytes_val(Big *b, long minlen) {
  if (minlen < 0) minlen = 0; if (minlen > (1 << 20)) minlen = 1 << 20;
  int total = 0;
  if (b->n > 0) { int hi = b->n - 1; total = hi * 4 + 4 - __builtin_clz(b->d[hi]) / 8; }
  if (minlen > total) total = (int)minlen;
  char *p = xalloc(total > 0 ? total : 1); memset(p, 0, total > 0 ? total : 1);
  for (int i = 0; i < b->n; i++) {
    uint32_t x = b->d[i];
    for (int k = 0; k < 4; k++) {
      int pos = total - 1 - (i * 4 + k);
      if (pos < 0) break;
      p[pos] = (char)((x >> (8 * k)) & 0xFF);
    }
  }
  Value v = mkstr(p, total); free(p); return v;
}
static bool big_arg(Value v, Big *b) { if (IS_T(v, O_STR)) return big_from_hex(AS_STR(v)->chars, AS_STR(v)->len, b); return big_parse(v, b); }
NAT(bg_powmod) {
  Big b, e, m;
  if (!big_arg(a[0], &b)) FAIL("الأساس غير صالح (هكس أو رقم)");
  if (!big_arg(a[1], &e)) { free(b.d); FAIL("الأس غير صالح (هكس أو رقم)"); }
  if (!big_arg(a[2], &m)) { free(b.d); free(e.d); FAIL("المعامل غير صالح (هكس أو رقم)"); }
  if (e.neg) { free(b.d); free(e.d); free(m.d); FAIL("الأس لا يكون سالبا"); }
  if (m.neg || m.n == 0) { free(b.d); free(e.d); free(m.d); FAIL("المعامل يجب أن يكون موجبا غير صفري"); }
  Big r = big_modpow(&b, &e, &m);
  *res = big_hex_val(&r, 1);
  free(r.d); free(b.d); free(e.d); free(m.d); return true;
}
NAT(bg_from_hex) {
  STRA(0); Big b;
  if (!big_from_hex(AS_STR(a[0])->chars, AS_STR(a[0])->len, &b)) FAIL("نص هكس غير صالح");
  *res = big_val(&b); free(b.d); return true;
}
NAT(bg_to_hex) {
  Big b;
  if (!big_parse(a[0], &b)) FAIL("القيمة ليست عددا صحيحا");
  long minlen = argc > 1 && IS_NUM(a[1]) ? (long)a[1].as.n : 1;
  *res = big_hex_val(&b, minlen); free(b.d); return true;
}
NAT(bg_from_bytes) {
  STRA(0); Big b;
  big_from_bytes(AS_STR(a[0])->chars, AS_STR(a[0])->len, &b);
  *res = big_val(&b); free(b.d); return true;
}
NAT(bg_to_bytes) {
  Big b;
  if (!big_parse(a[0], &b)) FAIL("القيمة ليست عددا صحيحا");
  if (b.neg) { free(b.d); FAIL("إلى_بايتات لا تقبل قيمة سالبة"); }
  long minlen = argc > 1 && IS_NUM(a[1]) ? (long)a[1].as.n : 0;
  *res = big_bytes_val(&b, minlen); free(b.d); return true;
}

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
