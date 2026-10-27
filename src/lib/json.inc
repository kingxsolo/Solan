static void json_nl(Buf *b, int indent, int lvl) { if (indent > 0) { buf_addc(b, '\n'); for (int i = 0; i < indent * lvl; i++) buf_addc(b, ' '); } }
static void json_enc(Value v, Buf *b, int indent, int lvl) {
  if (lvl > 60) { buf_fmt(b, "null"); return; }
  switch (v.t) {
    case V_NIL: case V_EMPTY: buf_fmt(b, "null"); return;
    case V_BOOL: buf_fmt(b, v.as.b ? "true" : "false"); return;
    case V_NUM: if (isnan(v.as.n) || isinf(v.as.n)) buf_fmt(b, "null"); else fmt_num_raw(v.as.n, b); return;
    default: break;
  }
  switch (v.as.o->t) {
    case O_STR: json_str(b, AS_STR(v)); return;
    case O_LIST: {
      List *l = AS_LIST(v); if (!l->a.n) { buf_fmt(b, "[]"); return; }
      buf_addc(b, '[');
      for (int i = 0; i < l->a.n; i++) { if (i) buf_addc(b, ','); json_nl(b, indent, lvl + 1); json_enc(l->a.v[i], b, indent, lvl + 1); }
      json_nl(b, indent, lvl); buf_addc(b, ']'); return;
    }
    case O_MAP: case O_INST: {
      Dict *d = v.as.o->t == O_MAP ? &AS_MAP(v)->d : &AS_INST(v)->fields; bool first = true;
      buf_addc(b, '{');
      for (int i = 0; i < d->n; i++) {
        if (d->e[i].k.t == V_EMPTY) continue;
        if (!first) buf_addc(b, ','); first = false; json_nl(b, indent, lvl + 1);
        json_str(b, tostr(d->e[i].k)); buf_addc(b, ':'); if (indent > 0) buf_addc(b, ' '); json_enc(d->e[i].v, b, indent, lvl + 1);
      }
      if (!first) json_nl(b, indent, lvl);
      buf_addc(b, '}'); return;
    }
    default: json_str(b, tostr(v)); return;
  }
}
NAT(j_enc) {
  Buf b; buf_init(&b); json_enc(a[0], &b, argc > 1 && IS_NUM(a[1]) ? (int)a[1].as.n : 0, 0);
  *res = mkstr(buf_cstr(&b), b.n); free(b.p); return true;
}
typedef struct { const char *p, *end; bool err; } JP;
static void jws(JP *j) { while (j->p < j->end && (*j->p == ' ' || *j->p == '\n' || *j->p == '\t' || *j->p == '\r')) j->p++; }
static Value jval(JP *j, int depth);
static Value jstr(JP *j) {
  Buf b; buf_init(&b); j->p++;
  while (j->p < j->end && *j->p != '"') {
    if (*j->p == '\\' && j->p + 1 < j->end) {
      j->p++;
      switch (*j->p) {
        case 'n': buf_addc(&b, '\n'); break; case 't': buf_addc(&b, '\t'); break; case 'r': buf_addc(&b, '\r'); break;
        case 'b': buf_addc(&b, '\b'); break; case 'f': buf_addc(&b, '\f'); break;
        case 'u': {
          uint32_t cp = 0; for (int k = 0; k < 4 && j->p + 1 < j->end; k++) { int h = hexv(*++j->p); if (h < 0) { j->err = true; break; } cp = cp * 16 + h; }
          if (cp >= 0xD800 && cp < 0xDC00 && j->p + 6 < j->end && j->p[1] == '\\' && j->p[2] == 'u') {
            uint32_t lo = 0; j->p += 2; for (int k = 0; k < 4; k++) { int h = hexv(*++j->p); if (h < 0) { j->err = true; break; } lo = lo * 16 + h; }
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          char t[4]; buf_add(&b, t, u8_encode(cp, t)); break;
        }
        default: buf_addc(&b, *j->p);
      }
      j->p++;
    } else buf_addc(&b, *j->p++);
  }
  if (j->p >= j->end) j->err = true; else j->p++;
  Value v = mkstr(buf_cstr(&b), b.n); free(b.p); return v;
}
static Value jval(JP *j, int depth) {
  jws(j); if (j->p >= j->end || depth > 100) { j->err = true; return NIL; }
  char c = *j->p;
  if (c == '{') {
    Map *m = new_map(); j->p++; jws(j);
    if (j->p < j->end && *j->p == '}') { j->p++; return OBJV(m); }
    for (;;) {
      jws(j); if (j->p >= j->end || *j->p != '"') { j->err = true; return OBJV(m); }
      Value k = jstr(j); jws(j); if (j->p >= j->end || *j->p != ':') { j->err = true; return OBJV(m); } j->p++;
      Value v = jval(j, depth + 1); if (j->err) return OBJV(m); dset(&m->d, k, v); jws(j);
      if (j->p < j->end && *j->p == ',') { j->p++; continue; }
      if (j->p < j->end && *j->p == '}') { j->p++; break; }
      j->err = true; break;
    }
    return OBJV(m);
  }
  if (c == '[') {
    List *l = new_list(); j->p++; jws(j);
    if (j->p < j->end && *j->p == ']') { j->p++; return OBJV(l); }
    for (;;) {
      Value v = jval(j, depth + 1); if (j->err) return OBJV(l); varr_push(&l->a, v); jws(j);
      if (j->p < j->end && *j->p == ',') { j->p++; continue; }
      if (j->p < j->end && *j->p == ']') { j->p++; break; }
      j->err = true; break;
    }
    return OBJV(l);
  }
  if (c == '"') return jstr(j);
  if (j->end - j->p >= 4 && !strncmp(j->p, "true", 4)) { j->p += 4; return BOOLV(true); }
  if (j->end - j->p >= 5 && !strncmp(j->p, "false", 5)) { j->p += 5; return BOOLV(false); }
  if (j->end - j->p >= 4 && !strncmp(j->p, "null", 4)) { j->p += 4; return NIL; }
  if (c == '-' || (c >= '0' && c <= '9')) { char *e; double d = strtod(j->p, &e); if (e == j->p) { j->err = true; return NIL; } j->p = e; return NUMV(d); }
  j->err = true; return NIL;
}
NAT(j_dec) {
  STRA(0); JP j = {AS_STR(a[0])->chars, AS_STR(a[0])->chars + AS_STR(a[0])->len, false};
  Value v = jval(&j, 0); jws(&j); if (j.err || j.p != j.end) FAIL("جيسون غير صالح"); *res = v; return true;
}

static void shq(Buf *b, const char *s, int n) {
  buf_addc(b, '\'');
  for (int i = 0; i < n; i++) { if (s[i] == '\'') buf_add(b, "'\\''", 4); else buf_addc(b, s[i]); }
  buf_addc(b, '\'');
}
static bool net_fetch_curl(int argc, Value *a, Value *res) {
  PERM(PM_NET);
  STRA(0); Buf cmd; buf_init(&cmd); buf_fmt(&cmd, "curl -sS -L --max-time 60 -w '\\n%%{http_code}'");
  Str *method = NULL; char tmpl[64] = ""; Value body = NIL; bool json_body = false;
  if (argc > 1 && IS_T(a[1], O_MAP)) {
    Dict *o = &AS_MAP(a[1])->d; Value v;
    if (mget(o, KN("طريقة"), &v) && IS_T(v, O_STR)) method = AS_STR(v);
    if (mget(o, KN("رؤوس"), &v) && IS_T(v, O_MAP)) {
      Dict *h = &AS_MAP(v)->d;
      for (int i = 0; i < h->n; i++) {
        if (h->e[i].k.t == V_EMPTY) continue; Str *hk = tostr(h->e[i].k), *hv = tostr(h->e[i].v);
        Buf hb; buf_init(&hb); buf_fmt(&hb, "%s: %s", hk->chars, hv->chars); buf_add(&cmd, " -H ", 4); shq(&cmd, hb.p, hb.n); free(hb.p);
      }
    }
    if (mget(o, KN("جسم"), &v) && !IS_NIL(v)) { body = v; json_body = IS_T(v, O_MAP) || IS_T(v, O_LIST); }
  }
  if (method) { buf_add(&cmd, " -X ", 4); shq(&cmd, method->chars, method->len); }
  if (!IS_NIL(body)) {
    Buf bb; buf_init(&bb); if (json_body) json_enc(body, &bb, 0, 0); else { Str *s = tostr(body); buf_add(&bb, s->chars, s->len); }
    snprintf(tmpl, sizeof tmpl, "%s/solan_XXXXXX", os_tmpdir()); int fd = os_mkstemp(tmpl);
    if (fd < 0) { free(bb.p); free(cmd.p); FAIL("تعذّر إنشاء ملف مؤقت"); }
    if (bb.n) { ssize_t w = write(fd, bb.p, bb.n); (void)w; } close(fd); free(bb.p);
    if (json_body) buf_add(&cmd, " -H 'Content-Type: application/json'", 35);
    buf_add(&cmd, " --data-binary @", 16); shq(&cmd, tmpl, (int)strlen(tmpl));
  }
  buf_add(&cmd, " ", 1); shq(&cmd, AS_STR(a[0])->chars, AS_STR(a[0])->len); buf_add(&cmd, " 2>&1", 5);
  fflush(stdout); FILE *p = popen(buf_cstr(&cmd), "r"); free(cmd.p);
  if (!p) { if (tmpl[0]) remove(tmpl); FAIL("تعذّر تشغيل curl (ثبّته: pkg install curl)"); }
  Buf out; buf_init(&out); char tmp[4096]; size_t n; while ((n = fread(tmp, 1, sizeof tmp, p)) > 0) buf_add(&out, tmp, (int)n);
  int st = pclose(p); if (tmpl[0]) remove(tmpl);
  const char *s = buf_cstr(&out); int len = out.n, cut = len;
  while (cut > 0 && s[cut - 1] != '\n') cut--;
  int code = 0; if (cut > 0) code = atoi(s + cut);
  if (cut == 0 || (st != 0 && code == 0)) { Str *m = intern(s, len); free(out.p); vm.exc = OBJV(intern_c("فشل الاتصال: ")); (void)m; Buf eb; buf_init(&eb); buf_fmt(&eb, "فشل الاتصال: %s", m->chars); vm.exc = mkstr(eb.p, eb.n); free(eb.p); return false; }
  Map *r = new_map(); dset(&r->d, KX("حالة"), NUMV(code)); dset(&r->d, KN("جسم"), mkstr(s, cut - 1 > 0 ? cut - 1 : 0));
  dset(&r->d, KN("ناجح"), BOOLV(code >= 200 && code < 300)); free(out.p); *res = OBJV(r); return true;
}

static uint32_t rol32(uint32_t x, int s) { return (x << s) | (x >> (32 - s)); }
static uint32_t ror32(uint32_t x, int s) { return (x >> s) | (x << (32 - s)); }
static void sha1_blk(uint32_t *h, const uint8_t *p) {
  uint32_t w[80];
  for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
  for (int i = 16; i < 80; i++) w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; } else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
    else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; } else { f = b ^ c ^ d; k = 0xCA62C1D6; }
    uint32_t t = rol32(a, 5) + f + e + k + w[i]; e = d; d = c; c = rol32(b, 30); b = a; a = t;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}
