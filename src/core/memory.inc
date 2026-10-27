static void *xalloc(size_t n) {
  void *p = malloc(n ? n : 1);
  if (!p) { fputs("سولان: نفدت الذاكرة\n", stderr); exit(1); }
  return p;
}
static void *xrealloc(void *p, size_t n) {
  p = realloc(p, n ? n : 1);
  if (!p) { fputs("سولان: نفدت الذاكرة\n", stderr); exit(1); }
  return p;
}

#define POOL_MAX 256
#define POOL_CHUNK (64 * 1024)
static void *g_pool_fl[POOL_MAX / 16 + 1];
static void *pool_alloc(size_t n) {
#ifdef SOLAN_NOPOOL
  return xalloc(n);
#endif
  if (n > POOL_MAX) return xalloc(n);
  size_t c = (n + 15) >> 4; void *p = g_pool_fl[c];
  if (p) { g_pool_fl[c] = *(void **)p; return p; }
  size_t sz = c << 4, cnt = POOL_CHUNK / sz; char *blk = (char *)xalloc(cnt * sz);
  for (size_t i = 1; i < cnt; i++) { *(void **)(blk + i * sz) = g_pool_fl[c]; g_pool_fl[c] = blk + i * sz; }
  return blk;
}
static void pool_free(void *p, size_t n) {
#ifdef SOLAN_NOPOOL
  (void)n; free(p); return;
#endif
  if (n > POOL_MAX) { free(p); return; }
  size_t c = (n + 15) >> 4; *(void **)p = g_pool_fl[c]; g_pool_fl[c] = p;
}
static Obj *newobj(size_t sz, OT t) {
  Obj *o = (Obj *)pool_alloc(sz);
  o->t = t; o->mark = false; o->next = vm.objs; vm.objs = o;
  if (++vm.nobj >= vm.next_gc) vm.gc_pending = true;
  return o;
}

static void buf_init(Buf *b) { b->p = NULL; b->n = b->cap = 0; }
static void buf_add(Buf *b, const char *s, int n) {
  if (b->n + n + 1 > b->cap) {
    int nc = b->cap ? b->cap * 2 : 64;
    while (nc < b->n + n + 1) nc *= 2;
    b->p = xrealloc(b->p, nc); b->cap = nc;
  }
  if (n) memcpy(b->p + b->n, s, n);
  b->n += n; b->p[b->n] = 0;
}
static void buf_addc(Buf *b, char c) { buf_add(b, &c, 1); }
static void buf_fmt(Buf *b, const char *fmt, ...) {
  char tmp[512]; va_list ap;
  va_start(ap, fmt); int n = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
  if (n >= (int)sizeof tmp) {
    char *big = xalloc(n + 1);
    va_start(ap, fmt); vsnprintf(big, n + 1, fmt, ap); va_end(ap);
    buf_add(b, big, n); free(big);
  } else buf_add(b, tmp, n);
}
static const char *buf_cstr(Buf *b) { if (!b->p) buf_add(b, "", 0); return b->p; }

static int u8_decode(const unsigned char *s, uint32_t *cp) {
  if (s[0] < 0x80) { *cp = s[0]; return 1; }
  if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) { *cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F); return 2; }
  if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
    *cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); return 3;
  }
  if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
    *cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); return 4;
  }
  *cp = s[0]; return 1;
}
static int u8_encode(uint32_t c, char *o) {
  if (c < 0x80) { o[0] = c; return 1; }
  if (c < 0x800) { o[0] = 0xC0 | (c >> 6); o[1] = 0x80 | (c & 0x3F); return 2; }
  if (c < 0x10000) { o[0] = 0xE0 | (c >> 12); o[1] = 0x80 | ((c >> 6) & 0x3F); o[2] = 0x80 | (c & 0x3F); return 3; }
  o[0] = 0xF0 | (c >> 18); o[1] = 0x80 | ((c >> 12) & 0x3F); o[2] = 0x80 | ((c >> 6) & 0x3F); o[3] = 0x80 | (c & 0x3F); return 4;
}
static int u8_len(const char *s, int n) {
  int c = 0; for (int i = 0; i < n; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) c++;
  return c;
}

static int u8_off(const char *s, int n, int ci) {
  int c = 0;
  for (int i = 0; i < n; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) { if (c == ci) return i; c++; }
  return c == ci ? n : -1;
}
static int u8_charsize(const char *s) { uint32_t cp; return u8_decode((const unsigned char *)s, &cp); }

static uint32_t norm_cp(uint32_t c) {
  switch (c) {
    case 0x623: case 0x625: case 0x622: case 0x671: return 0x627;
    case 0x629: return 0x647;
    case 0x649: return 0x64A;
    case 0x624: return 0x648;
    case 0x626: return 0x64A;
    default: break;
  }
  if (c >= 0x660 && c <= 0x669) return '0' + (c - 0x660);
  if (c >= 0x6F0 && c <= 0x6F9) return '0' + (c - 0x6F0);
  return c;
}
static bool skip_cp(uint32_t c) { return c == 0x640 || (c >= 0x64B && c <= 0x65F) || c == 0x670; }
static int norm_into(const char *s, int n, char *out) {
  int o = 0, i = 0;
  while (i < n) {
    uint32_t cp; int l = u8_decode((const unsigned char *)s + i, &cp); i += l;
    if (skip_cp(cp)) continue;
    o += u8_encode(norm_cp(cp), out + o);
  }
  out[o] = 0; return o;
}

static inline uint64_t hb_rd64(const char *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static inline uint64_t hb_rd32(const char *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint64_t hb_mix(uint64_t a, uint64_t b) {
  __uint128_t m = (__uint128_t)(a ^ 0x9E3779B97F4A7C15ull) * (b ^ 0xD6E8FEB86659FD93ull);
  return (uint64_t)m ^ (uint64_t)(m >> 64);
}
static uint32_t hash_bytes(const char *s, int n) {
  uint64_t h;
  if (n <= 32) {
    uint32_t f = 2166136261u;
    for (int i = 0; i < n; i++) { f ^= (uint8_t)s[i]; f *= 16777619u; }
    return f;
  }
  if (n <= 128) {
    h = (uint64_t)n * 0x100000001B3ull;
    int i = 0;
    for (; i + 16 <= n; i += 16) h = hb_mix(hb_rd64(s + i) ^ h, hb_rd64(s + i + 8));
    if (i < n) h = hb_mix(hb_rd64(s + n - 16) ^ h, hb_rd64(s + n - 8));
  } else {
    h = (uint64_t)n * 0x100000001B3ull;
    h = hb_mix(hb_rd64(s) ^ h, hb_rd64(s + 8)); h = hb_mix(hb_rd64(s + 16) ^ h, hb_rd64(s + 24));
    h = hb_mix(hb_rd64(s + n - 32) ^ h, hb_rd64(s + n - 24)); h = hb_mix(hb_rd64(s + n - 16) ^ h, hb_rd64(s + n - 8));
    int step = (n - 64) / 16; if (step < 8) step = 8;
    for (int i = 32; i + 8 <= n - 32; i += step) h = hb_mix(hb_rd64(s + i) ^ h, 0xA0761D6478BD642Full);
  }
  return (uint32_t)(h ^ (h >> 32));
}
static void intern_grow(void) {
  int nc = vm.sbcap * 2; Str **nb = xalloc(sizeof(Str *) * nc);
  for (int i = 0; i < nc; i++) nb[i] = NULL;
  for (int i = 0; i < vm.sbcap; i++) {
    Str *p = vm.sb[i];
    while (p) { Str *nx = p->hnext; uint32_t k = p->hash & (nc - 1); p->hnext = nb[k]; nb[k] = p; p = nx; }
  }
  free(vm.sb); vm.sb = nb; vm.sbcap = nc;
}
static Str *intern_alloc(int n) {
  if (n > 1024) { vm.sbytes += (size_t)n; if (vm.sbytes > ((size_t)48 << 20)) vm.gc_pending = true; }
  Str *st = (Str *)newobj(sizeof(Str) + (size_t)n + 1, O_STR);
  st->chars = (char *)(st + 1); st->sb = NULL;
  st->len = n; st->hash = 0; st->hnext = NULL; st->chars[n] = 0; return st;
}
static Str *intern_finish(Str *st) {
  int n = st->len; uint32_t h = hash_bytes(st->chars, n); uint32_t b = h & (vm.sbcap - 1);
  for (Str *p = vm.sb[b]; p; p = p->hnext)
    if (p->hash == h && p->len == n && memcmp(p->chars, st->chars, n) == 0) {
      vm.objs = st->o.next; vm.nobj--; pool_free(st, sizeof(Str) + (size_t)n + 1); return p;
    }
  st->hash = h; st->hnext = vm.sb[b]; vm.sb[b] = st; vm.sbcount++;
  if (vm.sbcount > vm.sbcap) intern_grow();
  return st;
}
static Str *intern(const char *s, int n) {
  uint32_t h = hash_bytes(s, n);
  uint32_t b = h & (vm.sbcap - 1);
  for (Str *p = vm.sb[b]; p; p = p->hnext)
    if (p->hash == h && p->len == n && memcmp(p->chars, s, n) == 0) return p;
  Str *st = (Str *)newobj(sizeof(Str) + n + 1, O_STR);
  st->chars = (char *)(st + 1); st->sb = NULL;
  st->len = n; st->hash = h; memcpy(st->chars, s, n); st->chars[n] = 0;
  st->hnext = vm.sb[b]; vm.sb[b] = st; vm.sbcount++;
  if (vm.sbcount > vm.sbcap) intern_grow();
  return st;
}
static Str *intern_c(const char *s) { return intern(s, (int)strlen(s)); }

static void sbuf_unref(SBuf *b) { if (b && --b->refs <= 0) free(b); }

#define SB_MIN_LEN 1024
static Str *str_extend(Str *a, const char *pb, int lb) {
  int n = a->len + lb; SBuf *sb = a->sb;
  if (!(sb && sb->used == a->len && a->chars == sb->data && n < sb->cap)) {
    int cap = n + n / 2 + 64; sb = (SBuf *)xalloc(sizeof(SBuf) + (size_t)cap + 1); sb->refs = 0; sb->cap = cap;
    memcpy(sb->data, a->chars, (size_t)a->len); sb->used = a->len;
    vm.sbytes += (size_t)cap; if (vm.sbytes > ((size_t)48 << 20)) vm.gc_pending = true;
  }
  memcpy(sb->data + a->len, pb, (size_t)lb); sb->used = n; sb->data[n] = 0;
  Str *r = (Str *)newobj(sizeof(Str), O_STR);
  r->len = n; r->chars = sb->data; r->sb = sb; sb->refs++; r->hnext = NULL;
  uint32_t h = hash_bytes(r->chars, n); uint32_t b = h & (vm.sbcap - 1);
  for (Str *p = vm.sb[b]; p; p = p->hnext)
    if (p->hash == h && p->len == n && memcmp(p->chars, r->chars, (size_t)n) == 0) { vm.objs = r->o.next; vm.nobj--; sbuf_unref(sb); pool_free(r, sizeof(Str)); return p; }
  r->hash = h; r->hnext = vm.sb[b]; vm.sb[b] = r; vm.sbcount++;
  if (vm.sbcount > vm.sbcap) intern_grow();
  return r;
}

static Str *normstr(const char *s) {
  int n = (int)strlen(s); char *t = xalloc(n * 2 + 8);
  int m = norm_into(s, n, t); Str *r = intern(t, m); free(t); return r;
}
static Str *normstr_n(const char *s, int n) {
  char *t = xalloc(n * 2 + 8); int m = norm_into(s, n, t); Str *r = intern(t, m); free(t); return r;
}
static Str **g_kroots[8192]; static int g_nkroots;
static Str *kstr_reg(const char *s, Str **slot, int exact) {
  Str *r = exact ? intern(s, (int)strlen(s)) : normstr(s);
  if (g_nkroots < 8192) { *slot = r; g_kroots[g_nkroots++] = slot; }
  return r;
}
#define KN(s) OBJV(({ static Str *_ks; _ks ? _ks : kstr_reg((s), &_ks, 0); }))
#define KX(s) OBJV(({ static Str *_kx; _kx ? _kx : kstr_reg((s), &_kx, 1); }))
#define K(s) OBJV(normstr(s))
#define KDYN(s) K(s)

static uint32_t vhash(Value k) {
  switch (k.t) {
    case V_NUM: { double d = k.as.n; if (d == 0) d = 0; uint64_t u; memcpy(&u, &d, 8); return (uint32_t)(u ^ (u >> 32)) * 2654435761u; }
    case V_BOOL: return k.as.b ? 3 : 5;
    case V_NIL: return 7;
    case V_OBJ: if (k.as.o->t == O_STR) return ((Str *)k.as.o)->hash; return (uint32_t)(((uintptr_t)k.as.o) >> 4) * 2654435761u;
    default: return 0;
  }
}
static bool keq(Value a, Value b) {
  if (a.t != b.t) return false;
  switch (a.t) {
    case V_NUM: return a.as.n == b.as.n;
    case V_BOOL: return a.as.b == b.as.b;
    case V_NIL: case V_EMPTY: return true;
    default: return a.as.o == b.as.o;
  }
}

static int dfind(Dict *d, Value k, uint32_t h) {
  if (!d->idx) return -1;
  uint32_t m = d->icap - 1, i = h & m;
  for (;;) {
    int x = d->idx[i];
    if (x == -1) return -1;
    if (keq(d->e[x].k, k)) return x;
    i = (i + 1) & m;
  }
}
static void drebuild(Dict *d, int ncap) {
  if (g_mfn_memo.d == d) g_mfn_memo.d = NULL;
  Ent *ne = xalloc(sizeof(Ent) * ncap); int n = 0;
  for (int i = 0; i < d->n; i++) if (d->e[i].k.t != V_EMPTY) ne[n++] = d->e[i];
  free(d->e); d->e = ne; d->n = n; d->cap = ncap; d->live = n;
  free(d->idx); d->icap = ncap * 2; d->idx = xalloc(sizeof(int) * d->icap);
  for (int i = 0; i < d->icap; i++) d->idx[i] = -1;
  for (int i = 0; i < n; i++) {
    uint32_t m = d->icap - 1, j = vhash(ne[i].k) & m;
    while (d->idx[j] != -1) j = (j + 1) & m;
    d->idx[j] = i;
  }
}

static void dset(Dict *d, Value k, Value v) {
  uint32_t h = vhash(k); int x = dfind(d, k, h);
  if (x >= 0) { d->e[x].v = v; return; }
  if (g_mfn_memo.d == d) g_mfn_memo.d = NULL;
  if (d->n == d->cap) drebuild(d, d->cap == 0 ? 4 : (d->live * 2 >= d->cap ? d->cap * 2 : d->cap));
  uint32_t m = d->icap - 1, j = h & m;
  while (d->idx[j] != -1) j = (j + 1) & m;
  d->idx[j] = d->n; d->e[d->n].k = k; d->e[d->n].v = v; d->n++; d->live++;
}
static bool dget(Dict *d, Value k, Value *out) {
  int x = dfind(d, k, vhash(k)); if (x < 0) return false;
  *out = d->e[x].v; return true;
}

static bool ddel(Dict *d, Value k, Value *old) {
  int x = dfind(d, k, vhash(k)); if (x < 0) return false;
  if (old) *old = d->e[x].v;
  if (g_mfn_memo.d == d) g_mfn_memo.d = NULL;
  d->e[x].k = EMPTYV; d->e[x].v = NIL; d->live--; return true;
}
static void dfree(Dict *d) { if (g_mfn_memo.d == d) g_mfn_memo.d = NULL; free(d->e); free(d->idx); d->e = NULL; d->idx = NULL; d->n = d->cap = d->live = d->icap = 0; }

static bool mget(Dict *d, Value k, Value *out) {
  if (dget(d, k, out)) return true;
  if (!IS_T(k, O_STR)) return false;
  Str *want = AS_STR(k);
  for (int i = 0; i < d->n; i++) {
    Value kk = d->e[i].k; if (!IS_T(kk, O_STR)) continue;
    Str *ks = AS_STR(kk); if (ks->len > 200) continue;
    char tmp[420]; int nl = norm_into(ks->chars, ks->len, tmp);
    if (nl == want->len && memcmp(tmp, want->chars, nl) == 0) { *out = d->e[i].v; return true; }
  }
  return false;
}

static void varr_push(VArr *a, Value v) {
  if (a->n == a->cap) { a->cap = a->cap ? a->cap * 2 : 8; a->v = xrealloc(a->v, sizeof(Value) * a->cap); }
  a->v[a->n++] = v;
}

static List *new_list(void) { List *l = (List *)newobj(sizeof(List), O_LIST); l->a.v = NULL; l->a.n = l->a.cap = 0; return l; }
static Map *new_map(void) { Map *m = (Map *)newobj(sizeof(Map), O_MAP); memset(&m->d, 0, sizeof(Dict)); m->module = false; return m; }
static Fun *new_fun(void) {
  Fun *f = (Fun *)newobj(sizeof(Fun), O_FUN);
  f->arity = f->minar = f->upc = 0; f->variadic = false; f->kind = 0; f->getter = false; f->name = NULL; f->gc = NULL; f->ndbg = f->capdbg = 0; f->dbgname = NULL; f->dbgslot = NULL; f->file = NULL; f->lc = NULL; f->jreg = NULL; f->fj = NULL; f->fjst = 0; f->fjcnt = 0;
  memset(&f->ch, 0, sizeof(Chunk)); return f;
}

static Closure *new_closure(Fun *f, Map *g) {
  Closure *c = (Closure *)newobj(sizeof(Closure), O_CLO);
  c->f = f; c->globals = g; c->upc = f->upc;
  c->upv = f->upc ? xalloc(sizeof(Upv *) * (size_t)f->upc) : NULL;
  for (int i = 0; i < f->upc; i++) c->upv[i] = NULL;
  return c;
}
static Upv *new_upv(Value *loc) { Upv *u = (Upv *)newobj(sizeof(Upv), O_UPV); u->loc = loc; u->closed = NIL; u->next = NULL; return u; }
static Native *new_native(Str *name, NativeFn fn, int mn, int mx) {
  Native *n = (Native *)newobj(sizeof(Native), O_NAT); n->name = name; n->fn = fn; n->minar = mn; n->maxar = mx; n->ud = NULL; return n;
}
static Class *new_class(Str *name) {
  Class *c = (Class *)newobj(sizeof(Class), O_CLS); c->name = name; c->super = NULL; memset(&c->methods, 0, sizeof(Dict)); memset(&c->statics, 0, sizeof(Dict)); return c;
}
static Instance *new_instance(Class *c) {
  Instance *i = (Instance *)newobj(sizeof(Instance), O_INST); i->cls = c; memset(&i->fields, 0, sizeof(Dict)); i->fnf = false; return i;
}
static Bound *new_bound(Value r, Value m) { Bound *b = (Bound *)newobj(sizeof(Bound), O_BOUND); b->recv = r; b->method = m; return b; }
