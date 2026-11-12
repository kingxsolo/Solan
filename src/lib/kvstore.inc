#define KV_MAGIC "SOLKV01\n"
#define KV_SLOTS 64
#define KV_BUCKETS 512
#define KV_KEYMAX (1u << 20)
#define KV_VALMAX (256u << 20)
#define KV_COMP_MIN 128
#define KV_COMP_MAX (128u << 20)
#define KV_F_DEL 0x01
#define KV_F_COMP 0x02

#if defined(_WIN32)
#define kv_fseeko(f, o, w) _fseeki64((f), (o), (w))
#define kv_ftello(f) _ftelli64(f)
#else
#define kv_fseeko(f, o, w) fseeko((f), (o), (w))
#define kv_ftello(f) ftello(f)
#endif

typedef struct KVNode {
  struct KVNode *next;
  char *key;
  uint32_t klen;
  uint8_t type;
  unsigned char *val;
  uint32_t vlen;
} KVNode;

typedef struct KV {
  FILE *f;
  char *path;
  KVNode *b[KV_BUCKETS];
  int live, stale;
  long long logsize;
} KV;

static KV *kvs[KV_SLOTS];

static uint32_t kv_hash(const char *k, uint32_t n) {
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) { h ^= (unsigned char)k[i]; h *= 16777619u; }
  return h;
}
static void kv_wr32(unsigned char *p, uint32_t u) { p[0] = (unsigned char)u; p[1] = (unsigned char)(u >> 8); p[2] = (unsigned char)(u >> 16); p[3] = (unsigned char)(u >> 24); }
static uint32_t kv_rd32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void kv_wr64(unsigned char *p, double d) { uint64_t u; memcpy(&u, &d, 8); for (int i = 0; i < 8; i++) p[i] = (unsigned char)(u >> (8 * i)); }
static double kv_rd64(const unsigned char *p) { uint64_t u = 0; for (int i = 7; i >= 0; i--) u = (u << 8) | p[i]; double d; memcpy(&d, &u, 8); return d; }

static bool kv_fremove(const char *p) {
#if defined(_WIN32)
  wchar_t *w = w_utf16(p); int r = _wremove(w); free(w); return r == 0;
#else
  return remove(p) == 0;
#endif
}
static bool kv_frename(const char *a, const char *b) {
#if defined(_WIN32)
  wchar_t *wa = w_utf16(a), *wb = w_utf16(b); int r = _wrename(wa, wb); free(wa); free(wb); return r == 0;
#else
  return rename(a, b) == 0;
#endif
}

static void kv_free_node(KVNode *n) { free(n->key); free(n->val); free(n); }

static KVNode *kv_find(KV *kv, const char *key, uint32_t klen, KVNode ***prev) {
  KVNode **pp = &kv->b[kv_hash(key, klen) & (KV_BUCKETS - 1)];
  for (KVNode *n = *pp; n; pp = &n->next, n = n->next)
    if (n->klen == klen && memcmp(n->key, key, klen) == 0) { if (prev) *prev = pp; return n; }
  return NULL;
}

static void kv_set_mem(KV *kv, const char *key, uint32_t klen, uint8_t type, const unsigned char *val, uint32_t vlen) {
  KVNode *n = kv_find(kv, key, klen, NULL);
  if (!n) {
    uint32_t h = kv_hash(key, klen) & (KV_BUCKETS - 1);
    n = (KVNode *)xalloc(sizeof(KVNode));
    n->key = (char *)xalloc((size_t)klen + 1);
    memcpy(n->key, key, klen); n->key[klen] = 0;
    n->klen = klen; n->type = type; n->val = NULL; n->vlen = 0;
    n->next = kv->b[h]; kv->b[h] = n;
    kv->live++;
  } else {
    free(n->val); n->val = NULL; n->vlen = 0;
    n->type = type;
    kv->stale++;
  }
  if (vlen) { n->val = (unsigned char *)xalloc(vlen); memcpy(n->val, val, vlen); n->vlen = vlen; }
}

static bool kv_del_mem(KV *kv, const char *key, uint32_t klen) {
  KVNode **pp; KVNode *n = kv_find(kv, key, klen, &pp);
  if (!n) return false;
  *pp = n->next;
  kv_free_node(n);
  kv->live--;
  return true;
}

static bool kv_write_rec(FILE *f, const char *key, uint32_t klen, uint8_t type, const unsigned char *val, uint32_t vlen, uint8_t flags) {
  unsigned char h[14];
  uint32_t crc = fl_crc32(0, (const unsigned char *)key, klen);
  if (vlen) crc = fl_crc32(crc, val, vlen);
  kv_wr32(h, klen); kv_wr32(h + 4, vlen); h[8] = type; h[9] = flags; kv_wr32(h + 10, crc);
  if (fwrite(h, 1, 14, f) != 14) return false;
  if (klen && fwrite(key, 1, klen, f) != klen) return false;
  if (vlen && fwrite(val, 1, vlen, f) != vlen) return false;
  return true;
}

static bool kv_encode(uint8_t type, const unsigned char *val, uint32_t vlen, const unsigned char **out, uint32_t *outlen, uint8_t *outflags, FlBuf *scratch) {
  memset(scratch, 0, sizeof *scratch);
  *out = val; *outlen = vlen; *outflags = 0;
  if (type == 4 && vlen >= KV_COMP_MIN && vlen <= KV_COMP_MAX) {
    if (fl_deflate_raw(val, (int)vlen, scratch) == 0 && scratch->n > 0 && scratch->n * 8 <= (size_t)vlen * 7) {
      *out = scratch->p; *outlen = (uint32_t)scratch->n; *outflags = KV_F_COMP;
      return true;
    }
    free(scratch->p); scratch->p = NULL;
  }
  return false;
}

static bool kv_append(KV *kv, const char *key, uint32_t klen, uint8_t type, const unsigned char *val, uint32_t vlen, uint8_t eflags) {
  if (!kv->f) { rt_error("قاعدة_سريعة: الملف غير مفتوح للكتابة"); return false; }
  FlBuf scratch; const unsigned char *wval; uint32_t wlen; uint8_t wflags;
  bool comp = kv_encode(type, val, vlen, &wval, &wlen, &wflags, &scratch);
  bool ok = kv_write_rec(kv->f, key, klen, type, wval, wlen, (uint8_t)(wflags | eflags));
  if (ok && fflush(kv->f) != 0) ok = false;
  free(scratch.p);
  if (!ok) { fclose(kv->f); kv->f = NULL; rt_error("قاعدة_سريعة: فشل الكتابة في '%s'", kv->path); return false; }
  kv->logsize += 14 + (long long)klen + (long long)wlen;
  return true;
}

static bool kv_scan(KV *kv) {
  FILE *f = kv->f;
  kv_fseeko(f, 0, SEEK_END);
  long long sz = kv_ftello(f);
  kv_fseeko(f, 0, SEEK_SET);
  if (sz == 0) {
    if (fwrite(KV_MAGIC, 1, 8, f) != 8 || fflush(f) != 0) { fclose(f); kv->f = NULL; rt_error("قاعدة_سريعة: فشل تهيئة '%s'", kv->path); return false; }
    kv->logsize = 8;
    return true;
  }
  unsigned char mg[8];
  if (fread(mg, 1, 8, f) != 8 || memcmp(mg, KV_MAGIC, 8) != 0) { fclose(f); kv->f = NULL; rt_error("'%s' ليست قاعدة_سريعة صالحة", kv->path); return false; }
  long long off = 8;
  for (;;) {
    if (sz - off < 14) break;
    unsigned char h[14];
    if (fread(h, 1, 14, f) != 14) break;
    uint32_t klen = kv_rd32(h), vlen = kv_rd32(h + 4);
    uint8_t type = h[8], flags = h[9];
    if (type > 4 || (flags & ~(KV_F_DEL | KV_F_COMP)) || klen > KV_KEYMAX || vlen > KV_VALMAX || off + 14 + (long long)klen + (long long)vlen > sz) break;
    unsigned char *body = (unsigned char *)xalloc((size_t)klen + vlen + 1);
    if (fread(body, 1, (size_t)klen + vlen, f) != (size_t)klen + vlen) { free(body); break; }
    if (fl_crc32(0, body, (size_t)klen + vlen) != kv_rd32(h + 10)) { free(body); break; }
    if (flags & KV_F_DEL) {
      if (kv_del_mem(kv, (const char *)body, klen)) kv->stale += 2; else kv->stale++;
    } else {
      const unsigned char *v = body + klen; uint32_t vn = vlen; unsigned char *dec = NULL;
      if ((flags & KV_F_COMP) && vn) {
        FlBuf o; memset(&o, 0, sizeof o); size_t used;
        if (fl_inflate_raw(v, vn, &o, &used) != 0) { free(o.p); free(body); break; }
        dec = o.p; v = o.p; vn = (uint32_t)o.n;
      }
      kv_set_mem(kv, (const char *)body, klen, type, v, vn);
      free(dec);
    }
    free(body);
    off += 14 + (long long)klen + (long long)vlen;
  }
  kv_fseeko(f, off, SEEK_SET);
  kv->logsize = off;
  return true;
}

static int kv_cmp_node(const void *a, const void *b) {
  const KVNode *x = *(KVNode *const *)a, *y = *(KVNode *const *)b;
  uint32_t m = x->klen < y->klen ? x->klen : y->klen;
  int r = memcmp(x->key, y->key, m);
  if (r) return r;
  return x->klen < y->klen ? -1 : (x->klen > y->klen ? 1 : 0);
}

static bool kv_copy_file(const char *src, const char *dst) {
  FILE *s = fopen(src, "rb"); if (!s) return false;
  FILE *d = fopen(dst, "wb"); if (!d) { fclose(s); return false; }
  char buf[65536]; size_t r; bool ok = true;
  while ((r = fread(buf, 1, sizeof buf, s)) > 0) if (fwrite(buf, 1, r, d) != r) { ok = false; break; }
  if (fflush(d) != 0) ok = false;
  fclose(s);
  if (fclose(d) != 0) ok = false;
  return ok;
}

static bool kv_compact(KV *kv) {
  if (!kv->f) { rt_error("قاعدة_سريعة: الملف غير مفتوح"); return false; }
  size_t tl = strlen(kv->path) + 5;
  char *tmp = (char *)xalloc(tl);
  snprintf(tmp, tl, "%s.tmp", kv->path);
  FILE *t = fopen(tmp, "wb");
  if (!t) { free(tmp); rt_error("قاعدة_سريعة: تعذّر إنشاء '%s'", tmp); return false; }
  if (fwrite(KV_MAGIC, 1, 8, t) != 8) { fclose(t); kv_fremove(tmp); free(tmp); kv->f = fopen(kv->path, "r+b"); if (kv->f) kv_fseeko(kv->f, 0, SEEK_END); rt_error("قاعدة_سريعة: فشل كتابة ملف الضغط"); return false; }
  int n = kv->live, i = 0;
  KVNode **arr = (KVNode **)xalloc(sizeof(KVNode *) * (size_t)(n > 0 ? n : 1));
  for (int b = 0; b < KV_BUCKETS; b++) for (KVNode *nd = kv->b[b]; nd; nd = nd->next) arr[i++] = nd;
  qsort(arr, (size_t)n, sizeof(KVNode *), kv_cmp_node);
  bool ok = true;
  for (int j = 0; j < n && ok; j++) {
    FlBuf scratch; const unsigned char *wval; uint32_t wlen; uint8_t wflags;
    bool comp = kv_encode(arr[j]->type, arr[j]->val, arr[j]->vlen, &wval, &wlen, &wflags, &scratch);
    ok = kv_write_rec(t, arr[j]->key, arr[j]->klen, arr[j]->type, wval, wlen, wflags);
    free(scratch.p);
  }
  free(arr);
  if (ok) ok = fflush(t) == 0;
  if (ok) ok = fclose(t) == 0; else fclose(t);
  if (!ok) {
    kv_fremove(tmp); free(tmp);
    kv->f = fopen(kv->path, "r+b");
    if (kv->f) kv_fseeko(kv->f, 0, SEEK_END);
    rt_error("قاعدة_سريعة: فشل كتابة ملف الضغط لـ '%s'", kv->path);
    return false;
  }
  fclose(kv->f); kv->f = NULL;
  kv_fremove(kv->path);
  if (!kv_frename(tmp, kv->path) && !kv_copy_file(tmp, kv->path)) {
    free(tmp);
    rt_error("قاعدة_سريعة: فشل استبدال الملف أثناء ضغط '%s'؛ البيانات محفوظة في '%s.tmp'", kv->path, kv->path);
    return false;
  }
  kv_fremove(tmp);
  free(tmp);
  kv->f = fopen(kv->path, "r+b");
  if (!kv->f) { rt_error("قاعدة_سريعة: تعذّر إعادة فتح '%s' بعد الضغط", kv->path); return false; }
  kv->stale = 0;
  kv_fseeko(kv->f, 0, SEEK_END);
  kv->logsize = kv_ftello(kv->f);
  return true;
}

static bool kv_maybe_compact(KV *kv) {
  if (kv->stale > kv->live + 64 && kv->logsize >= 65536) return kv_compact(kv);
  return true;
}

static bool kv_arg(Value v, KV **out) {
  if (!IS_NUM(v) || v.as.n < 0 || v.as.n >= KV_SLOTS || !kvs[(int)v.as.n]) { rt_error("مقبض قاعدة_سريعة غير صالح"); return false; }
  *out = kvs[(int)v.as.n];
  return true;
}

static bool kv_keyarg(Value v, const char **key, uint32_t *klen) {
  if (!IS_T(v, O_STR)) { rt_error("قاعدة_سريعة: المفتاح يجب أن يكون نصا"); return false; }
  Str *s = AS_STR(v);
  if (s->len < 0 || (uint32_t)s->len > KV_KEYMAX) { rt_error("قاعدة_سريعة: مفتاح طويل جدا"); return false; }
  if (s->len > 0 && memchr(s->chars, 0, (size_t)s->len)) { rt_error("قاعدة_سريعة: المفتاح لا يجوز أن يحتوي بايت صفر"); return false; }
  *key = s->chars; *klen = (uint32_t)s->len;
  return true;
}

NAT(kv_open) {
  STRA(0);
  int slot = -1;
  for (int i = 0; i < KV_SLOTS; i++) if (!kvs[i]) { slot = i; break; }
  if (slot < 0) FAIL("فُتحت مخازن قاعدة_سريعة كثيرة");
  KV *kv = (KV *)xalloc(sizeof(KV));
  memset(kv, 0, sizeof *kv);
  size_t pl = strlen(AS_STR(a[0])->chars) + 1;
  kv->path = (char *)xalloc(pl);
  memcpy(kv->path, AS_STR(a[0])->chars, pl);
  kv->f = fopen(kv->path, "r+b");
  if (!kv->f) {
    char *tmp = (char *)xalloc(pl + 4);
    snprintf(tmp, pl + 4, "%s.tmp", kv->path);
    FILE *t = fopen(tmp, "rb");
    if (t) { fclose(t); kv_fremove(kv->path); kv_frename(tmp, kv->path); }
    free(tmp);
    kv->f = fopen(kv->path, "r+b");
  }
  if (!kv->f) kv->f = fopen(kv->path, "w+b");
  if (!kv->f) { free(kv->path); free(kv); FAIL("تعذّر فتح '%s'", AS_STR(a[0])->chars); }
  if (!kv_scan(kv)) { free(kv->path); free(kv); return false; }
  if (kv->stale > kv->live && kv->logsize >= 8192 && !kv_compact(kv)) { free(kv->path); free(kv); return false; }
  kvs[slot] = kv;
  *res = NUMV(slot);
  return true;
}

NAT(kv_put) {
  KV *kv; if (!kv_arg(a[0], &kv)) return false;
  const char *key; uint32_t klen; if (!kv_keyarg(a[1], &key, &klen)) return false;
  Value v = a[2];
  uint8_t type; const unsigned char *val; uint32_t vlen; unsigned char nb[8];
  if (IS_NIL(v)) { type = 0; val = NULL; vlen = 0; }
  else if (v.t == V_BOOL) { type = v.as.b ? 2 : 1; val = NULL; vlen = 0; }
  else if (IS_NUM(v)) { type = 3; kv_wr64(nb, v.as.n); val = nb; vlen = 8; }
  else if (IS_T(v, O_STR)) { type = 4; val = (const unsigned char *)AS_STR(v)->chars; vlen = (uint32_t)AS_STR(v)->len; }
  else FAIL("قاعدة_سريعة: نوع القيمة غير مدعوم (المدعوم: عدم، صح، غلط، رقم، نص؛ استخدم جيسون.ترميز للبيانات المعقدة)");
  kv_set_mem(kv, key, klen, type, val, vlen);
  if (!kv_append(kv, key, klen, type, val, vlen, 0)) return false;
  if (!kv_maybe_compact(kv)) return false;
  *res = BOOLV(true);
  return true;
}

NAT(kv_get) {
  KV *kv; if (!kv_arg(a[0], &kv)) return false;
  const char *key; uint32_t klen; if (!kv_keyarg(a[1], &key, &klen)) return false;
  KVNode *n = kv_find(kv, key, klen, NULL);
  if (!n) { *res = argc > 2 ? a[2] : NIL; return true; }
  if (n->type == 0) *res = NIL;
  else if (n->type == 1) *res = BOOLV(false);
  else if (n->type == 2) *res = BOOLV(true);
  else if (n->type == 3) *res = NUMV(n->vlen >= 8 ? kv_rd64(n->val) : 0.0);
  else *res = n->vlen > 0 ? mkstr((const char *)n->val, (int)n->vlen) : mkstr("", 0);
  return true;
}

NAT(kv_has) {
  KV *kv; if (!kv_arg(a[0], &kv)) return false;
  const char *key; uint32_t klen; if (!kv_keyarg(a[1], &key, &klen)) return false;
  *res = BOOLV(kv_find(kv, key, klen, NULL) != NULL);
  return true;
}

NAT(kv_del) {
  KV *kv; if (!kv_arg(a[0], &kv)) return false;
  const char *key; uint32_t klen; if (!kv_keyarg(a[1], &key, &klen)) return false;
  bool had = kv_del_mem(kv, key, klen);
  if (had) {
    if (!kv_append(kv, key, klen, 0, NULL, 0, KV_F_DEL)) return false;
    kv->stale += 2;
    if (!kv_maybe_compact(kv)) return false;
  }
  *res = BOOLV(had);
  return true;
}

NAT(kv_incr) {
  KV *kv; if (!kv_arg(a[0], &kv)) return false;
  const char *key; uint32_t klen; if (!kv_keyarg(a[1], &key, &klen)) return false;
  if (argc > 2 && !IS_NUM(a[2])) FAIL("المعامل 3 يجب أن يكون رقما");
  double amt = argc > 2 ? a[2].as.n : 1.0;
  KVNode *n = kv_find(kv, key, klen, NULL);
  if (n && n->type != 3) FAIL("قاعدة_سريعة: لا يمكن زيادة '%s' لأنه ليس رقما", key);
  double nv = (n ? kv_rd64(n->val) : 0.0) + amt;
  unsigned char nb[8];
  kv_wr64(nb, nv);
  kv_set_mem(kv, key, klen, 3, nb, 8);
  if (!kv_append(kv, key, klen, 3, nb, 8, 0)) return false;
  if (!kv_maybe_compact(kv)) return false;
  *res = NUMV(nv);
  return true;
}

static int kv_cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

NAT(kv_keys) {
  KV *kv; if (!kv_arg(a[0], &kv)) return false;
  List *l = new_list();
  if (kv->live > 0) {
    char **arr = (char **)xalloc(sizeof(char *) * (size_t)kv->live);
    int i = 0;
    for (int b = 0; b < KV_BUCKETS; b++) for (KVNode *nd = kv->b[b]; nd; nd = nd->next) arr[i++] = nd->key;
    qsort(arr, (size_t)kv->live, sizeof(char *), kv_cmp_str);
    for (int j = 0; j < kv->live; j++) varr_push(&l->a, mkstr(arr[j], (int)strlen(arr[j])));
    free(arr);
  }
  *res = OBJV(l);
  return true;
}

NAT(kv_count) {
  KV *kv; if (!kv_arg(a[0], &kv)) return false;
  *res = NUMV(kv->live);
  return true;
}

NAT(kv_flush) {
  KV *kv; if (!kv_arg(a[0], &kv)) return false;
  if (kv->f && fflush(kv->f) != 0) FAIL("قاعدة_سريعة: فشل الحفظ");
  *res = BOOLV(true);
  return true;
}

NAT(kv_compact_nat) {
  KV *kv; if (!kv_arg(a[0], &kv)) return false;
  if (!kv_compact(kv)) return false;
  *res = BOOLV(true);
  return true;
}

NAT(kv_close) {
  if (!IS_NUM(a[0]) || a[0].as.n < 0 || a[0].as.n >= KV_SLOTS || !kvs[(int)a[0].as.n]) FAIL("مقبض قاعدة_سريعة غير صالح");
  KV *kv = kvs[(int)a[0].as.n];
  kvs[(int)a[0].as.n] = NULL;
  if (kv->f) fclose(kv->f);
  for (int b = 0; b < KV_BUCKETS; b++) { KVNode *n = kv->b[b]; while (n) { KVNode *nx = n->next; kv_free_node(n); n = nx; } }
  free(kv->path); free(kv);
  *res = BOOLV(true);
  return true;
}
