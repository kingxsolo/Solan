static void chacha_quarter(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
  *a += *b; *d ^= *a; *d = (*d << 16) | (*d >> 16);
  *c += *d; *b ^= *c; *b = (*b << 12) | (*b >> 20);
  *a += *b; *d ^= *a; *d = (*d << 8) | (*d >> 24);
  *c += *d; *b ^= *c; *b = (*b << 7) | (*b >> 25);
}
static void chacha_block(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, uint8_t out[64]) {
  uint32_t s[16], w[16];
  s[0] = 0x61707865; s[1] = 0x3320646e; s[2] = 0x79622d32; s[3] = 0x6b206574;
  for (int i = 0; i < 8; i++) s[4 + i] = (uint32_t)key[4 * i] | ((uint32_t)key[4 * i + 1] << 8) | ((uint32_t)key[4 * i + 2] << 16) | ((uint32_t)key[4 * i + 3] << 24);
  s[12] = counter;
  for (int i = 0; i < 3; i++) s[13 + i] = (uint32_t)nonce[4 * i] | ((uint32_t)nonce[4 * i + 1] << 8) | ((uint32_t)nonce[4 * i + 2] << 16) | ((uint32_t)nonce[4 * i + 3] << 24);
  memcpy(w, s, sizeof s);
  for (int i = 0; i < 10; i++) {
    chacha_quarter(&w[0], &w[4], &w[8], &w[12]);
    chacha_quarter(&w[1], &w[5], &w[9], &w[13]);
    chacha_quarter(&w[2], &w[6], &w[10], &w[14]);
    chacha_quarter(&w[3], &w[7], &w[11], &w[15]);
    chacha_quarter(&w[0], &w[5], &w[10], &w[15]);
    chacha_quarter(&w[1], &w[6], &w[11], &w[12]);
    chacha_quarter(&w[2], &w[7], &w[8], &w[13]);
    chacha_quarter(&w[3], &w[4], &w[9], &w[14]);
  }
  for (int i = 0; i < 16; i++) {
    uint32_t v = w[i] + s[i];
    out[4 * i] = (uint8_t)v; out[4 * i + 1] = (uint8_t)(v >> 8); out[4 * i + 2] = (uint8_t)(v >> 16); out[4 * i + 3] = (uint8_t)(v >> 24);
  }
}
static void chacha_stream(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, const uint8_t *in, int n, uint8_t *out) {
  uint8_t ks[64];
  int off = 0;
  while (off < n) {
    chacha_block(key, nonce, counter++, ks);
    int take = n - off > 64 ? 64 : n - off;
    for (int k = 0; k < take; k++) out[off + k] = (uint8_t)(in[off + k] ^ ks[k]);
    off += take;
  }
}
typedef struct { uint32_t r[5], h[5], pad[4]; int leftover; uint8_t buf[16]; } PolyState;
static uint32_t poly_ld(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void poly_blocks(PolyState *st, const uint8_t *m, int blocks, uint32_t hibit) {
  const uint32_t r0 = st->r[0], r1 = st->r[1], r2 = st->r[2], r3 = st->r[3], r4 = st->r[4];
  const uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
  uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4];
  for (int b = 0; b < blocks; b++, m += 16) {
    h0 += poly_ld(m) & 0x3ffffff;
    h1 += (poly_ld(m + 3) >> 2) & 0x3ffffff;
    h2 += (poly_ld(m + 6) >> 4) & 0x3ffffff;
    h3 += (poly_ld(m + 9) >> 6) & 0x3ffffff;
    h4 += (poly_ld(m + 12) >> 8) | hibit;
    uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
    uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
    uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
    uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
    uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;
    uint64_t c = d0 >> 26; h0 = (uint32_t)d0 & 0x3ffffff;
    d1 += c; c = d1 >> 26; h1 = (uint32_t)d1 & 0x3ffffff;
    d2 += c; c = d2 >> 26; h2 = (uint32_t)d2 & 0x3ffffff;
    d3 += c; c = d3 >> 26; h3 = (uint32_t)d3 & 0x3ffffff;
    d4 += c; c = d4 >> 26; h4 = (uint32_t)d4 & 0x3ffffff;
    h0 += (uint32_t)c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
    h1 += (uint32_t)c;
  }
  st->h[0] = h0; st->h[1] = h1; st->h[2] = h2; st->h[3] = h3; st->h[4] = h4;
}
static void poly_update(PolyState *st, const uint8_t *m, int n) {
  if (st->leftover) {
    int take = 16 - st->leftover;
    if (take > n) take = n;
    memcpy(st->buf + st->leftover, m, take);
    st->leftover += take; m += take; n -= take;
    if (st->leftover == 16) { poly_blocks(st, st->buf, 1, 0x01000000u); st->leftover = 0; }
  }
  while (n >= 16) { poly_blocks(st, m, 1, 0x01000000u); m += 16; n -= 16; }
  if (n) { memcpy(st->buf, m, n); st->leftover = n; }
}
static void poly_final(PolyState *st, uint8_t mac[16]) {
  if (st->leftover) {
    int i = st->leftover;
    st->buf[i++] = 1;
    while (i < 16) st->buf[i++] = 0;
    poly_blocks(st, st->buf, 1, 0);
  }
  uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4];
  uint32_t c = h0 >> 26; h0 &= 0x3ffffff;
  h1 += c; c = h1 >> 26; h1 &= 0x3ffffff;
  h2 += c; c = h2 >> 26; h2 &= 0x3ffffff;
  h3 += c; c = h3 >> 26; h3 &= 0x3ffffff;
  h4 += c; c = h4 >> 26; h4 &= 0x3ffffff;
  h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
  h1 += c;
  uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
  uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
  uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
  uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
  uint32_t g4 = h4 + c - 0x04000000u;
  uint32_t mask = (g4 >> 31) - 1;
  g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
  mask = ~mask;
  h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2; h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;
  uint32_t y0 = (h0 | (h1 << 26));
  uint32_t y1 = ((h1 >> 6) | (h2 << 20));
  uint32_t y2 = ((h2 >> 12) | (h3 << 14));
  uint32_t y3 = ((h3 >> 18) | (h4 << 8));
  uint64_t f = (uint64_t)y0 + st->pad[0]; y0 = (uint32_t)f;
  f = (uint64_t)y1 + st->pad[1] + (f >> 32); y1 = (uint32_t)f;
  f = (uint64_t)y2 + st->pad[2] + (f >> 32); y2 = (uint32_t)f;
  f = (uint64_t)y3 + st->pad[3] + (f >> 32); y3 = (uint32_t)f;
  mac[0] = (uint8_t)y0; mac[1] = (uint8_t)(y0 >> 8); mac[2] = (uint8_t)(y0 >> 16); mac[3] = (uint8_t)(y0 >> 24);
  mac[4] = (uint8_t)y1; mac[5] = (uint8_t)(y1 >> 8); mac[6] = (uint8_t)(y1 >> 16); mac[7] = (uint8_t)(y1 >> 24);
  mac[8] = (uint8_t)y2; mac[9] = (uint8_t)(y2 >> 8); mac[10] = (uint8_t)(y2 >> 16); mac[11] = (uint8_t)(y2 >> 24);
  mac[12] = (uint8_t)y3; mac[13] = (uint8_t)(y3 >> 8); mac[14] = (uint8_t)(y3 >> 16); mac[15] = (uint8_t)(y3 >> 24);
}
static void poly_mac(const uint8_t key[32], const uint8_t *msg, int n, uint8_t mac[16]) {
  PolyState st;
  st.r[0] = poly_ld(key) & 0x3ffffff;
  st.r[1] = (poly_ld(key + 3) >> 2) & 0x3ffff03;
  st.r[2] = (poly_ld(key + 6) >> 4) & 0x3ffc0ff;
  st.r[3] = (poly_ld(key + 9) >> 6) & 0x3f03fff;
  st.r[4] = (poly_ld(key + 12) >> 8) & 0x00fffff;
  st.h[0] = st.h[1] = st.h[2] = st.h[3] = st.h[4] = 0;
  st.pad[0] = poly_ld(key + 16); st.pad[1] = poly_ld(key + 20); st.pad[2] = poly_ld(key + 24); st.pad[3] = poly_ld(key + 28);
  st.leftover = 0;
  poly_update(&st, msg, n);
  poly_final(&st, mac);
}
static void chacha_aead(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, int alen, const uint8_t *in, int n, uint8_t *out, uint8_t tag[16]) {
  uint8_t otk[64];
  chacha_block(key, nonce, 0, otk);
  chacha_stream(key, nonce, 1, in, n, out);
  int pl = (16 - (alen % 16)) % 16, cl = (16 - (n % 16)) % 16;
  uint8_t *m = (uint8_t *)xalloc(alen + pl + n + cl + 16);
  int o = 0;
  if (alen) { memcpy(m + o, aad, alen); o += alen; }
  memset(m + o, 0, pl); o += pl;
  if (n) { memcpy(m + o, out, n); o += n; }
  memset(m + o, 0, cl); o += cl;
  uint64_t av = (uint64_t)alen, cv = (uint64_t)n;
  for (int k = 0; k < 8; k++) m[o + k] = (uint8_t)(av >> (8 * k));
  for (int k = 0; k < 8; k++) m[o + 8 + k] = (uint8_t)(cv >> (8 * k));
  poly_mac(otk, m, o + 16, tag);
  free(m);
}
static bool chacha_hexarg(Value v, uint8_t *out, int outmax, int *outn, const char *what) {
  if (!IS_T(v, O_STR)) { char m[120]; snprintf(m, sizeof m, "%s يجب أن تكون نصا هكس", what); rt_error(m); return false; }
  Str *s = AS_STR(v);
  int n = aes_unhex(s->chars, s->len, out, outmax);
  if (n < 0) { char m[160]; snprintf(m, sizeof m, "%s ليست هكس صالحة", what); rt_error(m); return false; }
  *outn = n;
  return true;
}
NAT(c_chacha20) {
  uint8_t key[32], nonce[12]; int kn = 0, nn = 0;
  if (!chacha_hexarg(a[0], key, 32, &kn, "المفتاح")) return false;
  if (kn != 32) FAIL("مفتاح chacha20 يجب أن يكون 32 بايت (64 خانة هكس)");
  if (!chacha_hexarg(a[1], nonce, 12, &nn, "النونس")) return false;
  if (nn != 12) FAIL("النونس يجب أن يكون 12 بايت (24 خانة هكس)");
  STRA(2);
  uint32_t counter = 0;
  if (argc > 3) { NUMA(3); if (a[3].as.n < 0 || a[3].as.n > 4294967295.0) FAIL("العداد خارج النطاق"); counter = (uint32_t)a[3].as.n; }
  Str *s = AS_STR(a[2]);
  int n = s->len / 2;
  uint8_t *in = (uint8_t *)xalloc(n + 1), *out = (uint8_t *)xalloc(n + 1);
  if (n && aes_unhex(s->chars, s->len, in, n + 1) < 0) { free(in); free(out); FAIL("النص ليست هكس صالحة"); }
  chacha_stream(key, nonce, counter, in, n, out);
  *res = hex_or_raw(out, n, false);
  free(in); free(out);
  return true;
}
static bool chacha_aead_common(Value *a, int argc, Value *res, bool decrypt) {
  uint8_t key[32], nonce[12]; int kn = 0, nn = 0;
  if (!chacha_hexarg(a[0], key, 32, &kn, "المفتاح")) return false;
  if (kn != 32) FAIL("مفتاح chacha20_poly1305 يجب أن يكون 32 بايت");
  if (!chacha_hexarg(a[1], nonce, 12, &nn, "النونس")) return false;
  if (nn != 12) FAIL("النونس يجب أن يكون 12 بايت");
  uint8_t *din = NULL, *aad = NULL; int dn = 0, an = 0;
  int aad_idx = decrypt ? 4 : 3;
  Str *s = AS_STR(a[2]);
  din = (uint8_t *)xalloc(s->len / 2 + 1);
  dn = aes_unhex(s->chars, s->len, din, s->len / 2 + 1);
  if (dn < 0) { free(din); FAIL("النص ليست هكس صالحة"); }
  if (argc > aad_idx && IS_T(a[aad_idx], O_STR) && AS_STR(a[aad_idx])->len) {
    Str *sa = AS_STR(a[aad_idx]);
    aad = (uint8_t *)xalloc(sa->len / 2 + 1);
    an = aes_unhex(sa->chars, sa->len, aad, sa->len / 2 + 1);
    if (an < 0) { free(din); free(aad); FAIL("البيانات الإضافية ليست هكس صالحة"); }
  }
  bool ok = true;
  if (!decrypt) {
    uint8_t *out = (uint8_t *)xalloc(dn + 16), tag[16];
    chacha_aead(key, nonce, aad, an, din, dn, out, tag);
    *res = aes_list2(hex_or_raw(out, dn, false), hex_or_raw(tag, 16, false));
    free(out);
  } else {
    if (argc < 4 || !IS_T(a[3], O_STR)) { free(din); free(aad); FAIL("توقعت وسم المصادقة وسيطا رابعا"); }
    uint8_t tag[16]; int tn = 0;
    if (!chacha_hexarg(a[3], tag, 16, &tn, "وسم المصادقة") || tn != 16) { free(din); free(aad); FAIL("وسم المصادقة يجب أن يكون 16 بايت"); }
    uint8_t otk[64], calc[16];
    chacha_block(key, nonce, 0, otk);
    int pl = (16 - (an % 16)) % 16, cl = (16 - (dn % 16)) % 16;
    uint8_t *m = (uint8_t *)xalloc(an + pl + dn + cl + 16);
    int o = 0;
    if (an) { memcpy(m + o, aad, an); o += an; }
    memset(m + o, 0, pl); o += pl;
    if (dn) { memcpy(m + o, din, dn); o += dn; }
    memset(m + o, 0, cl); o += cl;
    uint64_t av = (uint64_t)an, cv = (uint64_t)dn;
    for (int k = 0; k < 8; k++) m[o + k] = (uint8_t)(av >> (8 * k));
    for (int k = 0; k < 8; k++) m[o + 8 + k] = (uint8_t)(cv >> (8 * k));
    poly_mac(otk, m, o + 16, calc);
    free(m);
    uint8_t diff = 0;
    for (int k = 0; k < 16; k++) diff |= (uint8_t)(calc[k] ^ tag[k]);
    ok = diff == 0;
    uint8_t *pt = (uint8_t *)xalloc(dn + 16);
    if (ok) chacha_stream(key, nonce, 1, din, dn, pt);
    *res = aes_list2(ok ? hex_or_raw(pt, dn, false) : mkstr("", 0), NUMV(ok ? 1 : 0));
    free(pt);
  }
  free(din); if (aad) free(aad);
  return true;
}
NAT(c_chacha_aead) { return chacha_aead_common(a, argc, res, false); }
NAT(c_chacha_aead_dec) { return chacha_aead_common(a, argc, res, true); }
