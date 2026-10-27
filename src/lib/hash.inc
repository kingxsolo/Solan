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
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void b64_enc(const uint8_t *d, size_t n, Buf *o) {
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    buf_addc(o, B64[(v >> 18) & 63]); buf_addc(o, B64[(v >> 12) & 63]);
    buf_addc(o, i + 1 < n ? B64[(v >> 6) & 63] : '='); buf_addc(o, i + 2 < n ? B64[v & 63] : '=');
  }
}
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
  char *h = xalloc(n * 2 + 1); for (int i = 0; i < n; i++) sprintf(h + 2 * i, "%02x", d[i]);
  Value v = mkstr(h, n * 2); free(h); return v;
}
NAT(c_sha1) { STRA(0); uint8_t o[20]; sha1_raw((const uint8_t *)AS_STR(a[0])->chars, AS_STR(a[0])->len, o); *res = hex_or_raw(o, 20, argc > 1 && !falsey(a[1])); return true; }
NAT(c_sha256) { STRA(0); uint8_t o[32]; sha256_raw((const uint8_t *)AS_STR(a[0])->chars, AS_STR(a[0])->len, o); *res = hex_or_raw(o, 32, argc > 1 && !falsey(a[1])); return true; }
NAT(c_hmac) {
  STRA(0); STRA(1); uint8_t o[32];
  hmac_sha256((const uint8_t *)AS_STR(a[0])->chars, AS_STR(a[0])->len, (const uint8_t *)AS_STR(a[1])->chars, AS_STR(a[1])->len, o);
  *res = hex_or_raw(o, 32, argc > 2 && !falsey(a[2])); return true;
}
NAT(c_b64) { STRA(0); Buf b; buf_init(&b); b64_enc((const uint8_t *)AS_STR(a[0])->chars, AS_STR(a[0])->len, &b); *res = mkstr(buf_cstr(&b), b.n); free(b.p); return true; }
NAT(c_unb64) { STRA(0); Buf b; buf_init(&b); if (!b64_dec(AS_STR(a[0])->chars, AS_STR(a[0])->len, &b)) { free(b.p); FAIL("base64 غير صالح"); } *res = mkstr(buf_cstr(&b), b.n); free(b.p); return true; }
NAT(c_hex) { STRA(0); *res = hex_or_raw((const uint8_t *)AS_STR(a[0])->chars, AS_STR(a[0])->len, false); return true; }
NAT(c_unhex) {
  STRA(0); Str *s = AS_STR(a[0]); if (s->len % 2) FAIL("hex غير صالح");
  Buf b; buf_init(&b);
  for (int i = 0; i < s->len; i += 2) { int h = hexv(s->chars[i]), l = hexv(s->chars[i + 1]); if (h < 0 || l < 0) { free(b.p); FAIL("hex غير صالح"); } buf_addc(&b, (char)(h * 16 + l)); }
  *res = mkstr(buf_cstr(&b), b.n); free(b.p); return true;
}
NAT(c_rand) { NUMA(0); int n = (int)a[0].as.n; if (n < 0 || n > 1 << 20) FAIL("حجم غير صالح"); char *t = xalloc(n + 1); if (!os_random(t, (size_t)n)) for (int i = 0; i < n; i++) t[i] = (char)(rng_next() & 255); *res = mkstr(t, n); free(t); return true; }
