typedef struct { Big p, a, b, gx, gy, n, pm2, nm2; int bytes; int ready; } XECCurve;
typedef struct { Big x, y, z; } XECPt;

static void ec_big_to_bytes(const Big *b, uint8_t *out, int len) {
  memset(out, 0, (size_t)len);
  for (int i = 0; i < b->n; i++) {
    uint32_t w = b->d[i];
    for (int k = 0; k < 4; k++) {
      int pos = len - 1 - (i * 4 + k);
      if (pos < 0) break;
      out[pos] = (uint8_t)(w >> (8 * k));
    }
  }
}

static Big ec_small(int v) { Big b = big_new(1); b.d[0] = (uint32_t)v; return b; }

static Big ec_modm(const Big *m, const Big *x) {
  Big q, r;
  bmag_divmod(x, m, &q, &r);
  free(q.d);
  return r;
}

static Big ec_addm(const Big *m, const Big *a, const Big *b) {
  Big s = bmag_add(a, b);
  if (bmag_cmp(&s, m) >= 0) { Big t = bmag_sub(&s, m); free(s.d); return t; }
  return s;
}

static Big ec_subm(const Big *m, const Big *a, const Big *b) {
  if (bmag_cmp(a, b) >= 0) return bmag_sub(a, b);
  Big t = bmag_sub(m, b);
  Big r = bmag_add(a, &t);
  free(t.d);
  return r;
}

static Big ec_mulm(const Big *m, const Big *a, const Big *b) {
  Big t = bmag_mul(a, b);
  Big r = ec_modm(m, &t);
  free(t.d);
  return r;
}

static XECCurve ec_cvs[4];
static int ec_cvs_ready = 0;

static XECCurve *ecbig_curve(int id) {
  if (id != 2 && id != 3) return NULL;
  if (!ec_cvs_ready) { memset(ec_cvs, 0, sizeof ec_cvs); ec_cvs_ready = 1; }
  XECCurve *cv = &ec_cvs[id];
  if (cv->ready) return cv;
  const char *ps, *bs, *gxs, *gys, *ns;
  if (id == 2) {
    ps = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFFFF0000000000000000FFFFFFFF";
    bs = "B3312FA7E23EE7E4988E056BE3F82D19181D9C6EFE8141120314088F5013875AC656398D8A2ED19D2A85C8EDD3EC2AEF";
    gxs = "AA87CA22BE8B05378EB1C71EF320AD746E1D3B628BA79B9859F741E082542A385502F25DBF55296C3A545E3872760AB7";
    gys = "3617DE4A96262C6F5D9E98BF9292DC29F8F41DBD289A147CE9DA3113B5F0B8C00A60B1CE1D7E819D7A431D7C90EA0E5F";
    ns = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC7634D81F4372DDF581A0DB248B0A77AECEC196ACCC52973";
    cv->bytes = 48;
  } else {
    ps = "1FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
    bs = "51953EB9618E1C9A1F929A21A0B68540EEA2DA725B99B315F3B8B489918EF109E156193951EC7E937B1652C0BD3BB1BF073573DF883D2C34F1EF451FD46B503F00";
    gxs = "C6858E06B70404E9CD9E3ECB662395B4429C648139053FB521F828AF606B4D3DBAA14B5E77EFE75928FE1DC127A2FFA8DE3348B3C1856A429BF97E7E31C2E5BD66";
    gys = "11839296A789A3BC0045C8A5FB42C7D1BD998F54449579B446817AFBD17273E662C97EE72995EF42640C550B9013FAD0761353C7086A272C24088BE94769FD16650";
    ns = "1FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFA51868783BF2F966B7FCC0148F709A5D03BB5C9B8899C47AEBB6FB71E91386409";
    cv->bytes = 66;
  }
  big_from_hex(ps, (int)strlen(ps), &cv->p);
  big_from_hex(bs, (int)strlen(bs), &cv->b);
  big_from_hex(gxs, (int)strlen(gxs), &cv->gx);
  big_from_hex(gys, (int)strlen(gys), &cv->gy);
  big_from_hex(ns, (int)strlen(ns), &cv->n);
  Big three = ec_small(3);
  Big two = ec_small(2);
  cv->a = bmag_sub(&cv->p, &three);
  cv->pm2 = bmag_sub(&cv->p, &two);
  cv->nm2 = bmag_sub(&cv->n, &two);
  free(three.d);
  free(two.d);
  cv->ready = 1;
  return cv;
}

static XECPt ec_pt_inf(void) { XECPt p; p.x = big_new(0); p.y = big_new(0); p.z = big_new(0); return p; }
static bool ec_pt_is_inf(const XECPt *p) { return p->z.n == 0; }
static void ec_pt_free(XECPt *p) { free(p->x.d); free(p->y.d); free(p->z.d); }
static XECPt ec_pt_copy(const XECPt *p) { XECPt r; r.x = big_copy(&p->x); r.y = big_copy(&p->y); r.z = big_copy(&p->z); return r; }

static void ec_pt_dbl(const XECCurve *cv, const XECPt *p, XECPt *out) {
  if (ec_pt_is_inf(p) || p->y.n == 0) { *out = ec_pt_inf(); return; }
  const Big *P = &cv->p;
  Big delta = ec_mulm(P, &p->z, &p->z);
  Big gamma = ec_mulm(P, &p->y, &p->y);
  Big beta = ec_mulm(P, &p->x, &gamma);
  Big xm = ec_subm(P, &p->x, &delta);
  Big xp = ec_addm(P, &p->x, &delta);
  Big t1 = ec_mulm(P, &xm, &xp);
  Big three = ec_small(3);
  Big alpha = ec_mulm(P, &t1, &three);
  Big a2 = ec_mulm(P, &alpha, &alpha);
  Big eight = ec_small(8);
  Big b8 = ec_mulm(P, &beta, &eight);
  Big x3 = ec_subm(P, &a2, &b8);
  Big yz = ec_addm(P, &p->y, &p->z);
  Big yz2 = ec_mulm(P, &yz, &yz);
  Big z3 = ec_subm(P, &yz2, &gamma);
  z3 = ec_subm(P, &z3, &delta);
  Big four = ec_small(4);
  Big b4 = ec_mulm(P, &beta, &four);
  Big b4x = ec_subm(P, &b4, &x3);
  Big y3 = ec_mulm(P, &alpha, &b4x);
  Big g2 = ec_mulm(P, &gamma, &gamma);
  Big g8 = ec_mulm(P, &g2, &eight);
  y3 = ec_subm(P, &y3, &g8);
  free(delta.d); free(gamma.d); free(beta.d); free(xm.d); free(xp.d); free(t1.d); free(three.d);
  free(a2.d); free(eight.d); free(b8.d); free(yz.d); free(yz2.d); free(four.d); free(b4.d); free(b4x.d);
  free(g2.d); free(g8.d);
  out->x = x3;
  out->y = y3;
  out->z = z3;
}

static void ec_pt_add(const XECCurve *cv, const XECPt *p, const XECPt *q, XECPt *out) {
  if (ec_pt_is_inf(p)) { *out = ec_pt_copy(q); return; }
  if (ec_pt_is_inf(q)) { *out = ec_pt_copy(p); return; }
  const Big *P = &cv->p;
  Big two = ec_small(2);
  Big z1z1 = ec_mulm(P, &p->z, &p->z);
  Big z2z2 = ec_mulm(P, &q->z, &q->z);
  Big u1 = ec_mulm(P, &p->x, &z2z2);
  Big u2 = ec_mulm(P, &q->x, &z1z1);
  Big s1 = ec_mulm(P, &p->y, &q->z);
  s1 = ec_mulm(P, &s1, &z2z2);
  Big s2 = ec_mulm(P, &q->y, &p->z);
  s2 = ec_mulm(P, &s2, &z1z1);
  Big h = ec_subm(P, &u2, &u1);
  Big rr = ec_subm(P, &s2, &s1);
  if (h.n == 0) {
    bool dbl = rr.n == 0;
    free(two.d); free(z1z1.d); free(z2z2.d); free(u1.d); free(u2.d); free(s1.d); free(s2.d); free(h.d); free(rr.d);
    if (dbl) ec_pt_dbl(cv, p, out);
    else *out = ec_pt_inf();
    return;
  }
  Big h2 = ec_mulm(P, &h, &two);
  Big i = ec_mulm(P, &h2, &h2);
  Big j = ec_mulm(P, &h, &i);
  Big r2 = ec_mulm(P, &rr, &two);
  Big v = ec_mulm(P, &u1, &i);
  Big x3 = ec_mulm(P, &r2, &r2);
  x3 = ec_subm(P, &x3, &j);
  Big v2 = ec_mulm(P, &v, &two);
  x3 = ec_subm(P, &x3, &v2);
  Big vx = ec_subm(P, &v, &x3);
  Big y3 = ec_mulm(P, &r2, &vx);
  Big s1j = ec_mulm(P, &s1, &j);
  Big s1j2 = ec_mulm(P, &s1j, &two);
  y3 = ec_subm(P, &y3, &s1j2);
  Big zz = ec_addm(P, &p->z, &q->z);
  Big zz2 = ec_mulm(P, &zz, &zz);
  Big z3 = ec_subm(P, &zz2, &z1z1);
  z3 = ec_subm(P, &z3, &z2z2);
  z3 = ec_mulm(P, &z3, &h);
  free(two.d); free(z1z1.d); free(z2z2.d); free(u1.d); free(u2.d); free(s1.d); free(s2.d); free(h.d); free(rr.d);
  free(h2.d); free(i.d); free(j.d); free(r2.d); free(v.d); free(v2.d); free(vx.d); free(s1j.d); free(s1j2.d);
  free(zz.d); free(zz2.d);
  out->x = x3;
  out->y = y3;
  out->z = z3;
}

static void ec_pt_mul(const XECCurve *cv, const XECPt *p, const uint8_t *k, int klen, XECPt *out) {
  XECPt acc = ec_pt_inf();
  int started = 0;
  for (int i = 0; i < klen; i++) {
    for (int b = 7; b >= 0; b--) {
      if (started) { XECPt t; ec_pt_dbl(cv, &acc, &t); ec_pt_free(&acc); acc = t; }
      if ((k[i] >> b) & 1) {
        if (!started) { acc = ec_pt_copy(p); started = 1; }
        else { XECPt t; ec_pt_add(cv, &acc, p, &t); ec_pt_free(&acc); acc = t; }
      }
    }
  }
  *out = acc;
}

static bool ec_pt_affine_x(const XECCurve *cv, const XECPt *p, uint8_t *out) {
  if (ec_pt_is_inf(p)) return false;
  Big zi = big_modpow(&p->z, &cv->pm2, &cv->p);
  Big zi2 = ec_mulm(&cv->p, &zi, &zi);
  Big x = ec_mulm(&cv->p, &p->x, &zi2);
  ec_big_to_bytes(&x, out, cv->bytes);
  free(zi.d);
  free(zi2.d);
  free(x.d);
  return true;
}

static bool ec_on_curve(const XECCurve *cv, const Big *x, const Big *y) {
  Big lhs = ec_mulm(&cv->p, y, y);
  Big x3 = ec_mulm(&cv->p, x, x);
  x3 = ec_mulm(&cv->p, &x3, x);
  Big ax = ec_mulm(&cv->p, &cv->a, x);
  Big rhs = ec_addm(&cv->p, &x3, &ax);
  rhs = ec_addm(&cv->p, &rhs, &cv->b);
  bool ok = bmag_cmp(&lhs, &rhs) == 0;
  free(lhs.d); free(x3.d); free(ax.d); free(rhs.d);
  return ok;
}

static bool ecbig_decompress(int id, const uint8_t *pt, int n, uint8_t *out, int *outn) {
  XECCurve *cv = ecbig_curve(id);
  if (!cv || n <= 1) return false;
  int full = 1 + 2 * cv->bytes;
  if (n == full && pt[0] == 0x04) {
    memcpy(out, pt, (size_t)n);
    *outn = n;
    return true;
  }
  if (n != 1 + cv->bytes || (pt[0] != 0x02 && pt[0] != 0x03)) return false;
  Big x;
  big_from_bytes((const char *)pt + 1, cv->bytes, &x);
  if (bmag_cmp(&x, &cv->p) >= 0) { free(x.d); return false; }
  Big x3 = ec_mulm(&cv->p, &x, &x);
  x3 = ec_mulm(&cv->p, &x3, &x);
  Big ax = ec_mulm(&cv->p, &cv->a, &x);
  Big rhs = ec_addm(&cv->p, &x3, &ax);
  rhs = ec_addm(&cv->p, &rhs, &cv->b);
  Big one = ec_small(1);
  Big four = ec_small(4);
  Big p1 = bmag_add(&cv->p, &one);
  Big q, e;
  bmag_divmod(&p1, &four, &e, &q);
  free(q.d); free(p1.d); free(one.d); free(four.d);
  Big y = big_modpow(&rhs, &e, &cv->p);
  free(e.d);
  Big y2 = ec_mulm(&cv->p, &y, &y);
  bool ok = bmag_cmp(&y2, &rhs) == 0;
  free(y2.d);
  if (!ok) { free(x.d); free(x3.d); free(ax.d); free(rhs.d); free(y.d); return false; }
  if ((y.d[0] & 1) != (pt[0] & 1)) {
    Big ny = bmag_sub(&cv->p, &y);
    free(y.d);
    y = ny;
  }
  out[0] = 0x04;
  ec_big_to_bytes(&x, out + 1, cv->bytes);
  ec_big_to_bytes(&y, out + 1 + cv->bytes, cv->bytes);
  *outn = full;
  free(x.d); free(x3.d); free(ax.d); free(rhs.d); free(y.d);
  return true;
}

static bool ecbig_ecdsa_verify(int id, const uint8_t *q, int qlen, const uint8_t *hash, int hlen, const uint8_t *rb, int rlen, const uint8_t *sb, int slen) {
  XECCurve *cv = ecbig_curve(id);
  if (!cv) return false;
  int full = 1 + 2 * cv->bytes;
  if (qlen != full || q[0] != 0x04) return false;
  while (rlen > 1 && rb[0] == 0) { rb++; rlen--; }
  while (slen > 1 && sb[0] == 0) { sb++; slen--; }
  if (rlen <= 0 || slen <= 0 || rlen > cv->bytes || slen > cv->bytes) return false;
  Big qx, qy;
  big_from_bytes((const char *)q + 1, cv->bytes, &qx);
  big_from_bytes((const char *)q + 1 + cv->bytes, cv->bytes, &qy);
  if (!ec_on_curve(cv, &qx, &qy)) { free(qx.d); free(qy.d); return false; }
  Big r, s;
  big_from_bytes((const char *)rb, rlen, &r);
  big_from_bytes((const char *)sb, slen, &s);
  if (r.n == 0 || s.n == 0 || bmag_cmp(&r, &cv->n) >= 0 || bmag_cmp(&s, &cv->n) >= 0) {
    free(qx.d); free(qy.d); free(r.d); free(s.d);
    return false;
  }
  uint8_t zb[80];
  memset(zb, 0, sizeof zb);
  int hl = hlen < cv->bytes ? hlen : cv->bytes;
  if (hl > 0) memcpy(zb + cv->bytes - hl, hash, (size_t)hl);
  Big z;
  big_from_bytes((const char *)zb, cv->bytes, &z);
  Big w = big_modpow(&s, &cv->nm2, &cv->n);
  Big u1 = ec_mulm(&cv->n, &z, &w);
  Big u2 = ec_mulm(&cv->n, &r, &w);
  uint8_t u1b[80], u2b[80];
  ec_big_to_bytes(&u1, u1b, cv->bytes);
  ec_big_to_bytes(&u2, u2b, cv->bytes);
  XECPt G, Qp, A, B, R;
  G.x = big_copy(&cv->gx);
  G.y = big_copy(&cv->gy);
  G.z = ec_small(1);
  Qp.x = big_copy(&qx);
  Qp.y = big_copy(&qy);
  Qp.z = ec_small(1);
  ec_pt_mul(cv, &G, u1b, cv->bytes, &A);
  ec_pt_mul(cv, &Qp, u2b, cv->bytes, &B);
  ec_pt_add(cv, &A, &B, &R);
  bool ok = false;
  if (!ec_pt_is_inf(&R)) {
    uint8_t rx[80];
    if (ec_pt_affine_x(cv, &R, rx)) {
      Big v;
      big_from_bytes((const char *)rx, cv->bytes, &v);
      Big vr = ec_modm(&cv->n, &v);
      ok = bmag_cmp(&vr, &r) == 0;
      free(v.d);
      free(vr.d);
    }
  }
  ec_pt_free(&G); ec_pt_free(&Qp); ec_pt_free(&A); ec_pt_free(&B); ec_pt_free(&R);
  free(qx.d); free(qy.d); free(r.d); free(s.d); free(z.d); free(w.d); free(u1.d); free(u2.d);
  return ok;
}
