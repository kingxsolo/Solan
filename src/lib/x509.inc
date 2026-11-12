static void sha384_raw(const uint8_t *d, size_t n, uint8_t out[48]) {
  uint64_t h[8] = {0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
                   0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL};
  size_t i = 0;
  uint8_t tmp[256];
  for (; i + 128 <= n; i += 128) sha512_blk(h, d + i);
  size_t r = n - i;
  memcpy(tmp, d + i, r);
  tmp[r++] = 0x80;
  size_t tot = (r + 16 <= 128) ? 128 : 256;
  memset(tmp + r, 0, tot - r);
  uint64_t bits = (uint64_t)n * 8;
  for (int k = 0; k < 8; k++) tmp[tot - 1 - k] = (uint8_t)((bits >> (8 * k)) & 255);
  sha512_blk(h, tmp);
  if (tot == 256) sha512_blk(h, tmp + 128);
  for (int k = 0; k < 6; k++)
    for (int b = 0; b < 8; b++) out[k * 8 + b] = (uint8_t)(h[k] >> (56 - 8 * b));
}

#define XL 8
typedef struct { uint32_t n[XL], r2[XL]; uint32_t n0inv; } XMont;

static void xmont_load_be(uint32_t *l, const uint8_t *b, int len) {
  memset(l, 0, XL * sizeof(uint32_t));
  for (int i = 0; i < len && i < 32; i++) l[i >> 2] |= (uint32_t)b[len - 1 - i] << ((i & 3) * 8);
}

static int xmont_cmp(const uint32_t *a, const uint32_t *b) {
  for (int i = XL - 1; i >= 0; i--) { if (a[i] < b[i]) return -1; if (a[i] > b[i]) return 1; }
  return 0;
}

static void xmont_sub_n(const XMont *m, const uint32_t *a, uint32_t *out) {
  uint32_t borrow = 0;
  for (int i = 0; i < XL; i++) {
    uint64_t s = (uint64_t)a[i] - m->n[i] - borrow;
    out[i] = (uint32_t)s;
    borrow = (uint32_t)(s >> 63);
  }
}

static void xmont_init(XMont *m, const uint8_t *mod, int len) {
  xmont_load_be(m->n, mod, len);
  uint32_t inv = 1;
  for (int i = 0; i < 5; i++) inv = inv * (2u - m->n[0] * inv);
  m->n0inv = 0u - inv;
  uint32_t r[XL + 1];
  memset(r, 0, sizeof r);
  r[0] = 1;
  for (int k = 0; k < 512; k++) {
    uint32_t carry = 0;
    for (int i = 0; i < XL + 1; i++) {
      uint32_t nc = r[i] >> 31;
      r[i] = (r[i] << 1) | carry;
      carry = nc;
    }
    if (r[XL] || xmont_cmp(r, m->n) >= 0) {
      uint32_t borrow = 0;
      for (int i = 0; i < XL; i++) {
        uint64_t s = (uint64_t)r[i] - m->n[i] - borrow;
        r[i] = (uint32_t)s;
        borrow = (uint32_t)(s >> 63);
      }
      r[XL] = 0;
    }
  }
  memcpy(m->r2, r, sizeof m->r2);
}

static void xmont_mul(const XMont *m, const uint32_t *a, const uint32_t *b, uint32_t *out) {
  uint32_t t[XL + 2];
  memset(t, 0, sizeof t);
  for (int i = 0; i < XL; i++) {
    uint64_t C = 0;
    for (int j = 0; j < XL; j++) {
      uint64_t s = (uint64_t)t[j] + (uint64_t)a[i] * b[j] + C;
      t[j] = (uint32_t)s;
      C = s >> 32;
    }
    uint64_t s2 = (uint64_t)t[XL] + C;
    t[XL] = (uint32_t)s2;
    t[XL + 1] = (uint32_t)(s2 >> 32);
    uint32_t mi = t[0] * m->n0inv;
    uint64_t c2 = ((uint64_t)t[0] + (uint64_t)mi * m->n[0]) >> 32;
    for (int j = 1; j < XL; j++) {
      uint64_t s = (uint64_t)t[j] + (uint64_t)mi * m->n[j] + c2;
      t[j - 1] = (uint32_t)s;
      c2 = s >> 32;
    }
    uint64_t s3 = (uint64_t)t[XL] + c2;
    t[XL - 1] = (uint32_t)s3;
    t[XL] = t[XL + 1] + (uint32_t)(s3 >> 32);
    t[XL + 1] = 0;
  }
  if (t[XL] || xmont_cmp(t, m->n) >= 0) xmont_sub_n(m, t, out);
  else memcpy(out, t, XL * sizeof(uint32_t));
}

static const uint32_t XM_ONE[XL] = {1, 0, 0, 0, 0, 0, 0, 0};
static const uint32_t XM_ZERO[XL] = {0, 0, 0, 0, 0, 0, 0, 0};

static bool xmont_is_zero_arr(const uint32_t *a) { for (int i = 0; i < XL; i++) if (a[i]) return false; return true; }

static void xmont_from(const XMont *m, const uint8_t *be, int len, uint32_t *out) {
  uint32_t v[XL];
  xmont_load_be(v, be, len);
  xmont_mul(m, v, m->r2, out);
}

static void xmont_to_bytes(const XMont *m, const uint32_t *a, uint8_t *out, int outlen) {
  uint32_t t[XL];
  xmont_mul(m, a, XM_ONE, t);
  memset(out, 0, (size_t)outlen);
  for (int i = 0; i < 32 && i < outlen; i++) out[outlen - 1 - i] = (uint8_t)(t[i >> 2] >> ((i & 3) * 8));
}

static void xmont_pow(const XMont *m, const uint32_t *base_m, const uint8_t *exp_be, int explen, uint32_t *out) {
  uint32_t acc[XL];
  xmont_mul(m, XM_ONE, m->r2, acc);
  for (int i = 0; i < explen; i++) {
    for (int b = 7; b >= 0; b--) {
      xmont_mul(m, acc, acc, acc);
      if ((exp_be[i] >> b) & 1) xmont_mul(m, acc, base_m, acc);
    }
  }
  memcpy(out, acc, sizeof acc);
}

static void xfp_add(const XMont *m, const uint32_t *a, const uint32_t *b, uint32_t *out) {
  uint32_t carry = 0;
  for (int i = 0; i < XL; i++) {
    uint64_t s = (uint64_t)a[i] + b[i] + carry;
    out[i] = (uint32_t)s;
    carry = (uint32_t)(s >> 32);
  }
  if (carry || xmont_cmp(out, m->n) >= 0) {
    uint32_t t[XL];
    memcpy(t, out, sizeof t);
    xmont_sub_n(m, t, out);
  }
}

static void xfp_sub(const XMont *m, const uint32_t *a, const uint32_t *b, uint32_t *out) {
  uint32_t borrow = 0;
  for (int i = 0; i < XL; i++) {
    uint64_t s = (uint64_t)a[i] - b[i] - borrow;
    out[i] = (uint32_t)s;
    borrow = (uint32_t)(s >> 63);
  }
  if (borrow) {
    uint32_t t[XL], carry = 0;
    memcpy(t, out, sizeof t);
    for (int i = 0; i < XL; i++) {
      uint64_t s = (uint64_t)t[i] + m->n[i] + carry;
      out[i] = (uint32_t)s;
      carry = (uint32_t)(s >> 32);
    }
  }
}

static void xfp_mul(const XMont *m, const uint32_t *a, const uint32_t *b, uint32_t *out) { xmont_mul(m, a, b, out); }

typedef struct { uint32_t x[XL], y[XL], z[XL]; } XPt;

static void xp256_inf(XPt *p) { memset(p->x, 0, sizeof p->x); memset(p->y, 0, sizeof p->y); memset(p->z, 0, sizeof p->z); }

static bool xp256_is_inf(const XPt *p) { return xmont_is_zero_arr(p->z); }

static const char *XP256_P = "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF";
static const char *XP256_N = "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551";
static const char *XP256_GX = "6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296";
static const char *XP256_GY = "4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5";
static const char *XP256_B = "5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B";
static const char *XP256_SQRT = "3FFFFFFFC0000000400000000000000000000000400000000000000000000000";

static XMont xp256_fp, xp256_ord;
static uint32_t xp256_gx[XL], xp256_gy[XL], xp256_a[XL], xp256_b[XL], xp256_one[XL];
static uint8_t xp256_pm2[32], xp256_nm2[32], xp256_sqrt[32];
static int xp256_ready = 0;

static int xh2v(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}

static void xh2b(const char *s, uint8_t *out, int n) {
  for (int i = 0; i < n; i++) out[i] = (uint8_t)((xh2v(s[2 * i]) << 4) | xh2v(s[2 * i + 1]));
}

static void xp256_init(void) {
  if (xp256_ready) return;
  uint8_t pb[32], nb[32], gxb[32], gyb[32], bb[32], ab[32];
  xh2b(XP256_P, pb, 32);
  xh2b(XP256_N, nb, 32);
  xh2b(XP256_GX, gxb, 32);
  xh2b(XP256_GY, gyb, 32);
  xh2b(XP256_B, bb, 32);
  xh2b(XP256_SQRT, xp256_sqrt, 32);
  memcpy(ab, pb, 32);
  ab[31] -= 3;
  memcpy(xp256_pm2, pb, 32);
  xp256_pm2[31] -= 2;
  memcpy(xp256_nm2, nb, 32);
  xp256_nm2[31] -= 2;
  xmont_init(&xp256_fp, pb, 32);
  xmont_init(&xp256_ord, nb, 32);
  xmont_from(&xp256_fp, gxb, 32, xp256_gx);
  xmont_from(&xp256_fp, gyb, 32, xp256_gy);
  xmont_from(&xp256_fp, ab, 32, xp256_a);
  xmont_from(&xp256_fp, bb, 32, xp256_b);
  xmont_mul(&xp256_fp, XM_ONE, xp256_fp.r2, xp256_one);
  xp256_ready = 1;
}

static void xp256_dbl(XPt *r, const XPt *p) {
  uint32_t y2[XL], c[XL], s[XL], m[XL], z4[XL], z2[XL], t[XL], t2[XL];
  if (xp256_is_inf(p) || xmont_is_zero_arr(p->y)) { xp256_inf(r); return; }
  xfp_mul(&xp256_fp, p->y, p->y, y2);
  xfp_mul(&xp256_fp, y2, y2, c);
  xfp_mul(&xp256_fp, p->z, p->z, z2);
  xfp_mul(&xp256_fp, z2, z2, z4);
  xfp_mul(&xp256_fp, p->x, y2, t);
  xfp_add(&xp256_fp, t, t, s);
  xfp_add(&xp256_fp, s, s, s);
  xfp_mul(&xp256_fp, p->x, p->x, t2);
  xfp_add(&xp256_fp, t2, t2, t);
  xfp_add(&xp256_fp, t, t2, t);
  xfp_mul(&xp256_fp, xp256_a, z4, t2);
  xfp_add(&xp256_fp, t, t2, m);
  uint32_t x3[XL], y3[XL], z3[XL], tmp[XL];
  xfp_mul(&xp256_fp, m, m, x3);
  xfp_add(&xp256_fp, s, s, tmp);
  xfp_sub(&xp256_fp, x3, tmp, x3);
  xfp_sub(&xp256_fp, s, x3, tmp);
  xfp_mul(&xp256_fp, m, tmp, y3);
  xfp_add(&xp256_fp, c, c, t);
  xfp_add(&xp256_fp, t, t, t);
  xfp_add(&xp256_fp, t, t, t);
  xfp_sub(&xp256_fp, y3, t, y3);
  xfp_mul(&xp256_fp, p->y, p->z, t);
  xfp_add(&xp256_fp, t, t, z3);
  memcpy(r->x, x3, sizeof x3);
  memcpy(r->y, y3, sizeof y3);
  memcpy(r->z, z3, sizeof z3);
}

static void xp256_add(XPt *r, const XPt *p, const XPt *q) {
  if (xp256_is_inf(p)) { *r = *q; return; }
  if (xp256_is_inf(q)) { *r = *p; return; }
  uint32_t z1z1[XL], z2z2[XL], u1[XL], u2[XL], s1[XL], s2[XL], h[XL], rr[XL], hh[XL], hhh[XL], v[XL];
  xfp_mul(&xp256_fp, p->z, p->z, z1z1);
  xfp_mul(&xp256_fp, q->z, q->z, z2z2);
  xfp_mul(&xp256_fp, p->x, z2z2, u1);
  xfp_mul(&xp256_fp, q->x, z1z1, u2);
  xfp_mul(&xp256_fp, p->y, q->z, s1);
  xfp_mul(&xp256_fp, s1, z2z2, s1);
  xfp_mul(&xp256_fp, q->y, p->z, s2);
  xfp_mul(&xp256_fp, s2, z1z1, s2);
  xfp_sub(&xp256_fp, u2, u1, h);
  xfp_sub(&xp256_fp, s2, s1, rr);
  if (xmont_is_zero_arr(h)) {
    if (xmont_is_zero_arr(rr)) { xp256_dbl(r, p); return; }
    xp256_inf(r);
    return;
  }
  xfp_mul(&xp256_fp, h, h, hh);
  xfp_mul(&xp256_fp, h, hh, hhh);
  xfp_mul(&xp256_fp, u1, hh, v);
  uint32_t x3[XL], y3[XL], z3[XL], tmp[XL], tmp2[XL];
  xfp_mul(&xp256_fp, rr, rr, x3);
  xfp_sub(&xp256_fp, x3, hhh, x3);
  xfp_add(&xp256_fp, v, v, tmp);
  xfp_sub(&xp256_fp, x3, tmp, x3);
  xfp_sub(&xp256_fp, v, x3, tmp);
  xfp_mul(&xp256_fp, rr, tmp, y3);
  xfp_mul(&xp256_fp, s1, hhh, tmp2);
  xfp_sub(&xp256_fp, y3, tmp2, y3);
  xfp_mul(&xp256_fp, p->z, q->z, z3);
  xfp_mul(&xp256_fp, z3, h, z3);
  memcpy(r->x, x3, sizeof x3);
  memcpy(r->y, y3, sizeof y3);
  memcpy(r->z, z3, sizeof z3);
}

static void xp256_mul(const XPt *p, const uint8_t *k32, XPt *out) {
  XPt r;
  xp256_inf(&r);
  for (int i = 255; i >= 0; i--) {
    xp256_dbl(&r, &r);
    if ((k32[31 - (i >> 3)] >> (i & 7)) & 1) xp256_add(&r, &r, p);
  }
  *out = r;
}

static bool xp256_affine_x(const XPt *p, uint8_t out[32]) {
  if (xp256_is_inf(p)) return false;
  uint32_t zinv[XL], zinv2[XL], x[XL];
  xmont_pow(&xp256_fp, p->z, xp256_pm2, 32, zinv);
  xfp_mul(&xp256_fp, zinv, zinv, zinv2);
  xfp_mul(&xp256_fp, p->x, zinv2, x);
  xmont_to_bytes(&xp256_fp, x, out, 32);
  return true;
}

static bool xp256_decompress(const uint8_t *in, int n, uint8_t out[65], int *outn);

static bool xp256_affine_y(const XPt *p, uint8_t out[32]) {
  if (xp256_is_inf(p)) return false;
  uint32_t zinv[XL], zinv2[XL], zinv3[XL], y[XL];
  xmont_pow(&xp256_fp, p->z, xp256_pm2, 32, zinv);
  xfp_mul(&xp256_fp, zinv, zinv, zinv2);
  xfp_mul(&xp256_fp, zinv2, zinv, zinv3);
  xfp_mul(&xp256_fp, p->y, zinv3, y);
  xmont_to_bytes(&xp256_fp, y, out, 32);
  return true;
}

static void xp256_pubkey(const uint8_t *priv32, uint8_t out65[65]) {
  xp256_init();
  XPt G, R;
  memset(&G, 0, sizeof G);
  memcpy(G.x, xp256_gx, sizeof G.x);
  memcpy(G.y, xp256_gy, sizeof G.y);
  memcpy(G.z, xp256_one, sizeof G.z);
  xp256_mul(&G, priv32, &R);
  out65[0] = 4;
  if (!xp256_affine_x(&R, out65 + 1)) memset(out65, 0, 65);
  else if (!xp256_affine_y(&R, out65 + 33)) memset(out65, 0, 65);
}

static bool xp256_ecdh(const uint8_t *peer, int plen, const uint8_t *priv32, uint8_t out[32]) {
  xp256_init();
  uint8_t Q[65];
  int Qn;
  if (!xp256_decompress(peer, plen, Q, &Qn)) return false;
  XPt P, R;
  memset(&P, 0, sizeof P);
  xmont_from(&xp256_fp, Q + 1, 32, P.x);
  xmont_from(&xp256_fp, Q + 33, 32, P.y);
  memcpy(P.z, xp256_one, sizeof P.z);
  xp256_mul(&P, priv32, &R);
  if (xp256_is_inf(&R)) return false;
  return xp256_affine_x(&R, out);
}

static bool xp256_on_curve(const uint8_t *x32, const uint8_t *y32) {
  uint32_t x[XL], y[XL], l[XL], r[XL], t[XL];
  xmont_from(&xp256_fp, x32, 32, x);
  xmont_from(&xp256_fp, y32, 32, y);
  xfp_mul(&xp256_fp, x, x, t);
  xfp_mul(&xp256_fp, t, x, l);
  xfp_add(&xp256_fp, x, x, t);
  xfp_add(&xp256_fp, t, x, t);
  xfp_sub(&xp256_fp, l, t, l);
  xfp_add(&xp256_fp, l, xp256_b, l);
  xfp_mul(&xp256_fp, y, y, r);
  return xmont_cmp(r, l) == 0;
}

static bool xp256_decompress(const uint8_t *in, int n, uint8_t out[65], int *outn) {
  if (n == 65 && in[0] == 4) { memcpy(out, in, 65); *outn = 65; return true; }
  if (n == 33 && (in[0] == 2 || in[0] == 3)) {
    uint32_t x[XL], t[XL], a3[XL];
    uint8_t yb[32];
    xmont_from(&xp256_fp, in + 1, 32, x);
    xfp_mul(&xp256_fp, x, x, t);
    xfp_mul(&xp256_fp, t, x, t);
    xfp_mul(&xp256_fp, xp256_a, x, a3);
    xfp_sub(&xp256_fp, t, a3, t);
    xfp_add(&xp256_fp, t, xp256_b, t);
    xmont_pow(&xp256_fp, t, xp256_sqrt, 32, t);
    xmont_to_bytes(&xp256_fp, t, yb, 32);
    if ((yb[31] & 1) != (in[0] & 1)) {
      uint32_t neg[XL];
      xfp_sub(&xp256_fp, XM_ZERO, t, neg);
      xmont_to_bytes(&xp256_fp, neg, yb, 32);
    }
    out[0] = 4;
    memcpy(out + 1, in + 1, 32);
    memcpy(out + 33, yb, 32);
    *outn = 65;
    return xp256_on_curve(out + 1, out + 33);
  }
  return false;
}

static bool xp256_ecdsa_verify(const uint8_t *q, int qlen, int hash_id, const uint8_t *hash, int hlen, const uint8_t *rb, int rlen, const uint8_t *sb, int slen) {
  xp256_init();
  uint8_t Q[65];
  int Qn;
  if (!xp256_decompress(q, qlen, Q, &Qn)) return false;
  if (!xp256_on_curve(Q + 1, Q + 33)) return false;
  uint8_t rb32[32], sb32[32];
  memset(rb32, 0, 32);
  memset(sb32, 0, 32);
  while (rlen > 1 && rb[0] == 0) { rb++; rlen--; }
  while (slen > 1 && sb[0] == 0) { sb++; slen--; }
  if (rlen <= 0 || rlen > 32 || slen <= 0 || slen > 32) return false;
  memcpy(rb32 + 32 - rlen, rb, (size_t)rlen);
  memcpy(sb32 + 32 - slen, sb, (size_t)slen);
  uint32_t r_m[XL], s_m[XL];
  xmont_from(&xp256_ord, rb32, 32, r_m);
  xmont_from(&xp256_ord, sb32, 32, s_m);
  if (xmont_is_zero_arr(r_m) || xmont_is_zero_arr(s_m)) return false;
  uint8_t z32[32];
  memset(z32, 0, 32);
  int hl = hlen > 32 ? 32 : hlen;
  memcpy(z32 + 32 - hl, hash, (size_t)hl);
  uint32_t z_l[XL];
  xmont_load_be(z_l, z32, 32);
  if (xmont_cmp(z_l, xp256_ord.n) >= 0) {
    uint32_t t[XL];
    xmont_sub_n(&xp256_ord, z_l, t);
    memcpy(z_l, t, sizeof t);
  }
  uint32_t z_m[XL];
  xmont_mul(&xp256_ord, z_l, xp256_ord.r2, z_m);
  uint32_t w_m[XL];
  xmont_pow(&xp256_ord, s_m, xp256_nm2, 32, w_m);
  uint32_t u1_m[XL], u2_m[XL], u1b[XL], u2b[XL];
  xmont_mul(&xp256_ord, z_m, w_m, u1_m);
  xmont_mul(&xp256_ord, r_m, w_m, u2_m);
  xmont_to_bytes(&xp256_ord, u1_m, (uint8_t *)u1b, 32);
  xmont_to_bytes(&xp256_ord, u2_m, (uint8_t *)u2b, 32);
  XPt G, Qp, A, B, R;
  memset(&G, 0, sizeof G);
  memcpy(G.x, xp256_gx, sizeof G.x);
  memcpy(G.y, xp256_gy, sizeof G.y);
  memcpy(G.z, xp256_one, sizeof G.z);
  memset(&Qp, 0, sizeof Qp);
  xmont_from(&xp256_fp, Q + 1, 32, Qp.x);
  xmont_from(&xp256_fp, Q + 33, 32, Qp.y);
  memcpy(Qp.z, xp256_one, sizeof Qp.z);
  xp256_mul(&G, (const uint8_t *)u1b, &A);
  xp256_mul(&Qp, (const uint8_t *)u2b, &B);
  xp256_add(&R, &A, &B);
  if (xp256_is_inf(&R)) return false;
  uint8_t rx[32];
  if (!xp256_affine_x(&R, rx)) return false;
  uint32_t v_l[XL];
  xmont_load_be(v_l, rx, 32);
  if (xmont_cmp(v_l, xp256_ord.n) >= 0) {
    uint32_t t[XL];
    xmont_sub_n(&xp256_ord, v_l, t);
    memcpy(v_l, t, sizeof t);
  }
  uint32_t r_l[XL];
  xmont_load_be(r_l, rb32, 32);
  return xmont_cmp(v_l, r_l) == 0;
}

#define XH_SHA1 1
#define XH_SHA256 2
#define XH_SHA384 3
#define XH_SHA512 4

static void x509_hash_msg(int hash_id, const uint8_t *d, size_t n, uint8_t *out, int *outlen) {
  if (hash_id == XH_SHA1) { sha1_raw(d, n, out); *outlen = 20; }
  else if (hash_id == XH_SHA256) { sha256_raw(d, n, out); *outlen = 32; }
  else if (hash_id == XH_SHA384) { sha384_raw(d, n, out); *outlen = 48; }
  else { sha512_raw(d, n, out); *outlen = 64; }
}

static const uint8_t x509_di_sha1[15] = {0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a, 0x05, 0x00, 0x04, 0x14};
static const uint8_t x509_di_sha256[19] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
static const uint8_t x509_di_sha384[19] = {0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0x04, 0x30};
static const uint8_t x509_di_sha512[19] = {0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40};

static bool x509_rsa_verify(const uint8_t *nb, int nlen, const uint8_t *eb, int elen, int hash_id, const uint8_t *hash, int hlen, const uint8_t *sig, int slen) {
  if (nlen < 32 || nlen > 1024 || elen <= 0 || elen > 8 || slen <= 0 || slen > 1024) return false;
  while (nlen > 1 && nb[0] == 0) { nb++; nlen--; }
  Big N, E, S;
  big_from_bytes((const char *)nb, nlen, &N);
  big_from_bytes((const char *)eb, elen, &E);
  big_from_bytes((const char *)sig, slen, &S);
  bool ok = false;
  if (bmag_cmp(&S, &N) < 0) {
    Big M = big_modpow(&S, &E, &N);
    Value emv = big_bytes_val(&M, nlen);
    if (IS_T(emv, O_STR)) {
      Str *em = AS_STR(emv);
      if (em->len == nlen && em->chars[0] == 0 && em->chars[1] == 1) {
        const uint8_t *di = NULL;
        int dilen = 0;
        if (hash_id == XH_SHA1) { di = x509_di_sha1; dilen = 15; }
        else if (hash_id == XH_SHA256) { di = x509_di_sha256; dilen = 19; }
        else if (hash_id == XH_SHA384) { di = x509_di_sha384; dilen = 19; }
        else if (hash_id == XH_SHA512) { di = x509_di_sha512; dilen = 19; }
        if (di) {
          int need = dilen + hlen;
          int i = 2;
          while (i < em->len && em->chars[i] == (char)0xFF) i++;
          if (i >= 10 && i < em->len && em->chars[i] == 0) {
            int off = i + 1;
            if (em->len - off == need && memcmp(em->chars + off, di, (size_t)dilen) == 0 && memcmp(em->chars + off + dilen, hash, (size_t)hlen) == 0) ok = true;
          }
        }
      }
    }
    free(M.d);
  }
  free(N.d); free(E.d); free(S.d);
  return ok;
}

static bool x509_mgf1(int hash_id, const uint8_t *seed, int sl, uint8_t *out, int olen) {
  int hl = hash_id == XH_SHA384 ? 48 : hash_id == XH_SHA512 ? 64 : 32;
  uint8_t buf[72], h[64];
  uint32_t ctr = 0;
  int off = 0;
  while (off < olen) {
    memcpy(buf, seed, (size_t)sl);
    buf[sl] = (uint8_t)(ctr >> 24); buf[sl + 1] = (uint8_t)(ctr >> 16); buf[sl + 2] = (uint8_t)(ctr >> 8); buf[sl + 3] = (uint8_t)ctr;
    if (hash_id == XH_SHA384) sha384_raw(buf, (size_t)sl + 4, h);
    else if (hash_id == XH_SHA512) sha512_raw(buf, (size_t)sl + 4, h);
    else sha256_raw(buf, (size_t)sl + 4, h);
    int take = olen - off < hl ? olen - off : hl;
    memcpy(out + off, h, (size_t)take);
    off += take;
    ctr++;
  }
  return true;
}

static bool x509_rsa_pss_verify(const uint8_t *nb, int nlen, const uint8_t *eb, int elen, int hash_id, const uint8_t *hash, int hlen, const uint8_t *sig, int slen) {
  if (nlen < 32 || nlen > 1024 || elen <= 0 || elen > 8) return false;
  if (hlen < 16 || hlen > 64 || slen < hlen + 2 || slen > 1024) return false;
  while (nlen > 1 && nb[0] == 0) { nb++; nlen--; }
  Big N, E, S;
  big_from_bytes((const char *)nb, nlen, &N);
  big_from_bytes((const char *)eb, elen, &E);
  big_from_bytes((const char *)sig, slen, &S);
  bool ok = false;
  if (bmag_cmp(&S, &N) < 0) {
    Big M = big_modpow(&S, &E, &N);
    Value emv = big_bytes_val(&M, nlen);
    if (IS_T(emv, O_STR)) {
      Str *em = AS_STR(emv);
      int emlen = em->len;
      if (emlen == nlen && (uint8_t)em->chars[emlen - 1] == 0xbc) {
        int b = 0;
        uint8_t topb = (uint8_t)nb[0];
        while (topb) { b++; topb >>= 1; }
        int emBits = (nlen - 1) * 8 + b - 1;
        int topzero = 8 * emlen - emBits;
        if (topzero >= 1 && topzero <= 8) {
          const uint8_t *mdb = (const uint8_t *)em->chars;
          const uint8_t *H = (const uint8_t *)em->chars + (emlen - hlen - 1);
          if ((mdb[0] >> (8 - topzero)) == 0) {
            int dblen = emlen - hlen - 1;
            uint8_t *db = (uint8_t *)xalloc((size_t)dblen);
            x509_mgf1(hash_id, H, hlen, db, dblen);
            for (int i = 0; i < dblen; i++) db[i] ^= mdb[i];
            db[0] &= (uint8_t)(0xFFu >> topzero);
            int sl = hlen;
            bool shape = dblen > sl;
            for (int i = 0; shape && i < dblen - sl - 1; i++) if (db[i] != 0) shape = false;
            if (shape && db[dblen - sl - 1] != 0x01) shape = false;
            if (shape) {
              uint8_t m2[8 + 64 + 64], h2[64];
              memset(m2, 0, 8);
              memcpy(m2 + 8, hash, (size_t)hlen);
              memcpy(m2 + 8 + hlen, db + (dblen - sl), (size_t)sl);
              int m2n = 8 + hlen + sl;
              if (hash_id == XH_SHA384) sha384_raw(m2, (size_t)m2n, h2);
              else if (hash_id == XH_SHA512) sha512_raw(m2, (size_t)m2n, h2);
              else sha256_raw(m2, (size_t)m2n, h2);
              ok = memcmp(h2, H, (size_t)hlen) == 0;
            }
            free(db);
          }
        }
      }
    }
    free(M.d);
  }
  free(N.d); free(E.d); free(S.d);
  return ok;
}

typedef struct { const uint8_t *p; int n; } XDer;

static bool xder_get(XDer *d, unsigned char *tag, XDer *tlv, XDer *content) {
  if (d->n < 2) return false;
  unsigned char t = d->p[0];
  int lb, hl;
  if ((d->p[1] & 0x80) == 0) { lb = d->p[1]; hl = 2; }
  else {
    int nb = d->p[1] & 0x7F;
    if (nb == 0 || nb > 4 || d->n < 2 + nb) return false;
    lb = 0;
    for (int i = 0; i < nb; i++) lb = (lb << 8) | d->p[2 + i];
    hl = 2 + nb;
  }
  if (lb < 0 || hl + lb > d->n) return false;
  if (tag) *tag = t;
  if (tlv) { tlv->p = d->p; tlv->n = hl + lb; }
  if (content) { content->p = d->p + hl; content->n = lb; }
  d->p += hl + lb;
  d->n -= hl + lb;
  return true;
}

static bool xder_c(XDer *d, unsigned char *tag, XDer *content) { return xder_get(d, tag, NULL, content); }

static bool xder_oid_is(const XDer *oid, const uint8_t *want, int wn) {
  return oid->n == wn && memcmp(oid->p, want, (size_t)wn) == 0;
}

static const uint8_t XOID_RSA[9] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
static const uint8_t XOID_RSA_SHA1[9] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x05};
static const uint8_t XOID_RSA_SHA256[9] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B};
static const uint8_t XOID_RSA_SHA384[9] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0C};
static const uint8_t XOID_RSA_SHA512[9] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0D};
static const uint8_t XOID_EC[7] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
static const uint8_t XOID_P256[8] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
static const uint8_t XOID_P384[5] = {0x2B, 0x81, 0x04, 0x00, 0x22};
static const uint8_t XOID_P521[5] = {0x2B, 0x81, 0x04, 0x00, 0x23};
static const uint8_t XOID_ECDSA_SHA256[8] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02};
static const uint8_t XOID_ECDSA_SHA384[8] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x03};
static const uint8_t XOID_ECDSA_SHA512[8] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x04};
static const uint8_t XOID_SAN[3] = {0x55, 0x1D, 0x11};
static const uint8_t XOID_BC[3] = {0x55, 0x1D, 0x13};
static const uint8_t XOID_CN[3] = {0x55, 0x04, 0x03};

typedef struct {
  XDer tbs;
  XDer sigalg;
  XDer sig;
  XDer issuer, subject, spki;
  XDer san;
  int has_san, has_bc, is_ca;
  long long nb, na;
  int ktype;
  XDer rsa_n, rsa_e;
  uint8_t ec_q[133];
  int ec_qlen;
  int ec_curve;
} X509C;

static long long x509_days(int y, int m, int d) {
  y -= m <= 2;
  long long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long long)doe - 719468;
}

static long long x509_time(const XDer *t) {
  if (t->n < 11) return -1;
  char b[32];
  int n = t->n < 31 ? t->n : 31;
  memcpy(b, t->p, (size_t)n);
  b[n] = 0;
  int y, mo, da, h, mi, s;
  if (b[n - 1] != 'Z') return -1;
  if (n == 13) {
    y = (b[0] - '0') * 10 + (b[1] - '0');
    y += y >= 50 ? 1900 : 2000;
    mo = (b[2] - '0') * 10 + (b[3] - '0');
    da = (b[4] - '0') * 10 + (b[5] - '0');
    h = (b[6] - '0') * 10 + (b[7] - '0');
    mi = (b[8] - '0') * 10 + (b[9] - '0');
    s = (b[10] - '0') * 10 + (b[11] - '0');
  } else if (n == 15) {
    y = (b[0] - '0') * 1000 + (b[1] - '0') * 100 + (b[2] - '0') * 10 + (b[3] - '0');
    mo = (b[4] - '0') * 10 + (b[5] - '0');
    da = (b[6] - '0') * 10 + (b[7] - '0');
    h = (b[8] - '0') * 10 + (b[9] - '0');
    mi = (b[10] - '0') * 10 + (b[11] - '0');
    s = (b[12] - '0') * 10 + (b[13] - '0');
  } else return -1;
  if (mo < 1 || mo > 12 || da < 1 || da > 31) return -1;
  return (x509_days(y, mo, da) * 86400) + h * 3600 + mi * 60 + s;
}

static bool x509_parse_sigalg(const XDer *alg, int *ktype, int *hash_id) {
  XDer d = *alg, seq, oid;
  unsigned char t;
  if (!xder_c(&d, &t, &seq) || t != 0x30) return false;
  if (!xder_c(&seq, &t, &oid) || t != 0x06) return false;
  if (xder_oid_is(&oid, XOID_RSA_SHA256, 9)) { *ktype = 1; *hash_id = XH_SHA256; return true; }
  if (xder_oid_is(&oid, XOID_RSA_SHA384, 9)) { *ktype = 1; *hash_id = XH_SHA384; return true; }
  if (xder_oid_is(&oid, XOID_RSA_SHA512, 9)) { *ktype = 1; *hash_id = XH_SHA512; return true; }
  if (xder_oid_is(&oid, XOID_RSA_SHA1, 9)) { *ktype = 1; *hash_id = XH_SHA1; return true; }
  if (xder_oid_is(&oid, XOID_ECDSA_SHA256, 8)) { *ktype = 2; *hash_id = XH_SHA256; return true; }
  if (xder_oid_is(&oid, XOID_ECDSA_SHA384, 8)) { *ktype = 2; *hash_id = XH_SHA384; return true; }
  if (xder_oid_is(&oid, XOID_ECDSA_SHA512, 8)) { *ktype = 2; *hash_id = XH_SHA512; return true; }
  return false;
}

static bool x509_parse_spki(const XDer *spki, X509C *c) {
  XDer d = *spki, algc, algseq, oid, bs;
  unsigned char t;
  if (!xder_c(&d, &t, &algc) || t != 0x30) return false;
  if (!xder_c(&algc, &t, &algseq) || t != 0x30) return false;
  if (!xder_c(&algseq, &t, &oid) || t != 0x06) return false;
  if (xder_oid_is(&oid, XOID_RSA, 9)) {
    if (!xder_c(&algc, &t, &bs) || t != 0x03 || bs.n < 2 || bs.p[0] != 0) return false;
    XDer key = {bs.p + 1, bs.n - 1}, kseq, kn, ke;
    if (!xder_c(&key, &t, &kseq) || t != 0x30) return false;
    if (!xder_c(&kseq, &t, &kn) || t != 0x02) return false;
    if (!xder_c(&kseq, &t, &ke) || t != 0x02) return false;
    c->ktype = 1;
    c->rsa_n = kn;
    c->rsa_e = ke;
    return true;
  }
  if (xder_oid_is(&oid, XOID_EC, 7)) {
    XDer curve;
    if (!xder_c(&algseq, &t, &curve) || t != 0x06) return false;
    int cid = 0;
    if (xder_oid_is(&curve, XOID_P256, 8)) cid = 1;
    else if (xder_oid_is(&curve, XOID_P384, 5)) cid = 2;
    else if (xder_oid_is(&curve, XOID_P521, 5)) cid = 3;
    else return false;
    if (!xder_c(&algc, &t, &bs) || t != 0x03 || bs.n < 2 || bs.p[0] != 0) return false;
    XDer pt = {bs.p + 1, bs.n - 1};
    if (cid == 1) {
      uint8_t out[65];
      int on;
      xp256_init();
      if (!xp256_decompress(pt.p, pt.n, out, &on)) return false;
      c->ktype = 2;
      c->ec_curve = 1;
      memcpy(c->ec_q, out, 65);
      c->ec_qlen = 65;
      return true;
    }
    uint8_t out[133];
    int on = 0;
    if (!ecbig_decompress(cid, pt.p, pt.n, out, &on)) return false;
    c->ktype = 2;
    c->ec_curve = cid;
    memcpy(c->ec_q, out, (size_t)on);
    c->ec_qlen = on;
    return true;
  }
  return false;
}

static bool x509_parse(X509C *c, const uint8_t *der, int n) {
  memset(c, 0, sizeof *c);
  XDer d = {der, n}, cert, tbs, v;
  unsigned char t;
  if (!xder_c(&d, &t, &cert) || t != 0x30) return false;
  XDer tbs_tlv;
  if (!xder_get(&cert, &t, &tbs_tlv, &tbs) || t != 0x30) return false;
  c->tbs = tbs_tlv;
  if (!xder_get(&cert, &t, &c->sigalg, NULL) || t != 0x30) return false;
  if (!xder_c(&cert, &t, &v) || t != 0x03 || v.n < 1 || v.p[0] != 0) return false;
  c->sig.p = v.p + 1;
  c->sig.n = v.n - 1;
  XDer tb = tbs;
  if (tb.n > 0 && tb.p[0] == 0xA0) {
    if (!xder_get(&tb, &t, NULL, NULL) || t != 0xA0) return false;
  }
  if (!xder_c(&tb, &t, &v) || t != 0x02) return false;
  if (!xder_get(&tb, &t, NULL, NULL) || t != 0x30) return false;
  if (!xder_get(&tb, &t, &c->issuer, NULL) || t != 0x30) return false;
  if (!xder_c(&tb, &t, &v) || t != 0x30) return false;
  XDer val = v, t1, t2;
  if (!xder_c(&val, &t, &t1) || (t != 0x17 && t != 0x18)) return false;
  if (!xder_c(&val, &t, &t2) || (t != 0x17 && t != 0x18)) return false;
  c->nb = x509_time(&t1);
  c->na = x509_time(&t2);
  if (c->nb < 0 || c->na < 0) return false;
  if (!xder_get(&tb, &t, &c->subject, NULL) || t != 0x30) return false;
  if (!xder_get(&tb, &t, &c->spki, NULL) || t != 0x30) return false;
  if (!x509_parse_spki(&c->spki, c)) return false;
  while (tb.n > 0) {
    XDer ev;
    if (!xder_get(&tb, &t, NULL, &ev)) break;
    if (t != 0xA3) continue;
    XDer list;
    unsigned char et;
    if (!xder_c(&ev, &et, &list) || et != 0x30) continue;
    while (list.n > 0) {
      XDer e2, oid, ov;
      if (!xder_get(&list, &et, NULL, &e2) || et != 0x30) continue;
      if (!xder_c(&e2, &et, &oid) || et != 0x06) continue;
      if (!xder_c(&e2, &et, &ov)) continue;
      if (et == 0x01) { if (!xder_c(&e2, &et, &ov)) continue; }
      if (et != 0x04) continue;
      if (xder_oid_is(&oid, XOID_SAN, 3)) {
        XDer sq;
        unsigned char t2c;
        if (xder_c(&ov, &t2c, &sq) && t2c == 0x30) { c->san = sq; c->has_san = 1; }
      } else if (xder_oid_is(&oid, XOID_BC, 3)) {
        XDer sq;
        unsigned char t2c;
        c->has_bc = 1;
        if (xder_c(&ov, &t2c, &sq) && t2c == 0x30 && sq.n > 0 && sq.p[0] == 0x01) {
          XDer probe = sq, bval;
          unsigned char t3;
          if (xder_c(&probe, &t3, &bval) && bval.n > 0) c->is_ca = bval.p[0] != 0;
        }
      }
    }
  }
  return true;
}

static bool x509_time_ok(const X509C *c) {
  long long now = (long long)time(NULL);
  return now >= c->nb && now <= c->na;
}

static bool x509_host_label_eq(const char *a, int al, const char *b, int bl) {
  if (al != bl) return false;
  for (int i = 0; i < al; i++) {
    char ca = a[i], cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    if (ca != cb) return false;
  }
  return true;
}

static bool x509_host_match(const char *pat, int pl, const char *host, int hl) {
  if (pl > 0 && pat[pl - 1] == '.') pl--;
  if (hl > 0 && host[hl - 1] == '.') hl--;
  if (pl >= 2 && pat[0] == '*' && pat[1] == '.') {
    const char *dot = memchr(host, '.', (size_t)hl);
    if (!dot) return false;
    return x509_host_label_eq(pat + 2, pl - 2, dot + 1, (int)(host + hl - dot - 1));
  }
  return x509_host_label_eq(pat, pl, host, hl);
}

static bool x509_host_ok(const X509C *c, const char *host) {
  if (!host || !*host) return true;
  int hl = (int)strlen(host);
  if (c->has_san) {
    XDer s = c->san;
    unsigned char t;
    XDer gn;
    while (s.n > 0) {
      if (!xder_get(&s, &t, NULL, &gn)) break;
      if (t == 0x82 && x509_host_match((const char *)gn.p, gn.n, host, hl)) return true;
    }
    return false;
  }
  XDer sub = c->subject, rdn, atv, oid, val;
  unsigned char t;
  const char *cn = NULL;
  int cnl = 0;
  while (sub.n > 0) {
    if (!xder_c(&sub, &t, &rdn) || t != 0x31) break;
    while (rdn.n > 0) {
      if (!xder_c(&rdn, &t, &atv) || t != 0x30) break;
      if (!xder_c(&atv, &t, &oid) || t != 0x06) break;
      if (!xder_c(&atv, &t, &val) || t != 0x0C) break;
      if (xder_oid_is(&oid, XOID_CN, 3)) { cn = (const char *)val.p; cnl = val.n; }
    }
  }
  return cn && x509_host_match(cn, cnl, host, hl);
}

static bool x509_sig_verify(const X509C *sigd, const X509C *keyc) {
  int ktype = 0, hash_id = 0;
  if (!x509_parse_sigalg(&sigd->sigalg, &ktype, &hash_id)) return false;
  uint8_t hash[64];
  int hlen;
  x509_hash_msg(hash_id, sigd->tbs.p, sigd->tbs.n, hash, &hlen);
  if (ktype == 1 && keyc->ktype == 1) return x509_rsa_verify(keyc->rsa_n.p, keyc->rsa_n.n, keyc->rsa_e.p, keyc->rsa_e.n, hash_id, hash, hlen, sigd->sig.p, sigd->sig.n);
  if (ktype == 2 && keyc->ktype == 2) {
    XDer d = sigd->sig, sq, ri, si;
    unsigned char t;
    if (!xder_c(&d, &t, &sq) || t != 0x30) return false;
    if (!xder_c(&sq, &t, &ri) || t != 0x02) return false;
    if (!xder_c(&sq, &t, &si) || t != 0x02) return false;
    if (keyc->ec_curve == 1) return xp256_ecdsa_verify(keyc->ec_q, keyc->ec_qlen, hash_id, hash, hlen, ri.p, ri.n, si.p, si.n);
    return ecbig_ecdsa_verify(keyc->ec_curve, keyc->ec_q, keyc->ec_qlen, hash, hlen, ri.p, ri.n, si.p, si.n);
  }
  return false;
}

static bool x509_cv_verify(const X509C *c, int hash_id, const uint8_t *hash, int hlen, const uint8_t *sig, int slen) {
  if (c->ktype == 1) return x509_rsa_pss_verify(c->rsa_n.p, c->rsa_n.n, c->rsa_e.p, c->rsa_e.n, hash_id, hash, hlen, sig, slen);
  if (c->ktype == 2) {
    XDer d = {sig, slen}, sq, ri, si;
    unsigned char t;
    if (!xder_c(&d, &t, &sq) || t != 0x30) return false;
    if (!xder_c(&sq, &t, &ri) || t != 0x02) return false;
    if (!xder_c(&sq, &t, &si) || t != 0x02) return false;
    if (c->ec_curve == 1) return xp256_ecdsa_verify(c->ec_q, c->ec_qlen, hash_id, hash, hlen, ri.p, ri.n, si.p, si.n);
    return ecbig_ecdsa_verify(c->ec_curve, c->ec_q, c->ec_qlen, hash, hlen, ri.p, ri.n, si.p, si.n);
  }
  return false;
}

typedef struct { uint8_t *der; int n; } XBlob;

static XBlob *x509_roots = NULL;
static int x509_nroots = 0, x509_rcap = 0;

static void x509_add_root(const uint8_t *der, int n) {
  if (n <= 0 || n > 65536) return;
  if (x509_nroots == x509_rcap) { x509_rcap = x509_rcap ? x509_rcap * 2 : 64; x509_roots = (XBlob *)xrealloc(x509_roots, sizeof(XBlob) * (size_t)x509_rcap); }
  uint8_t *cp = (uint8_t *)xalloc((size_t)n);
  memcpy(cp, der, (size_t)n);
  x509_roots[x509_nroots].der = cp;
  x509_roots[x509_nroots].n = n;
  x509_nroots++;
}

#if SOLAN_WIN
typedef struct { unsigned long enc; unsigned char *pb; unsigned long cb; } XCertCtx;
static bool x509_load_roots_win(void) {
  HMODULE h = LoadLibraryA("crypt32.dll");
  if (!h) return false;
  void *(*xopen)(unsigned long, const void *) = (void *(*)(unsigned long, const void *))GetProcAddress(h, "CertOpenSystemStoreW");
  const void *(*xenum)(void *, const void *) = (const void *(*)(void *, const void *))GetProcAddress(h, "CertEnumCertificatesInStore");
  int (*xclose)(void *) = (int (*)(void *))GetProcAddress(h, "CertCloseStore");
  if (!xopen || !xenum || !xclose) return false;
  static const wchar_t rootname[] = L"ROOT";
  void *st = xopen(0, rootname);
  if (!st) return false;
  const XCertCtx *c = (const XCertCtx *)xenum(st, NULL);
  while (c) {
    x509_add_root(c->pb, (int)c->cb);
    c = (const XCertCtx *)xenum(st, c);
  }
  xclose(st);
  return x509_nroots > 0;
}
#endif

static bool x509_pem_bundle(const char *path) {
  size_t len;
  char *data = read_file(path, &len);
  if (!data) return false;
  int added = 0;
  const char *p = data, *end = data + len;
  while (p < end) {
    const char *b = strstr(p, "-----BEGIN CERTIFICATE-----");
    if (!b || b >= end) break;
    b += 27;
    const char *e = strstr(b, "-----END CERTIFICATE-----");
    if (!e || e > end) break;
    Buf raw;
    buf_init(&raw);
    for (const char *q = b; q < e; q++) {
      char c = *q;
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '-' || c == '_') buf_addc(&raw, c);
    }
    Buf out;
    buf_init(&out);
    if (b64_dec(buf_cstr(&raw), raw.n, &out) && out.n > 0) { x509_add_root((const uint8_t *)out.p, out.n); added++; }
    free(out.p);
    free(raw.p);
    p = e + 5;
  }
  free(data);
  return added > 0;
}

static bool x509_load_roots(void) {
  static int loaded = 0;
  if (loaded) return x509_nroots > 0;
  loaded = 1;
  const char *env = getenv("SOLAN_CA_BUNDLE");
  if (env && *env) x509_pem_bundle(env);
#if SOLAN_WIN
  if (!x509_nroots) x509_load_roots_win();
#else
  static const char *bundles[] = {
    "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem",
    "/etc/ssl/ca-bundle.crt", "/usr/local/share/certs/ca-root-nss.crt", "/etc/ca-certificates.crt", NULL};
  for (int i = 0; bundles[i] && !x509_nroots; i++) x509_pem_bundle(bundles[i]);
#endif
  return x509_nroots > 0;
}

static bool x509_same(const XDer *a, const XDer *b) { return a->n == b->n && memcmp(a->p, b->p, (size_t)a->n) == 0; }

static bool x509_verify_chain(const uint8_t *leaf, int leafn, const uint8_t **chain, const int *chainn, int nchain, const char *host, char *err, int es) {
  X509C cur;
  if (!x509_parse(&cur, leaf, leafn)) { snprintf(err, es, "شهادة الخادم غير صالحة (تعذر التحليل)"); return false; }
  if (!x509_time_ok(&cur)) { snprintf(err, es, "شهادة الخادم منتهية الصلاحية أو غير سارية بعد"); return false; }
  if (host && !x509_host_ok(&cur, host)) { snprintf(err, es, "اسم المضيف '%s' لا يطابق الشهادة", host); return false; }
  if (!x509_load_roots()) { snprintf(err, es, "لا يوجد مخزن شهادات جذور — لا يمكن التحقق"); return false; }
  for (int depth = 0; depth < 8; depth++) {
    for (int i = 0; i < x509_nroots; i++) {
      X509C rc;
      if (!x509_parse(&rc, x509_roots[i].der, x509_roots[i].n)) continue;
      if (x509_same(&rc.subject, &cur.subject) && x509_same(&rc.spki, &cur.spki)) return true;
    }
    X509C next;
    int found = 0;
    for (int i = 0; i < nchain && !found; i++) {
      X509C cand;
      if (!x509_parse(&cand, chain[i], chainn[i])) continue;
      if (!x509_same(&cand.subject, &cur.issuer)) continue;
      if (!x509_time_ok(&cand)) continue;
      if (cand.has_bc && !cand.is_ca) continue;
      if (!x509_sig_verify(&cur, &cand)) continue;
      if (x509_same(&cand.subject, &cand.issuer)) continue;
      next = cand;
      found = 1;
    }
    if (!found) {
      for (int i = 0; i < x509_nroots && !found; i++) {
        X509C cand;
        if (!x509_parse(&cand, x509_roots[i].der, x509_roots[i].n)) continue;
        if (!x509_same(&cand.subject, &cur.issuer)) continue;
        if (!x509_sig_verify(&cur, &cand)) continue;
        return true;
      }
      snprintf(err, es, "سلسلة الشهادات غير موثوقة (لا يوجد جذر موثوق للشهادة)");
      return false;
    }
    if (next.ktype == 0) { snprintf(err, es, "شهادة وسيطة بلا مفتاح صالح"); return false; }
    cur = next;
  }
  snprintf(err, es, "سلسلة الشهادات أعمق من المسموح");
  return false;
}
