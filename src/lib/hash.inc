static const uint32_t K256[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
static void sha256_blk(uint32_t *h, const uint8_t *p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K256[i] + w[i];
    uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22), mj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + mj;
    hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
static void md_run(const uint8_t *d, size_t n, void (*blk)(uint32_t *, const uint8_t *), uint32_t *h) {
  size_t i = 0; for (; i + 64 <= n; i += 64) blk(h, d + i);
  uint8_t t[128]; size_t r = n - i; memcpy(t, d + i, r); t[r++] = 0x80;
  size_t tot = (r + 8 <= 64) ? 64 : 128; memset(t + r, 0, tot - r);
  uint64_t bits = (uint64_t)n * 8; for (int k = 0; k < 8; k++) t[tot - 1 - k] = (bits >> (8 * k)) & 255;
  blk(h, t); if (tot == 128) blk(h, t + 64);
}
static void sha1_raw(const uint8_t *d, size_t n, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}; md_run(d, n, sha1_blk, h);
  for (int i = 0; i < 5; i++) { out[4 * i] = h[i] >> 24; out[4 * i + 1] = h[i] >> 16; out[4 * i + 2] = h[i] >> 8; out[4 * i + 3] = h[i]; }
}
static void sha256_raw(const uint8_t *d, size_t n, uint8_t out[32]) {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}; md_run(d, n, sha256_blk, h);
  for (int i = 0; i < 8; i++) { out[4 * i] = h[i] >> 24; out[4 * i + 1] = h[i] >> 16; out[4 * i + 2] = h[i] >> 8; out[4 * i + 3] = h[i]; }
}
static void hmac_sha256(const uint8_t *key, size_t kl, const uint8_t *d, size_t n, uint8_t out[32]) {
  uint8_t k[64] = {0}, ip[64], op[64];
  if (kl > 64) sha256_raw(key, kl, k); else memcpy(k, key, kl);
  for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
  uint8_t *t = xalloc(64 + n); memcpy(t, ip, 64); memcpy(t + 64, d, n); uint8_t inner[32]; sha256_raw(t, 64 + n, inner); free(t);
  uint8_t t2[96]; memcpy(t2, op, 64); memcpy(t2 + 64, inner, 32); sha256_raw(t2, 96, out);
}
static void hmac_sha1(const uint8_t *key, size_t kl, const uint8_t *d, size_t n, uint8_t out[20]) {
  uint8_t k[64] = {0}, ip[64], op[64];
  if (kl > 64) sha1_raw(key, kl, k); else memcpy(k, key, kl);
  for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
  uint8_t *t = xalloc(64 + n); memcpy(t, ip, 64); memcpy(t + 64, d, n); uint8_t inner[20]; sha1_raw(t, 64 + n, inner); free(t);
  uint8_t t2[84]; memcpy(t2, op, 64); memcpy(t2 + 64, inner, 20); sha1_raw(t2, 84, out);
}
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void b64_enc_ex(const uint8_t *d, size_t n, Buf *o, bool url) {
  const char *A = url ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_" : B64;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    buf_addc(o, A[(v >> 18) & 63]); buf_addc(o, A[(v >> 12) & 63]);
    if (url) {
      if (i + 1 < n) buf_addc(o, A[(v >> 6) & 63]);
      if (i + 2 < n) buf_addc(o, A[v & 63]);
    } else {
      buf_addc(o, i + 1 < n ? A[(v >> 6) & 63] : '='); buf_addc(o, i + 2 < n ? A[v & 63] : '=');
    }
  }
}
static void b64_enc(const uint8_t *d, size_t n, Buf *o) { b64_enc_ex(d, n, o, false); }
static bool b64_dec(const char *s, int n, Buf *o) {
  uint32_t v = 0; int bits = 0;
  for (int i = 0; i < n; i++) {
    char c = s[i]; int x;
    if (c >= 'A' && c <= 'Z') x = c - 'A'; else if (c >= 'a' && c <= 'z') x = c - 'a' + 26; else if (c >= '0' && c <= '9') x = c - '0' + 52;
    else if (c == '+' || c == '-') x = 62; else if (c == '/' || c == '_') x = 63; else if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue; else return false;
    v = (v << 6) | x; bits += 6; if (bits >= 8) { bits -= 8; buf_addc(o, (char)((v >> bits) & 255)); }
  }
  return true;
}
static Value hex_or_raw(const uint8_t *d, int n, bool raw) {
  if (raw) return mkstr((const char *)d, n);
  static const char HEXC[16] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
  char *h = xalloc(n * 2 + 1);
  for (int i = 0; i < n; i++) { h[2 * i] = HEXC[d[i] >> 4]; h[2 * i + 1] = HEXC[d[i] & 15]; }
  Value v = mkstr(h, n * 2); free(h); return v;
}
NAT(c_sha1) { STRA(0); uint8_t o[20]; sha1_raw((const uint8_t *)AS_STR(a[0])->chars, AS_STR(a[0])->len, o); *res = hex_or_raw(o, 20, argc > 1 && !falsey(a[1])); return true; }
NAT(c_sha256) { STRA(0); uint8_t o[32]; sha256_raw((const uint8_t *)AS_STR(a[0])->chars, AS_STR(a[0])->len, o); *res = hex_or_raw(o, 32, argc > 1 && !falsey(a[1])); return true; }
NAT(c_hmac) {
  STRA(0); STRA(1); uint8_t o[32];
  hmac_sha256((const uint8_t *)AS_STR(a[0])->chars, AS_STR(a[0])->len, (const uint8_t *)AS_STR(a[1])->chars, AS_STR(a[1])->len, o);
  *res = hex_or_raw(o, 32, argc > 2 && !falsey(a[2])); return true;
}
NAT(c_hmac_sha1) {
  STRA(0); STRA(1); uint8_t o[20];
  hmac_sha1((const uint8_t *)AS_STR(a[0])->chars, AS_STR(a[0])->len, (const uint8_t *)AS_STR(a[1])->chars, AS_STR(a[1])->len, o);
  *res = hex_or_raw(o, 20, argc > 2 && !falsey(a[2])); return true;
}
NAT(c_b64) { STRA(0); Buf b; buf_init(&b); b64_enc((const uint8_t *)AS_STR(a[0])->chars, AS_STR(a[0])->len, &b); *res = mkstr(buf_cstr(&b), b.n); free(b.p); return true; }
NAT(c_b64url) { STRA(0); Buf b; buf_init(&b); b64_enc_ex((const uint8_t *)AS_STR(a[0])->chars, AS_STR(a[0])->len, &b, true); *res = mkstr(buf_cstr(&b), b.n); free(b.p); return true; }
NAT(c_unb64) { STRA(0); Buf b; buf_init(&b); if (!b64_dec(AS_STR(a[0])->chars, AS_STR(a[0])->len, &b)) { free(b.p); FAIL("base64 غير صالح"); } *res = mkstr(buf_cstr(&b), b.n); free(b.p); return true; }
NAT(c_hex) { STRA(0); *res = hex_or_raw((const uint8_t *)AS_STR(a[0])->chars, AS_STR(a[0])->len, false); return true; }
NAT(c_unhex) {
  STRA(0); Str *s = AS_STR(a[0]); if (s->len % 2) FAIL("hex غير صالح");
  Buf b; buf_init(&b);
  for (int i = 0; i < s->len; i += 2) { int h = hexv(s->chars[i]), l = hexv(s->chars[i + 1]); if (h < 0 || l < 0) { free(b.p); FAIL("hex غير صالح"); } buf_addc(&b, (char)(h * 16 + l)); }
  *res = mkstr(buf_cstr(&b), b.n); free(b.p); return true;
}
NAT(c_rand) { NUMA(0); int n = (int)a[0].as.n; if (n < 0 || n > 1 << 20) FAIL("حجم غير صالح"); char *t = xalloc(n + 1); if (!os_random(t, (size_t)n)) for (int i = 0; i < n; i++) t[i] = (char)(rng_next() & 255); *res = mkstr(t, n); free(t); return true; }
NAT(c_crc32) {
  STRA(0); Str *s = AS_STR(a[0]);
  uint32_t c = 0xFFFFFFFFu;
  static uint32_t tbl[256]; static bool init = false;
  if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t k = i; for (int b = 0; b < 8; b++) k = (k & 1) ? 0xEDB88320u ^ (k >> 1) : k >> 1; tbl[i] = k; } init = true; }
  for (int i = 0; i < s->len; i++) c = tbl[(c ^ (unsigned char)s->chars[i]) & 0xFF] ^ (c >> 8);
  c = ~c;
  char h[9]; snprintf(h, sizeof h, "%08x", c); *res = mkstr(h, 8); return true;
}
NAT(c_pbkdf2_sha256) {
  STRA(0); STRA(1); NUMA(2); STRA(3);
  Str *pw = AS_STR(a[0]), *salt = AS_STR(a[1]);
  long iters = (long)a[2].as.n; long dklen = (long)(argc > 3 ? a[3].as.n : 32);
  if (iters < 1 || iters > 10000000) FAIL("عدد التكرارات خارج النطاق (1 - 10000000)");
  if (dklen < 1 || dklen > 4096) FAIL("طول المفتاح خارج النطاق (1 - 4096)");
  uint8_t *out = (uint8_t *)xalloc((size_t)dklen);
  uint32_t blocks = (uint32_t)((dklen + 31) / 32);
  for (uint32_t blk = 1; blk <= blocks; blk++) {
    uint8_t ib[4] = { (uint8_t)(blk >> 24), (uint8_t)(blk >> 16), (uint8_t)(blk >> 8), (uint8_t)blk };
    uint8_t *first = (uint8_t *)xalloc(salt->len + 4);
    memcpy(first, salt->chars, (size_t)salt->len); memcpy(first + salt->len, ib, 4);
    uint8_t u[32], acc[32];
    {
      uint8_t k[64] = {0}, ip[64], op[64];
      size_t kl = (size_t)pw->len;
      if (kl > 64) { uint8_t kh[32]; sha256_raw((const uint8_t *)pw->chars, kl, kh); memcpy(k, kh, 32); } else memcpy(k, pw->chars, kl);
      for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
      size_t n1 = 64 + salt->len + 4;
      uint8_t *t1 = (uint8_t *)xalloc(n1);
      memcpy(t1, ip, 64); memcpy(t1 + 64, first, salt->len + 4);
      sha256_raw(t1, n1, u); free(t1);
      size_t n2 = 64 + 32;
      uint8_t *t2 = (uint8_t *)xalloc(n2);
      memcpy(t2, op, 64); memcpy(t2 + 64, u, 32);
      sha256_raw(t2, n2, acc); free(t2);
    }
    for (long it = 1; it < iters; it++) {
      uint8_t k[64] = {0}, ip[64], op[64];
      size_t kl = (size_t)pw->len;
      if (kl > 64) { uint8_t kh[32]; sha256_raw((const uint8_t *)pw->chars, kl, kh); memcpy(k, kh, 32); } else memcpy(k, pw->chars, kl);
      for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
      uint8_t *t1 = (uint8_t *)xalloc(96); memcpy(t1, ip, 64); memcpy(t1 + 64, u, 32);
      sha256_raw(t1, 96, u); free(t1);
      uint8_t *t2 = (uint8_t *)xalloc(96); memcpy(t2, op, 64); memcpy(t2 + 64, u, 32);
      sha256_raw(t2, 96, u); free(t2);
      for (int i = 0; i < 32; i++) acc[i] ^= u[i];
    }
    free(first);
    int take = dklen - (int)((blk - 1) * 32) < 32 ? dklen - (int)((blk - 1) * 32) : 32;
    memcpy(out + (blk - 1) * 32, acc, (size_t)take);
  }
  *res = hex_or_raw(out, (int)dklen, false);
  free(out); return true;
}
