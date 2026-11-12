





static uint64_t crv_ror64(uint64_t x, int s) { return (x >> s) | (x << (64 - s)); }


static const uint64_t K512[80] = {
  0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
  0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
  0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
  0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
  0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
  0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
  0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
  0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
  0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
  0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
  0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
  0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
  0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
  0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
  0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
  0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
  0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
  0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
  0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
  0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};
static void sha512_blk(uint64_t *h, const uint8_t *p) {
  uint64_t w[80];
  for (int i = 0; i < 16; i++)
    w[i] = ((uint64_t)p[8 * i] << 56) | ((uint64_t)p[8 * i + 1] << 48) | ((uint64_t)p[8 * i + 2] << 40) | ((uint64_t)p[8 * i + 3] << 32) |
           ((uint64_t)p[8 * i + 4] << 24) | ((uint64_t)p[8 * i + 5] << 16) | ((uint64_t)p[8 * i + 6] << 8) | (uint64_t)p[8 * i + 7];
  for (int i = 16; i < 80; i++) {
    uint64_t s0 = crv_ror64(w[i - 15], 1) ^ crv_ror64(w[i - 15], 8) ^ (w[i - 15] >> 7);
    uint64_t s1 = crv_ror64(w[i - 2], 19) ^ crv_ror64(w[i - 2], 61) ^ (w[i - 2] >> 6);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 80; i++) {
    uint64_t S1 = crv_ror64(e, 14) ^ crv_ror64(e, 18) ^ crv_ror64(e, 41), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K512[i] + w[i];
    uint64_t S0 = crv_ror64(a, 28) ^ crv_ror64(a, 34) ^ crv_ror64(a, 39), mj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + mj;
    hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
static void sha512_raw(const uint8_t *d, size_t n, uint8_t out[64]) {
  uint64_t h[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                   0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
  size_t i = 0;
  for (; i + 128 <= n; i += 128) sha512_blk(h, d + i);
  uint8_t t[256]; size_t r = n - i; memcpy(t, d + i, r); t[r++] = 0x80;
  size_t tot = (r + 16 <= 128) ? 128 : 256; memset(t + r, 0, tot - r);
  uint64_t bits = (uint64_t)n * 8;
  for (int k = 0; k < 8; k++) t[tot - 1 - k] = (uint8_t)((bits >> (8 * k)) & 255);
  sha512_blk(h, t); if (tot == 256) sha512_blk(h, t + 128);
  for (int k = 0; k < 8; k++) {
    out[8 * k] = (uint8_t)(h[k] >> 56); out[8 * k + 1] = (uint8_t)(h[k] >> 48); out[8 * k + 2] = (uint8_t)(h[k] >> 40); out[8 * k + 3] = (uint8_t)(h[k] >> 32);
    out[8 * k + 4] = (uint8_t)(h[k] >> 24); out[8 * k + 5] = (uint8_t)(h[k] >> 16); out[8 * k + 6] = (uint8_t)(h[k] >> 8); out[8 * k + 7] = (uint8_t)h[k];
  }
}
static void sha512_cat(uint8_t out[64], const uint8_t *p1, size_t n1, const uint8_t *p2, size_t n2, const uint8_t *p3, size_t n3) {
  size_t tot = n1 + n2 + n3;
  uint8_t *b = (uint8_t *)xalloc(tot + 1);
  if (n1) memcpy(b, p1, n1);
  if (n2) memcpy(b + n1, p2, n2);
  if (n3) memcpy(b + n1 + n2, p3, n3);
  sha512_raw(b, tot, out);
  free(b);
}


typedef int64_t fe[16];

static void fe_carry(fe h);

static void fe_zero(fe h) { for (int i = 0; i < 16; i++) h[i] = 0; }
static void fe_one(fe h) { fe_zero(h); h[0] = 1; }
static void fe_copy(fe d, const fe s) { for (int i = 0; i < 16; i++) d[i] = s[i]; }
static void fe_small(fe h, int64_t v) { fe_zero(h); h[0] = v; fe_carry(h); }
static void fe_cswap(fe a, fe b, int c) {
  int64_t m = -(int64_t)(c & 1);
  for (int i = 0; i < 16; i++) { int64_t t = m & (a[i] ^ b[i]); a[i] ^= t; b[i] ^= t; }
}


static void fe_carry(fe h) {
  int64_t c;
  for (int j = 0; j < 2; j++) {
    for (int i = 0; i <= 14; i++) { c = h[i] >> 16; h[i] &= 0xFFFF; h[i + 1] += c; }
    c = h[15] >> 16;
    h[15] &= 0xFFFF;
    h[0] += 38 * c;
  }

  if (h[0] >= (1 << 16)) {
    int64_t d = h[0] >> 16; h[0] &= 0xFFFF;
    for (int i = 1; i <= 15 && d; i++) { h[i] += d; d = h[i] >> 16; h[i] &= 0xFFFF; }
    if (d) h[0] += 38 * d;
  } else if (h[0] < 0) {
    int64_t d = h[0] >> 16; h[0] &= 0xFFFF;
    for (int i = 1; i <= 15 && d; i++) { h[i] += d; d = h[i] >> 16; h[i] &= 0xFFFF; }
    if (d) h[0] += 38 * d;
  }
}
static void fe_add(fe h, const fe a, const fe b) { for (int i = 0; i < 16; i++) h[i] = a[i] + b[i]; fe_carry(h); }
static void fe_sub(fe h, const fe a, const fe b) { for (int i = 0; i < 16; i++) h[i] = a[i] - b[i]; fe_carry(h); }
static void fe_neg(fe h, const fe a) { for (int i = 0; i < 16; i++) h[i] = -a[i]; fe_carry(h); }

static void fe_mul(fe h, const fe a, const fe b) {
  fe x, y; fe_copy(x, a); fe_copy(y, b);
  fe_carry(x); fe_carry(y);
  int64_t t[31];
  for (int k = 0; k <= 30; k++) {
    int64_t s = 0;
    int lo = k > 15 ? k - 15 : 0, hi = k < 15 ? k : 15;
    for (int i = lo; i <= hi; i++) s += x[i] * y[k - i];
    t[k] = s;
  }
  for (int k = 30; k >= 16; k--) t[k - 16] += 38 * t[k];
  for (int i = 0; i < 16; i++) h[i] = t[i];
  fe_carry(h);
}
static void fe_sqr(fe h, const fe a) { fe_mul(h, a, a); }


static const int64_t fe_p[16] = {0xFFED,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,0x7FFF};



static void fe_tobytes(uint8_t out[32], const fe h) {
  fe t; fe_copy(t, h); fe_carry(t);
  for (int n = 0; n < 2; n++) {
    int64_t d[16], br = 0;
    for (int i = 0; i < 16; i++) {
      int64_t x = t[i] - fe_p[i] - br;
      if (x < 0) { x += 1 << 16; br = 1; } else br = 0;
      d[i] = x;
    }
    if (br) break;
    for (int i = 0; i < 16; i++) t[i] = d[i];
  }
  for (int i = 0; i < 16; i++) { out[2 * i] = (uint8_t)(t[i] & 0xFF); out[2 * i + 1] = (uint8_t)((t[i] >> 8) & 0xFF); }
}
static void fe_frombytes(fe h, const uint8_t in[32]) {
  for (int i = 0; i < 16; i++) h[i] = (int64_t)(uint8_t)in[2 * i] | ((int64_t)(uint8_t)in[2 * i + 1] << 8);
  fe_carry(h);
}

static bool fe_canon(const fe h) {
  fe t; fe_copy(t, h); fe_carry(t);
  int64_t br = 0;
  for (int i = 0; i < 16; i++) { int64_t x = t[i] - fe_p[i] - br; if (x < 0) { x += 1 << 16; br = 1; } else br = 0; }
  return br != 0;
}
static bool fe_iszero(const fe h) { uint8_t b[32]; fe_tobytes(b, h); for (int i = 0; i < 32; i++) if (b[i]) return false; return true; }
static int fe_parity(const fe h) { uint8_t b[32]; fe_tobytes(b, h); return b[0] & 1; }



static const uint8_t fe_exp_p2[32] = {0xEB,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F};
static const uint8_t fe_exp_p14[32] = {0xFB,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x1F};
static const uint8_t fe_exp_p38[32] = {0xFE,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x0F};
static const uint8_t fe_exp_p58[32] = {0xFD,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x0F};

static void fe_pow(fe out, const fe base, const uint8_t e[32]) {
  fe r; fe_one(r);
  int i = 255;
  while (i >= 0 && !((e[i >> 3] >> (i & 7)) & 1)) i--;
  for (; i >= 0; i--) {
    fe_mul(r, r, r);
    if ((e[i >> 3] >> (i & 7)) & 1) fe_mul(r, r, base);
  }
  fe_copy(out, r);
}
static void fe_invert(fe out, const fe a) { fe_pow(out, a, fe_exp_p2); }


typedef struct { fe X, Y, Z, T; } ge;
static fe fe_d, fe_d2, fe_sqrtm1, fe_a24;
static ge fe_base;
static int crv_ready = 0;


static void x25519_core(uint8_t out[32], const uint8_t k[32], const uint8_t u[32]) {
  uint8_t kk[32], uu[32];
  memcpy(kk, k, 32); memcpy(uu, u, 32);
  kk[0] &= 248; kk[31] &= 127; kk[31] |= 64;
  uu[31] &= 127;
  fe x1, x2, z2, x3, z3, a, aa, b, bb, e, c, d, da, cb, t0, t1;
  fe_frombytes(x1, uu);
  fe_one(x2); fe_zero(z2); fe_copy(x3, x1); fe_one(z3);
  int swap = 0;
  for (int t = 254; t >= 0; t--) {
    int kt = (kk[t >> 3] >> (t & 7)) & 1;
    swap ^= kt;
    fe_cswap(x2, x3, swap); fe_cswap(z2, z3, swap);
    swap = kt;
    fe_add(a, x2, z2);  fe_sqr(aa, a);
    fe_sub(b, x2, z2);  fe_sqr(bb, b);
    fe_sub(e, aa, bb);
    fe_add(c, x3, z3);  fe_sub(d, x3, z3);
    fe_mul(da, d, a);   fe_mul(cb, c, b);
    fe_add(t0, da, cb); fe_sqr(x3, t0);
    fe_sub(t1, da, cb); fe_sqr(t1, t1); fe_mul(z3, x1, t1);
    fe_mul(x2, aa, bb);
    fe_mul(t0, fe_a24, e); fe_add(t1, aa, t0); fe_mul(z2, e, t1);
  }
  fe_cswap(x2, x3, swap); fe_cswap(z2, z3, swap);
  fe_invert(z2, z2);
  fe_mul(x2, x2, z2);
  fe_tobytes(out, x2);
}


static void ge_identity(ge *r) { fe_zero(r->X); fe_one(r->Y); fe_one(r->Z); fe_zero(r->T); }
static void ge_copy(ge *d, const ge *s) { *d = *s; }


static void ge_add(ge *r, const ge *p, const ge *q) {
  fe A, B, C, D, E, F, G, H, t0, t1;
  fe_sub(t0, p->Y, p->X); fe_sub(t1, q->Y, q->X); fe_mul(A, t0, t1);
  fe_add(t0, p->Y, p->X); fe_add(t1, q->Y, q->X); fe_mul(B, t0, t1);
  fe_mul(C, p->T, q->T); fe_mul(C, C, fe_d2);
  fe_mul(D, p->Z, q->Z); fe_add(D, D, D);
  fe_sub(E, B, A); fe_sub(F, D, C); fe_add(G, D, C); fe_add(H, B, A);
  fe_mul(r->X, E, F); fe_mul(r->Y, G, H); fe_mul(r->T, E, H); fe_mul(r->Z, F, G);
}

static void ge_mul(ge *r, const uint8_t s[32], const ge *p) {
  ge acc, q; ge_identity(&acc); ge_copy(&q, p);
  int top = 255;
  while (top >= 0 && !((s[top >> 3] >> (top & 7)) & 1)) top--;
  for (int i = 0; i <= top; i++) {
    if ((s[i >> 3] >> (i & 7)) & 1) ge_add(&acc, &acc, &q);
    if (i < top) ge_add(&q, &q, &q);
  }
  ge_copy(r, &acc);
}

static void ge_compress(uint8_t out[32], const ge *p) {
  fe zi, x, y;
  fe_invert(zi, p->Z);
  fe_mul(y, p->Y, zi);
  fe_mul(x, p->X, zi);
  fe_tobytes(out, y);
  out[31] |= (uint8_t)(fe_parity(x) << 7);
}

static bool ge_decompress(ge *r, const uint8_t in[32]) {
  fe y, y2, u, v, v2, v3, v4, v7, uv7, x, t, d1, d2, one;
  int sign = in[31] >> 7;
  fe_frombytes(y, in);
  y[15] &= 0x7FFF;
  if (!fe_canon(y)) return false;
  fe_one(one);
  fe_sqr(y2, y);
  fe_sub(u, y2, one);
  fe_mul(v, y2, fe_d); fe_add(v, v, one);
  if (fe_iszero(u)) {
    if (sign) return false;
    fe_zero(r->X); fe_copy(r->Y, y); fe_one(r->Z); fe_zero(r->T);
    return true;
  }
  fe_sqr(v2, v);
  fe_mul(v3, v2, v);
  fe_mul(v4, v2, v2);
  fe_mul(v7, v4, v3);
  fe_mul(uv7, u, v7);
  fe_pow(x, uv7, fe_exp_p58);
  fe_mul(x, x, u);
  fe_mul(x, x, v3);
  fe_sqr(t, x); fe_mul(t, t, v);
  fe_sub(d1, t, u);
  if (!fe_iszero(d1)) {
    fe_add(d2, t, u);
    if (!fe_iszero(d2)) return false;
    fe_mul(x, x, fe_sqrtm1);
  }
  if (fe_parity(x) != sign) fe_neg(x, x);
  fe_mul(r->T, x, y);
  fe_copy(r->X, x); fe_copy(r->Y, y); fe_one(r->Z);
  return true;
}


static const uint8_t sc_L[32] = {0xed,0xd3,0xf5,0x5c,0x1a,0x63,0x12,0x58,0xd6,0x9c,0xf7,0xa2,0xde,0xf9,0xde,0x14,
                                 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x10};
static void sc_reduce(uint8_t out[32], const uint8_t in[64]) {
  uint8_t r[32], t[32];
  memset(r, 0, 32);
  for (int i = 511; i >= 0; i--) {
    int carry = (in[i >> 3] >> (i & 7)) & 1;
    for (int j = 0; j < 32; j++) { int v = r[j] * 2 + carry; r[j] = (uint8_t)(v & 0xFF); carry = v >> 8; }

    int br = 0;
    for (int j = 0; j < 32; j++) { int v = r[j] - sc_L[j] - br; if (v < 0) { v += 256; br = 1; } else br = 0; t[j] = (uint8_t)v; }
    if (!br) memcpy(r, t, 32);
  }
  memcpy(out, r, 32);
}

static void sc_muladd(uint8_t out[32], const uint8_t a[32], const uint8_t b[32], const uint8_t c[32]) {
  int64_t t[64];
  memset(t, 0, sizeof(t));
  for (int i = 0; i < 32; i++) {
    int64_t ai = a[i];
    if (!ai) continue;
    for (int j = 0; j < 32; j++) t[i + j] += ai * b[j];
  }
  for (int j = 0; j < 32; j++) t[j] += c[j];
  for (int k = 0; k < 63; k++) { t[k + 1] += t[k] >> 8; t[k] &= 0xFF; }
  uint8_t pl[64];
  for (int k = 0; k < 64; k++) pl[k] = (uint8_t)t[k];
  sc_reduce(out, pl);
}
static bool sc_lt_L(const uint8_t s[32]) {
  for (int i = 31; i >= 0; i--) if (s[i] != sc_L[i]) return s[i] < sc_L[i];
  return false;
}



static void ed_expand(const uint8_t seed[32], uint8_t a[32], uint8_t prefix[32]) {
  uint8_t h[64];
  sha512_raw(seed, 32, h);
  memcpy(a, h, 32);
  a[0] &= 248; a[31] &= 127; a[31] |= 64;
  memcpy(prefix, h + 32, 32);
}
static void ed_pubkey(uint8_t pk[32], const uint8_t a[32]) {
  ge A; ge_mul(&A, a, &fe_base); ge_compress(pk, &A);
}
static void ed_sign(uint8_t sig[64], const uint8_t seed[32], const uint8_t *msg, size_t mlen) {
  uint8_t a[32], prefix[32], pk[32], rh[64], kh[64], r[32], k[32];
  ed_expand(seed, a, prefix);
  ed_pubkey(pk, a);
  sha512_cat(rh, prefix, 32, msg, mlen, NULL, 0);
  sc_reduce(r, rh);
  ge R; ge_mul(&R, r, &fe_base);
  ge_compress(sig, &R);
  sha512_cat(kh, sig, 32, pk, 32, msg, mlen);
  sc_reduce(k, kh);
  sc_muladd(sig + 32, k, a, r);
}

static bool ed_verify(const uint8_t pk[32], const uint8_t *msg, size_t mlen, const uint8_t sig[64]) {
  if (!sc_lt_L(sig + 32)) return false;
  ge A, R;
  if (!ge_decompress(&A, pk)) return false;
  if (!ge_decompress(&R, sig)) return false;
  uint8_t kh[64], k[32];
  sha512_cat(kh, sig, 32, pk, 32, msg, mlen);
  sc_reduce(k, kh);
  ge SB, kA;
  ge_mul(&SB, sig + 32, &fe_base);
  ge_mul(&kA, k, &A);
  ge_add(&kA, &R, &kA);
  uint8_t e1[32], e2[32];
  ge_compress(e1, &SB); ge_compress(e2, &kA);
  return memcmp(e1, e2, 32) == 0;
}



static void crv_init(void) {
  if (crv_ready) return;
  crv_ready = 1;
  fe one, two, four, five, c1, inv;
  fe_one(one);
  fe_add(two, one, one);
  fe_add(four, two, two);
  fe_add(five, four, one);
  fe_small(c1, 121665);
  fe c2; fe_small(c2, 121666);
  fe_invert(inv, c2);
  fe_mul(fe_d, c1, inv);
  fe_neg(fe_d, fe_d);
  fe_add(fe_d2, fe_d, fe_d);
  fe_small(fe_a24, 121665);
  fe_pow(fe_sqrtm1, two, fe_exp_p14);
  fe_invert(inv, five);
  fe_mul(four, four, inv);
  uint8_t yb[32]; fe_tobytes(yb, four);
  ge_decompress(&fe_base, yb);
}


static bool crv_getN(Value v, uint8_t *out, int n) {
  if (!IS_T(v, O_STR)) return false;
  Str *s = AS_STR(v);
  if (s->len != n * 2) return false;
  for (int i = 0; i < n; i++) {
    int h = hexv(s->chars[2 * i]), l = hexv(s->chars[2 * i + 1]);
    if (h < 0 || l < 0) return false;
    out[i] = (uint8_t)(h * 16 + l);
  }
  return true;
}
NAT(c_sha512) { STRA(0); uint8_t o[64]; sha512_raw((const uint8_t *)AS_STR(a[0])->chars, AS_STR(a[0])->len, o); *res = hex_or_raw(o, 64, argc > 1 && !falsey(a[1])); return true; }
NAT(c_x25519_sec) {
  uint8_t s[32];
  if (argc > 0 && !falsey(a[0])) { if (!crv_getN(a[0], s, 32)) FAIL("مفتاح x25519 غير صالح: hex بطول 64 خانة"); }
  else if (!os_random(s, 32)) FAIL("فشل توليد العشوائية");
  *res = hex_or_raw(s, 32, false); return true;
}
NAT(c_x25519_pub) {
  uint8_t k[32], o[32];
  if (!crv_getN(a[0], k, 32)) FAIL("مفتاح x25519 غير صالح: hex بطول 64 خانة");
  crv_init();
  static const uint8_t nine[32] = {9};
  x25519_core(o, k, nine);
  *res = hex_or_raw(o, 32, false); return true;
}
NAT(c_x25519_shared) {
  uint8_t k[32], u[32], o[32];
  if (!crv_getN(a[0], k, 32)) FAIL("مفتاح x25519 غير صالح: hex بطول 64 خانة");
  if (!crv_getN(a[1], u, 32)) FAIL("مفتاح عام x25519 غير صالح: hex بطول 64 خانة");
  crv_init();
  x25519_core(o, k, u);
  *res = hex_or_raw(o, 32, false); return true;
}
NAT(c_ed25519_sec) {
  uint8_t s[32];
  if (argc > 0 && !falsey(a[0])) { if (!crv_getN(a[0], s, 32)) FAIL("مفتاح ed25519 غير صالح: hex بطول 64 خانة"); }
  else if (!os_random(s, 32)) FAIL("فشل توليد العشوائية");
  *res = hex_or_raw(s, 32, false); return true;
}
NAT(c_ed25519_pub) {
  uint8_t seed[32], sc[32], prefix[32], pk[32];
  if (!crv_getN(a[0], seed, 32)) FAIL("مفتاح ed25519 غير صالح: hex بطول 64 خانة");
  crv_init();
  ed_expand(seed, sc, prefix);
  ed_pubkey(pk, sc);
  *res = hex_or_raw(pk, 32, false); return true;
}
NAT(c_ed25519_sign) {
  uint8_t seed[32], sig[64];
  STRA(1);
  if (!crv_getN(a[0], seed, 32)) FAIL("مفتاح ed25519 غير صالح: hex بطول 64 خانة");
  crv_init();
  Str *m = AS_STR(a[1]);
  ed_sign(sig, seed, (const uint8_t *)m->chars, (size_t)m->len);
  *res = hex_or_raw(sig, 64, false); return true;
}
NAT(c_ed25519_verify) {
  uint8_t pk[32], sig[64];
  STRA(1);
  if (!crv_getN(a[0], pk, 32)) FAIL("مفتاح عام ed25519 غير صالح: hex بطول 64 خانة");
  if (!crv_getN(a[2], sig, 64)) FAIL("توقيع غير صالح: hex بطول 128 خانة");
  crv_init();
  Str *m = AS_STR(a[1]);
  *res = BOOLV(ed_verify(pk, (const uint8_t *)m->chars, (size_t)m->len, sig));
  return true;
}
static void hmac_sha512(const uint8_t *key, size_t kl, const uint8_t *d, size_t n, uint8_t out[64]) {
  uint8_t k[128] = {0}, ip[128], op[128];
  if (kl > 128) sha512_raw(key, kl, k); else memcpy(k, key, kl);
  for (int i = 0; i < 128; i++) { ip[i] = (uint8_t)(k[i] ^ 0x36); op[i] = (uint8_t)(k[i] ^ 0x5c); }
  uint8_t *t = (uint8_t *)xalloc(128 + n); memcpy(t, ip, 128); memcpy(t + 128, d, n);
  uint8_t inner[64]; sha512_raw(t, 128 + n, inner); free(t);
  uint8_t t2[192]; memcpy(t2, op, 128); memcpy(t2 + 128, inner, 64); sha512_raw(t2, 192, out);
}
NAT(c_hmac_sha512) {
  STRA(0); STRA(1);
  Str *ks = AS_STR(a[0]), *ms = AS_STR(a[1]);
  if ((ks->len | ms->len) & 1) FAIL("مدخلات hmac_sha512 يجب أن تكون هكس صالحة (طول زوجي)");
  int kn = ks->len / 2, mn = ms->len / 2;
  uint8_t *k = (uint8_t *)xalloc(kn + 1), *m = (uint8_t *)xalloc(mn + 1);
  bool ok = true;
  for (int i = 0; i < kn && ok; i++) { int h = hexv(ks->chars[2 * i]), l2 = hexv(ks->chars[2 * i + 1]); if (h < 0 || l2 < 0) ok = false; else k[i] = (uint8_t)(h * 16 + l2); }
  for (int i = 0; i < mn && ok; i++) { int h = hexv(ms->chars[2 * i]), l2 = hexv(ms->chars[2 * i + 1]); if (h < 0 || l2 < 0) ok = false; else m[i] = (uint8_t)(h * 16 + l2); }
  if (!ok) { free(k); free(m); FAIL("مدخلات hmac_sha512 ليست هكس صالحة"); }
  uint8_t o[64];
  hmac_sha512(k, kn, m, mn, o);
  free(k); free(m);
  *res = hex_or_raw(o, 64, argc > 2 && !falsey(a[2]));
  return true;
}
typedef void (*hmac_fn)(const uint8_t *, size_t, const uint8_t *, size_t, uint8_t *);
static void hkdf_expand(hmac_fn hmac, size_t olen, const uint8_t *prk, size_t prklen, const uint8_t *info, size_t ilen, uint8_t *okm, int l) {
  uint8_t t[64]; int tlen = 0, done = 0; uint8_t counter = 1;
  while (done < l) {
    uint8_t *b = (uint8_t *)xalloc(tlen + ilen + 1);
    memcpy(b, t, tlen); memcpy(b + tlen, info, ilen); b[tlen + ilen] = counter;
    hmac(prk, prklen, b, tlen + ilen + 1, t);
    free(b);
    int take = (int)olen < l - done ? (int)olen : l - done;
    memcpy(okm + done, t, take);
    done += take; tlen = (int)olen; counter++;
  }
}
static bool hkdf_common(Value *a, int argc, Value *res, bool sha512) {
  STRA(0); STRA(1); STRA(2); NUMA(3);
  int l = (int)a[3].as.n;
  int maxl = sha512 ? 16320 : 8160;
  if (l < 1 || l > maxl) { char m[120]; snprintf(m, sizeof m, "الطول خارج النطاق (1-%d)", maxl); FAIL("%s", m); }
  Str *is = AS_STR(a[0]), *ss = AS_STR(a[1]), *ifs = AS_STR(a[2]);
  if ((is->len | ss->len | ifs->len) & 1) FAIL("مدخلات hkdf يجب أن تكون هكس صالحة (طول زوجي)");
  int in = is->len / 2, sn = ss->len / 2, fn = ifs->len / 2;
  uint8_t *ikm = (uint8_t *)xalloc(in + 1), *salt = (uint8_t *)xalloc(sn + 1), *info = (uint8_t *)xalloc(fn + 1);
  bool ok = true;
  for (int i = 0; i < in && ok; i++) { int h = hexv(is->chars[2 * i]), l2 = hexv(is->chars[2 * i + 1]); if (h < 0 || l2 < 0) ok = false; else ikm[i] = (uint8_t)(h * 16 + l2); }
  for (int i = 0; i < sn && ok; i++) { int h = hexv(ss->chars[2 * i]), l2 = hexv(ss->chars[2 * i + 1]); if (h < 0 || l2 < 0) ok = false; else salt[i] = (uint8_t)(h * 16 + l2); }
  for (int i = 0; i < fn && ok; i++) { int h = hexv(ifs->chars[2 * i]), l2 = hexv(ifs->chars[2 * i + 1]); if (h < 0 || l2 < 0) ok = false; else info[i] = (uint8_t)(h * 16 + l2); }
  if (!ok) { free(ikm); free(salt); free(info); FAIL("مدخلات hkdf ليست هكس صالحة"); }
  uint8_t prk[64];
  uint8_t *okm = (uint8_t *)xalloc(l);
  if (sha512) { hmac_sha512(salt, sn, ikm, in, prk); hkdf_expand(hmac_sha512, 64, prk, 64, info, fn, okm, l); }
  else { hmac_sha256(salt, sn, ikm, in, prk); hkdf_expand(hmac_sha256, 32, prk, 32, info, fn, okm, l); }
  *res = hex_or_raw(okm, l, false);
  free(ikm); free(salt); free(info); free(okm);
  return true;
}
NAT(c_hkdf_sha256) { return hkdf_common(a, argc, res, false); }
NAT(c_hkdf_sha512) { return hkdf_common(a, argc, res, true); }
