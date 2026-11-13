#define ZIPC_LFH 0x04034b50u
#define ZIPC_CDH 0x02014b50u
#define ZIPC_EOCD 0x06054b50u
#define ZIP_SLOTS 16
#define ZIP_EFS 0x0800

typedef struct ZipE {
  char *name;
  uint32_t nlen, csize, usize, crc;
  long long off;
  uint16_t method, dostime, dosdate;
  bool is_dir;
} ZipE;

typedef struct ZipH {
  bool writer;
  FILE *f;
  char *path;
  int n, cap;
  ZipE *e;
} ZipH;

static ZipH *zhs[ZIP_SLOTS];

static void z_wr16(unsigned char *p, uint16_t u) { p[0] = (unsigned char)u; p[1] = (unsigned char)(u >> 8); }
static uint16_t z_rd16(const unsigned char *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static void z_wr32(unsigned char *p, uint32_t u) { p[0] = (unsigned char)u; p[1] = (unsigned char)(u >> 8); p[2] = (unsigned char)(u >> 16); p[3] = (unsigned char)(u >> 24); }
static uint32_t z_rd32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static void z_dostime(uint16_t *t, uint16_t *d) {
  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  if (!lt) { *t = 0; *d = 0; return; }
  *t = (uint16_t)((lt->tm_hour << 11) | (lt->tm_min << 5) | (lt->tm_sec >> 1));
  int y = lt->tm_year + 1900 - 1980; if (y < 0) y = 0;
  *d = (uint16_t)((y << 9) | ((lt->tm_mon + 1) << 5) | lt->tm_mday);
}

static bool z_has_high(const char *s, uint32_t n) { for (uint32_t i = 0; i < n; i++) if ((unsigned char)s[i] >= 0x80) return true; return false; }

static bool z_safe_name(const char *name, uint32_t nlen, char *out, size_t outn) {
  if (nlen == 0 || (size_t)nlen + 1 >= outn) return false;
  if (name[0] == '/' || name[0] == '\\') return false;
  if (nlen >= 2 && ((name[0] | 32) >= 'a') && ((name[0] | 32) <= 'z') && name[1] == ':') return false;
  size_t o = 0; uint32_t i = 0;
  while (i < nlen) {
    while (i < nlen && (name[i] == '/' || name[i] == '\\')) i++;
    uint32_t s = i;
    while (i < nlen && name[i] != '/' && name[i] != '\\') i++;
    uint32_t cl = i - s;
    if (!cl) continue;
    if (cl == 2 && name[s] == '.' && name[s + 1] == '.') return false;
    if (cl == 1 && name[s] == '.') continue;
    if (o + cl + 1 >= outn) return false;
    if (o) out[o++] = '/';
    memcpy(out + o, name + s, cl); o += cl;
  }
  out[o] = 0;
  return o > 0;
}

static bool zh_arg(Value v, ZipH **out) {
  if (!IS_NUM(v) || v.as.n < 0 || v.as.n >= ZIP_SLOTS || !zhs[(int)v.as.n]) { rt_error("مقبض أرشيف غير صالح"); return false; }
  *out = zhs[(int)v.as.n];
  return true;
}

static bool z_writer_arg(Value v, ZipH **out) {
  if (!zh_arg(v, out)) return false;
  if (!(*out)->writer) { rt_error("المقبض مفتوح للقراءة فقط؛ استخدم افتح_كتابة"); return false; }
  return true;
}

static void z_free(ZipH *h) {
  if (h->f) fclose(h->f);
  for (int i = 0; i < h->n; i++) free(h->e[i].name);
  free(h->e);
  free(h->path);
  free(h);
}

static bool z_put_data(ZipH *h, const char *name, uint32_t nlen, const unsigned char *data, uint32_t len) {
  if (!h->f) { rt_error("أرشيف: الملف غير مفتوح للكتابة"); return false; }
  char safe[1024];
  if (!z_safe_name(name, nlen, safe, sizeof safe)) { rt_error("أرشيف: اسم مسار غير آمن داخل الأرشيف"); return false; }
  uint32_t slen = (uint32_t)strlen(safe);
  bool efs = z_has_high(safe, slen);
  uint32_t crc = fl_crc32(0, data, len);
  FlBuf c; memset(&c, 0, sizeof c);
  uint16_t method = 0; const unsigned char *wdata = data; uint32_t wlen = len;
  if (len > 0 && len <= KV_COMP_MAX && fl_deflate_raw(data, (int)len, &c) == 0 && c.n > 0 && (uint32_t)c.n < len) {
    method = 8; wdata = c.p; wlen = (uint32_t)c.n;
  } else { free(c.p); c.p = NULL; }
  uint16_t dt, dd; z_dostime(&dt, &dd);
  long long off = kv_ftello(h->f);
  unsigned char hb[30];
  z_wr32(hb, ZIPC_LFH); z_wr16(hb + 4, 20); z_wr16(hb + 6, efs ? ZIP_EFS : 0); z_wr16(hb + 8, method);
  z_wr16(hb + 10, dt); z_wr16(hb + 12, dd); z_wr32(hb + 14, crc); z_wr32(hb + 18, wlen); z_wr32(hb + 22, len);
  z_wr16(hb + 26, (uint16_t)slen); z_wr16(hb + 28, 0);
  bool ok = fwrite(hb, 1, 30, h->f) == 30 && fwrite(safe, 1, slen, h->f) == slen && (!wlen || fwrite(wdata, 1, wlen, h->f) == wlen);
  free(c.p);
  if (!ok || fflush(h->f) != 0) { fclose(h->f); h->f = NULL; rt_error("أرشيف: فشل الكتابة في '%s'", h->path); return false; }
  if (h->n == h->cap) { h->cap = h->cap ? h->cap * 2 : 16; h->e = (ZipE *)xrealloc(h->e, sizeof(ZipE) * (size_t)h->cap); }
  ZipE *e = &h->e[h->n++];
  memset(e, 0, sizeof *e);
  e->name = (char *)xalloc((size_t)slen + 1); memcpy(e->name, safe, (size_t)slen + 1);
  e->nlen = slen; e->csize = wlen; e->usize = len; e->crc = crc; e->off = off;
  e->method = method; e->dostime = dt; e->dosdate = dd;
  return true;
}

static bool z_wfinalize(ZipH *h) {
  FILE *f = h->f;
  if (h->n > 65535) { fclose(f); h->f = NULL; rt_error("أرشيف: أكثر من 65535 إدخالاً غير مدعوم"); return false; }
  long long cdoff = kv_ftello(f);
  if (cdoff > 0xFFFFFFFFll) { fclose(f); h->f = NULL; rt_error("أرشيف: ملفات فوق 4غيغابايت تتطلب ZIP64 غير مدعوم"); return false; }
  long long cdsize = 0;
  for (int i = 0; i < h->n; i++) {
    ZipE *e = &h->e[i];
    if (e->off > 0xFFFFFFFFll) { fclose(f); h->f = NULL; rt_error("أرشيف: ملفات فوق 4غيغابايت تتطلب ZIP64 غير مدعوم"); return false; }
    unsigned char cb[46];
    z_wr32(cb, ZIPC_CDH); z_wr16(cb + 4, 0x031E); z_wr16(cb + 6, 20);
    z_wr16(cb + 8, z_has_high(e->name, e->nlen) ? ZIP_EFS : 0); z_wr16(cb + 10, e->method);
    z_wr16(cb + 12, e->dostime); z_wr16(cb + 14, e->dosdate);
    z_wr32(cb + 16, e->crc); z_wr32(cb + 20, e->csize); z_wr32(cb + 24, e->usize);
    z_wr16(cb + 28, (uint16_t)e->nlen); z_wr16(cb + 30, 0); z_wr16(cb + 32, 0);
    z_wr16(cb + 34, 0); z_wr16(cb + 36, 0); z_wr32(cb + 38, 0x81A40000u); z_wr32(cb + 42, (uint32_t)e->off);
    if (fwrite(cb, 1, 46, f) != 46 || fwrite(e->name, 1, e->nlen, f) != e->nlen) { fclose(f); h->f = NULL; rt_error("أرشيف: فشل كتابة الدليل في '%s'", h->path); return false; }
    cdsize += 46 + e->nlen;
  }
  unsigned char eb[22];
  z_wr32(eb, ZIPC_EOCD); z_wr16(eb + 4, 0); z_wr16(eb + 6, 0);
  z_wr16(eb + 8, (uint16_t)h->n); z_wr16(eb + 10, (uint16_t)h->n);
  z_wr32(eb + 12, (uint32_t)cdsize); z_wr32(eb + 16, (uint32_t)cdoff); z_wr16(eb + 20, 0);
  if (fwrite(eb, 1, 22, f) != 22 || fflush(f) != 0 || fclose(f) != 0) { h->f = NULL; rt_error("أرشيف: فشل إنهاء '%s'", h->path); return false; }
  h->f = NULL;
  return true;
}

static bool z_ropen(ZipH *h) {
  FILE *f = h->f;
  kv_fseeko(f, 0, SEEK_END);
  long long sz = kv_ftello(f);
  if (sz < 22) { rt_error("'%s' ليس أرشيف zip صالحا", h->path); return false; }
  long long back = sz < 22 + 65535 ? sz : 22 + 65535;
  unsigned char *tail = (unsigned char *)xalloc((size_t)back);
  kv_fseeko(f, sz - back, SEEK_SET);
  if (fread(tail, 1, (size_t)back, f) != (size_t)back) { free(tail); rt_error("أرشيف: فشل قراءة '%s'", h->path); return false; }
  long long eopos = -1;
  for (long long i = (long long)back - 22; i >= 0; i--) {
    if (z_rd32(tail + i) != ZIPC_EOCD) continue;
    uint16_t cl = z_rd16(tail + i + 20);
    if (i + 22 + cl <= back) { eopos = sz - back + i; break; }
  }
  free(tail);
  if (eopos < 0) { rt_error("'%s' ليس أرشيف zip صالحا", h->path); return false; }
  unsigned char eb[22];
  kv_fseeko(f, eopos, SEEK_SET);
  if (fread(eb, 1, 22, f) != 22) { rt_error("أرشيف: فشل قراءة نهاية الدليل"); return false; }
  int count = z_rd16(eb + 10);
  long long cdoff = (long long)(uint32_t)z_rd32(eb + 16);
  long long cdsize = (long long)(uint32_t)z_rd32(eb + 12);
  if (count == 0xFFFF || cdoff == 0xFFFFFFFFll || cdsize == 0xFFFFFFFFll) { rt_error("أرشيف ZIP64 غير مدعوم"); return false; }
  unsigned char *cd = (unsigned char *)xalloc((size_t)cdsize + 1);
  kv_fseeko(f, cdoff, SEEK_SET);
  if (fread(cd, 1, (size_t)cdsize, f) != (size_t)cdsize) { free(cd); rt_error("أرشيف: فشل قراءة الدليل المركزي"); return false; }
  long long p = 0;
  for (int i = 0; i < count; i++) {
    if (p + 46 > cdsize || z_rd32(cd + p) != ZIPC_CDH) { free(cd); rt_error("أرشيف تالف: الدليل المركزي"); return false; }
    uint16_t flags = z_rd16(cd + p + 8), method = z_rd16(cd + p + 10);
    uint32_t csize = z_rd32(cd + p + 20), usize = z_rd32(cd + p + 24), crc = z_rd32(cd + p + 16);
    uint16_t nlen = z_rd16(cd + p + 28), elen = z_rd16(cd + p + 30), clen = z_rd16(cd + p + 32);
    long long off = (long long)(uint32_t)z_rd32(cd + p + 42);
    if (p + 46 + (long long)nlen + elen + clen > cdsize) { free(cd); rt_error("أرشيف تالف: الدليل المركزي"); return false; }
    if (flags & 1) { free(cd); rt_error("أرشيف مشفّر غير مدعوم"); return false; }
    if (csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu || off == 0xFFFFFFFFll) { free(cd); rt_error("أرشيف ZIP64 غير مدعوم"); return false; }
    if (h->n == h->cap) { h->cap = h->cap ? h->cap * 2 : 16; h->e = (ZipE *)xrealloc(h->e, sizeof(ZipE) * (size_t)h->cap); }
    ZipE *e = &h->e[h->n++];
    memset(e, 0, sizeof *e);
    e->name = (char *)xalloc((size_t)nlen + 1);
    memcpy(e->name, cd + p + 46, nlen); e->name[nlen] = 0;
    e->nlen = nlen; e->csize = csize; e->usize = usize; e->crc = crc; e->off = off; e->method = method;
    e->is_dir = nlen > 0 && (e->name[nlen - 1] == '/' || e->name[nlen - 1] == '\\');
    p += 46 + (long long)nlen + elen + clen;
  }
  free(cd);
  return true;
}

static bool z_name_eq(const char *a, uint32_t al, const char *b, uint32_t bl) {
  if (al >= 2 && a[0] == '.' && a[1] == '/') { a += 2; al -= 2; }
  if (bl >= 2 && b[0] == '.' && b[1] == '/') { b += 2; bl -= 2; }
  if (al != bl) return false;
  for (uint32_t i = 0; i < al; i++) {
    char ca = a[i] == '\\' ? '/' : a[i];
    char cb = b[i] == '\\' ? '/' : b[i];
    if (ca != cb) return false;
  }
  return true;
}

static ZipE *z_find(ZipH *h, const char *name, uint32_t nlen) {
  for (int i = 0; i < h->n; i++) {
    if (z_name_eq(h->e[i].name, h->e[i].nlen, name, nlen)) return &h->e[i];
  }
  return NULL;
}

static bool z_read_data(ZipH *h, ZipE *e, unsigned char **out, uint32_t *outn) {
  if (!h->f) { rt_error("أرشيف: الملف غير مفتوح للقراءة"); return false; }
  unsigned char lb[30];
  kv_fseeko(h->f, e->off, SEEK_SET);
  if (fread(lb, 1, 30, h->f) != 30 || z_rd32(lb) != ZIPC_LFH) { rt_error("أرشيف تالف: ترويسة محلية"); return false; }
  uint16_t nlen = z_rd16(lb + 26), elen = z_rd16(lb + 28);
  unsigned char *raw = (unsigned char *)xalloc(e->csize ? e->csize : 1);
  kv_fseeko(h->f, e->off + 30 + nlen + elen, SEEK_SET);
  if (e->csize && fread(raw, 1, e->csize, h->f) != e->csize) { free(raw); rt_error("أرشيف تالف: بيانات مقطوعة في '%s'", e->name); return false; }
  if (e->method == 0) {
    if (e->csize != e->usize) { free(raw); rt_error("أرشيف تالف في '%s'", e->name); return false; }
    *out = raw; *outn = e->csize;
    return true;
  }
  if (e->method != 8) { free(raw); rt_error("أرشيف: طريقة ضغط غير مدعومة في '%s'", e->name); return false; }
  FlBuf o; memset(&o, 0, sizeof o); size_t used;
  int rc = e->csize ? fl_inflate_raw(raw, e->csize, &o, &used) : 0;
  free(raw);
  if (rc != 0 || (uint32_t)o.n != e->usize) { free(o.p); rt_error("أرشيف: بيانات تالفة في '%s'", e->name); return false; }
  if (fl_crc32(0, o.p, o.n) != e->crc) { free(o.p); rt_error("أرشيف: تحقق CRC فشل في '%s'", e->name); return false; }
  *out = o.p; *outn = (uint32_t)o.n;
  return true;
}

static bool z_extract_one(ZipH *h, ZipE *e, const char *dir, size_t dirlen, int *count) {
  char safe[1024];
  if (!z_safe_name(e->name, e->nlen, safe, sizeof safe)) { rt_error("أرشيف: مسار غير آمن داخل الأرشيف '%s'", e->name); return false; }
  char dest[PATH_MAX];
  if (dirlen) { if (snprintf(dest, sizeof dest, "%.*s/%s", (int)dirlen, dir, safe) >= (int)sizeof dest) { rt_error("أرشيف: مسار وجهة طويل جدا"); return false; } }
  else if (snprintf(dest, sizeof dest, "%s", safe) >= (int)sizeof dest) { rt_error("أرشيف: مسار وجهة طويل جدا"); return false; }
  if (e->is_dir) {
    if (!rt_mkdirs(dest)) { rt_error("أرشيف: تعذّر إنشاء المجلد '%s'", dest); return false; }
    (*count)++;
    return true;
  }
  char *slash = strrchr(dest, '/');
  if (slash) {
    *slash = 0;
    if (!rt_mkdirs(dest)) { rt_error("أرشيف: تعذّر إنشاء المجلد '%s'", dest); return false; }
    *slash = '/';
  }
  unsigned char *data; uint32_t dn;
  if (!z_read_data(h, e, &data, &dn)) return false;
  FILE *f = fopen(dest, "wb");
  if (!f) { free(data); rt_error("أرشيف: تعذّر كتابة '%s'", dest); return false; }
  bool ok = (!dn || fwrite(data, 1, dn, f) == dn) && fflush(f) == 0;
  fclose(f);
  free(data);
  if (!ok) { rt_error("أرشيف: فشل كتابة '%s'", dest); return false; }
  (*count)++;
  return true;
}

static bool z_walk(ZipH *h, const char *path, const char *prefix, size_t plen, int depth) {
  if (depth > 32) { rt_error("أرشيف: تعشيق مجلدات عميق جدا في '%s'", path); return false; }
  DIR *d = opendir(path);
  if (!d) { rt_error("أرشيف: تعذّر فتح المجلد '%s'", path); return false; }
  struct dirent *de; bool ok = true;
  while (ok && (de = readdir(d))) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
    char sub[PATH_MAX];
    if (snprintf(sub, sizeof sub, "%s/%s", path, de->d_name) >= (int)sizeof sub) continue;
    sstat_t st;
    if (sstat(sub, &st) != 0) continue;
    char nm[2048];
    if (plen) { if (snprintf(nm, sizeof nm, "%.*s%s", (int)plen, prefix, de->d_name) >= (int)sizeof nm) continue; }
    else if (snprintf(nm, sizeof nm, "%s", de->d_name) >= (int)sizeof nm) continue;
    if (S_ISDIR(st.st_mode)) {
      char pfx[2048];
      if (snprintf(pfx, sizeof pfx, "%s/", nm) >= (int)sizeof pfx) continue;
      ok = z_walk(h, sub, pfx, strlen(pfx), depth + 1);
    } else if (S_ISREG(st.st_mode)) {
      size_t n; char *data = read_file(sub, &n);
      if (!data) { rt_error("أرشيف: تعذّرت قراءة '%s'", sub); ok = false; break; }
      bool r = z_put_data(h, nm, (uint32_t)strlen(nm), (const unsigned char *)data, (uint32_t)n);
      free(data);
      ok = r;
    }
  }
  closedir(d);
  return ok;
}

NAT(z_open_w) {
  STRA(0);
  int slot = -1;
  for (int i = 0; i < ZIP_SLOTS; i++) if (!zhs[i]) { slot = i; break; }
  if (slot < 0) FAIL("فُتحت أرشيفات كثيرة");
  ZipH *h = (ZipH *)xalloc(sizeof *h);
  memset(h, 0, sizeof *h);
  h->writer = true;
  size_t pl = strlen(AS_STR(a[0])->chars) + 1;
  h->path = (char *)xalloc(pl);
  memcpy(h->path, AS_STR(a[0])->chars, pl);
  h->f = fopen(h->path, "wb");
  if (!h->f) { free(h->path); free(h); FAIL("تعذّر إنشاء '%s'", AS_STR(a[0])->chars); }
  zhs[slot] = h;
  *res = NUMV(slot);
  return true;
}

NAT(z_add) {
  ZipH *h; if (!z_writer_arg(a[0], &h)) return false;
  STRA(1); STRA(2);
  if (!z_put_data(h, AS_STR(a[1])->chars, (uint32_t)AS_STR(a[1])->len, (const unsigned char *)AS_STR(a[2])->chars, (uint32_t)AS_STR(a[2])->len)) return false;
  *res = BOOLV(true);
  return true;
}

NAT(z_add_file) {
  ZipH *h; if (!z_writer_arg(a[0], &h)) return false;
  STRA(1);
  size_t n; char *data = read_file(AS_STR(a[1])->chars, &n);
  if (!data) FAIL("تعذّرت قراءة '%s'", AS_STR(a[1])->chars);
  const char *nm; uint32_t nl;
  if (argc > 2 && IS_T(a[2], O_STR)) { nm = AS_STR(a[2])->chars; nl = (uint32_t)AS_STR(a[2])->len; }
  else {
    const char *p = AS_STR(a[1])->chars;
    const char *b = p;
    for (const char *q = p; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
    nm = b; nl = (uint32_t)strlen(b);
  }
  bool ok = z_put_data(h, nm, nl, (const unsigned char *)data, (uint32_t)n);
  free(data);
  if (!ok) return false;
  *res = BOOLV(true);
  return true;
}

NAT(z_add_dir) {
  ZipH *h; if (!z_writer_arg(a[0], &h)) return false;
  STRA(1);
  char safe[1024];
  const char *prefix = "";
  uint32_t plen = 0;
  if (argc > 2 && IS_T(a[2], O_STR)) {
    if (!z_safe_name(AS_STR(a[2])->chars, (uint32_t)AS_STR(a[2])->len, safe, sizeof safe)) FAIL("أرشيف: بادئة مسار غير آمنة");
    plen = (uint32_t)strlen(safe);
    if (plen) { safe[plen++] = '/'; safe[plen] = 0; }
    prefix = safe;
  }
  if (!z_walk(h, AS_STR(a[1])->chars, prefix, plen, 0)) return false;
  *res = BOOLV(true);
  return true;
}

NAT(z_open_r) {
  STRA(0);
  int slot = -1;
  for (int i = 0; i < ZIP_SLOTS; i++) if (!zhs[i]) { slot = i; break; }
  if (slot < 0) FAIL("فُتحت أرشيفات كثيرة");
  ZipH *h = (ZipH *)xalloc(sizeof *h);
  memset(h, 0, sizeof *h);
  size_t pl = strlen(AS_STR(a[0])->chars) + 1;
  h->path = (char *)xalloc(pl);
  memcpy(h->path, AS_STR(a[0])->chars, pl);
  h->f = fopen(h->path, "rb");
  if (!h->f) { free(h->path); free(h); FAIL("تعذّر فتح '%s'", AS_STR(a[0])->chars); }
  if (!z_ropen(h)) { z_free(h); return false; }
  zhs[slot] = h;
  *res = NUMV(slot);
  return true;
}

NAT(z_list) {
  ZipH *h; if (!zh_arg(a[0], &h)) return false;
  List *l = new_list();
  for (int i = 0; i < h->n; i++) {
    ZipE *e = &h->e[i];
    Map *m = new_map();
    dset(&m->d, KN("اسم"), mkstr(e->name, (int)e->nlen));
    dset(&m->d, KN("حجم"), NUMV(e->usize));
    dset(&m->d, KN("مضغوط"), NUMV(e->csize));
    dset(&m->d, KN("طريقة"), mkstr(e->method == 8 ? "deflate" : "تخزين", e->method == 8 ? 7 : 10));
    dset(&m->d, KN("مجلد"), BOOLV(e->is_dir));
    varr_push(&l->a, OBJV(m));
  }
  *res = OBJV(l);
  return true;
}

NAT(z_read) {
  ZipH *h; if (!zh_arg(a[0], &h)) return false;
  STRA(1);
  ZipE *e = z_find(h, AS_STR(a[1])->chars, (uint32_t)AS_STR(a[1])->len);
  if (!e) FAIL("الإدخال '%s' غير موجود في الأرشيف", AS_STR(a[1])->chars);
  if (e->is_dir) FAIL("'%s' مجلد داخل الأرشيف ولا يمكن قراءته", AS_STR(a[1])->chars);
  unsigned char *data; uint32_t dn;
  if (!z_read_data(h, e, &data, &dn)) return false;
  *res = dn > 0 ? mkstr((const char *)data, (int)dn) : mkstr("", 0);
  free(data);
  return true;
}

NAT(z_extract) {
  ZipH *h; if (!zh_arg(a[0], &h)) return false;
  STRA(1);
  ZipE *e = z_find(h, AS_STR(a[1])->chars, (uint32_t)AS_STR(a[1])->len);
  if (!e) FAIL("الإدخال '%s' غير موجود في الأرشيف", AS_STR(a[1])->chars);
  const char *dir = ""; size_t dirlen = 0;
  if (argc > 2 && IS_T(a[2], O_STR)) { dir = AS_STR(a[2])->chars; dirlen = strlen(dir); }
  int count = 0;
  if (!z_extract_one(h, e, dir, dirlen, &count)) return false;
  *res = BOOLV(true);
  return true;
}

NAT(z_extract_all) {
  ZipH *h; if (!zh_arg(a[0], &h)) return false;
  const char *dir = ""; size_t dirlen = 0;
  if (argc > 1 && IS_T(a[1], O_STR)) { dir = AS_STR(a[1])->chars; dirlen = strlen(dir); }
  int count = 0;
  for (int i = 0; i < h->n; i++) if (!z_extract_one(h, &h->e[i], dir, dirlen, &count)) return false;
  *res = NUMV(count);
  return true;
}

NAT(z_close) {
  ZipH *h; if (!zh_arg(a[0], &h)) return false;
  zhs[(int)a[0].as.n] = NULL;
  bool ok = true;
  if (h->writer && h->f) ok = z_wfinalize(h);
  else if (h->f) { fclose(h->f); h->f = NULL; }
  z_free(h);
  if (!ok) return false;
  *res = BOOLV(true);
  return true;
}

NAT(z_has) {
  ZipH *h; if (!zh_arg(a[0], &h)) return false;
  STRA(1);
  *res = BOOLV(z_find(h, AS_STR(a[1])->chars, (uint32_t)AS_STR(a[1])->len) != NULL);
  return true;
}

static void zip_register(void) {
  Map *m = mkmod("أرشيف");
  MF(m, "افتح_كتابة", z_open_w, 1, 1);
  MF(m, "اضف", z_add, 3, 3);
  MF(m, "اضف_ملف", z_add_file, 2, 3);
  MF(m, "اضف_مجلد", z_add_dir, 2, 3);
  MF(m, "افتح_قراءة", z_open_r, 1, 1);
  MF(m, "محتويات", z_list, 1, 1);
  MF(m, "اقرأ", z_read, 2, 2);
  MF(m, "استخرج", z_extract, 2, 3);
  MF(m, "فك_الكل", z_extract_all, 1, 2);
  MF(m, "يوجد", z_has, 2, 2);
  MF(m, "اغلق", z_close, 1, 1);
}
