#include "flate.inc"

static bool z_load(void) { return true; }
static bool z_inflate(const char *in, int n, Buf *out, int wbits) {
  if (n < 0) return false;
  FlBuf b; memset(&b, 0, sizeof b);
  int ok = fl_inflate((const unsigned char *)in, (size_t)n, &b, wbits);
  if (ok && b.n) buf_add(out, (const char *)b.p, (int)b.n);
  free(b.p); return ok != 0;
}
static bool z_deflate(const char *in, int n, Buf *out, int wbits) {
  if (n < 0) return false;
  FlBuf b; memset(&b, 0, sizeof b);
  int ok = fl_deflate((const unsigned char *)in, (size_t)n, &b, wbits);
  if (ok && b.n) buf_add(out, (const char *)b.p, (int)b.n);
  free(b.p); return ok != 0;
}
NAT(z_gzip) { STRA(0); Buf b; buf_init(&b); if (!z_deflate(AS_STR(a[0])->chars, AS_STR(a[0])->len, &b, 31)) { free(b.p); FAIL("فشل الضغط (ذاكرة؟)"); } *res = mkstr(buf_cstr(&b), b.n); free(b.p); return true; }
NAT(z_gunzip) { STRA(0); Buf b; buf_init(&b); if (!z_inflate(AS_STR(a[0])->chars, AS_STR(a[0])->len, &b, 47)) { free(b.p); FAIL("بيانات مضغوطة غير صالحة"); } *res = mkstr(buf_cstr(&b), b.n); free(b.p); return true; }
NAT(z_zlib) { STRA(0); Buf b; buf_init(&b); if (!z_deflate(AS_STR(a[0])->chars, AS_STR(a[0])->len, &b, 15)) { free(b.p); FAIL("فشل الضغط (ذاكرة؟)"); } *res = mkstr(buf_cstr(&b), b.n); free(b.p); return true; }
