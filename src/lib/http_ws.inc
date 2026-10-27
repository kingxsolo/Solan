static bool z_load(void);
static bool z_inflate(const char *in, int n, Buf *out, int wbits);
typedef struct { char scheme[8], host[256], path[2048]; int port; bool tls; } Url;
static bool parse_url(const char *u, Url *o) {
  const char *p = strstr(u, "://"); if (!p || p - u > 7 || p == u) return false;
  for (int i = 0; i < p - u; i++) o->scheme[i] = (char)tolower((unsigned char)u[i]); o->scheme[p - u] = 0; p += 3;
  const char *e = p; while (*e && *e != '/' && *e != '?' && *e != '#' && *e != ':') e++;
  int hl = (int)(e - p); if (hl <= 0 || hl >= (int)sizeof o->host) return false; memcpy(o->host, p, hl); o->host[hl] = 0;
  bool tls = !strcmp(o->scheme, "https") || !strcmp(o->scheme, "wss"); o->tls = tls; o->port = tls ? 443 : 80;
  if (*e == ':') { o->port = atoi(e + 1); e++; while (*e >= '0' && *e <= '9') e++; }
  if (*e == '#' || !*e) snprintf(o->path, sizeof o->path, "/");
  else { int pl = 0; const char *q = e; if (*q == '?') o->path[pl++] = '/'; while (*q && *q != '#' && pl < (int)sizeof o->path - 1) o->path[pl++] = *q++; o->path[pl] = 0; }
  return o->port > 0;
}
static void url_encode(const char *s, int n, Buf *b) {
  for (int i = 0; i < n; i++) { unsigned char c = s[i]; if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') buf_addc(b, c); else buf_fmt(b, "%%%02X", c); }
}
static void url_decode(const char *s, int n, Buf *b, bool plus) {
  for (int i = 0; i < n; i++) {
    if (s[i] == '%' && i + 2 < n + 0 && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) { buf_addc(b, (char)(hexv(s[i + 1]) * 16 + hexv(s[i + 2]))); i += 2; }
    else if (plus && s[i] == '+') buf_addc(b, ' '); else buf_addc(b, s[i]);
  }
}
NAT(net_urlenc) { STRA(0); Buf b; buf_init(&b); url_encode(AS_STR(a[0])->chars, AS_STR(a[0])->len, &b); *res = mkstr(buf_cstr(&b), b.n); free(b.p); return true; }
NAT(net_urldec) { STRA(0); Buf b; buf_init(&b); url_decode(AS_STR(a[0])->chars, AS_STR(a[0])->len, &b, true); *res = mkstr(buf_cstr(&b), b.n); free(b.p); return true; }

static bool dict_has_ci(Dict *d, const char *name) {
  if (!d) return false;
  for (int i = 0; i < d->n; i++) { if (d->e[i].k.t == V_EMPTY || !IS_T(d->e[i].k, O_STR)) continue; if (!strcasecmp(AS_STR(d->e[i].k)->chars, name)) return true; }
  return false;
}
static void add_user_headers(Buf *rq, Dict *h) {
  if (!h) return;
  for (int i = 0; i < h->n; i++) { if (h->e[i].k.t == V_EMPTY) continue; Str *k = tostr(h->e[i].k), *v = tostr(h->e[i].v); buf_fmt(rq, "%s: %s\r\n", k->chars, v->chars); }
}
static void lower_inplace(char *s) { for (; *s; s++) *s = (char)tolower((unsigned char)*s); }

static bool read_headers(int fd, Map *hm) {
  for (;;) {
    Buf ln; buf_init(&ln); int r = rb_getline(fd, &ln);
    if (r <= 0) { free(ln.p); return false; }
    if (ln.n == 0) { free(ln.p); return true; }
    char *c = strchr(ln.p, ':');
    if (c) {
      *c = 0; char *v = c + 1; while (*v == ' ' || *v == '\t') v++; lower_inplace(ln.p);
      Value key = mkstr(ln.p, (int)strlen(ln.p)), old;
      if (dget(&hm->d, key, &old) && IS_T(old, O_STR)) { Buf j; buf_init(&j); buf_add(&j, AS_STR(old)->chars, AS_STR(old)->len); buf_add(&j, ", ", 2); buf_add(&j, v, (int)strlen(v)); dset(&hm->d, key, mkstr(j.p, j.n)); free(j.p); }
      else dset(&hm->d, key, mkstr(v, (int)strlen(v)));
    }
    free(ln.p);
  }
}
static bool hget(Map *hm, const char *k, Str **out) { Value v; if (dget(&hm->d, mkstr(k, (int)strlen(k)), &v) && IS_T(v, O_STR)) { *out = AS_STR(v); return true; } return false; }
static bool read_body(int fd, Map *hm, bool nobody, Buf *body) {
  if (nobody) return true;
  Str *te, *cl;
  if (hget(hm, "transfer-encoding", &te) && strcasestr(te->chars, "chunked")) {
    for (;;) {
      Buf ln; buf_init(&ln); int r = rb_getline(fd, &ln); if (r <= 0) { free(ln.p); return false; }
      long sz = strtol(buf_cstr(&ln), NULL, 16); free(ln.p);
      if (sz == 0) { for (;;) { Buf t; buf_init(&t); int q = rb_getline(fd, &t); int e = t.n; free(t.p); if (q <= 0 || e == 0) break; } return true; }
      if (sz < 0 || body->n + sz > (256 << 20)) return false;
      if (rb_exact(fd, (int)sz, body) != 1) return false;
      Buf crlf; buf_init(&crlf); rb_getline(fd, &crlf); free(crlf.p);
    }
  }
  if (hget(hm, "content-length", &cl)) {
    long n = atol(cl->chars); if (n < 0 || n > (256 << 20)) return false;
    return n == 0 || rb_exact(fd, (int)n, body) == 1;
  }
  for (;;) {
    Buf *b = &rbuf[fd]; if (b->n) { buf_add(body, b->p, b->n); rb_consume(fd, b->n); }
    if (body->n > (256 << 20)) return false;
    int r = rb_fill(fd); if (r == 0) return true; if (r < 0) return r == -2 ? false : true;
  }
}

static bool http_stream_emit(Value callback, const char *data, int n, bool *callback_failed) {
  if (n <= 0) return true;
  Value arg = mkstr(data, n), out; Value *saved = vm.sp; push(arg);
  bool ok = call_fn(callback, 1, &arg, &out);
  vm.sp = saved;
  if (!ok) { *callback_failed = true; return false; }
  return true;
}

static bool read_stream_payload(int fd, long remaining, Value callback, bool collect, Buf *body, long *total, bool *callback_failed) {
  bool until_eof = remaining < 0;
  while (until_eof || remaining > 0) {
    Buf *b = &rbuf[fd];
    if (b->n == 0) {
      int r = rb_fill(fd);
      if (r == 0) return until_eof;
      if (r < 0) return false;
      b = &rbuf[fd];
    }
    int n = b->n > 16384 ? 16384 : b->n;
    if (!until_eof && (long)n > remaining) n = (int)remaining;
    if (n <= 0 || *total + n > (256L << 20)) return false;
    char piece[16384]; memcpy(piece, b->p, (size_t)n); rb_consume(fd, n);
    *total += n;
    if (collect) buf_add(body, piece, n);
    if (!http_stream_emit(callback, piece, n, callback_failed)) return false;
    if (!until_eof) remaining -= n;
  }
  return true;
}

static bool read_body_stream(int fd, Map *hm, bool nobody, Value callback, bool collect, Buf *body, bool *callback_failed) {
  if (nobody) return true;
  Str *te, *cl; long total = 0;
  if (hget(hm, "transfer-encoding", &te) && strcasestr(te->chars, "chunked")) {
    for (;;) {
      Buf ln; buf_init(&ln); int r = rb_getline(fd, &ln);
      if (r <= 0) { free(ln.p); return false; }
      long sz = strtol(buf_cstr(&ln), NULL, 16); free(ln.p);
      if (sz == 0) {
        for (;;) { Buf t; buf_init(&t); int q = rb_getline(fd, &t); int e = t.n; free(t.p); if (q <= 0 || e == 0) break; }
        return true;
      }
      if (sz < 0 || total + sz > (256L << 20)) return false;
      if (!read_stream_payload(fd, sz, callback, collect, body, &total, callback_failed)) return false;
      Buf crlf; buf_init(&crlf); int q = rb_getline(fd, &crlf); free(crlf.p); if (q <= 0) return false;
    }
  }
  if (hget(hm, "content-length", &cl)) {
    long n = atol(cl->chars); if (n < 0 || n > (256L << 20)) return false;
    return n == 0 || read_stream_payload(fd, n, callback, collect, body, &total, callback_failed);
  }
  return read_stream_payload(fd, -1, callback, collect, body, &total, callback_failed);
}

static int http_request(const char *method0, const char *url0, Dict *hdrs, Str *body0, bool bodyjson, int tmo, bool verify, int maxredir, Value callback, bool collect_stream_body, Value *res) {
  bool streaming = !IS_NIL(callback);
  char method[16]; snprintf(method, sizeof method, "%s", method0); char url[4096]; snprintf(url, sizeof url, "%s", url0); Str *body = body0;
  for (int hop = 0;; hop++) {
    Url u; if (!parse_url(url, &u) || (strcmp(u.scheme, "http") && strcmp(u.scheme, "https"))) { rt_error("رابط غير صالح: %s", url); return 1; }
    if (u.tls && !tls_load()) return 2;
    char err[300]; int fd = tcp_connect(u.host, u.port, tmo, err, sizeof err); if (fd < 0) { rt_error("%s", err); return 1; }
    if (u.tls && !tls_wrap_client(fd, u.host, verify, err, sizeof err)) { conn_close(fd); rt_error("%s", err); return 1; }
    tmo_ms[fd] = tmo;
    Buf rq; buf_init(&rq); buf_fmt(&rq, "%s %s HTTP/1.1\r\nHost: %s", method, u.path, u.host);
    if ((u.tls && u.port != 443) || (!u.tls && u.port != 80)) buf_fmt(&rq, ":%d", u.port);
    buf_fmt(&rq, "\r\nConnection: close\r\n");
    if (streaming) { if (!dict_has_ci(hdrs, "accept-encoding")) buf_fmt(&rq, "Accept-Encoding: identity\r\n"); }
    else buf_fmt(&rq, "Accept-Encoding: %s\r\n", z_load() ? "gzip" : "identity");
    if (!dict_has_ci(hdrs, "user-agent")) buf_fmt(&rq, "User-Agent: solan/%s\r\n", SOLAN_VERSION);
    if (!dict_has_ci(hdrs, "accept")) buf_fmt(&rq, "Accept: */*\r\n");
    add_user_headers(&rq, hdrs);
    if (body || !strcmp(method, "POST") || !strcmp(method, "PUT") || !strcmp(method, "PATCH")) {
      buf_fmt(&rq, "Content-Length: %d\r\n", body ? body->len : 0);
      if (bodyjson && !dict_has_ci(hdrs, "content-type")) buf_fmt(&rq, "Content-Type: application/json\r\n");
    }
    buf_fmt(&rq, "\r\n"); if (body) buf_add(&rq, body->chars, body->len);
    int wr = conn_write(fd, rq.p, rq.n); free(rq.p);
    if (wr < 0) { conn_close(fd); rt_error("فشل إرسال الطلب"); return 1; }
    Buf st; buf_init(&st); int r = rb_getline(fd, &st);
    if (r <= 0) { free(st.p); conn_close(fd); rt_error(r == -2 ? "انتهت مهلة الاتصال" : "أغلق الخادم الاتصال بدون رد"); return 1; }
    int code = 0; if (strncmp(st.p, "HTTP/", 5) == 0) { char *sp = strchr(st.p, ' '); if (sp) code = atoi(sp + 1); }
    free(st.p); if (!code) { conn_close(fd); rt_error("رد HTTP غير صالح"); return 1; }
    Map *hm = new_map(); push(OBJV(hm));
    if (!read_headers(fd, hm)) { vm.sp--; conn_close(fd); rt_error("فشل قراءة رؤوس الرد"); return 1; }
    Str *loc = NULL;
    bool redirect = (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) && hop < maxredir && hget(hm, "location", &loc);
    Buf bd; buf_init(&bd); bool callback_failed = false;
    bool no_body = !strcmp(method, "HEAD") || code == 204 || code == 304 || code < 200;
    bool ok = streaming && !redirect
      ? read_body_stream(fd, hm, no_body, callback, collect_stream_body, &bd, &callback_failed)
      : read_body(fd, hm, no_body, &bd);
    conn_close(fd);
    { Str *ce; if (ok && bd.n > 0 && hget(hm, "content-encoding", &ce) && (strcasestr(ce->chars, "gzip") || strcasestr(ce->chars, "deflate"))) { Buf dec; buf_init(&dec); if (z_inflate(bd.p, bd.n, &dec, 47)) { free(bd.p); bd = dec; } else free(dec.p); } }
    if (callback_failed) { vm.sp--; free(bd.p); return 1; }
    if (streaming && !ok) { vm.sp--; free(bd.p); rt_error("فشل قراءة جسم الرد المتدفق (انتهت المهلة؟)"); return 1; }
    if (!ok && bd.n == 0) { vm.sp--; free(bd.p); rt_error("فشل قراءة جسم الرد (انتهت المهلة؟)"); return 1; }
    if (redirect) {
      char nu[4096];
      if (strstr(loc->chars, "://")) snprintf(nu, sizeof nu, "%s", loc->chars);
      else if (loc->chars[0] == '/') snprintf(nu, sizeof nu, "%s://%s:%d%s", u.scheme, u.host, u.port, loc->chars);
      else snprintf(nu, sizeof nu, "%s://%s:%d/%s", u.scheme, u.host, u.port, loc->chars);
      snprintf(url, sizeof url, "%s", nu);
      if (code == 303 || ((code == 301 || code == 302) && strcmp(method, "GET") && strcmp(method, "HEAD"))) { snprintf(method, sizeof method, "GET"); body = NULL; }
      vm.sp--; free(bd.p); continue;
    }
    Map *r2 = new_map(); dset(&r2->d, KX("حالة"), NUMV(code)); dset(&r2->d, KN("جسم"), mkstr(buf_cstr(&bd), bd.n));
    dset(&r2->d, KX("رؤوس"), OBJV(hm)); dset(&r2->d, KN("ناجح"), BOOLV(code >= 200 && code < 300)); dset(&r2->d, KN("رابط"), mkstr(url, (int)strlen(url)));
    vm.sp--; free(bd.p); *res = OBJV(r2); return 0;
  }
}
static bool net_fetch_curl(int argc, Value *a, Value *res);
static const char *mime_of(const char *p) {
  const char *d = strrchr(p, '.'); if (!d) return "application/octet-stream"; d++;
  static const struct { const char *e, *m; } t[] = {{"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"gif", "image/gif"}, {"webp", "image/webp"}, {"pdf", "application/pdf"},
    {"txt", "text/plain"}, {"json", "application/json"}, {"mp4", "video/mp4"}, {"mp3", "audio/mpeg"}, {"ogg", "audio/ogg"}, {"zip", "application/zip"}, {"html", "text/html"}, {NULL, NULL}};
  for (int i = 0; t[i].e; i++) if (!strcasecmp(d, t[i].e)) return t[i].m;
  return "application/octet-stream";
}
static bool build_multipart(Dict *form, Buf *mp, char *boundary, int bs) {
  snprintf(boundary, bs, "----solan%08x%08x", (unsigned)rng_next(), (unsigned)rng_next());
  for (int i = 0; i < form->n; i++) {
    if (form->e[i].k.t == V_EMPTY) continue; Str *k = tostr(form->e[i].k); Value v = form->e[i].v;
    buf_fmt(mp, "--%s\r\n", boundary);
    if (IS_T(v, O_MAP)) {
      Dict *fd = &AS_MAP(v)->d; Value pv, nv, tv; const char *fname = "file"; const char *ctype = NULL; char *fdata = NULL; size_t flen = 0; bool own = false;
      if (mget(fd, KN("اسم_الملف"), &nv) && IS_T(nv, O_STR)) fname = AS_STR(nv)->chars;
      if (mget(fd, KN("ملف"), &pv) && IS_T(pv, O_STR)) {
        fdata = read_file(AS_STR(pv)->chars, &flen); if (!fdata) { rt_error("تعذّر قراءة الملف '%s'", AS_STR(pv)->chars); return false; } own = true;
        if (!mget(fd, KN("اسم_الملف"), &nv)) { const char *sl = strrchr(AS_STR(pv)->chars, '/'); fname = sl ? sl + 1 : AS_STR(pv)->chars; }
        ctype = mime_of(AS_STR(pv)->chars);
      } else if (mget(fd, KN("بيانات"), &pv) && IS_T(pv, O_STR)) { fdata = AS_STR(pv)->chars; flen = AS_STR(pv)->len; ctype = "application/octet-stream"; }
      else { rt_error("حقل النموذج '%s' يحتاج 'ملف' أو 'بيانات'", k->chars); return false; }
      if (mget(fd, KN("نوع"), &tv) && IS_T(tv, O_STR)) ctype = AS_STR(tv)->chars;
      buf_fmt(mp, "Content-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\nContent-Type: %s\r\n\r\n", k->chars, fname, ctype);
      buf_add(mp, fdata, (int)flen); buf_add(mp, "\r\n", 2); if (own) free(fdata);
    } else { Str *t = tostr(v); buf_fmt(mp, "Content-Disposition: form-data; name=\"%s\"\r\n\r\n", k->chars); buf_add(mp, t->chars, t->len); buf_add(mp, "\r\n", 2); }
  }
  buf_fmt(mp, "--%s--\r\n", boundary); return true;
}
NAT(net_fetch) {
  PERM(PM_NET);
  STRA(0); Value m = optv(argc, a, 1, "طريقة"), h = optv(argc, a, 1, "رؤوس"), b = optv(argc, a, 1, "جسم"), tm = optv(argc, a, 1, "مهلة"), vf = optv(argc, a, 1, "تحقق"), rd = optv(argc, a, 1, "تحويلات");
  Value fm = optv(argc, a, 1, "نموذج"), fu = optv(argc, a, 1, "نموذج_رابط");
  char method[16] = "GET"; if (IS_T(m, O_STR)) { snprintf(method, sizeof method, "%s", AS_STR(m)->chars); for (char *c = method; *c; c++) *c = (char)toupper((unsigned char)*c); } else if (!IS_NIL(b) || IS_T(fm, O_MAP) || IS_T(fu, O_MAP)) snprintf(method, sizeof method, "POST");
  Str *body = NULL; bool js = false; Map *hm2 = NULL;
  if (IS_T(fm, O_MAP) || IS_T(fu, O_MAP)) {
    hm2 = new_map(); push(OBJV(hm2));
    if (IS_T(h, O_MAP)) { Dict *hd = &AS_MAP(h)->d; for (int i = 0; i < hd->n; i++) if (hd->e[i].k.t != V_EMPTY) dset(&hm2->d, hd->e[i].k, hd->e[i].v); }
    Buf mp; buf_init(&mp); char ct[200];
    if (IS_T(fm, O_MAP)) {
      char bd[64]; if (!build_multipart(&AS_MAP(fm)->d, &mp, bd, sizeof bd)) { free(mp.p); vm.sp--; return false; }
      snprintf(ct, sizeof ct, "multipart/form-data; boundary=%s", bd);
    } else {
      Dict *fd = &AS_MAP(fu)->d; bool first = true;
      for (int i = 0; i < fd->n; i++) { if (fd->e[i].k.t == V_EMPTY) continue; Str *k = tostr(fd->e[i].k), *v = tostr(fd->e[i].v); if (!first) buf_addc(&mp, '&'); first = false; url_encode(k->chars, k->len, &mp); buf_addc(&mp, '='); url_encode(v->chars, v->len, &mp); }
      snprintf(ct, sizeof ct, "application/x-www-form-urlencoded");
    }
    dset(&hm2->d, mkstr("Content-Type", 12), mkstr(ct, (int)strlen(ct))); body = intern(buf_cstr(&mp), mp.n); free(mp.p); h = OBJV(hm2);
  } else if (!IS_NIL(b)) { if (IS_T(b, O_MAP) || IS_T(b, O_LIST)) { Buf jb; buf_init(&jb); json_enc(b, &jb, 0, 0); body = intern(buf_cstr(&jb), jb.n); free(jb.p); js = true; } else body = tostr(b); }
  push(body ? OBJV(body) : NIL);
  int r = http_request(method, AS_STR(a[0])->chars, IS_T(h, O_MAP) ? &AS_MAP(h)->d : NULL, body, js, IS_NUM(tm) ? (int)(tm.as.n * 1000) : 60000, IS_NIL(vf) || !falsey(vf), IS_NUM(rd) ? (int)rd.as.n : 5, NIL, false, res);
  vm.sp--; if (hm2) vm.sp--;
  if (r == 2) return net_fetch_curl(argc, a, res);
  return r == 0;
}

NAT(net_fetch_stream) {
  PERM(PM_NET); STRA(0);
  if (argc < 2 || !(IS_T(a[1], O_CLO) || IS_T(a[1], O_NAT) || IS_T(a[1], O_BOUND))) FAIL("جلب_متدفق يحتاج دالة callback كوسيط ثانٍ");
  Value callback = a[1];
  Value m = optv(argc, a, 2, "طريقة"), h = optv(argc, a, 2, "رؤوس"), b = optv(argc, a, 2, "جسم");
  Value tm = optv(argc, a, 2, "مهلة"), vf = optv(argc, a, 2, "تحقق"), rd = optv(argc, a, 2, "تحويلات");
  Value fm = optv(argc, a, 2, "نموذج"), fu = optv(argc, a, 2, "نموذج_رابط"), collect_v = optv(argc, a, 2, "اجمع_الجسم");
  bool collect = !IS_NIL(collect_v) && !falsey(collect_v);
  char method[16] = "GET";
  if (IS_T(m, O_STR)) { snprintf(method, sizeof method, "%s", AS_STR(m)->chars); for (char *c = method; *c; c++) *c = (char)toupper((unsigned char)*c); }
  else if (!IS_NIL(b) || IS_T(fm, O_MAP) || IS_T(fu, O_MAP)) snprintf(method, sizeof method, "POST");
  Str *body = NULL; bool js = false; Map *hm2 = NULL;
  if (IS_T(fm, O_MAP) || IS_T(fu, O_MAP)) {
    hm2 = new_map(); push(OBJV(hm2));
    if (IS_T(h, O_MAP)) { Dict *hd = &AS_MAP(h)->d; for (int i = 0; i < hd->n; i++) if (hd->e[i].k.t != V_EMPTY) dset(&hm2->d, hd->e[i].k, hd->e[i].v); }
    Buf mp; buf_init(&mp); char ct[200];
    if (IS_T(fm, O_MAP)) {
      char bd[64]; if (!build_multipart(&AS_MAP(fm)->d, &mp, bd, sizeof bd)) { free(mp.p); vm.sp--; return false; }
      snprintf(ct, sizeof ct, "multipart/form-data; boundary=%s", bd);
    } else {
      Dict *fd = &AS_MAP(fu)->d; bool first = true;
      for (int i = 0; i < fd->n; i++) { if (fd->e[i].k.t == V_EMPTY) continue; Str *k = tostr(fd->e[i].k), *v = tostr(fd->e[i].v); if (!first) buf_addc(&mp, '&'); first = false; url_encode(k->chars, k->len, &mp); buf_addc(&mp, '='); url_encode(v->chars, v->len, &mp); }
      snprintf(ct, sizeof ct, "application/x-www-form-urlencoded");
    }
    dset(&hm2->d, mkstr("Content-Type", 12), mkstr(ct, (int)strlen(ct))); body = intern(buf_cstr(&mp), mp.n); free(mp.p); h = OBJV(hm2);
  } else if (!IS_NIL(b)) { if (IS_T(b, O_MAP) || IS_T(b, O_LIST)) { Buf jb; buf_init(&jb); json_enc(b, &jb, 0, 0); body = intern(buf_cstr(&jb), jb.n); free(jb.p); js = true; } else body = tostr(b); }
  push(body ? OBJV(body) : NIL);
  int r = http_request(method, AS_STR(a[0])->chars, IS_T(h, O_MAP) ? &AS_MAP(h)->d : NULL, body, js,
    IS_NUM(tm) ? (int)(tm.as.n * 1000) : 60000, IS_NIL(vf) || !falsey(vf), IS_NUM(rd) ? (int)rd.as.n : 5,
    callback, collect, res);
  vm.sp--; if (hm2) vm.sp--;
  if (r == 2) FAIL("جلب_متدفق عبر HTTPS يحتاج OpenSSL 3 وقت التشغيل؛ لا يستخدم fallback مجمّعاً كي لا يفقد التدفق الحي");
  return r == 0;
}

static const char *status_text(int c) {
  switch (c) { case 200: return "OK"; case 201: return "Created"; case 204: return "No Content"; case 301: return "Moved Permanently"; case 302: return "Found";
    case 304: return "Not Modified"; case 400: return "Bad Request"; case 401: return "Unauthorized"; case 403: return "Forbidden"; case 404: return "Not Found";
    case 405: return "Method Not Allowed"; case 500: return "Internal Server Error"; default: return "OK"; }
}
static void send_response(int fd, Value r) {
  int status = 200; Str *body = NULL; const char *ctype = "text/plain; charset=utf-8"; Dict *extra = NULL; Buf jb; buf_init(&jb);
  if (IS_T(r, O_STR)) { body = AS_STR(r); if (body->len && body->chars[0] == '<') ctype = "text/html; charset=utf-8"; }
  else if (IS_NIL(r)) status = 204;
  else if (IS_T(r, O_MAP)) {
    Dict *d = &AS_MAP(r)->d; Value v;
    if (mget(d, KN("حالة"), &v) && IS_NUM(v)) status = (int)v.as.n;
    if (mget(d, KN("رؤوس"), &v) && IS_T(v, O_MAP)) extra = &AS_MAP(v)->d;
    if (mget(d, KN("جسم"), &v) && !IS_NIL(v)) {
      if (IS_T(v, O_STR)) { body = AS_STR(v); if (body->len && body->chars[0] == '<') ctype = "text/html; charset=utf-8"; }
      else if (IS_T(v, O_MAP) || IS_T(v, O_LIST)) { json_enc(v, &jb, 0, 0); body = intern(buf_cstr(&jb), jb.n); ctype = "application/json; charset=utf-8"; }
      else body = tostr(v);
    }
  } else if (IS_T(r, O_LIST)) { json_enc(r, &jb, 0, 0); body = intern(buf_cstr(&jb), jb.n); ctype = "application/json; charset=utf-8"; }
  else body = tostr(r);
  free(jb.p);
  Buf o; buf_init(&o); buf_fmt(&o, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\nConnection: close\r\n", status, status_text(status), body ? body->len : 0);
  if (!dict_has_ci(extra, "content-type")) buf_fmt(&o, "Content-Type: %s\r\n", ctype);
  add_user_headers(&o, extra); buf_fmt(&o, "\r\n"); if (body && status != 204) buf_add(&o, body->chars, body->len);
  conn_write(fd, o.p, o.n); free(o.p);
}
static Map *parse_query(const char *q) {
  Map *m = new_map();
  while (q && *q) {
    const char *amp = strchr(q, '&'); int len = amp ? (int)(amp - q) : (int)strlen(q); const char *eq = memchr(q, '=', len);
    Buf k, v; buf_init(&k); buf_init(&v);
    if (eq) { url_decode(q, (int)(eq - q), &k, true); url_decode(eq + 1, len - (int)(eq - q) - 1, &v, true); } else url_decode(q, len, &k, true);
    if (k.n) dset(&m->d, mkstr(buf_cstr(&k), k.n), mkstr(buf_cstr(&v), v.n)); free(k.p); free(v.p);
    if (!amp) break; q = amp + 1;
  }
  return m;
}

static Value read_request(int fd, const char *ip) {
  Buf ln; buf_init(&ln); int r = rb_getline(fd, &ln); if (r <= 0 || ln.n == 0) { free(ln.p); return NIL; }
  char *sp1 = strchr(ln.p, ' '); if (!sp1) { free(ln.p); return NIL; } *sp1 = 0; char *target = sp1 + 1; char *sp2 = strchr(target, ' '); if (sp2) *sp2 = 0;
  Map *req = new_map(); push(OBJV(req));
  dset(&req->d, KX("طريقة"), mkstr(ln.p, (int)strlen(ln.p)));
  char *qm = strchr(target, '?'); Buf pth; buf_init(&pth); url_decode(target, qm ? (int)(qm - target) : (int)strlen(target), &pth, false);
  dset(&req->d, KN("مسار"), mkstr(buf_cstr(&pth), pth.n)); free(pth.p);
  dset(&req->d, KN("استعلام"), OBJV(parse_query(qm ? qm + 1 : "")));
  dset(&req->d, KN("عنوان"), mkstr(ip, (int)strlen(ip))); free(ln.p);
  Map *hm = new_map(); dset(&req->d, KX("رؤوس"), OBJV(hm)); if (!read_headers(fd, hm)) { vm.sp--; return NIL; }
  Str *cl; Buf body; buf_init(&body);
  if (hget(hm, "content-length", &cl)) { long n = atol(cl->chars); if (n > 0 && n < (64 << 20)) rb_exact(fd, (int)n, &body); }
  dset(&req->d, KN("جسم"), mkstr(buf_cstr(&body), body.n)); free(body.p);
  vm.sp--; return OBJV(req);
}
static void handle_conn(int c, const char *ip, Value handler) {
  tmo_ms[c] = 30000; Value req = read_request(c, ip);
  if (!IS_NIL(req)) {
    Value *sv = vm.sp; Value out; push(req);
    if (call_fn(handler, 1, &req, &out)) send_response(c, out);
    else {
      Str *e = tostr(vm.exc); fprintf(stderr, "خطأ في المعالج: %s\n", e->chars); vm.sp = sv; vm.exc = NIL;
      Map *em = new_map(); push(OBJV(em)); dset(&em->d, KX("حالة"), NUMV(500)); dset(&em->d, KN("جسم"), mkstr("خطأ داخلي", (int)strlen("خطأ داخلي")));
      send_response(c, OBJV(em)); vm.sp--;
    }
    vm.sp = sv;
  }
  conn_close(c);
}
NAT(net_serve) {
  PERM(PM_NET);
  NUMA(0); if (!(IS_T(a[1], O_CLO) || IS_T(a[1], O_NAT) || IS_T(a[1], O_BOUND))) FAIL("الخادم يحتاج دالة معالجة");
  Value ad = optv(argc, a, 2, "عنوان"), ce = optv(argc, a, 2, "شهادة"), ke = optv(argc, a, 2, "مفتاح"), mx = optv(argc, a, 2, "اقصى_طلبات"), fk = optv(argc, a, 2, "عمليات"); char err[300];
  int lfd = listen_on((int)a[0].as.n, IS_T(ad, O_STR) ? AS_STR(ad)->chars : NULL, IS_T(ce, O_STR) ? AS_STR(ce)->chars : NULL, IS_T(ke, O_STR) ? AS_STR(ke)->chars : NULL, err, sizeof err);
  if (lfd < 0) FAIL("%s", err);
  bool forkmode = !IS_NIL(fk) && !falsey(fk);
#if SOLAN_FORK
  if (forkmode) signal(SIGCHLD, SIG_IGN);
#else
  forkmode = false;
#endif
  int served = 0, limit = IS_NUM(mx) ? (int)mx.as.n : -1;
  while (limit < 0 || served < limit) {
    char ip[64]; int c = accept_one(lfd, ip, sizeof ip); if (c == -2) continue; if (c < 0) break;
    if (forkmode) {
#if SOLAN_FORK
      fflush(stdout); pid_t pid = fork();
      if (pid == 0) { close(lfd); handle_conn(c, ip, a[1]); fflush(stdout); _exit(0); }
#else
      pid_t pid = 1;
#endif
      if (ssl_of[c]) tl.SSL_free(ssl_of[c]); close(c); fd_setup(c); served++; continue;
    }
    handle_conn(c, ip, a[1]); served++;
  }
  conn_close(lfd); *res = NUMV(served); return true;
}
NAT(net_readreq) { FDA(0, fd); STRA(1); tmo_ms[fd] = 5000; *res = read_request(fd, AS_STR(a[1])->chars); return true; }
NAT(net_writeresp) { FDA(0, fd); send_response(fd, a[1]); *res = NIL; return true; }

static int ws_send_frame(int fd, int opcode, const char *d, size_t n) {
  bool mask = !(fflag[fd] & 2); Buf f; buf_init(&f); buf_addc(&f, (char)(0x80 | opcode)); uint8_t mk = mask ? 0x80 : 0;
  if (n < 126) buf_addc(&f, (char)(mk | n)); else if (n < 65536) { buf_addc(&f, (char)(mk | 126)); buf_addc(&f, (char)(n >> 8)); buf_addc(&f, (char)(n & 255)); }
  else { buf_addc(&f, (char)(mk | 127)); for (int i = 7; i >= 0; i--) buf_addc(&f, (char)((n >> (8 * i)) & 255)); }
  if (mask) { uint8_t m[4]; for (int i = 0; i < 4; i++) m[i] = rng_next() & 255; buf_add(&f, (char *)m, 4); for (size_t i = 0; i < n; i++) buf_addc(&f, (char)(d[i] ^ m[i & 3])); }
  else buf_add(&f, d, (int)n);
  int r = conn_write(fd, f.p, f.n); free(f.p); return r;
}

static int ws_recv(int fd, int tmo, Buf *msg) {
  int saved = tmo_ms[fd]; int result = -1; msg->n = 0;
  for (;;) {
    if (tmo >= 0) tmo_ms[fd] = tmo;
    Buf h; buf_init(&h); int r = rb_exact(fd, 2, &h);
    tmo_ms[fd] = -1;
    if (r != 1) { free(h.p); result = r == 0 ? 0 : r; break; }
    int b0 = (uint8_t)h.p[0], b1 = (uint8_t)h.p[1]; free(h.p);
    int op = b0 & 15; bool fin = b0 & 0x80, masked = b1 & 0x80; uint64_t len = b1 & 127;
    Buf ex; buf_init(&ex);
    if (len == 126) { if (rb_exact(fd, 2, &ex) != 1) { free(ex.p); break; } len = ((uint8_t)ex.p[0] << 8) | (uint8_t)ex.p[1]; }
    else if (len == 127) { if (rb_exact(fd, 8, &ex) != 1) { free(ex.p); break; } len = 0; for (int i = 0; i < 8; i++) len = (len << 8) | (uint8_t)ex.p[i]; }
    free(ex.p);
    uint8_t mk[4] = {0}; if (masked) { Buf mb; buf_init(&mb); if (rb_exact(fd, 4, &mb) != 1) { free(mb.p); break; } memcpy(mk, mb.p, 4); free(mb.p); }
    if (len > (256u << 20)) break;
    Buf pl; buf_init(&pl); if (len > 0 && rb_exact(fd, (int)len, &pl) != 1) { free(pl.p); break; }
    if (masked) for (uint64_t i = 0; i < len; i++) pl.p[i] ^= mk[i & 3];
    if (op == 8) { ws_send_frame(fd, 8, pl.p ? pl.p : "", pl.n > 2 ? 2 : pl.n); free(pl.p); result = 0; break; }
    if (op == 9) { ws_send_frame(fd, 10, pl.p ? pl.p : "", pl.n); free(pl.p); continue; }
    if (op == 10) { free(pl.p); continue; }
    if (pl.n) buf_add(msg, pl.p, pl.n); free(pl.p);
    if (fin) { result = 1; break; }
  }
  tmo_ms[fd] = saved; return result;
}
NAT(ws_connect) {
  STRA(0); Url u; if (!parse_url(AS_STR(a[0])->chars, &u)) FAIL("رابط غير صالح");
  Value h = optv(argc, a, 1, "رؤوس"), tm = optv(argc, a, 1, "مهلة"), vf = optv(argc, a, 1, "تحقق");
  int tmo = IS_NUM(tm) ? (int)(tm.as.n * 1000) : 15000; char err[300];
  int fd = tcp_connect(u.host, u.port, tmo, err, sizeof err); if (fd < 0) FAIL("%s", err);
  if (u.tls && !tls_wrap_client(fd, u.host, IS_NIL(vf) || !falsey(vf), err, sizeof err)) { conn_close(fd); FAIL("%s", err); }
  uint8_t key[16]; for (int i = 0; i < 16; i++) key[i] = rng_next() & 255; Buf kb; buf_init(&kb); b64_enc(key, 16, &kb);
  Buf rq; buf_init(&rq); buf_fmt(&rq, "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n", u.path, u.host, u.port, buf_cstr(&kb));
  free(kb.p); if (IS_T(h, O_MAP)) add_user_headers(&rq, &AS_MAP(h)->d); buf_fmt(&rq, "\r\n");
  tmo_ms[fd] = tmo; int w = conn_write(fd, rq.p, rq.n); free(rq.p); if (w < 0) { conn_close(fd); FAIL("فشل إرسال طلب الترقية"); }
  Buf st; buf_init(&st); int r = rb_getline(fd, &st); int code = 0; if (r > 0) { char *sp = strchr(st.p, ' '); if (sp) code = atoi(sp + 1); } free(st.p);
  if (code != 101) { conn_close(fd); FAIL("رفض الخادم اتصال WebSocket (الحالة %d)", code); }
  Map *hm = new_map(); push(OBJV(hm)); bool ok = read_headers(fd, hm); vm.sp--; if (!ok) { conn_close(fd); FAIL("فشل قراءة رد WebSocket"); }
  fflag[fd] |= 1; tmo_ms[fd] = -1; *res = NUMV(fd); return true;
}
NAT(ws_upgrade) {
  FDA(0, fd); Buf ln; buf_init(&ln); int r = rb_getline(fd, &ln);
  if (r <= 0) { free(ln.p); *res = NIL; return true; }
  char *s1 = strchr(ln.p, ' '); char path[1024] = "/"; if (s1) { char *s2 = strchr(s1 + 1, ' '); if (s2) *s2 = 0; snprintf(path, sizeof path, "%s", s1 + 1); } free(ln.p);
  Map *hm = new_map(); push(OBJV(hm)); if (!read_headers(fd, hm)) { vm.sp--; *res = NIL; return true; }
  Str *key; if (!hget(hm, "sec-websocket-key", &key)) { vm.sp--; *res = NIL; return true; }
  Buf cat; buf_init(&cat); buf_add(&cat, key->chars, key->len); buf_fmt(&cat, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
  uint8_t dg[20]; sha1_raw((uint8_t *)cat.p, cat.n, dg); free(cat.p); Buf acc; buf_init(&acc); b64_enc(dg, 20, &acc);
  Buf rs; buf_init(&rs); buf_fmt(&rs, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", buf_cstr(&acc));
  free(acc.p); conn_write(fd, rs.p, rs.n); free(rs.p); fflag[fd] |= 3;
  Map *out = new_map(); dset(&out->d, KN("مسار"), mkstr(path, (int)strlen(path))); dset(&out->d, KX("رؤوس"), OBJV(hm)); vm.sp--; *res = OBJV(out); return true;
}
NAT(ws_send) {
  FDA(0, fd); Str *s = tostr(a[1]); bool bin = argc > 2 && !falsey(a[2]);
  if (ws_send_frame(fd, bin ? 2 : 1, s->chars, s->len) < 0) FAIL("فشل إرسال الرسالة (الاتصال مغلق؟)"); *res = NUMV(s->len); return true;
}
NAT(ws_recv_n) {
  FDA(0, fd); int tmo = argc > 1 && IS_NUM(a[1]) ? (int)(a[1].as.n * 1000) : -1; Buf m; buf_init(&m); int r = ws_recv(fd, tmo, &m);
  if (r == 1) *res = mkstr(buf_cstr(&m), m.n); else if (r == -2) *res = BOOLV(false); else *res = NIL;
  free(m.p); return true;
}
NAT(ws_ping) { FDA(0, fd); ws_send_frame(fd, 9, "", 0); *res = NIL; return true; }
NAT(ws_close) { FDA(0, fd); ws_send_frame(fd, 8, "\x03\xe8", 2); conn_close(fd); *res = NIL; return true; }
