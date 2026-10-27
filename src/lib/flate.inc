#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef FL_MAXOUT
#define FL_MAXOUT ((size_t)512 << 20)
#endif

typedef struct { unsigned char *p; size_t n, cap; int oom; } FlBuf;

static int fb_reserve(FlBuf *b, size_t k) {
  if (b->oom) return -1;
  if (b->n + k < b->n) { b->oom = 1; return -1; }
  if (b->n + k > b->cap) {
    size_t nc = b->cap ? b->cap : 4096;
    while (nc < b->n + k) { if (nc > ((size_t)-1) / 2) { b->oom = 1; return -1; } nc *= 2; }
    unsigned char *np = (unsigned char *)realloc(b->p, nc);
    if (!np) { b->oom = 1; return -1; }
    b->p = np; b->cap = nc;
  }
  return 0;
}
static void fb_put(FlBuf *b, const void *d, size_t k) { if (k && fb_reserve(b, k) == 0) { memcpy(b->p + b->n, d, k); b->n += k; } }
static void fb_byte(FlBuf *b, int c) { if (fb_reserve(b, 1) == 0) b->p[b->n++] = (unsigned char)c; }

static uint32_t fl_crc_tab[256]; static int fl_crc_ready;
static uint32_t fl_crc32(uint32_t crc, const unsigned char *p, size_t n) {
  if (!fl_crc_ready) {
    for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); fl_crc_tab[i] = c; }
    fl_crc_ready = 1;
  }
  crc = ~crc; while (n--) crc = fl_crc_tab[(crc ^ *p++) & 0xff] ^ (crc >> 8); return ~crc;
}
static uint32_t fl_adler32(uint32_t a, const unsigned char *p, size_t n) {
  uint32_t s1 = a & 0xffff, s2 = a >> 16;
  while (n) { size_t k = n < 5552 ? n : 5552; n -= k; while (k--) { s1 += *p++; s2 += s1; } s1 %= 65521; s2 %= 65521; }
  return (s2 << 16) | s1;
}

static const unsigned short FL_LBASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const unsigned short FL_LEXT[29]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const unsigned short FL_DBASE[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const unsigned short FL_DEXT[30]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static const unsigned char  FL_CLORD[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

typedef struct { unsigned short count[16], symbol[288]; } FlHuf;
typedef struct { const unsigned char *in; size_t n, pos; uint32_t bb; int bc; FlBuf *out; size_t base, maxout; int err; } FlInf;

static int fi_bits(FlInf *s, int need) {
  while (s->bc < need) {
    if (s->pos >= s->n) { s->err = 1; return 0; }
    s->bb |= (uint32_t)s->in[s->pos++] << s->bc; s->bc += 8;
  }
  int v = (int)(s->bb & ((1u << need) - 1)); s->bb >>= need; s->bc -= need; return v;
}

static int fh_build(FlHuf *h, const unsigned short *len, int n) {
  unsigned short offs[16]; int l, left = 1;
  for (l = 0; l < 16; l++) h->count[l] = 0;
  for (int s = 0; s < n; s++) h->count[len[s]]++;
  if (h->count[0] == n) return 0;
  for (l = 1; l < 16; l++) { left <<= 1; left -= h->count[l]; if (left < 0) return left; }
  offs[1] = 0; for (l = 1; l < 15; l++) offs[l + 1] = (unsigned short)(offs[l] + h->count[l]);
  for (int s = 0; s < n; s++) if (len[s]) h->symbol[offs[len[s]]++] = (unsigned short)s;
  return left;
}
static int fh_decode(FlInf *s, const FlHuf *h) {
  int code = 0, first = 0, index = 0;
  for (int l = 1; l < 16; l++) {
    code |= fi_bits(s, 1); if (s->err) return -1;
    int c = h->count[l];
    if (code - c < first) return h->symbol[index + (code - first)];
    index += c; first += c; first <<= 1; code <<= 1;
  }
  return -2;
}
static int fi_stored(FlInf *s) {
  s->bb = 0; s->bc = 0;
  if (s->pos + 4 > s->n) return -1;
  unsigned len = s->in[s->pos] | ((unsigned)s->in[s->pos + 1] << 8), nl = s->in[s->pos + 2] | ((unsigned)s->in[s->pos + 3] << 8);
  s->pos += 4;
  if (len != (~nl & 0xffffu)) return -1;
  if (s->pos + len > s->n || s->out->n + len > s->maxout) return -1;
  fb_put(s->out, s->in + s->pos, len); s->pos += len;
  return s->out->oom ? -1 : 0;
}
static int fi_codes(FlInf *s, const FlHuf *lc, const FlHuf *dc) {
  for (;;) {
    int sym = fh_decode(s, lc); if (sym < 0) return -1;
    if (sym < 256) { if (s->out->n + 1 > s->maxout) return -1; fb_byte(s->out, sym); if (s->out->oom) return -1; }
    else if (sym == 256) return 0;
    else {
      sym -= 257; if (sym >= 29) return -1;
      unsigned len = FL_LBASE[sym] + (unsigned)fi_bits(s, FL_LEXT[sym]); if (s->err) return -1;
      int ds = fh_decode(s, dc); if (ds < 0 || ds >= 30) return -1;
      size_t dist = FL_DBASE[ds] + (unsigned)fi_bits(s, FL_DEXT[ds]); if (s->err) return -1;
      if (dist > s->out->n - s->base) return -1;
      if (s->out->n + len > s->maxout || fb_reserve(s->out, len) != 0) return -1;
      unsigned char *d = s->out->p + s->out->n;
      for (unsigned i = 0; i < len; i++) d[i] = d[(ptrdiff_t)i - (ptrdiff_t)dist];
      s->out->n += len;
    }
  }
}
static FlHuf fl_fix_l, fl_fix_d; static int fl_fix_ready;
static int fi_fixed(FlInf *s) {
  if (!fl_fix_ready) {
    unsigned short l[288], d[30]; int i;
    for (i = 0; i < 288; i++) l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
    fh_build(&fl_fix_l, l, 288); for (i = 0; i < 30; i++) d[i] = 5; fh_build(&fl_fix_d, d, 30); fl_fix_ready = 1;
  }
  return fi_codes(s, &fl_fix_l, &fl_fix_d);
}
static int fi_dynamic(FlInf *s) {
  unsigned short lens[320]; FlHuf lc, dc, cc;
  int nlen = fi_bits(s, 5) + 257, ndist = fi_bits(s, 5) + 1, ncode = fi_bits(s, 4) + 4;
  if (s->err || nlen > 286 || ndist > 30) return -1;
  int i; for (i = 0; i < 19; i++) lens[i] = 0;
  for (i = 0; i < ncode; i++) lens[FL_CLORD[i]] = (unsigned short)fi_bits(s, 3);
  if (s->err) return -1;
  if (fh_build(&cc, lens, 19) != 0) return -1;
  int idx = 0;
  while (idx < nlen + ndist) {
    int sym = fh_decode(s, &cc); if (sym < 0) return -1;
    if (sym < 16) lens[idx++] = (unsigned short)sym;
    else {
      int rep, v = 0;
      if (sym == 16) { if (idx == 0) return -1; v = lens[idx - 1]; rep = 3 + fi_bits(s, 2); }
      else if (sym == 17) rep = 3 + fi_bits(s, 3);
      else rep = 11 + fi_bits(s, 7);
      if (s->err || idx + rep > nlen + ndist) return -1;
      while (rep--) lens[idx++] = (unsigned short)v;
    }
  }
  if (lens[256] == 0) return -1;
  int err = fh_build(&lc, lens, nlen);
  if (err && (err < 0 || nlen != lc.count[0] + lc.count[1])) return -1;
  err = fh_build(&dc, lens + nlen, ndist);
  if (err && (err < 0 || ndist != dc.count[0] + dc.count[1])) return -1;
  return fi_codes(s, &lc, &dc);
}

static int fl_inflate_raw(const unsigned char *in, size_t n, FlBuf *out, size_t *used) {
  FlInf s; memset(&s, 0, sizeof s); s.in = in; s.n = n; s.out = out; s.base = out->n; s.maxout = FL_MAXOUT;
  int last, rc = 0;
  do {
    last = fi_bits(&s, 1); int type = fi_bits(&s, 2); if (s.err) return -1;
    rc = type == 0 ? fi_stored(&s) : type == 1 ? fi_fixed(&s) : type == 2 ? fi_dynamic(&s) : -1;
    if (rc) return rc;
  } while (!last);
  *used = s.pos; return 0;
}
static int fl_zlib_header_ok(const unsigned char *in, size_t n) {
  return n >= 6 && (in[0] & 0x0f) == 8 && (in[0] >> 4) <= 7 && ((in[0] << 8) | in[1]) % 31 == 0 && !(in[1] & 0x20);
}
static int fl_inflate_zlib(const unsigned char *in, size_t n, FlBuf *out) {
  if (!fl_zlib_header_ok(in, n)) return -1;
  size_t base = out->n, used = 0;
  if (fl_inflate_raw(in + 2, n - 2, out, &used)) return -1;
  if (2 + used + 4 > n) return -1;
  const unsigned char *t = in + 2 + used;
  uint32_t want = ((uint32_t)t[0] << 24) | ((uint32_t)t[1] << 16) | ((uint32_t)t[2] << 8) | t[3];
  return fl_adler32(1, out->p + base, out->n - base) == want ? 0 : -1;
}
static int fl_inflate_gzip(const unsigned char *in, size_t n, FlBuf *out) {
  size_t pos = 0; int members = 0;
  do {
    if (pos + 18 > n || in[pos] != 0x1f || in[pos + 1] != 0x8b || in[pos + 2] != 8) return members ? 0 : -1;
    unsigned flg = in[pos + 3]; if (flg & 0xe0) return -1;
    size_t p = pos + 10;
    if (flg & 4) { if (p + 2 > n) return -1; size_t xl = in[p] | ((size_t)in[p + 1] << 8); p += 2 + xl; if (p > n) return -1; }
    if (flg & 8) { while (p < n && in[p]) p++; if (p >= n) return -1; p++; }
    if (flg & 16) { while (p < n && in[p]) p++; if (p >= n) return -1; p++; }
    if (flg & 2) { p += 2; if (p > n) return -1; }
    size_t mbase = out->n, used = 0;
    if (fl_inflate_raw(in + p, n - p, out, &used)) return -1;
    p += used; if (p + 8 > n) return -1;
    uint32_t crc = in[p] | ((uint32_t)in[p + 1] << 8) | ((uint32_t)in[p + 2] << 16) | ((uint32_t)in[p + 3] << 24);
    uint32_t isz = in[p + 4] | ((uint32_t)in[p + 5] << 8) | ((uint32_t)in[p + 6] << 16) | ((uint32_t)in[p + 7] << 24);
    if (fl_crc32(0, out->p + mbase, out->n - mbase) != crc || (uint32_t)(out->n - mbase) != isz) return -1;
    pos = p + 8; members++;
  } while (pos + 2 <= n && in[pos] == 0x1f && in[pos + 1] == 0x8b);
  return 0;
}

static int fl_inflate(const unsigned char *in, size_t n, FlBuf *out, int wbits) {
  size_t base = out->n; int rc = -1, fmt;
  if (wbits < 0) fmt = 0; else if (wbits >= 32) fmt = 3; else if (wbits >= 16) fmt = 2; else fmt = 1;
  if (fmt == 3) fmt = (n >= 2 && in[0] == 0x1f && in[1] == 0x8b) ? 2 : (fl_zlib_header_ok(in, n) ? 1 : 0);
  if (fmt == 0) { size_t used; rc = fl_inflate_raw(in, n, out, &used); }
  else if (fmt == 1) rc = fl_inflate_zlib(in, n, out);
  else rc = fl_inflate_gzip(in, n, out);
  if (rc || out->oom) { out->n = base; return 0; }
  return 1;
}

#define FL_MAXTOK 32768
#define FL_BLKRAW 60000
#define FL_WIN 32768
typedef struct { FlBuf *out; uint64_t bb; int bc; } FlWr;
static void fw_put(FlWr *w, uint32_t v, int nb) {
  w->bb |= (uint64_t)v << w->bc; w->bc += nb;
  while (w->bc >= 8) { fb_byte(w->out, (int)(w->bb & 0xff)); w->bb >>= 8; w->bc -= 8; }
}
static void fw_align(FlWr *w) { if (w->bc > 0) { fb_byte(w->out, (int)(w->bb & 0xff)); w->bb = 0; w->bc = 0; } }

static void fl_build_len(const uint32_t *freq, int n, int maxbits, unsigned char *len) {
  uint32_t f[300]; int i; for (i = 0; i < n; i++) f[i] = freq[i];
  for (;;) {
    int idx[300], m = 0;
    for (i = 0; i < n; i++) { len[i] = 0; if (f[i]) idx[m++] = i; }
    if (m == 0) return;
    if (m == 1) { len[idx[0]] = 1; return; }
    for (i = 1; i < m; i++) { int t = idx[i], j = i - 1; while (j >= 0 && f[idx[j]] > f[t]) { idx[j + 1] = idx[j]; j--; } idx[j + 1] = t; }
    uint32_t nf[600]; int par[600], depth[600], next = m, a, b, li = 0, ji = m;
    for (i = 0; i < m; i++) nf[i] = f[idx[i]];
    for (int k = 0; k < m - 1; k++) {
      if (li < m && (ji >= next || nf[li] <= nf[ji])) a = li++; else a = ji++;
      if (li < m && (ji >= next || nf[li] <= nf[ji])) b = li++; else b = ji++;
      nf[next] = nf[a] + nf[b]; par[a] = par[b] = next; next++;
    }
    depth[next - 1] = 0; int maxd = 0;
    for (i = next - 2; i >= 0; i--) { depth[i] = depth[par[i]] + 1; if (i < m && depth[i] > maxd) maxd = depth[i]; }
    if (maxd > maxbits) { for (i = 0; i < n; i++) if (f[i]) f[i] = (f[i] + 1) >> 1; continue; }
    for (i = 0; i < m; i++) len[idx[i]] = (unsigned char)depth[i];
    return;
  }
}
static void fl_make_codes(const unsigned char *len, int n, unsigned short *code) {
  int bl[16] = {0}, c = 0; unsigned short next[16];
  for (int i = 0; i < n; i++) bl[len[i]]++;
  bl[0] = 0; next[0] = 0;
  for (int b = 1; b < 16; b++) { c = (c + bl[b - 1]) << 1; next[b] = (unsigned short)c; }
  for (int i = 0; i < n; i++) if (len[i]) {
    unsigned v = next[len[i]]++, r = 0; for (int k = 0; k < len[i]; k++) { r = (r << 1) | (v & 1); v >>= 1; }
    code[i] = (unsigned short)r;
  } else code[i] = 0;
}
static int fl_lcode(int len) { if (len >= 258) return 28; int i = 27; while (FL_LBASE[i] > len) i--; return i; }
static int fl_dcode(unsigned d) { int i = 29; while (FL_DBASE[i] > d) i--; return i; }

static void fl_write_block(FlWr *w, const unsigned short *tl, const unsigned short *td, int nt, const unsigned char *raw, size_t rawlen, int final) {
  uint32_t lf[286], df[30], cf[19]; unsigned char ll[288], dl[30], cl[19]; unsigned short lcod[288], dcod[30], ccod[19];
  memset(ll, 0, sizeof ll); memset(lf, 0, sizeof lf); memset(df, 0, sizeof df); memset(cf, 0, sizeof cf);
  uint64_t ebits = 0; lf[256] = 1;
  for (int i = 0; i < nt; i++) {
    if (td[i] == 0) lf[tl[i]]++;
    else { int lc = fl_lcode(tl[i]), dc = fl_dcode(td[i]); lf[257 + lc]++; df[dc]++; ebits += FL_LEXT[lc] + FL_DEXT[dc]; }
  }
  fl_build_len(lf, 286, 15, ll); fl_build_len(df, 30, 15, dl);
  { int any = 0; for (int i = 0; i < 30; i++) if (dl[i]) any = 1; if (!any) dl[0] = 1; }
  int hlit = 286; while (hlit > 257 && !ll[hlit - 1]) hlit--;
  int hdist = 30; while (hdist > 1 && !dl[hdist - 1]) hdist--;
  unsigned char seq[316]; int tot = hlit + hdist;
  memcpy(seq, ll, (size_t)hlit); memcpy(seq + hlit, dl, (size_t)hdist);
  unsigned char csym[320], cext[320], cbit[320]; int nc = 0;
#define CL_EMIT(s, e, b) do { csym[nc] = (unsigned char)(s); cext[nc] = (unsigned char)(e); cbit[nc] = (unsigned char)(b); cf[s]++; nc++; } while (0)
  for (int i = 0; i < tot;) {
    int v = seq[i], run = 1; while (i + run < tot && seq[i + run] == v) run++;
    if (v == 0) {
      int r = run; while (r >= 11) { int k = r > 138 ? 138 : r; CL_EMIT(18, k - 11, 7); r -= k; }
      if (r >= 3) { CL_EMIT(17, r - 3, 3); r = 0; } while (r-- > 0) CL_EMIT(0, 0, 0);
    } else {
      CL_EMIT(v, 0, 0); int r = run - 1;
      while (r >= 3) { int k = r > 6 ? 6 : r; CL_EMIT(16, k - 3, 2); r -= k; } while (r-- > 0) CL_EMIT(v, 0, 0);
    }
    i += run;
  }
  fl_build_len(cf, 19, 7, cl); fl_make_codes(cl, 19, ccod);
  int hclen = 19; while (hclen > 4 && !cl[FL_CLORD[hclen - 1]]) hclen--;
  uint64_t dyn = 17 + (uint64_t)hclen * 3 + ebits, fix = 3 + ebits, sto;
  for (int i = 0; i < 19; i++) dyn += (uint64_t)cf[i] * cl[i];
  for (int i = 0; i < nc; i++) dyn += cbit[i];
  for (int i = 0; i < 286; i++) { dyn += (uint64_t)lf[i] * ll[i]; fix += (uint64_t)lf[i] * (i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8); }
  for (int i = 0; i < 30; i++) { dyn += (uint64_t)df[i] * dl[i]; fix += (uint64_t)df[i] * 5; }
  sto = rawlen <= 65535 ? 3 + 7 + 32 + (uint64_t)rawlen * 8 : ~(uint64_t)0;
  int type = (sto <= dyn && sto <= fix) ? 0 : (fix <= dyn ? 1 : 2);
  fw_put(w, (uint32_t)final, 1); fw_put(w, (uint32_t)type, 2);
  if (type == 0) {
    fw_align(w); fb_byte(w->out, (int)(rawlen & 0xff)); fb_byte(w->out, (int)(rawlen >> 8));
    fb_byte(w->out, (int)(~rawlen & 0xff)); fb_byte(w->out, (int)((~rawlen >> 8) & 0xff)); fb_put(w->out, raw, rawlen); return;
  }
  if (type == 1) {
    int i; for (i = 0; i < 288; i++) ll[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
    for (i = 0; i < 30; i++) dl[i] = 5;
  } else {
    fw_put(w, (uint32_t)(hlit - 257), 5); fw_put(w, (uint32_t)(hdist - 1), 5); fw_put(w, (uint32_t)(hclen - 4), 4);
    for (int i = 0; i < hclen; i++) fw_put(w, cl[FL_CLORD[i]], 3);
    for (int i = 0; i < nc; i++) { fw_put(w, ccod[csym[i]], cl[csym[i]]); if (cbit[i]) fw_put(w, cext[i], cbit[i]); }
  }
  fl_make_codes(ll, 288, lcod); fl_make_codes(dl, 30, dcod);
  for (int i = 0; i < nt; i++) {
    if (td[i] == 0) fw_put(w, lcod[tl[i]], ll[tl[i]]);
    else {
      int lc = fl_lcode(tl[i]), dc = fl_dcode(td[i]);
      fw_put(w, lcod[257 + lc], ll[257 + lc]); if (FL_LEXT[lc]) fw_put(w, (uint32_t)(tl[i] - FL_LBASE[lc]), FL_LEXT[lc]);
      fw_put(w, dcod[dc], dl[dc]); if (FL_DEXT[dc]) fw_put(w, (uint32_t)(td[i] - FL_DBASE[dc]), FL_DEXT[dc]);
    }
  }
  fw_put(w, lcod[256], ll[256]);
}
static unsigned fl_hash3(const unsigned char *p) { return (((unsigned)p[0] << 10) ^ ((unsigned)p[1] << 5) ^ p[2]) & 0x7fff; }
static int fl_find(const unsigned char *in, int n, int pos, const int *head, const int *prev, int *dist) {
  int maxlen = n - pos; if (maxlen > 258) maxlen = 258; if (maxlen < 3) return 0;
  int cand = head[fl_hash3(in + pos)], best = 0, bd = 0, chain = 128;
  while (cand >= 0 && pos - cand <= FL_WIN && chain-- > 0) {
    if (in[cand + best] == in[pos + best]) {
      int l = 0; while (l < maxlen && in[cand + l] == in[pos + l]) l++;
      if (l > best) { best = l; bd = pos - cand; if (l >= maxlen) break; }
    }
    int nx = prev[cand & (FL_WIN - 1)]; if (nx >= cand) break; cand = nx;
  }
  if (best < 3 || (best == 3 && bd > 4096)) return 0;
  *dist = bd; return best;
}
static int fl_deflate_raw(const unsigned char *in, int n, FlBuf *out) {
  FlWr w; w.out = out; w.bb = 0; w.bc = 0;
  unsigned short *tl = (unsigned short *)malloc(sizeof(unsigned short) * FL_MAXTOK * 2), *td;
  int *head = (int *)malloc(sizeof(int) * 32768), *prev = (int *)malloc(sizeof(int) * FL_WIN);
  if (!tl || !head || !prev) { free(tl); free(head); free(prev); return -1; }
  td = tl + FL_MAXTOK; memset(head, 0xff, sizeof(int) * 32768);
  int nt = 0, i = 0, plen = 0, pdist = 0, havep = 0; size_t rstart = 0, rlen = 0;
#define FL_INS(P) do { int P_ = (P); if (n - P_ >= 3) { unsigned h_ = fl_hash3(in + P_); prev[P_ & (FL_WIN - 1)] = head[h_]; head[h_] = P_; } } while (0)
#define FL_FLUSH(FIN) do { fl_write_block(&w, tl, td, nt, in + rstart, rlen, (FIN)); rstart += rlen; rlen = 0; nt = 0; } while (0)
#define FL_LIT(B) do { tl[nt] = (B); td[nt] = 0; nt++; rlen++; if (nt >= FL_MAXTOK || rlen >= FL_BLKRAW) FL_FLUSH(0); } while (0)
#define FL_MATCH(L, D) do { tl[nt] = (unsigned short)(L); td[nt] = (unsigned short)(D); nt++; rlen += (size_t)(L); if (nt >= FL_MAXTOK || rlen >= FL_BLKRAW) FL_FLUSH(0); } while (0)
  while (i < n) {
    int l = 0, d = 0;
    if (n - i >= 3) { l = fl_find(in, n, i, head, prev, &d); FL_INS(i); }
    if (havep) {
      if (l > plen) { FL_LIT(in[i - 1]); plen = l; pdist = d; i++; }
      else { FL_MATCH(plen, pdist); int end = i - 1 + plen; for (int k = i + 1; k < end; k++) FL_INS(k); i = end; havep = 0; }
    } else if (l >= 3) { plen = l; pdist = d; havep = 1; i++; }
    else { FL_LIT(in[i]); i++; }
  }
  if (havep) FL_MATCH(plen, pdist);
  FL_FLUSH(1); fw_align(&w);
  free(tl); free(head); free(prev);
  return out->oom ? -1 : 0;
}

static int fl_deflate(const unsigned char *in, size_t n, FlBuf *out, int wbits) {
  if (n > 0x7fffffffu) return 0;
  size_t base = out->n; int fmt = wbits < 0 ? 0 : (wbits >= 16 ? 2 : 1);
  if (fmt == 1) { fb_byte(out, 0x78); fb_byte(out, 0x9c); }
  else if (fmt == 2) { static const unsigned char h[10] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 3}; fb_put(out, h, 10); }
  if (fl_deflate_raw(in, (int)n, out)) { out->n = base; return 0; }
  if (fmt == 1) { uint32_t a = fl_adler32(1, in, n); fb_byte(out, (int)(a >> 24)); fb_byte(out, (int)(a >> 16)); fb_byte(out, (int)(a >> 8)); fb_byte(out, (int)a); }
  else if (fmt == 2) {
    uint32_t c = fl_crc32(0, in, n), s = (uint32_t)n;
    for (int k = 0; k < 4; k++) fb_byte(out, (int)(c >> (8 * k)));
    for (int k = 0; k < 4; k++) fb_byte(out, (int)(s >> (8 * k)));
  }
  if (out->oom) { out->n = base; return 0; }
  return 1;
}
