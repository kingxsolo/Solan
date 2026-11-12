
#define T13_AES 0x1301
#define T13_CHACHA 0x1303

typedef struct {
  int fd, suite, keylen, write_ap, read_ap, hs_done, closed;
  int is12, s12_state;
  uint8_t ch_rand[32], sid[32];
  uint8_t x_priv[32], x_pub[32], p_priv[32], p_pub[65];
  uint8_t c_hs_sec[32], s_hs_sec[32], mas_sec[32], c_ap_sec[32], s_ap_sec[32];
  uint8_t wkey[32], wiv[16], rkey[32], riv[16];
  uint8_t wmac[32], rmac[32];
  AesCtx waes, raes;
  int winit, rinit;
  uint64_t wseq, rseq;
  Buf tr, raw, dec, hsb;
  uint8_t master12[48], crand12[32], srand12[32];
  int ems12, kx12, sigalg12[2], ecdhe_gid, ecdh12_len;
  uint8_t ecdh12[48], ecdh_pub[65], g_priv[48], ecdh384_pub[97], g384_priv[48];
  int last_alert;
} T13;

static void t13_hkdf_extract(uint8_t out[32], const uint8_t *salt, int sl, const uint8_t *ikm, int il) {
  hmac_sha256(salt, (size_t)sl, ikm, (size_t)il, out);
}
static void t13_hkdf_expand(const uint8_t prk[32], const uint8_t *info, int il, uint8_t *out, int olen) {
  uint8_t t[32], buf[192];
  int tn = 0, off = 0;
  uint8_t ctr = 1;
  while (off < olen) {
    int p = 0;
    if (tn) { memcpy(buf, t, 32); p = 32; }
    if (il > 0) { memcpy(buf + p, info, (size_t)il); p += il; }
    buf[p++] = ctr;
    hmac_sha256(prk, 32, buf, p, t);
    int take = olen - off < 32 ? olen - off : 32;
    memcpy(out + off, t, (size_t)take);
    off += take;
    tn = 32;
    ctr++;
  }
}
static void t13_elabel(const uint8_t sec[32], const char *label, const uint8_t *ctx, int cl, uint8_t *out, int olen) {
  int ln = (int)strlen(label);
  uint8_t info[160];
  int p = 0;
  info[p++] = (uint8_t)(olen >> 8);
  info[p++] = (uint8_t)olen;
  info[p++] = (uint8_t)(ln + 6);
  memcpy(info + p, "tls13 ", 6); p += 6;
  memcpy(info + p, label, (size_t)ln); p += ln;
  info[p++] = (uint8_t)cl;
  if (cl && ctx) { memcpy(info + p, ctx, (size_t)cl); p += cl; }
  t13_hkdf_expand(sec, info, p, out, olen);
}
static void t13_dsecret(const uint8_t sec[32], const char *label, const uint8_t th[32], uint8_t out[32]) {
  t13_elabel(sec, label, th, 32, out, 32);
}
static void t13_keys(const uint8_t sec[32], int keylen, uint8_t *key, uint8_t iv[12]) {
  t13_elabel(sec, "key", NULL, 0, key, keylen);
  t13_elabel(sec, "iv", NULL, 0, iv, 12);
}
static void t13_apply(T13 *t, const uint8_t sec[32], int write) {
  uint8_t *key = write ? t->wkey : t->rkey;
  uint8_t *iv = write ? t->wiv : t->riv;
  t13_keys(sec, t->keylen, key, iv);
  if (t->suite == T13_AES) {
    if (write) aes_key_setup(&t->waes, key, 128);
    else aes_key_setup(&t->raes, key, 128);
  }
  if (write) { t->winit = 1; t->wseq = 0; t->write_ap = sec == t->c_ap_sec; }
  else { t->rinit = 1; t->rseq = 0; t->read_ap = sec == t->s_ap_sec; }
}
static void t13_nonce(const uint8_t iv[12], uint64_t seq, uint8_t out[12]) {
  memcpy(out, iv, 12);
  for (int i = 0; i < 8; i++) out[12 - 8 + i] ^= (uint8_t)(seq >> (8 * (7 - i)));
}
static int t13_cteq(const uint8_t *a, const uint8_t *b, int n) {
  uint8_t d = 0;
  for (int i = 0; i < n; i++) d |= (uint8_t)(a[i] ^ b[i]);
  return d;
}
static void t13_chacha_dec(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, int alen, const uint8_t *ct, int n, uint8_t *out, uint8_t tag[16]) {
  uint8_t otk[64];
  chacha_block(key, nonce, 0, otk);
  chacha_stream(key, nonce, 1, ct, n, out);
  int pl = (16 - (alen % 16)) % 16, cl = (16 - (n % 16)) % 16;
  uint8_t *m = (uint8_t *)xalloc((size_t)(alen + pl + n + cl + 16));
  int o = 0;
  if (alen) { memcpy(m, aad, (size_t)alen); o += alen; }
  memset(m + o, 0, (size_t)pl); o += pl;
  memcpy(m + o, ct, (size_t)n); o += n;
  memset(m + o, 0, (size_t)cl); o += cl;
  uint64_t av = (uint64_t)alen, cv = (uint64_t)n;
  for (int k = 0; k < 8; k++) m[o + k] = (uint8_t)(av >> (8 * k));
  for (int k = 0; k < 8; k++) m[o + 8 + k] = (uint8_t)(cv >> (8 * k));
  poly_mac(otk, m, o + 16, tag);
  free(m);
}
static void t12_hmac_raw(const uint8_t *key, int kl, const uint8_t *d, size_t dn, uint8_t out[32]) {
  uint8_t ip[64], op[64], b[96];
  memset(ip, 0x36, 64); memset(op, 0x5c, 64);
  for (int i = 0; i < kl && i < 64; i++) { ip[i] ^= key[i]; op[i] ^= key[i]; }
  Buf m;
  buf_init(&m);
  buf_add(&m, (const char *)ip, 64);
  if (d && dn) buf_add(&m, (const char *)d, (int)dn);
  uint8_t ih[32];
  sha256_raw((const uint8_t *)m.p, (size_t)m.n, ih);
  free(m.p);
  memcpy(b, op, 64);
  memcpy(b + 64, ih, 32);
  sha256_raw(b, 96, out);
}
static void t12_mac(T13 *t, int write, uint64_t seq, unsigned char type, const char *d, int n, uint8_t out[32]) {
  uint8_t hdr[13];
  hdr[0] = (uint8_t)(seq >> 56); hdr[1] = (uint8_t)(seq >> 48);
  hdr[2] = (uint8_t)(seq >> 40); hdr[3] = (uint8_t)(seq >> 32);
  hdr[4] = (uint8_t)(seq >> 24); hdr[5] = (uint8_t)(seq >> 16);
  hdr[6] = (uint8_t)(seq >> 8); hdr[7] = (uint8_t)seq;
  hdr[8] = type; hdr[9] = 3; hdr[10] = 3;
  hdr[11] = (uint8_t)(n >> 8); hdr[12] = (uint8_t)n;
  Buf m;
  buf_init(&m);
  buf_add(&m, (const char *)hdr, 13);
  buf_add(&m, d, n);
  t12_hmac_raw(write ? t->wmac : t->rmac, 32, (const uint8_t *)m.p, (size_t)m.n, out);
  free(m.p);
}
static void t12_prf(uint8_t *out, int olen, const uint8_t *secret, int slen, const char *label, const uint8_t *seed1, int n1, const uint8_t *seed2, int n2) {
  int ln = (int)strlen(label);
  int sl = ln + n1 + n2;
  uint8_t *sd = (uint8_t *)xalloc((size_t)sl);
  memcpy(sd, label, (size_t)ln);
  if (n1) memcpy(sd + ln, seed1, (size_t)n1);
  if (n2) memcpy(sd + ln + n1, seed2, (size_t)n2);
  uint8_t a[32];
  hmac_sha256(secret, (size_t)slen, sd, (size_t)sl, a);
  int off = 0;
  while (off < olen) {
    uint8_t pin[128];
    memcpy(pin, a, 32);
    memcpy(pin + 32, sd, (size_t)sl);
    uint8_t chunk[32];
    hmac_sha256(secret, (size_t)slen, pin, (size_t)(32 + sl), chunk);
    int take = olen - off < 32 ? olen - off : 32;
    memcpy(out + off, chunk, (size_t)take);
    off += take;
    hmac_sha256(secret, (size_t)slen, a, 32, a);
  }
  free(sd);
}
static void t12_keys(T13 *t, int w) {
  uint8_t km[160];
  int kl = t->suite == 0x003C || t->suite == 0xC027 ? 16 : 32;
  int ml = 32;
  int ivl = 16;
  int need = ml * 2 + kl * 2 + ivl * 2;
  t12_prf(km, need, t->master12, 48, "key expansion", t->srand12, 32, t->crand12, 32);
  uint8_t *cmk = km, *smk = km + ml, *ck = km + ml * 2, *mk = km + ml * 2 + kl;
  uint8_t *civ = km + ml * 2 + kl * 2, *miv = km + ml * 2 + kl * 2 + ivl;
  memcpy(t->wmac, cmk, (size_t)ml);
  memcpy(t->rmac, smk, (size_t)ml);
  if (w) {
    memcpy(t->wkey, ck, (size_t)kl);
    memcpy(t->wiv, civ, (size_t)ivl);
    aes_key_setup(&t->waes, ck, kl == 16 ? 128 : 256);
    t->winit = 1; t->wseq = 0;
  } else {
    memcpy(t->rkey, mk, (size_t)kl);
    memcpy(t->riv, miv, (size_t)ivl);
    aes_key_setup(&t->raes, mk, kl == 16 ? 128 : 256);
    t->rinit = 1; t->rseq = 0;
  }
  memset(km, 0, sizeof km);
  (void)cmk; (void)smk;
}
static int t12_seal(T13 *t, unsigned char type, const char *d, int n) {
  int kl = t->suite == 0x003C || t->suite == 0xC027 ? 16 : 32;
  while (n > 0) {
    int take = n > 16384 ? 16384 : n;
    uint8_t mac[32];
    t12_mac(t, 1, t->wseq, type, d, take, mac);
    int m = take + 32;
    int pad = 16 - (m % 16);
    if (pad > 255) pad = 16;
    int plen = 16 + m + pad;
    int total = 5 + plen;
    uint8_t *rec = (uint8_t *)xalloc((size_t)total);
    rec[0] = type; rec[1] = 3; rec[2] = 3;
    rec[3] = (uint8_t)(plen >> 8); rec[4] = (uint8_t)plen;
    os_random(rec + 5, 16);
    memcpy(rec + 5 + 16, d, (size_t)take);
    memcpy(rec + 5 + 16 + take, mac, 32);
    memset(rec + 5 + 16 + take + 32, (uint8_t)(pad - 1), (size_t)pad);
    if (!t->winit) t12_keys(t, 1);
    uint8_t prev[16];
    memcpy(prev, rec + 5, 16);
    for (int i = 21; i < total; i += 16) {
      uint8_t *blk = rec + i;
      for (int k = 0; k < 16; k++) blk[k] ^= prev[k];
      aes_enc_block(&t->waes, blk, blk);
      memcpy(prev, blk, 16);
    }
    int wr = sock_write(t->fd, (const char *)rec, (size_t)total);
    free(rec);
    if (wr != 0) return -1;
    d += take; n -= take;
    t->wseq++;
  }
  return 0;
}
static int t12_unseal(T13 *t, const uint8_t *body, int blen, unsigned char type, Buf *out) {
  if (!t->rinit) t12_keys(t, 0);
  if (blen < 16 + 16 + 32 + 1 || blen % 16 != 0) return -1;
  int plen = blen;
  uint8_t *pt = (uint8_t *)xalloc((size_t)plen);
  uint8_t prev[16];
  memcpy(prev, body, 16);
  for (int i = 16; i < plen; i += 16) {
    uint8_t blk[16], dec[16];
    memcpy(blk, body + i, 16);
    aes_dec_block(&t->raes, blk, dec);
    for (int k = 0; k < 16; k++) pt[i - 16 + k] = (uint8_t)(dec[k] ^ prev[k]);
    memcpy(prev, blk, 16);
  }
  int declen = plen - 16;
  uint8_t padv = pt[declen - 1];
  if ((int)padv + 1 + 32 > declen) { free(pt); return -1; }
  int dn = declen - (int)padv - 1 - 32;
  if (dn < 0) { free(pt); return -1; }
  uint8_t mac[32];
  t12_mac(t, 0, t->rseq, type, (const char *)pt, dn, mac);
  uint8_t dd = 0;
  for (int i = 0; i < 32; i++) dd |= (uint8_t)(mac[i] ^ pt[dn + i]);
  if (dd) { free(pt); return -1; }
  t->rseq++;
  buf_add(out, (const char *)pt, dn);
  free(pt);
  return dn;
}
static int t13_seal(T13 *t, unsigned char itype, const char *d, int n) {
  if (t->is12) return t12_seal(t, itype, d, n);
  while (n > 0) {
    int take = n > 16384 ? 16384 : n;
    int inlen = take + 1, ctlen = inlen + 16;
    uint8_t nonce[12], tag[16], hdr[5];
    uint8_t *inner = (uint8_t *)xalloc((size_t)inlen);
    uint8_t *ct = (uint8_t *)xalloc((size_t)ctlen);
    memcpy(inner, d, (size_t)take);
    inner[take] = itype;
    hdr[0] = 0x17; hdr[1] = 3; hdr[2] = 3;
    hdr[3] = (uint8_t)(ctlen >> 8); hdr[4] = (uint8_t)ctlen;
    t13_nonce(t->wiv, t->wseq, nonce);
    if (t->suite == T13_AES) {
      if (!t->winit) { aes_key_setup(&t->waes, t->wkey, 128); t->winit = 1; }
      gcm_crypt(&t->waes, nonce, 12, inner, inlen, ct, hdr, 5, tag, false);
    } else {
      chacha_aead(t->wkey, nonce, hdr, 5, inner, inlen, ct, tag);
    }
    Buf r;
    buf_init(&r);
    buf_add(&r, (const char *)hdr, 5);
    buf_add(&r, (const char *)ct, (size_t)inlen);
    buf_add(&r, (const char *)tag, 16);
    int wr = sock_write(t->fd, r.p, (size_t)r.n);
    free(r.p); free(inner); free(ct);
    if (wr != 0) return -1;
    d += take; n -= take;
    t->wseq++;
  }
  return 0;
}
static int t13_fill(T13 *t) {
  char tmp[16384];
  int r = (int)sock_read(t->fd, tmp, sizeof tmp);
  if (r > 0) buf_add(&t->raw, tmp, r);
  return r;
}
static int t13_rec_in(T13 *t, int *ct, Buf *out) {
  for (;;) {
    while (t->raw.n >= 5) {
      unsigned char rt = (unsigned char)t->raw.p[0];
      int rlen = ((unsigned char)t->raw.p[3] << 8) | (unsigned char)t->raw.p[4];
      if (rt != 0x16 && rt != 0x17 && rt != 0x14 && rt != 0x15) return -1;
      if (rlen > (1 << 14) + 1792) return -1;
      if (t->raw.n < 5 + rlen) break;
      const char *body = t->raw.p + 5;
      if (rt == 0x14) {
        memmove(t->raw.p, t->raw.p + 5 + rlen, (size_t)(t->raw.n - 5 - rlen));
        t->raw.n -= 5 + rlen;
        t->raw.p[t->raw.n] = 0;
        continue;
      }
      if (rt == 0x16) {
        if (t->rinit || t->hs_done) return -1;
        *ct = 0x16;
        buf_add(out, body, rlen);
        memmove(t->raw.p, t->raw.p + 5 + rlen, (size_t)(t->raw.n - 5 - rlen));
        t->raw.n -= 5 + rlen;
        t->raw.p[t->raw.n] = 0;
        return 1;
      }
      if (rt == 0x15) {
        if (t->rinit) return -1;
        *ct = 0x15;
        buf_add(out, body, rlen);
        memmove(t->raw.p, t->raw.p + 5 + rlen, (size_t)(t->raw.n - 5 - rlen));
        t->raw.n -= 5 + rlen;
        t->raw.p[t->raw.n] = 0;
        return 1;
      }
      if (!t->rinit) return -1;
      if (t->is12) {
        unsigned char rtyper = rt;
        Buf ob;
        buf_init(&ob);
        int dn = t12_unseal(t, (const uint8_t *)body, rlen, rtyper, &ob);
        if (dn < 0) { free(ob.p); return -1; }
        *ct = rtyper;
        if (dn > 0) buf_add(out, ob.p, ob.n);
        free(ob.p);
        memmove(t->raw.p, t->raw.p + 5 + rlen, (size_t)(t->raw.n - 5 - rlen));
        t->raw.n -= 5 + rlen;
        t->raw.p[t->raw.n] = 0;
        return 1;
      }
      int cl = rlen - 16;
      if (cl < 1) return -1;
      uint8_t nonce[12], tag[16], hdr[5];
      uint8_t *pt = (uint8_t *)xalloc((size_t)cl + 1);
      uint8_t *cb = (uint8_t *)xalloc((size_t)rlen);
      memcpy(cb, body, (size_t)rlen);
      hdr[0] = rt; hdr[1] = 3; hdr[2] = 3;
      hdr[3] = (uint8_t)(rlen >> 8); hdr[4] = (uint8_t)rlen;
      t13_nonce(t->riv, t->rseq, nonce);
      if (t->suite == T13_AES) {
        if (!t->rinit) { free(pt); free(cb); return -1; }
        gcm_crypt(&t->raes, nonce, 12, cb, cl, pt, hdr, 5, tag, true);
      } else {
        t13_chacha_dec(t->rkey, nonce, hdr, 5, cb, cl, pt, tag);
      }
      int okm = t13_cteq(tag, cb + cl, 16) == 0;
      free(cb);
      if (!okm) { free(pt); return -1; }
      t->rseq++;
      while (cl > 0 && pt[cl - 1] == 0) cl--;
      if (cl < 1 || (pt[cl - 1] != 0x16 && pt[cl - 1] != 0x15 && pt[cl - 1] != 0x17)) { free(pt); return -1; }
      *ct = pt[cl - 1];
      buf_add(out, (const char *)pt, cl - 1);
      free(pt);
      memmove(t->raw.p, t->raw.p + 5 + rlen, (size_t)(t->raw.n - 5 - rlen));
      t->raw.n -= 5 + rlen;
      t->raw.p[t->raw.n] = 0;
      return 1;
    }
    int r = t13_fill(t);
    if (r <= 0) return r;
  }
}
static int t13_hs_next(T13 *t, int *mt, Buf *out) {
  for (;;) {
    if (t->hsb.n >= 4) {
      const uint8_t *h = (const uint8_t *)t->hsb.p;
      long mlen = ((long)h[1] << 16) | ((long)h[2] << 8) | (long)h[3];
      if (mlen < 0 || mlen > (1 << 22)) return -1;
      if (t->hsb.n >= 4 + mlen) {
        *mt = h[0];
        buf_add(out, t->hsb.p + 4, (int)mlen);
        memmove(t->hsb.p, t->hsb.p + 4 + mlen, (size_t)(t->hsb.n - 4 - mlen));
        t->hsb.n -= 4 + mlen;
        t->hsb.p[t->hsb.n] = 0;
        return 1;
      }
    }
    int ct;
    Buf rb;
    buf_init(&rb);
    int r = t13_rec_in(t, &ct, &rb);
    if (r <= 0) { free(rb.p); return r; }
    if (ct == 0x16) buf_add(&t->hsb, rb.p, rb.n);
    else if (ct == 0x15) {
      long code = rb.n >= 2 ? (uint8_t)rb.p[1] : -1;
      free(rb.p);
      if (code == 0) return 0;
      return -2;
    } else { free(rb.p); return -1; }
    free(rb.p);
  }
}
static void t13_ext(Buf *ex, int et, const uint8_t *data, int n) {
  buf_addc(ex, (char)(et >> 8)); buf_addc(ex, (char)(et & 255));
  buf_addc(ex, (char)(n >> 8)); buf_addc(ex, (char)(n & 255));
  if (n > 0) buf_add(ex, (const char *)data, n);
}
static bool t13_is_ip(const char *h) {
  int dots = 0, digs = 0, col = 0;
  for (const char *p = h; *p; p++) {
    if (*p == '.') dots++;
    else if (*p == ':') col = 1;
    else if (*p >= '0' && *p <= '9') digs++;
    else return false;
  }
  return col == 1 || (dots == 3 && digs > 0);
}
static void t12_rot(T13 *t, int write) { t12_keys(t, write); }
static void t12_ecdh_gen(T13 *t) {
  xp256_pubkey(t->g_priv, t->ecdh_pub);
  static const uint8_t g384x[48] = {
    0xAA, 0x87, 0xCA, 0x22, 0xBE, 0x8B, 0x05, 0x37, 0x8E, 0xB1, 0xC7, 0x1E, 0xF3, 0x20, 0xAD, 0x74,
    0x6E, 0x1D, 0x3B, 0x62, 0x8B, 0xA7, 0x9B, 0x98, 0x59, 0xF7, 0x41, 0xE0, 0x82, 0x54, 0x2A, 0x38,
    0x55, 0x02, 0xF2, 0x5D, 0xBF, 0x55, 0x29, 0x6C, 0x3A, 0x54, 0x5E, 0x38, 0x72, 0x76, 0x0A, 0xB7
  };
  static const uint8_t g384y[48] = {
    0x36, 0x17, 0xDE, 0x4A, 0x96, 0x26, 0x2C, 0x6F, 0x5D, 0x9E, 0x98, 0xBF, 0x92, 0x92, 0xDC, 0x29,
    0xF8, 0xF4, 0x1D, 0xBD, 0x28, 0x9A, 0x14, 0x7C, 0xE9, 0xDA, 0x31, 0x13, 0xB5, 0xF0, 0xB8, 0xC0,
    0x0A, 0x60, 0xB1, 0xCE, 0x1D, 0x7E, 0x81, 0x9D, 0x7A, 0x43, 0x1D, 0x7C, 0x90, 0xEA, 0x0E, 0x5F
  };
  XECCurve *c384 = ecbig_curve(2);
  if (c384) {
    Big GX, GY, K;
    big_from_bytes((const char *)g384x, 48, &GX);
    big_from_bytes((const char *)g384y, 48, &GY);
    big_from_bytes((const char *)t->g384_priv, 48, &K);
    XECPt G;
    G.x = GX; G.y = GY; G.z = big_new(1);
    XECPt R;
    ec_pt_mul(c384, &G, t->g384_priv, 48, &R);
    ec_pt_free(&G);
    t->ecdh384_pub[0] = 0x04;
    if (!ec_pt_affine_x(c384, &R, t->ecdh384_pub + 1)) memset(t->ecdh384_pub, 0, 97);
    if (!ec_pt_affine_y(c384, &R, t->ecdh384_pub + 49)) memset(t->ecdh384_pub, 0, 97);
    ec_pt_free(&R);
    free(K.d);
  }
}
static void t13_rot(T13 *t, int write) {
  uint8_t ns[32];
  t13_elabel(write ? t->c_ap_sec : t->s_ap_sec, "traffic upd", NULL, 0, ns, 32);
  if (write) memcpy(t->c_ap_sec, ns, 32);
  else memcpy(t->s_ap_sec, ns, 32);
  t13_apply(t, write ? t->c_ap_sec : t->s_ap_sec, write);
}
static void t13_posths(T13 *t) {
  while (t->hsb.n >= 4) {
    const uint8_t *h = (const uint8_t *)t->hsb.p;
    long mlen = ((long)h[1] << 16) | ((long)h[2] << 8) | (long)h[3];
    if (mlen < 0 || mlen > (1 << 22) || t->hsb.n < 4 + mlen) break;
    unsigned char mtp = h[0];
    if (mtp == 0x18 && mlen == 1) {
      if (h[4] == 1) {
        uint8_t ku[5] = {0x18, 0, 0, 1, 0};
        t13_seal(t, 0x16, (const char *)ku, 5);
        t13_rot(t, 1);
      }
      t13_rot(t, 0);
    }
    memmove(t->hsb.p, t->hsb.p + 4 + mlen, (size_t)(t->hsb.n - 4 - mlen));
    t->hsb.n -= 4 + mlen;
    t->hsb.p[t->hsb.n] = 0;
  }
}
static bool t13_parse_certs(const uint8_t *p, int n, Buf *cs, int *cn, int max, int is12) {
  *cn = 0;
  int off = 0;
  if (n < 3) return false;
  long ll;
  if (!is12) {
    if (n < 4 || p[0] != 0) return false;
    off = 1;
  }
  ll = ((long)p[off] << 16) | ((long)p[off + 1] << 8) | (long)p[off + 2];
  off += 3;
  if (off + ll > n) return false;
  int end = off + (int)ll;
  while (off + 3 <= end && *cn < max) {
    long cl = ((long)p[off] << 16) | ((long)p[off + 1] << 8) | (long)p[off + 2];
    off += 3;
    if (cl <= 0 || off + cl > end) return false;
    buf_init(&cs[*cn]);
    buf_add(&cs[*cn], (const char *)p + off, (int)cl);
    off += (int)cl;
    if (off == end) { (*cn)++; return *cn > 0; }
    if (is12) { (*cn)++; continue; }
    if (off + 2 > end) { free(cs[*cn].p); return false; }
    int xl = (p[off] << 8) | p[off + 1];
    off += 2 + xl;
    if (off > end) { free(cs[*cn].p); return false; }
    (*cn)++;
  }
  return off == end && *cn > 0;
}
static bool t13_sh_ok(const uint8_t *p, int n, T13 *t, char *err, int es, uint8_t shared[32]) {
  if (n < 38) { snprintf(err, es, "ServerHello مبتور"); return false; }
  if (p[0] != 3 || (p[1] != 3 && p[1] != 4)) { snprintf(err, es, "الخادم رفض TLS (نسخة 0x%02X%02X)", p[0], p[1]); return false; }
  if (p[1] == 3 && n < 38) { snprintf(err, es, "ServerHello مبتور"); return false; }
  int pos = 34;
  int slen = p[pos++];
  if (p[1] == 3) {
    if (pos + slen + 3 > n) { snprintf(err, es, "ServerHello مبتور (sid)"); return false; }
  } else if (slen > 32 || pos + slen + 8 > n) { snprintf(err, es, "ServerHello مشوه (sid)"); return false; }
  if (slen > 32) { snprintf(err, es, "ServerHello مشوه (sid)"); return false; }
  if (p[1] == 4 && slen == 32 && memcmp(p + pos, t->sid, 32) != 0) { snprintf(err, es, "معرف الجلسة غير مطابق"); return false; }
  if (p[1] != 3 && slen != 0 && slen != 32) { snprintf(err, es, "ServerHello مشوه"); return false; }
  pos += slen;
  if (pos + 2 > n) { snprintf(err, es, "ServerHello مشوه"); return false; }
  int suite = (p[pos] << 8) | p[pos + 1];
  pos += 2;
  int sver = 0, grp = -1, klen = 0;
  const uint8_t *share = NULL;
  static const uint8_t hrr[32] = {0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11, 0xBE, 0x1D, 0x8C, 0x02, 0x1E, 0x65, 0xB8, 0x91, 0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB, 0x8C, 0x5E, 0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C};
  if (p[1] == 4 || (p[1] == 3 && n >= 38 && memcmp(p + 6, hrr, 32) == 0)) { snprintf(err, es, "الخادم طلب HelloRetry بمجموعات غير مدعومة"); return false; }
  if (suite != T13_AES && suite != T13_CHACHA) {
    t->is12 = 1;
    t->suite = suite;
    t->keylen = suite == 0x003C || suite == 0xC027 ? 16 : 32;
    t->kx12 = (suite == 0xC027 || suite == 0xC028) ? 2 : 1;
    t->ecdhe_gid = -1;
    int p2 = pos + 1, end2 = n;
    t->ems12 = 0;
    t->sigalg12[0] = -1;
    t->sigalg12[1] = -1;
    if (p2 + 2 <= end2) {
      int exl2 = (p[p2] << 8) | p[p2 + 1];
      p2 += 2;
      int eend2 = p2 + exl2;
      if (eend2 > end2) eend2 = end2;
      while (p2 + 4 <= eend2) {
        int et2 = (p[p2] << 8) | p[p2 + 1];
        int el2 = (p[p2 + 2] << 8) | p[p2 + 3];
        p2 += 4;
        if (p2 + el2 > eend2) break;
        if (et2 == 23) t->ems12 = 1;
        else if (et2 == 13 && el2 >= 2) {
          int na = (p[p2] << 8) | p[p2 + 1];
          if (na >= 2 && el2 >= 2 + na * 2) {
            t->sigalg12[0] = (p[p2 + 2] << 8) | p[p2 + 3];
            t->sigalg12[1] = (p[p2 + 2 + na * 2 - 2] << 8) | p[p2 + 2 + na * 2 - 1];
          }
        }
        p2 += el2;
      }
    }
    return true;
  }
  if (suite != T13_AES && suite != T13_CHACHA) { snprintf(err, es, "تشفيرة الخادم غير مدعومة (0x%04X)", suite); return false; }
  t->suite = suite;
  t->keylen = suite == T13_AES ? 16 : 32;
  if (p[pos] != 0) { snprintf(err, es, "ضغط غير مدعوم من الخادم"); return false; }
  pos += 1;
  if (pos + 2 > n) { snprintf(err, es, "ServerHello مشوه"); return false; }
  int exlen = (p[pos] << 8) | p[pos + 1];
  pos += 2;
  if (pos + exlen != n) { snprintf(err, es, "ServerHello مشوه (امتدادات)"); return false; }
  int end = n;
  while (pos + 4 <= end) {
    int et = (p[pos] << 8) | p[pos + 1];
    int el = (p[pos + 2] << 8) | p[pos + 3];
    pos += 4;
    if (pos + el > end) { snprintf(err, es, "ServerHello مشوه (امتداد)"); return false; }
    if (et == 43 && el == 2 && p[pos] == 3 && p[pos + 1] == 4) sver = 1;
    else if (et == 51 && el >= 4) {
      grp = (p[pos] << 8) | p[pos + 1];
      klen = (p[pos + 2] << 8) | p[pos + 3];
      if (klen >= 0 && pos + 4 + klen <= pos + el) share = p + pos + 4;
      else { snprintf(err, es, "key_share مشوه"); return false; }
    }
    pos += el;
  }
  if (!sver) { snprintf(err, es, "الخادم لا يؤكد TLS 1.3"); return false; }
  if (!share) { snprintf(err, es, "الخادم لم يرسل مفتاحاً مشارَكاً"); return false; }
  if (grp == 0x001D && klen == 32) {
    x25519_core(shared, t->x_priv, share);
    uint8_t z = 0;
    for (int i = 0; i < 32; i++) z |= shared[i];
    if (!z) { snprintf(err, es, "قناة X25519 فاسدة"); return false; }
  } else if (grp == 0x0017 && klen == 65) {
    if (!xp256_ecdh(share, 65, t->p_priv, shared)) { snprintf(err, es, "قناة P-256 فاسدة"); return false; }
  } else {
    snprintf(err, es, "مجموعة مفاتيح غير مدعومة (0x%04X)", grp);
    return false;
  }
  return true;
}
static bool t12_handshake(T13 *t, const char *host, bool verify, char *err, int es) {
  X509C leaf;
  memset(&leaf, 0, sizeof leaf);
  static _Thread_local uint8_t rsa_nb[512], rsa_eb[8];
  int rsa_nn = 0, rsa_en = 0;
  int state = 0;
  uint8_t pm[48];
  int cke_n = 0;
  uint8_t *cke_body = NULL;
  Buf tr12;
  buf_init(&tr12);
  buf_add(&tr12, t->tr.p, t->tr.n);
  for (;;) {
    int mt;
    Buf m;
    buf_init(&m);
    int r = t13_hs_next(t, &mt, &m);
    if (r <= 0) {
      free(m.p);
      free(tr12.p);
      snprintf(err, es, r == 0 ? "أغلق الخادم الاتصال أثناء مصافحة TLS 1.2" : r == -2 ? "رفض الخادم المصافحة (تنبيه)" : "مهلة أو خطأ أثناء مصافحة TLS 1.2");
      return false;
    }
    uint8_t hh[4] = {(uint8_t)mt, (uint8_t)((m.n >> 16) & 255), (uint8_t)((m.n >> 8) & 255), (uint8_t)(m.n & 255)};
    buf_add(&tr12, (const char *)hh, 4);
    buf_add(&tr12, m.p, m.n);
    if (mt == 11 && state == 0) {
      state = 1;
      Buf cs[8];
      int cn = 0;
      if (!t13_parse_certs((const uint8_t *)m.p, m.n, cs, &cn, 8, 1)) {
        for (int i = 0; i < cn; i++) free(cs[i].p);
        free(m.p);
        snprintf(err, es, "رسالة شهادات الخادم مشوهة");
        return false;
      }
      const uint8_t *chp[8];
      int chn[8];
      for (int i = 0; i < cn; i++) { chp[i] = (const uint8_t *)cs[i].p; chn[i] = cs[i].n; }
      if (verify) {
        char e2[260];
        if (!x509_verify_chain(chp[0], chn[0], (const uint8_t **)(chp + 1), chn + 1, cn - 1, host, e2, sizeof e2)) {
          for (int i = 0; i < cn; i++) free(cs[i].p);
          free(m.p);
          snprintf(err, es, "فشل التحقق من شهادة %s: %s", host, e2);
          return false;
        }
      }
      if (!x509_parse(&leaf, chp[0], chn[0])) {
        for (int i = 0; i < cn; i++) free(cs[i].p);
        free(m.p);
        snprintf(err, es, "تعذر تحليل شهادة الخادم");
        return false;
      }
      rsa_nn = leaf.rsa_n.n; rsa_en = leaf.rsa_e.n;
      if (t->kx12 == 1 && (rsa_nn < 64 || rsa_nn > 512 || rsa_en < 1 || rsa_en > 8)) {
        for (int i = 0; i < cn; i++) free(cs[i].p);
        free(m.p);
        snprintf(err, es, "مفتاح RSA في الشهادة غير صالح");
        return false;
      }
      if (t->kx12 == 2 && leaf.ktype == 1 && (rsa_nn < 64 || rsa_nn > 512 || rsa_en < 1 || rsa_en > 8)) {
        for (int i = 0; i < cn; i++) free(cs[i].p);
        free(m.p);
        snprintf(err, es, "مفتاح RSA في الشهادة غير صالح");
        return false;
      }
      if (t->kx12 == 2 && leaf.ktype != 1 && leaf.ktype != 2) {
        for (int i = 0; i < cn; i++) free(cs[i].p);
        free(m.p);
        snprintf(err, es, "نوع مفتاح الشهادة غير مدعوم لـECDHE");
        return false;
      }
      if (leaf.ktype == 1) {
        memcpy(rsa_nb, leaf.rsa_n.p, (size_t)rsa_nn);
        memcpy(rsa_eb, leaf.rsa_e.p, (size_t)rsa_en);
      }
      for (int i = 0; i < cn; i++) free(cs[i].p);
      if (t->kx12 == 1 && leaf.ktype != 1) {
        free(m.p);
        snprintf(err, es, "شهادة الخادم ليست RSA (مطلوب لـTLS 1.2 الاحتياطي)");
        return false;
      }
    } else if (mt == 12 && t->kx12 == 2 && (state == 1 || state == 2)) {
      state = 2;
      if (m.n < 4) { free(m.p); snprintf(err, es, "ServerKeyExchange مبتور"); return false; }
      int ctype = m.p[0];
      const uint8_t *spt = (const uint8_t *)m.p + 1;
      int sptn = m.n - 1;
      if (ctype != 3) { free(m.p); snprintf(err, es, ctype == 0 ? "الخادم طلب شهادة عميل (غير مدعوم)" : "نوع ServerKeyExchange غير مدعوم (%d)", ctype); return false; }
      if (sptn < 3) { free(m.p); snprintf(err, es, "ServerKeyExchange ECParameters مبتور"); return false; }
      int cid = (spt[0] << 8) | spt[1];
      int gid = 0, pbn = 0;
      const uint8_t *pb = NULL;
      if (cid == 0x001D) gid = 0;
      else if (cid == 0x0017) gid = 1;
      else if (cid == 0x0018) gid = 2;
      else { free(m.p); snprintf(err, es, "منحنى ECDHE غير مدعوم (0x%04X)", cid); return false; }
      t->ecdhe_gid = gid;
      pbn = spt[2];
      if (3 + pbn > sptn) { free(m.p); snprintf(err, es, "ServerKeyExchange مبتور (نقطة)"); return false; }
      pb = spt + 3;
      int roff = 4 + pbn;
      if (roff + 4 > m.n) { free(m.p); snprintf(err, es, "ServerKeyExchange مبتور (توقيع)"); return false; }
      int salg = (m.p[roff] << 8) | m.p[roff + 1];
      int slen = (m.p[roff + 2] << 8) | m.p[roff + 3];
      if (roff + 4 + slen > m.n) { free(m.p); snprintf(err, es, "ServerKeyExchange مبتور (توقيع)"); return false; }
      int alg_ok = 0;
      for (int ai = 0; ai < 2 && t->sigalg12[ai] >= 0; ai++) if (t->sigalg12[ai] == salg) alg_ok = 1;
      if (t->sigalg12[0] >= 0 && !alg_ok) { free(m.p); snprintf(err, es, "خوارزمية توقيع SKE غير مسموحة (0x%04X)", salg); return false; }
      {
        Buf sk;
        buf_init(&sk);
        buf_add(&sk, (const char *)t->crand12, 32);
        buf_add(&sk, (const char *)t->srand12, 32);
        buf_add(&sk, (const char *)spt - 1, 4 + pbn);
        uint8_t skh[32];
        sha256_raw((const uint8_t *)sk.p, (size_t)sk.n, skh);
        free(sk.p);
        if (leaf.ktype == 1) {
          if (salg == 0x0401) {
            if (!x509_rsa_verify(leaf.rsa_n.p, leaf.rsa_n.n, leaf.rsa_e.p, leaf.rsa_e.n, XH_SHA256, skh, 32, m.p + roff + 4, slen)) {
              free(m.p);
              snprintf(err, es, "فشل التحقق من توقيع ServerKeyExchange");
              return false;
            }
          } else if (salg == 0x0804) {
            if (!x509_rsa_pss_verify(leaf.rsa_n.p, leaf.rsa_n.n, leaf.rsa_e.p, leaf.rsa_e.n, XH_SHA256, skh, 32, m.p + roff + 4, slen)) {
              free(m.p);
              snprintf(err, es, "فشل التحقق من توقيع ServerKeyExchange (PSS)");
              return false;
            }
          } else {
            free(m.p);
            snprintf(err, es, "خوارزمية توقيع SKE غير مدعومة (0x%04X)", salg);
            return false;
          }
        } else if (leaf.ktype == 2 && gid == 1) {
          XDer sd = {m.p + roff + 4, slen}, sq, ri, si;
          unsigned char tt;
          if (!xder_c(&sd, &tt, &sq) || tt != 0x30 || !xder_c(&sq, &tt, &ri) || tt != 0x02 || !xder_c(&sq, &tt, &si) || tt != 0x02) {
            free(m.p);
            snprintf(err, es, "توقيع ECDSA مشوه في ServerKeyExchange");
            return false;
          }
          if (!xp256_ecdsa_verify(leaf.ec_q, leaf.ec_qlen, XH_SHA256, skh, 32, ri.p, ri.n, si.p, si.n)) {
            free(m.p);
            snprintf(err, es, "فشل التحقق من توقيع ServerKeyExchange");
            return false;
          }
        } else {
          free(m.p);
          snprintf(err, es, "خوارزمية توقيع SKE غير مدعومة (0x%04X)", salg);
          return false;
        }
      }
      uint8_t sh[48];
      int shn = 0;
      if (gid == 0) {
        x25519_core(sh, t->x_priv, pb);
        uint8_t zz = 0;
        for (int i = 0; i < 32; i++) zz |= sh[i];
        if (!zz) { free(m.p); snprintf(err, es, "قناة X25519 فاسدة"); return false; }
        shn = 32;
      } else if (gid == 1) {
        uint8_t Xfull[133];
        int Xn = 0;
        if (!xp256_decompress(pb, pbn, Xfull, &Xn)) { free(m.p); snprintf(err, es, "نقطة ECDHE P-256 فاسدة"); return false; }
        shn = xp256_ecdh(Xfull, Xn, t->p_priv, sh) ? 32 : 0;
      } else {
        uint8_t Xfull[133];
        int Xn = 0;
        if (!ecbig_decompress(2, pb, pbn, Xfull, &Xn)) { free(m.p); snprintf(err, es, "نقطة ECDHE P-384 فاسدة"); return false; }
        shn = ecbig_ecdh_shared(2, Xfull, Xn, t->g_priv, 48, sh);
      }
      if (shn <= 0) { free(m.p); snprintf(err, es, "فشل حساب سر ECDHE"); return false; }
      if (shn <= 0 || shn > 48) { free(m.p); snprintf(err, es, "سر ECDHE فاسد"); return false; }
      memset(t->ecdh12, 0, 48);
      memcpy(t->ecdh12, sh, (size_t)shn);
      t->ecdh12_len = shn;
      memset(sh, 0, sizeof sh);
    } else if (mt == 13) {
      free(m.p);
      snprintf(err, es, "الخادم طلب شهادة عميل (غير مدعوم)");
      return false;
    } else if (mt == 14 && (state == 1 || state == 2)) {
      break;
    } else {
      free(m.p);
      snprintf(err, es, "رسالة مصافحة TLS 1.2 غير متوقعة (نوع %d في مرحلة %d)", mt, state);
      return false;
    }
    free(m.p);
  }
  uint8_t pmsecret[48];
  int pmlen = 48;
  if (t->kx12 == 2) {
    memcpy(pmsecret, t->ecdh12, 48);
    pmlen = t->ecdh12_len;
    memset(t->ecdh12, 0, 48);
  } else {
    pm[0] = 3; pm[1] = 3;
    os_random(pm + 2, 46);
    int bn = rsa_nn;
    int boff = 0;
    while (boff < bn - 1 && rsa_nb[boff] == 0) boff++;
    bn -= boff;
    if (bn < 64 || bn < 48 + 3) { snprintf(err, es, "مفتاح RSA صغير جدا (%d)", bn); return false; }
    Big N, E, M, C;
    big_from_bytes((const char *)(rsa_nb + boff), bn, &N);
    big_from_bytes((const char *)rsa_eb, rsa_en, &E);
    uint8_t *pmb = (uint8_t *)xalloc((size_t)bn);
    int psn = bn - 3 - 48;
    if (psn < 8) { snprintf(err, es, "مفتاح RSA صغير جدا"); free(pmb); return false; }
    memset(pmb, 0, (size_t)bn);
    pmb[0] = 0;
    pmb[1] = 2;
    os_random(pmb + 2, psn);
    for (int i = 2; i < 2 + psn; i++) { while (pmb[i] == 0) os_random(pmb + i, 1); }
    memcpy(pmb + bn - 48, pm, 48);
    big_from_bytes((const char *)pmb, bn, &M);
    C = big_modpow(&M, &E, &N);
    int cn2 = bn;
    uint8_t *cb = (uint8_t *)xalloc((size_t)cn2);
    memset(cb, 0, (size_t)cn2);
    ec_big_to_bytes(&C, cb, cn2);
    Buf ckex;
    buf_init(&ckex);
    buf_addc(&ckex, (char)((cn2 >> 8) & 255));
    buf_addc(&ckex, (char)(cn2 & 255));
    buf_add(&ckex, (const char *)cb, cn2);
    free(cb);
    free(pmb);
    free(M.d); free(C.d); free(N.d); free(E.d);
    memcpy(pmsecret, pm, 48);
    memset(pm, 0, sizeof pm);
    cke_n = ckex.n;
    cke_body = (uint8_t *)xalloc((size_t)ckex.n);
    memcpy(cke_body, ckex.p, (size_t)ckex.n);
    free(ckex.p);
  }
  Buf ckex;
  buf_init(&ckex);
  if (t->kx12 == 2) {
    const uint8_t *pub = NULL;
    int pubn = 0;
    if (t->ecdhe_gid == 0) { pub = t->x_pub; pubn = 32; }
    else if (t->ecdhe_gid == 1) { pub = t->ecdh_pub; pubn = 65; }
    else { pub = t->ecdh384_pub; pubn = 97; }
    buf_addc(&ckex, (char)pubn);
    buf_add(&ckex, (const char *)pub, pubn);
  } else {
    buf_add(&ckex, (const char *)cke_body, (size_t)cke_n);
    free(cke_body);
  }
  char mh4[4];
  mh4[0] = 0x10;
  mh4[1] = (char)((ckex.n >> 16) & 255); mh4[2] = (char)((ckex.n >> 8) & 255); mh4[3] = (char)(ckex.n & 255);
  {
    uint8_t hmsg[512];
    int hml = 4 + ckex.n;
    uint8_t *hp = hmsg;
    if (hml > (int)sizeof hmsg) {
      hp = (uint8_t *)xalloc((size_t)hml);
    }
    memcpy(hp, mh4, 4);
    memcpy(hp + 4, ckex.p, (size_t)ckex.n);
    buf_add(&tr12, (const char *)hp, hml);
    if (hp != hmsg) free(hp);
  }
  int plen1 = 4 + ckex.n;
  int total1 = 5 + plen1;
  uint8_t *rec1 = (uint8_t *)xalloc((size_t)total1);
  rec1[0] = 0x16; rec1[1] = 3; rec1[2] = 3;
  rec1[3] = (uint8_t)(plen1 >> 8); rec1[4] = (uint8_t)plen1;
  memcpy(rec1 + 5, mh4, 4);
  memcpy(rec1 + 9, ckex.p, (size_t)ckex.n);
  int wr1 = sock_write(t->fd, (const char *)rec1, (size_t)total1);
  free(rec1); free(ckex.p);
  if (wr1 != 0) { snprintf(err, es, "فشل إرسال ClientKeyExchange"); return false; }
  t12_prf(t->master12, 48, pmsecret, pmlen, "master secret", t->crand12, 32, t->srand12, 32);
  if (t->ems12) {
    uint8_t emsh[32];
    sha256_raw((const uint8_t *)tr12.p, (size_t)tr12.n, emsh);
    t12_prf(t->master12, 48, pmsecret, pmlen, "extended master secret", emsh, 32, NULL, 0);
  }
  memset(pmsecret, 0, sizeof pmsecret);
  {
    uint8_t cc2[6] = {0x14, 3, 3, 0, 1, 1};
    int wr2 = sock_write(t->fd, (const char *)cc2, 6);
    if (wr2 != 0) { snprintf(err, es, "فشل إرسال ChangeCipherSpec"); return false; }
  }
  t12_keys(t, 1);
  uint8_t crf[32];
  sha256_raw((const uint8_t *)tr12.p, (size_t)tr12.n, crf);
  uint8_t vf[12];
  t12_prf(vf, 12, t->master12, 48, "client finished", crf, 32, NULL, 0);
  uint8_t fin[16];
  fin[0] = 0x14; fin[1] = 0; fin[2] = 0; fin[3] = 12;
  memcpy(fin + 4, vf, 12);
  if (t12_seal(t, 0x16, (const char *)fin, 16) != 0) { snprintf(err, es, "فشل إرسال Finished"); return false; }
  buf_add(&tr12, (const char *)fin, 16);
  {
    int got_ccs = 0;
    for (;;) {
      while (t->raw.n >= 5) {
        unsigned char rt = (unsigned char)t->raw.p[0];
        int rlen = ((unsigned char)t->raw.p[3] << 8) | (unsigned char)t->raw.p[4];
        if (rt == 0x15) {
          int ac = -1;
          if (t->raw.n >= 7) ac = ((unsigned char)t->raw.p[5] << 8) | (unsigned char)t->raw.p[6];
          snprintf(err, es, "رفض الخادم المصافحة (تنبيه TLS 1.2 كود=%d)", ac);
          return false;
        }
        if (rt == 0x14 && rlen == 1) {
          memmove(t->raw.p, t->raw.p + 5 + rlen, (size_t)(t->raw.n - 5 - rlen));
          t->raw.n -= 5 + rlen;
          t->raw.p[t->raw.n] = 0;
          got_ccs = 1;
          continue;
        }
        if (rt == 0x16 && got_ccs) {
          if (t->raw.n < 5 + rlen) break;
          Buf fb;
          buf_init(&fb);
          int dn = t12_unseal(t, (const uint8_t *)t->raw.p + 5, rlen, 0x16, &fb);
          if (dn < 0) { free(fb.p); snprintf(err, es, "فشل فك تشفير Finished الخادم"); return false; }
          unsigned char *fp = (unsigned char *)fb.p;
          int fn = fb.n;
          if (fn != 16 || fp[0] != 0x14) { free(fb.p); snprintf(err, es, "Finished الخادم مشوه"); return false; }
          uint8_t srf[32];
          sha256_raw((const uint8_t *)tr12.p, (size_t)tr12.n, srf);
          uint8_t svf[12];
          t12_prf(svf, 12, t->master12, 48, "server finished", srf, 32, NULL, 0);
          int okf = 1;
          for (int i = 0; i < 12; i++) if (svf[i] != fp[4 + i]) okf = 0;
          free(fb.p);
          if (!okf) { snprintf(err, es, "فشل التحقق من Finished الخادم"); return false; }
          memmove(t->raw.p, t->raw.p + 5 + rlen, (size_t)(t->raw.n - 5 - rlen));
          t->raw.n -= 5 + rlen;
          t->raw.p[t->raw.n] = 0;
          free(tr12.p);
          return true;
        }
        break;
      }
      int rr = t13_fill(t);
      if (rr <= 0) { snprintf(err, es, "انقطع الاتصال أثناء انتظار Finished الخادم"); return false; }
    }
  }
  return true;
}
static bool t13_handshake(T13 *t, const char *host, bool verify, const TlsOpts *o, char *err, int es) {
  crv_init();
  os_random(t->x_priv, 32);
  os_random(t->p_priv, 32);
  os_random(t->g_priv, 48);
  os_random(t->ch_rand, 32);
  os_random(t->sid, 32);
  static const uint8_t base9[32] = {9};
  x25519_core(t->x_pub, t->x_priv, base9);
  xp256_pubkey(t->p_priv, t->p_pub);
  t12_ecdh_gen(t);

  Buf ex;
  buf_init(&ex);
  int hl = (int)strlen(host);
  if (hl > 0 && !t13_is_ip(host) && hl < 256) {
    int dn = hl + 5, nn = hl + 3;
    uint8_t snibuf[280];
    if (dn <= (int)sizeof snibuf) {
      snibuf[0] = (uint8_t)(nn >> 8); snibuf[1] = (uint8_t)nn;
      snibuf[2] = 0; snibuf[3] = (uint8_t)(hl >> 8); snibuf[4] = (uint8_t)hl;
      memcpy(snibuf + 5, host, (size_t)hl);
      t13_ext(&ex, 0, snibuf, dn);
    }
  }
  {
    uint8_t groups[6] = {0, 4, 0, 0x1D, 0, 0x17};
    t13_ext(&ex, 10, groups, 6);
  }
  {
    uint8_t sv[5] = {4, 3, 4, 3, 3};
    t13_ext(&ex, 43, sv, 5);
  }
  {
    uint8_t sa[16] = {0, 14, 4, 1, 5, 1, 6, 1, 4, 3, 8, 4, 8, 5, 8, 6};
    t13_ext(&ex, 13, sa, 16);
  }
  {
    uint8_t ps[2] = {1, 1};
    t13_ext(&ex, 45, ps, 2);
  }
  {
    Buf ks;
    buf_init(&ks);
    buf_addc(&ks, 0x00); buf_addc(&ks, 0x1D);
    buf_addc(&ks, 0); buf_addc(&ks, 32);
    buf_add(&ks, (const char *)t->x_pub, 32);
    buf_addc(&ks, 0x00); buf_addc(&ks, 0x17);
    buf_addc(&ks, 0); buf_addc(&ks, 65);
    buf_add(&ks, (const char *)t->p_pub, 65);
    uint8_t ln2[2] = {(uint8_t)(ks.n >> 8), (uint8_t)ks.n};
    Buf kd;
    buf_init(&kd);
    buf_add(&kd, (const char *)ln2, 2);
    buf_add(&kd, ks.p, ks.n);
    t13_ext(&ex, 51, (const uint8_t *)kd.p, kd.n);
    free(ks.p);
    free(kd.p);
  }
  if (o && o->alpn && *o->alpn) {
    unsigned char w[128];
    int wn = alpn_wire(o->alpn, w, (int)sizeof w);
    if (wn > 0) {
      uint8_t hd[2] = {(uint8_t)(wn >> 8), (uint8_t)wn};
      Buf ad;
      buf_init(&ad);
      buf_add(&ad, (const char *)hd, 2);
      buf_add(&ad, (const char *)w, wn);
      t13_ext(&ex, 16, (const uint8_t *)ad.p, ad.n);
      free(ad.p);
    }
  }
  t13_ext(&ex, 23, NULL, 0);
  {
    int need = 512 - 86 - ex.n;
    if (need > 4) {
      uint8_t *pd = (uint8_t *)xalloc((size_t)need);
      memset(pd, 0, (size_t)need);
      t13_ext(&ex, 21, pd, need);
      free(pd);
    }
  }
  Buf bh;
  buf_init(&bh);
  buf_addc(&bh, 3); buf_addc(&bh, 3);
  buf_add(&bh, (const char *)t->ch_rand, 32);
  buf_addc(&bh, 32);
  buf_add(&bh, (const char *)t->sid, 32);
  buf_addc(&bh, 0); buf_addc(&bh, 10);
  buf_addc(&bh, 0x13); buf_addc(&bh, 0x01);
  buf_addc(&bh, 0x13); buf_addc(&bh, 0x03);
  buf_addc(&bh, 0); buf_addc(&bh, 0x3C);
  buf_addc(&bh, 0); buf_addc(&bh, 0x3D);
  buf_addc(&bh, 0xC0); buf_addc(&bh, 0x27);
  buf_addc(&bh, 1); buf_addc(&bh, 0);
  buf_addc(&bh, (char)((ex.n >> 8) & 255)); buf_addc(&bh, (char)(ex.n & 255));
  buf_add(&bh, ex.p, ex.n);
  free(ex.p);
  if (bh.n > 65000) { free(bh.p); snprintf(err, es, "ClientHello ضخم للغاية"); return false; }
  Buf hs;
  buf_init(&hs);
  buf_addc(&hs, 1);
  buf_addc(&hs, (char)((bh.n >> 16) & 255)); buf_addc(&hs, (char)((bh.n >> 8) & 255)); buf_addc(&hs, (char)(bh.n & 255));
  buf_add(&hs, bh.p, bh.n);
  free(bh.p);
  buf_add(&t->tr, hs.p, hs.n);
  {
    Buf rec;
    buf_init(&rec);
    buf_addc(&rec, 0x16); buf_addc(&rec, 3); buf_addc(&rec, 1);
    buf_addc(&rec, (char)((hs.n >> 8) & 255)); buf_addc(&rec, (char)(hs.n & 255));
    buf_add(&rec, hs.p, hs.n);
    int w = sock_write(t->fd, rec.p, (size_t)rec.n);
    free(rec.p);
    free(hs.p);
    if (w != 0) { snprintf(err, es, "فشل إرسال ClientHello"); return false; }
  }

  int mt;
  Buf mb;
  buf_init(&mb);
  int r = t13_hs_next(t, &mt, &mb);
  if (r <= 0) {
    free(mb.p);
    snprintf(err, es, r == 0 ? "أغلق الخادم الاتصال أثناء المصافحة" : "مهلة أو خطأ أثناء انتظار ServerHello");
    return false;
  }
  if (mt != 2) { free(mb.p); snprintf(err, es, "الخادم لم يرسل ServerHello أولاً"); return false; }
  uint8_t shared[32];
  if (!t13_sh_ok((const uint8_t *)mb.p, mb.n, t, err, es, shared)) { free(mb.p); return false; }
  if (t->is12) {
    memcpy(t->srand12, mb.p + 2, 32);
    memcpy(t->crand12, t->ch_rand, 32);
    {
      char mh[4];
      mh[0] = 2;
      mh[1] = (char)((mb.n >> 16) & 255); mh[2] = (char)((mb.n >> 8) & 255); mh[3] = (char)(mb.n & 255);
      buf_add(&t->tr, mh, 4);
      buf_add(&t->tr, mb.p, mb.n);
    }
    free(mb.p);
    return t12_handshake(t, host, verify, err, es);
  }
  {
    char mh[4];
    mh[0] = 2;
    mh[1] = (char)((mb.n >> 16) & 255); mh[2] = (char)((mb.n >> 8) & 255); mh[3] = (char)(mb.n & 255);
    buf_add(&t->tr, mh, 4);
    buf_add(&t->tr, mb.p, mb.n);
  }
  free(mb.p);
  {
    uint8_t zeroes[32] = {0}, th_empty[32], th[32], early[32], d1[32], hs_sec[32];
    sha256_raw((const uint8_t *)"", 0, th_empty);
    sha256_raw((const uint8_t *)t->tr.p, t->tr.n, th);
    t13_hkdf_extract(early, zeroes, 32, zeroes, 32);
    t13_elabel(early, "derived", th_empty, 32, d1, 32);
    t13_hkdf_extract(hs_sec, d1, 32, shared, 32);
    t13_dsecret(hs_sec, "c hs traffic", th, t->c_hs_sec);
    t13_dsecret(hs_sec, "s hs traffic", th, t->s_hs_sec);
    t13_apply(t, t->c_hs_sec, 1);
    t13_apply(t, t->s_hs_sec, 0);
    t13_elabel(hs_sec, "derived", th_empty, 32, d1, 32);
    t13_hkdf_extract(t->mas_sec, d1, 32, zeroes, 32);
  }

  X509C leaf;
  memset(&leaf, 0, sizeof leaf);
  int state = 0;
  for (;;) {
    int mt2;
    Buf m2;
    buf_init(&m2);
    int r2 = t13_hs_next(t, &mt2, &m2);
    if (r2 <= 0) {
      free(m2.p);
      snprintf(err, es, r2 == 0 ? "أغلق الخادم الاتصال أثناء المصافحة" : r2 == -2 ? "رفض الخادم المصافحة (تنبيه)" : "مهلة أو خطأ أثناء المصافحة");
      return false;
    }
    if (mt2 == 8 && state == 0) state = 1;
    else if (mt2 == 11 && state == 1) {
      state = 2;
      Buf cs[8];
      int cn = 0;
      if (!t13_parse_certs((const uint8_t *)m2.p, m2.n, cs, &cn, 8, 0)) {
        for (int i = 0; i < cn; i++) free(cs[i].p);
        free(m2.p);
        snprintf(err, es, "رسالة شهادات الخادم مشوهة");
        return false;
      }
      const uint8_t *chp[8];
      int chn[8];
      for (int i = 0; i < cn; i++) { chp[i] = (const uint8_t *)cs[i].p; chn[i] = cs[i].n; }
      if (verify) {
        char e2[260];
        if (!x509_verify_chain(chp[0], chn[0], (const uint8_t **)(chp + 1), chn + 1, cn - 1, host, e2, sizeof e2)) {
          for (int i = 0; i < cn; i++) free(cs[i].p);
          free(m2.p);
          snprintf(err, es, "فشل التحقق من شهادة %s: %s", host, e2);
          return false;
        }
      }
      if (!x509_parse(&leaf, chp[0], chn[0])) {
        for (int i = 0; i < cn; i++) free(cs[i].p);
        free(m2.p);
        snprintf(err, es, "تعذر تحليل شهادة الخادم");
        return false;
      }
      for (int i = 0; i < cn; i++) free(cs[i].p);
    } else if (mt2 == 15 && state == 2) {
      state = 3;
      if (m2.n < 4) { free(m2.p); snprintf(err, es, "CertificateVerify مبتور"); return false; }
      int salg = (m2.p[0] << 8) | m2.p[1];
      int slen = (m2.p[2] << 8) | m2.p[3];
      if (4 + slen > m2.n) { free(m2.p); snprintf(err, es, "CertificateVerify مشوه"); return false; }
      int hid;
      if (salg == 0x0403 || salg == 0x0804) hid = XH_SHA256;
      else if (salg == 0x0805) hid = XH_SHA384;
      else if (salg == 0x0806) hid = XH_SHA512;
      else { free(m2.p); snprintf(err, es, "خوارزمية توقيع غير مدعومة (0x%04X)", salg); return false; }
      uint8_t th3[64], hcv[64];
      int hl2 = hid == XH_SHA384 ? 48 : 32;
      sha256_raw((const uint8_t *)t->tr.p, t->tr.n, th3);
      Buf cvm;
      buf_init(&cvm);
      for (int i = 0; i < 64; i++) buf_addc(&cvm, ' ');
      buf_add(&cvm, "TLS 1.3, server CertificateVerify", 33);
      buf_addc(&cvm, 0);
      buf_add(&cvm, (const char *)th3, 32);
      if (hid == XH_SHA384) sha384_raw((const uint8_t *)cvm.p, (size_t)cvm.n, hcv);
      else if (hid == XH_SHA512) sha512_raw((const uint8_t *)cvm.p, (size_t)cvm.n, hcv);
      else sha256_raw((const uint8_t *)cvm.p, (size_t)cvm.n, hcv);
      free(cvm.p);
      if (!x509_cv_verify(&leaf, hid, hcv, hl2, (const uint8_t *)m2.p + 4, slen)) {
        free(m2.p);
        snprintf(err, es, "فشل التحقق من توقيع CertificateVerify");
        return false;
      }
    } else if (mt2 == 20 && state == 3) {
      if (m2.n != 32) { free(m2.p); snprintf(err, es, "طول Finished غير متوقع"); return false; }
      uint8_t fk[32], th4[32], expect[32];
      t13_elabel(t->s_hs_sec, "finished", NULL, 0, fk, 32);
      sha256_raw((const uint8_t *)t->tr.p, t->tr.n, th4);
      hmac_sha256(fk, 32, th4, 32, expect);
      if (t13_cteq(expect, (const uint8_t *)m2.p, 32) != 0) {
        free(m2.p);
        snprintf(err, es, "فشل التحقق من Finished الخادم");
        return false;
      }
      {
        char mh[4];
        mh[0] = 0x14;
        mh[1] = (char)((m2.n >> 16) & 255); mh[2] = (char)((m2.n >> 8) & 255); mh[3] = (char)(m2.n & 255);
        buf_add(&t->tr, mh, 4);
        buf_add(&t->tr, m2.p, m2.n);
      }
      free(m2.p);
      break;
    } else {
      free(m2.p);
      snprintf(err, es, "رسالة مصافحة غير متوقعة (نوع %d في مرحلة %d)", mt2, state);
      return false;
    }
    {
      char mh[4];
      mh[0] = (char)mt2;
      mh[1] = (char)((m2.n >> 16) & 255); mh[2] = (char)((m2.n >> 8) & 255); mh[3] = (char)(m2.n & 255);
      buf_add(&t->tr, mh, 4);
      buf_add(&t->tr, m2.p, m2.n);
    }
    free(m2.p);
  }
  {
    uint8_t th5[32], fk2[32], vd[32];
    sha256_raw((const uint8_t *)t->tr.p, t->tr.n, th5);
    t13_dsecret(t->mas_sec, "c ap traffic", th5, t->c_ap_sec);
    t13_dsecret(t->mas_sec, "s ap traffic", th5, t->s_ap_sec);
    t13_elabel(t->c_hs_sec, "finished", NULL, 0, fk2, 32);
    hmac_sha256(fk2, 32, th5, 32, vd);
    uint8_t hm[36];
    hm[0] = 0x14; hm[1] = 0; hm[2] = 0; hm[3] = 0x20;
    memcpy(hm + 4, vd, 32);
    if (t13_seal(t, 0x16, (const char *)hm, 36) != 0) { snprintf(err, es, "فشل إرسال Finished"); return false; }
    buf_add(&t->tr, (const char *)hm, 36);
  }
  t13_apply(t, t->c_ap_sec, 1);
  t13_apply(t, t->s_ap_sec, 0);
  t->write_ap = 1;
  t->read_ap = 1;
  t->hs_done = 1;
  return true;
}
static bool t13_wrap_client(int fd, const char *host, bool verify, const TlsOpts *o, char *err, int es) {
  if (fd < 0 || fd >= MAXFD) { snprintf(err, es, "معرف مقبس غير صالح"); return false; }
  T13 *t = (T13 *)xalloc(sizeof(T13));
  memset(t, 0, sizeof(*t));
  buf_init(&t->tr);
  buf_init(&t->raw);
  buf_init(&t->dec);
  buf_init(&t->hsb);
  t->fd = fd;
  if (!t13_handshake(t, host, verify, o, err, es)) {
    free(t->tr.p); free(t->raw.p); free(t->dec.p); free(t->hsb.p);
    free(t);
    return false;
  }
  t13_of[fd] = t;
  return true;
}
static int t13_buffered(int fd) {
  if (fd < 0 || fd >= MAXFD || !t13_of[fd]) return 0;
  T13 *t = (T13 *)t13_of[fd];
  return t->dec.n > 0;
}
static int t13_read(int fd, char *buf, size_t n) {
  if (fd < 0 || fd >= MAXFD || !t13_of[fd]) return -1;
  T13 *t = (T13 *)t13_of[fd];
  if (t->dec.n > 0) {
    int take = (int)n < t->dec.n ? (int)n : t->dec.n;
    memcpy(buf, t->dec.p, (size_t)take);
    memmove(t->dec.p, t->dec.p + take, (size_t)(t->dec.n - take));
    t->dec.n -= take;
    t->dec.p[t->dec.n] = 0;
    return take;
  }
  if (t->closed) return 0;
  for (;;) {
    int ct;
    Buf b;
    buf_init(&b);
    int r = t13_rec_in(t, &ct, &b);
    if (r < 0) { free(b.p); if (t->dec.n > 0) break; return r; }
    if (r == 0) {
      free(b.p);
      if (t->dec.n > 0) break;
      t->closed = 1;
      return 0;
    }
    if (ct == 0x17) {
      if (b.n > 0) {
        buf_add(&t->dec, b.p, b.n);
        free(b.p);
        break;
      }
      free(b.p);
      continue;
    }
    if (ct == 0x16) {
      buf_add(&t->hsb, b.p, b.n);
      free(b.p);
      t13_posths(t);
      continue;
    }
    if (ct == 0x15) {
      int desc = b.n >= 2 ? (uint8_t)b.p[1] : 0;
      free(b.p);
      if (t->dec.n > 0) break;
      if (desc == 0) {
        t->closed = 1;
        return 0;
      }
      return -1;
    }
    free(b.p);
    return -1;
  }
  {
    int take = (int)n < t->dec.n ? (int)n : t->dec.n;
    memcpy(buf, t->dec.p, (size_t)take);
    memmove(t->dec.p, t->dec.p + take, (size_t)(t->dec.n - take));
    t->dec.n -= take;
    t->dec.p[t->dec.n] = 0;
    return take;
  }
}
static int t13_write(int fd, const char *d, size_t n) {
  if (fd < 0 || fd >= MAXFD || !t13_of[fd]) return -1;
  T13 *t = (T13 *)t13_of[fd];
  if (n == 0) return 0;
  return t13_seal(t, 0x17, d, (int)n) == 0 ? (int)n : -1;
}
static void t13_close(int fd) {
  if (fd < 0 || fd >= MAXFD) return;
  T13 *t = (T13 *)t13_of[fd];
  t13_of[fd] = NULL;
  if (!t) return;
  if (!t->closed && t->winit) {
    uint8_t cn[2] = {1, 0};
    t13_seal(t, 0x15, (const char *)cn, 2);
  }
  free(t->tr.p);
  free(t->raw.p);
  free(t->dec.p);
  free(t->hsb.p);
  free(t);
}
