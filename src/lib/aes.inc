static const uint8_t AES_SBOX[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
static const uint8_t AES_IBOX[256] = {
  0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
  0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
  0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
  0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
  0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
  0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
  0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
  0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
  0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
  0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
  0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
  0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
  0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
  0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
  0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
  0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d};
static uint8_t aes_xt(uint8_t x) { return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b)); }
static uint8_t aes_gmul(uint8_t a, uint8_t b) {
  uint8_t r = 0;
  while (b) { if (b & 1) r ^= a; a = aes_xt(a); b >>= 1; }
  return r;
}
typedef struct { uint32_t rk[60]; int nr; uint8_t gtab[2][16][16][16]; } AesCtx;
static void aes_key_setup(AesCtx *c, const uint8_t *key, int bits) {
  int nk = bits / 32; c->nr = nk + 6; int nw = 4 * (c->nr + 1);
  for (int i = 0; i < nk; i++) c->rk[i] = (uint32_t)key[4 * i] << 24 | (uint32_t)key[4 * i + 1] << 16 | (uint32_t)key[4 * i + 2] << 8 | key[4 * i + 3];
  uint32_t rc = 1;
  for (int i = nk; i < nw; i++) {
    uint32_t t = c->rk[i - 1];
    if (i % nk == 0) {
      t = (t << 8) | (t >> 24);
      t = ((uint32_t)AES_SBOX[t >> 24] << 24) | ((uint32_t)AES_SBOX[(t >> 16) & 255] << 16) | ((uint32_t)AES_SBOX[(t >> 8) & 255] << 8) | AES_SBOX[t & 255];
      t ^= rc << 24; rc = aes_xt((uint8_t)rc);
    } else if (nk == 8 && (i % nk) == 4) {
      t = ((uint32_t)AES_SBOX[t >> 24] << 24) | ((uint32_t)AES_SBOX[(t >> 16) & 255] << 16) | ((uint32_t)AES_SBOX[(t >> 8) & 255] << 8) | AES_SBOX[t & 255];
    }
    c->rk[i] = c->rk[i - nk] ^ t;
  }
}
static void aes_ark(uint8_t s[16], const uint32_t *rk, int r) {
  for (int i = 0; i < 4; i++) {
    uint32_t k = rk[4 * r + i];
    s[4 * i] ^= (uint8_t)(k >> 24); s[4 * i + 1] ^= (uint8_t)(k >> 16); s[4 * i + 2] ^= (uint8_t)(k >> 8); s[4 * i + 3] ^= (uint8_t)k;
  }
}
static void aes_sr(uint8_t s[16]) {
  uint8_t t;
  t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
  t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
  t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
}
static void aes_mc(uint8_t s[16]) {
  for (int c = 0; c < 4; c++) {
    uint8_t *p = s + 4 * c, a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
    p[0] = (uint8_t)(aes_xt(a0) ^ a1 ^ aes_xt(a1) ^ a2 ^ a3);
    p[1] = (uint8_t)(a0 ^ aes_xt(a1) ^ a2 ^ aes_xt(a2) ^ a3);
    p[2] = (uint8_t)(a0 ^ a1 ^ aes_xt(a2) ^ a3 ^ aes_xt(a3));
    p[3] = (uint8_t)(aes_xt(a0) ^ a0 ^ a1 ^ a2 ^ aes_xt(a3));
  }
}
static void aes_enc_block(const AesCtx *c, const uint8_t in[16], uint8_t out[16]) {
  uint8_t s[16]; memcpy(s, in, 16);
  aes_ark(s, c->rk, 0);
  for (int r = 1; r < c->nr; r++) {
    for (int j = 0; j < 16; j++) s[j] = AES_SBOX[s[j]];
    aes_sr(s); aes_mc(s); aes_ark(s, c->rk, r);
  }
  for (int j = 0; j < 16; j++) s[j] = AES_SBOX[s[j]];
  aes_sr(s); aes_ark(s, c->rk, c->nr);
  memcpy(out, s, 16);
}
static void aes_isr(uint8_t s[16]) {
  uint8_t t;
  t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;
  t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
  t = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t;
}
static void aes_imc(uint8_t s[16]) {
  for (int c = 0; c < 4; c++) {
    uint8_t *p = s + 4 * c, a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
    p[0] = (uint8_t)(aes_gmul(a0, 0x0e) ^ aes_gmul(a1, 0x0b) ^ aes_gmul(a2, 0x0d) ^ aes_gmul(a3, 0x09));
    p[1] = (uint8_t)(aes_gmul(a0, 0x09) ^ aes_gmul(a1, 0x0e) ^ aes_gmul(a2, 0x0b) ^ aes_gmul(a3, 0x0d));
    p[2] = (uint8_t)(aes_gmul(a0, 0x0d) ^ aes_gmul(a1, 0x09) ^ aes_gmul(a2, 0x0e) ^ aes_gmul(a3, 0x0b));
    p[3] = (uint8_t)(aes_gmul(a0, 0x0b) ^ aes_gmul(a1, 0x0d) ^ aes_gmul(a2, 0x09) ^ aes_gmul(a3, 0x0e));
  }
}
static void aes_dec_block(const AesCtx *c, const uint8_t in[16], uint8_t out[16]) {
  uint8_t s[16]; memcpy(s, in, 16);
  aes_ark(s, c->rk, c->nr);
  for (int r = c->nr - 1; r > 0; r--) {
    aes_isr(s);
    for (int j = 0; j < 16; j++) s[j] = AES_IBOX[s[j]];
    aes_ark(s, c->rk, r); aes_imc(s);
  }
  aes_isr(s);
  for (int j = 0; j < 16; j++) s[j] = AES_IBOX[s[j]];
  aes_ark(s, c->rk, 0);
  memcpy(out, s, 16);
}
static void gcm_shr(uint8_t v[16]) {
  uint8_t c = 0;
  for (int i = 0; i < 16; i++) { uint8_t nc = (uint8_t)(v[i] & 1); v[i] = (uint8_t)((v[i] >> 1) | (c << 7)); c = nc; }
}
static void gcm_mul(const uint8_t x[16], const uint8_t y[16], uint8_t out[16]) {
  uint8_t z[16] = {0}, v[16];
  memcpy(v, y, 16);
  for (int i = 0; i < 128; i++) {
    if (x[i >> 3] & (0x80u >> (i & 7))) for (int k = 0; k < 16; k++) z[k] ^= v[k];
    uint8_t fall = (uint8_t)(v[15] & 1);
    gcm_shr(v);
    if (fall) v[0] ^= 0xE1;
  }
  memcpy(out, z, 16);
}
static void gcm_build_tables(AesCtx *c) {
  uint8_t h[16] = {0};
  aes_enc_block(c, h, h);
  uint8_t sb[16][16];
  memset(sb[0], 0, 16);
  memcpy(sb[8], h, 16);
  memcpy(sb[4], h, 16); { uint8_t fall = (uint8_t)(sb[4][15] & 1); gcm_shr(sb[4]); if (fall) sb[4][0] ^= 0xE1; }
  memcpy(sb[2], sb[4], 16); { uint8_t fall = (uint8_t)(sb[2][15] & 1); gcm_shr(sb[2]); if (fall) sb[2][0] ^= 0xE1; }
  memcpy(sb[1], sb[2], 16); { uint8_t fall = (uint8_t)(sb[1][15] & 1); gcm_shr(sb[1]); if (fall) sb[1][0] ^= 0xE1; }
  for (int n = 3; n < 16; n++) {
    if (!(n & (n - 1))) continue;
    int lb = n & -n;
    for (int q = 0; q < 16; q++) sb[n][q] = (uint8_t)(sb[n & (n - 1)][q] ^ sb[lb][q]);
  }
  for (int n = 0; n < 16; n++) memcpy(c->gtab[0][0][n], sb[n], 16);
  for (int p = 1; p < 16; p++) {
    for (int n = 0; n < 16; n++) {
      uint8_t *e = c->gtab[0][p][n];
      memcpy(e, c->gtab[0][p - 1][n], 16);
      for (int s = 0; s < 8; s++) { uint8_t fall = (uint8_t)(e[15] & 1); gcm_shr(e); if (fall) e[0] ^= 0xE1; }
    }
  }
  for (int p = 0; p < 16; p++) {
    for (int n = 0; n < 16; n++) {
      uint8_t *e = c->gtab[1][p][n];
      memcpy(e, c->gtab[0][p][n], 16);
      for (int s = 0; s < 4; s++) { uint8_t fall = (uint8_t)(e[15] & 1); gcm_shr(e); if (fall) e[0] ^= 0xE1; }
    }
  }
}
static void ghash_block(const AesCtx *c, uint8_t y[16], const uint8_t x[16]) {
  uint8_t acc[16] = {0};
  for (int k = 0; k < 16; k++) {
    uint8_t b = (uint8_t)(y[k] ^ x[k]);
    const uint8_t *t = c->gtab[0][k][b >> 4];
    for (int q = 0; q < 16; q++) acc[q] ^= t[q];
    t = c->gtab[1][k][b & 15];
    for (int q = 0; q < 16; q++) acc[q] ^= t[q];
  }
  memcpy(y, acc, 16);
}
static void gcm_inc32(uint8_t b[16]) {
  for (int i = 15; i >= 12; i--) { if (++b[i]) break; }
}
static void gcm_ghash(AesCtx *c, uint8_t y[16], const uint8_t *aad, int alen, const uint8_t *data, int dlen) {
  uint8_t blk[16];
  int i = 0;
  for (; i + 16 <= alen; i += 16) ghash_block(c, y, aad + i);
  if (i < alen) { memset(blk, 0, 16); memcpy(blk, aad + i, alen - i); ghash_block(c, y, blk); }
  i = 0;
  for (; i + 16 <= dlen; i += 16) ghash_block(c, y, data + i);
  if (i < dlen) { memset(blk, 0, 16); memcpy(blk, data + i, dlen - i); ghash_block(c, y, blk); }
  uint64_t abits = (uint64_t)alen * 8, dbits = (uint64_t)dlen * 8;
  memset(blk, 0, 16);
  for (int k = 0; k < 8; k++) blk[7 - k] = (uint8_t)(abits >> (8 * k));
  for (int k = 0; k < 8; k++) blk[15 - k] = (uint8_t)(dbits >> (8 * k));
  ghash_block(c, y, blk);
}
static void gcm_setup_iv(AesCtx *c, const uint8_t *iv, int ivlen, uint8_t j0[16]) {
  if (ivlen == 12) { memcpy(j0, iv, 12); j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1; }
  else {
    uint8_t y[16] = {0};
    int i = 0;
    for (; i + 16 <= ivlen; i += 16) ghash_block(c, y, iv + i);
    if (i < ivlen) { uint8_t blk[16]; memset(blk, 0, 16); memcpy(blk, iv + i, ivlen - i); ghash_block(c, y, blk); }
    uint8_t len[16]; memset(len, 0, 16);
    uint64_t bits = (uint64_t)ivlen * 8;
    for (int k = 0; k < 8; k++) len[15 - k] = (uint8_t)(bits >> (8 * k));
    ghash_block(c, y, len);
    memcpy(j0, y, 16);
  }
}
static bool gcm_crypt(AesCtx *c, const uint8_t *iv, int ivlen, const uint8_t *in, int n, uint8_t *out, const uint8_t *aad, int alen, uint8_t tag[16], bool decrypt) {
  uint8_t j0[16], ek0[16], cb[16], stream[16], y[16] = {0};
  gcm_build_tables(c);
  gcm_setup_iv(c, iv, ivlen, j0);
  aes_enc_block(c, j0, ek0);
  memcpy(cb, j0, 16);
  int off = 0;
  while (off < n) {
    gcm_inc32(cb);
    aes_enc_block(c, cb, stream);
    int take = n - off > 16 ? 16 : n - off;
    for (int k = 0; k < take; k++) out[off + k] = (uint8_t)(in[off + k] ^ stream[k]);
    off += take;
  }
  gcm_ghash(c, y, aad, alen, decrypt ? in : out, n);
  for (int k = 0; k < 16; k++) tag[k] = (uint8_t)(ek0[k] ^ y[k]);
  return true;
}
static int aes_unhex(const char *s, int n, uint8_t *out, int outmax) {
  static int8_t hv[256]; static int hinit = 0;
  if (!hinit) { memset(hv, -1, 256); for (int i = 0; i < 10; i++) hv['0' + i] = (int8_t)i; for (int i = 0; i < 6; i++) { hv['a' + i] = (int8_t)(10 + i); hv['A' + i] = (int8_t)(10 + i); } hinit = 1; }
  int k = 0;
  for (int i = 0; i < n; i++) {
    char ch = s[i];
    if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') continue;
    int h = hv[(unsigned char)ch];
    if (h < 0) return -1;
    if (k & 1) { out[(k - 1) / 2] = (uint8_t)(out[(k - 1) / 2] * 16 + h); }
    else { if (k / 2 >= outmax) return -1; out[k / 2] = (uint8_t)h; }
    k++;
  }
  return k % 2 ? -1 : k / 2;
}
static Value aes_list2(Value v1, Value v2) {
  List *l = new_list();
  varr_push(&l->a, v1); varr_push(&l->a, v2);
  return OBJV(l);
}
static bool aes_hexarg(Value v, uint8_t *out, int outmax, int *outn, const char *what) {
  if (!IS_T(v, O_STR)) { char m[120]; snprintf(m, sizeof m, "%s يجب أن تكون نصا هكس", what); rt_error(m); return false; }
  Str *s = AS_STR(v);
  int n = aes_unhex(s->chars, s->len, out, outmax);
  if (n < 0) { char m[160]; snprintf(m, sizeof m, "%s ليست هكس صالحة", what); rt_error(m); return false; }
  *outn = n;
  return true;
}
static bool aes_prepare(AesCtx *c, Value *a, int keybits, uint8_t *iv, int *ivlen, int ivmax) {
  uint8_t key[32]; int kn = 0, n = 0;
  if (!aes_hexarg(a[0], key, 32, &kn, "المفتاح")) return false;
  if (kn != keybits / 8) { char m[160]; snprintf(m, sizeof m, "المفتاح يجب أن يكون %d بايت (%d خانة هكس) وجد %d بايت", keybits / 8, keybits / 4, kn); rt_error(m); return false; }
  if (!aes_hexarg(a[1], iv, ivmax, &n, "آي في")) return false;
  if (n < 1) { rt_error("آي في فارغ"); return false; }
  *ivlen = n;
  aes_key_setup(c, key, keybits);
  return true;
}
static bool aes_gcm_common(Value *a, int argc, Value *res, int keybits, bool decrypt) {
  AesCtx *c = (AesCtx *)xalloc(sizeof(AesCtx));
  uint8_t iv[64]; int ivlen = 0;
  if (!aes_prepare(c, a, keybits, iv, &ivlen, 64)) { free(c); return false; }
  uint8_t *din = NULL, *aad = NULL; int dn = 0, an = 0;
  int aad_idx = decrypt ? 4 : 3;
  Str *s = AS_STR(a[2]);
  if (s->len) {
    din = (uint8_t *)xalloc(s->len / 2 + 1);
    dn = aes_unhex(s->chars, s->len, din, s->len / 2 + 1);
    if (dn < 0) { free(din); free(c); FAIL("النص ليست هكس صالحة"); }
  } else din = (uint8_t *)xalloc(1);
  if (argc > aad_idx && IS_T(a[aad_idx], O_STR) && AS_STR(a[aad_idx])->len) {
    Str *sa = AS_STR(a[aad_idx]);
    aad = (uint8_t *)xalloc(sa->len / 2 + 1);
    an = aes_unhex(sa->chars, sa->len, aad, sa->len / 2 + 1);
    if (an < 0) { free(din); free(aad); free(c); FAIL("البيانات الإضافية ليست هكس صالحة"); }
  }
  bool ok = true;
  if (!decrypt) {
    uint8_t *ct = (uint8_t *)xalloc(dn + 16), tag[16];
    gcm_crypt(c, iv, ivlen, din, dn, ct, aad, an, tag, false);
    *res = aes_list2(hex_or_raw(ct, dn, false), hex_or_raw(tag, 16, false));
    free(ct);
  } else {
    if (argc < 4 || !IS_T(a[3], O_STR)) { free(din); free(aad); free(c); FAIL("توقعت وسم المصادقة وسيطا رابعا"); }
    uint8_t tag[16]; int tn = 0;
    if (!aes_hexarg(a[3], tag, 16, &tn, "وسم المصادقة") || tn != 16) { free(din); free(aad); free(c); FAIL("وسم المصادقة يجب أن يكون 16 بايت"); }
    uint8_t *pt = (uint8_t *)xalloc(dn + 16), calc[16];
    gcm_crypt(c, iv, ivlen, din, dn, pt, aad, an, calc, true);
    uint8_t diff = 0;
    for (int k = 0; k < 16; k++) diff |= (uint8_t)(calc[k] ^ tag[k]);
    ok = diff == 0;
    *res = aes_list2(ok ? hex_or_raw(pt, dn, false) : mkstr("", 0), NUMV(ok ? 1 : 0));
    free(pt);
  }
  free(din); if (aad) free(aad); free(c);
  return true;
}
NAT(c_aes256_gcm) { return aes_gcm_common(a, argc, res, 256, false); }
NAT(c_aes256_gcm_dec) { return aes_gcm_common(a, argc, res, 256, true); }
NAT(c_aes128_gcm) { return aes_gcm_common(a, argc, res, 128, false); }
NAT(c_aes128_gcm_dec) { return aes_gcm_common(a, argc, res, 128, true); }
static bool aes_cbc_common(Value *a, int argc, Value *res, int keybits, bool decrypt) {
  AesCtx *c = (AesCtx *)xalloc(sizeof(AesCtx));
  uint8_t iv[16]; int ivlen = 0;
  if (!aes_prepare(c, a, keybits, iv, &ivlen, 16)) { free(c); return false; }
  if (ivlen != 16) { free(c); FAIL("آي في في CBC يجب أن يكون 16 بايت"); }
  bool pad = argc < 4 || !falsey(a[3]);
  Str *sd = AS_STR(a[2]);
  uint8_t *din = (uint8_t *)xalloc(sd->len / 2 + 16);
  int dn = aes_unhex(sd->chars, sd->len, din, sd->len / 2 + 16);
  if (dn < 0) { free(din); free(c); FAIL("النص ليست هكس صالحة"); }
  if (!decrypt) {
    int total = dn;
    if (pad) total = dn + (16 - dn % 16);
    if (total % 16) { free(din); free(c); FAIL("طول النص ليس مضاعف 16 ولم تُطلب البادئة"); }
    uint8_t *buf = (uint8_t *)xalloc(total + 16);
    memcpy(buf, din, dn);
    if (pad) { uint8_t pv = (uint8_t)(total - dn); memset(buf + dn, pv, total - dn); }
    uint8_t prev[16]; memcpy(prev, iv, 16);
    for (int off = 0; off < total; off += 16) {
      for (int k = 0; k < 16; k++) buf[off + k] ^= prev[k];
      aes_enc_block(c, buf + off, buf + off);
      memcpy(prev, buf + off, 16);
    }
    *res = hex_or_raw(buf, total, false);
    free(buf);
  } else {
    if (dn % 16) { free(din); free(c); FAIL("طول النص المشفر ليس مضاعف 16"); }
    uint8_t *buf = (uint8_t *)xalloc(dn + 16);
    memcpy(buf, din, dn);
    uint8_t prev[16], cur[16];
    memcpy(prev, iv, 16);
    for (int off = 0; off < dn; off += 16) {
      memcpy(cur, buf + off, 16);
      aes_dec_block(c, buf + off, buf + off);
      for (int k = 0; k < 16; k++) buf[off + k] ^= prev[k];
      memcpy(prev, cur, 16);
    }
    int outn = dn;
    if (pad) {
      if (dn == 0) { free(buf); free(din); free(c); *res = NIL; return true; }
      uint8_t pv = buf[dn - 1];
      if (pv < 1 || pv > 16) { free(buf); free(din); free(c); *res = NIL; return true; }
      for (int k = dn - pv; k < dn; k++) if (buf[k] != pv) { free(buf); free(din); free(c); *res = NIL; return true; }
      outn = dn - pv;
    }
    *res = hex_or_raw(buf, outn, false);
    free(buf);
  }
  free(din); free(c);
  return true;
}
NAT(c_aes256_cbc) { return aes_cbc_common(a, argc, res, 256, false); }
NAT(c_aes256_cbc_dec) { return aes_cbc_common(a, argc, res, 256, true); }
NAT(c_aes128_cbc) { return aes_cbc_common(a, argc, res, 128, false); }
NAT(c_aes128_cbc_dec) { return aes_cbc_common(a, argc, res, 128, true); }
