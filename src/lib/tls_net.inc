typedef struct {
  void *h; const void *(*TLS_client_method)(void); const void *(*TLS_server_method)(void);
  void *(*SSL_CTX_new)(const void *); int (*SSL_CTX_set_default_verify_paths)(void *); void (*SSL_CTX_set_verify)(void *, int, void *);
  void *(*SSL_new)(void *); int (*SSL_set_fd)(void *, int); long (*SSL_ctrl)(void *, int, long, void *);
  int (*SSL_set1_host)(void *, const char *); int (*SSL_connect)(void *); int (*SSL_accept)(void *);
  int (*SSL_read)(void *, void *, int); int (*SSL_write)(void *, const void *, int); int (*SSL_shutdown)(void *);
  void (*SSL_free)(void *); int (*SSL_get_error)(const void *, int); int (*SSL_pending)(const void *);
  int (*SSL_CTX_use_certificate_chain_file)(void *, const char *); int (*SSL_CTX_use_PrivateKey_file)(void *, const char *, int);
} TLSLib;
static TLSLib tl; static int tls_state = 0; static void *ctx_verify, *ctx_noverify;
static void *ssl_of[MAXFD]; static int tmo_ms[MAXFD]; static unsigned char fflag[MAXFD]; static Buf rbuf[MAXFD]; static void *srv_ctx[MAXFD];

#if SOLAN_WIN
#define TLS_HINT "نزّل OpenSSL 3 (مثلًا: winget install ShiningLight.OpenSSL.Light) أو ضع libssl-3-x64.dll بجوار solan.exe"
#elif SOLAN_MAC
#define TLS_HINT "ثبّت: brew install openssl@3"
#elif defined(__ANDROID__)
#define TLS_HINT "ثبّت: pkg install openssl"
#else
#define TLS_HINT "ثبّت libssl: apt install libssl3 | dnf install openssl-libs | pacman -S openssl | apk add openssl | pkg install openssl"
#endif
static bool tls_try(const char *n) { if (!tl.h) tl.h = dlopen(n, RTLD_NOW | RTLD_GLOBAL); return tl.h != NULL; }
static bool tls_load(void) {
  if (tls_state) return tls_state > 0;

  { const char *e = getenv("SOLAN_LIBSSL"); if (e && *e) tls_try(e); }
  #if SOLAN_WIN
  static const char *names[] = {"libssl-3-x64.dll", "libssl-3.dll", "libssl-1_1-x64.dll", "libssl-1_1.dll", NULL};
#else

  { const char *pf = getenv("PREFIX"); if (pf && *pf && !tl.h) { char b[PATH_MAX]; snprintf(b, sizeof b, "%s/lib/libssl.so.3", pf); tls_try(b); } }
  static const char *names[] = {
    "libssl.so.3", "libssl.so.1.1", "libssl.so.30", "libssl.so.111", "libssl.so",
    "/usr/lib/x86_64-linux-gnu/libssl.so.3", "/usr/lib/aarch64-linux-gnu/libssl.so.3", "/usr/lib/arm-linux-gnueabihf/libssl.so.3",
    "/usr/lib64/libssl.so.3", "/usr/lib/libssl.so.3", "/usr/local/lib/libssl.so.3", "/lib/libssl.so.3",
    "libssl.3.dylib", "/opt/homebrew/lib/libssl.3.dylib", "/usr/local/lib/libssl.3.dylib",
    "/opt/homebrew/opt/openssl@3/lib/libssl.3.dylib", "/usr/local/opt/openssl@3/lib/libssl.3.dylib", NULL};
#endif
  for (int i = 0; names[i] && !tl.h; i++) tls_try(names[i]);
  if (!tl.h) { tls_state = -1; return false; }
#define LS(n) do { *(void **)&tl.n = dlsym(tl.h, #n); if (!tl.n) { tls_state = -1; return false; } } while (0)
  LS(TLS_client_method); LS(TLS_server_method); LS(SSL_CTX_new); LS(SSL_CTX_set_default_verify_paths); LS(SSL_CTX_set_verify);
  LS(SSL_new); LS(SSL_set_fd); LS(SSL_ctrl); LS(SSL_connect); LS(SSL_accept); LS(SSL_read); LS(SSL_write); LS(SSL_shutdown);
  LS(SSL_free); LS(SSL_get_error); LS(SSL_pending); LS(SSL_CTX_use_certificate_chain_file); LS(SSL_CTX_use_PrivateKey_file);
  *(void **)&tl.SSL_set1_host = dlsym(tl.h, "SSL_set1_host");
  tls_state = 1; return true;
}
static void *get_ctx(bool verify) {
  void **c = verify ? &ctx_verify : &ctx_noverify;
  if (!*c) {
    void *x = tl.SSL_CTX_new(tl.TLS_client_method()); if (!x) return NULL;
    if (verify) { tl.SSL_CTX_set_default_verify_paths(x); tl.SSL_CTX_set_verify(x, 1, NULL); } else tl.SSL_CTX_set_verify(x, 0, NULL);
    *c = x;
  }
  return *c;
}
static void fd_setup(int fd) {
  if (fd < 0 || fd >= MAXFD) return;
  ssl_of[fd] = NULL; tmo_ms[fd] = -1; fflag[fd] = 0; rbuf[fd].n = 0; if (rbuf[fd].p) rbuf[fd].p[0] = 0; srv_ctx[fd] = NULL;
}
static void conn_close(int fd) {
  if (fd < 0) return;
  if (fd < MAXFD && ssl_of[fd]) { tl.SSL_shutdown(ssl_of[fd]); tl.SSL_free(ssl_of[fd]); }
  close(fd); fd_setup(fd);
}
static bool fd_buffered(int fd) { return rbuf[fd].n > 0 || (ssl_of[fd] && tl.SSL_pending(ssl_of[fd]) > 0); }

static ssize_t conn_read(int fd, char *buf, size_t n) {
  void *ssl = ssl_of[fd]; int tmo = tmo_ms[fd];
  if (tmo >= 0 && !(ssl && tl.SSL_pending(ssl) > 0)) {
    struct pollfd p = {fd, POLLIN, 0}; int pr; do pr = poll(&p, 1, tmo); while (pr < 0 && errno == EINTR);
    if (pr == 0) return -2; if (pr < 0) return -1;
  }
  if (fflag[fd] & 8) { ssize_t r; do r = read(fd, buf, n); while (r < 0 && errno == EINTR); if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return -2; return r; }
  if (ssl) {
    for (;;) {
      int r = tl.SSL_read(ssl, buf, n > 1 << 20 ? 1 << 20 : (int)n); if (r > 0) return r;
      int e = tl.SSL_get_error(ssl, r); if (e == 6) return 0;
      if (e == 2 || e == 3) {
        if (tmo >= 0 || (fflag[fd] & 4)) return -2;
        struct pollfd p = {fd, e == 2 ? POLLIN : POLLOUT, 0}; poll(&p, 1, -1); continue;
      }
      return e == 5 ? 0 : -1;
    }
  }
  ssize_t r; do r = recv(fd, buf, n, 0); while (r < 0 && errno == EINTR);
  if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return -2;
  return r;
}
static int conn_write(int fd, const char *d, size_t n) {
  void *ssl = ssl_of[fd]; size_t off = 0;
  while (off < n) {
    if (fflag[fd] & 8) { ssize_t w = write(fd, d + off, n - off); if (w < 0) { if (errno == EINTR) continue; return -1; } off += w; continue; }
    if (ssl) {
      int w = tl.SSL_write(ssl, d + off, (int)(n - off > (1 << 20) ? (1 << 20) : n - off)); if (w > 0) { off += w; continue; }
      int e = tl.SSL_get_error(ssl, w);
      if (e == 2 || e == 3) { struct pollfd p = {fd, e == 2 ? POLLIN : POLLOUT, 0}; poll(&p, 1, 30000); continue; }
      return -1;
    }
    ssize_t w = send(fd, d + off, n - off, MSG_NOSIGNAL);
    if (w < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) { struct pollfd p = {fd, POLLOUT, 0}; poll(&p, 1, 30000); continue; }
      return -1;
    }
    off += w;
  }
  return 0;
}
static int rb_fill(int fd) { char tmp[16384]; ssize_t r = conn_read(fd, tmp, sizeof tmp); if (r > 0) buf_add(&rbuf[fd], tmp, (int)r); return (int)r; }
static void rb_consume(int fd, int n) { Buf *b = &rbuf[fd]; memmove(b->p, b->p + n, b->n - n); b->n -= n; b->p[b->n] = 0; }
static int rb_getline(int fd, Buf *out) {
  for (;;) {
    Buf *b = &rbuf[fd]; char *nl = b->n ? memchr(b->p, '\n', b->n) : NULL;
    if (nl) { int len = (int)(nl - b->p), l2 = len; if (l2 > 0 && b->p[l2 - 1] == '\r') l2--; buf_add(out, b->p, l2); rb_consume(fd, len + 1); return 1; }
    if (b->n > (1 << 22)) return -1;
    int r = rb_fill(fd);
    if (r == 0) { b = &rbuf[fd]; if (b->n > 0) { buf_add(out, b->p, b->n); rb_consume(fd, b->n); return 1; } return 0; }
    if (r < 0) return r;
  }
}
static int rb_exact(int fd, int n, Buf *out) {
  for (;;) {
    Buf *b = &rbuf[fd];
    if (b->n >= n) { buf_add(out, b->p, n); rb_consume(fd, n); return 1; }
    int r = rb_fill(fd); if (r <= 0) return r;
  }
}
static int tcp_connect(const char *host, int port, int tmo, char *err, int es) {
  if (!host_allowed(host)) { snprintf(err, es, "الصلاحية غير ممنوحة: شبكة (المضيف %s)", host); return -1; }
  struct addrinfo hints; memset(&hints, 0, sizeof hints); hints.ai_socktype = SOCK_STREAM; struct addrinfo *res, *ai; char ps[16]; snprintf(ps, sizeof ps, "%d", port);
  int g = getaddrinfo(host, ps, &hints, &res);
  if (g) { snprintf(err, es, "تعذّر حل العنوان '%s': %s", host, gai_strerror(g)); return -1; }
  int fd = -1;
  for (ai = res; ai; ai = ai->ai_next) {
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol); if (fd < 0) continue;
    int fl = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int r = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (r < 0 && errno == EINPROGRESS) {
      struct pollfd p = {fd, POLLOUT, 0}; int pr = poll(&p, 1, tmo < 0 ? 30000 : tmo);
      if (pr > 0) { int e = 0; socklen_t l = sizeof e; getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &l); r = e ? -1 : 0; if (e) errno = e; }
      else { r = -1; if (pr == 0) errno = ETIMEDOUT; }
    }
    if (r == 0) { fcntl(fd, F_SETFL, fl); break; }
    close(fd); fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0) { snprintf(err, es, "تعذّر الاتصال بـ %s:%d (%s)", host, port, strerror(errno)); return -1; }
  if (fd >= MAXFD) { close(fd); snprintf(err, es, "عدد الاتصالات كبير جدا"); return -1; }
  int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one); fd_setup(fd); return fd;
}
static bool tls_wrap_client(int fd, const char *host, bool verify, char *err, int es) {
  if (!tls_load()) { snprintf(err, es, "التشفير غير متاح: لم أجد libssl — %s", TLS_HINT); return false; }
  void *ctx = get_ctx(verify); void *ssl = ctx ? tl.SSL_new(ctx) : NULL;
  if (!ssl) { snprintf(err, es, "تعذّر إنشاء جلسة TLS"); return false; }
  tl.SSL_set_fd(ssl, FDSYS(fd)); tl.SSL_ctrl(ssl, 55, 0, (void *)host);
  if (verify) { if (!tl.SSL_set1_host) { tl.SSL_free(ssl); snprintf(err, es, "نسخة libssl لا تدعم التحقق من اسم المضيف"); return false; } tl.SSL_set1_host(ssl, host); }
  if (tl.SSL_connect(ssl) != 1) { tl.SSL_free(ssl); snprintf(err, es, "فشلت مصافحة TLS مع %s (الشهادة غير موثوقة؟ جرّب {\"تحقق\": غلط})", host); return false; }
  ssl_of[fd] = ssl; return true;
}
static int listen_on(int port, const char *addr, const char *cert, const char *key, char *err, int es) {
  if (!perm_has(PM_NET)) { snprintf(err, es, "الصلاحية غير ممنوحة: شبكة (فتح منفذ)"); return -1; }
  int fd = -1; int one = 1, zero = 0, berr = 0;
  if (!addr || strchr(addr, ':')) {
    fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd >= 0) {
      setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one); setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof zero);
      struct sockaddr_in6 s6; memset(&s6, 0, sizeof s6); s6.sin6_family = AF_INET6; s6.sin6_port = htons((uint16_t)port);
      if (addr) inet_pton(AF_INET6, addr, &s6.sin6_addr); else s6.sin6_addr = in6addr_any;
      if (bind(fd, (struct sockaddr *)&s6, sizeof s6) < 0 || listen(fd, 128) < 0) { berr = errno; close(fd); fd = -1; }
    }
  }
  if (fd < 0 && (!addr || !strchr(addr, ':'))) {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
      setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
      struct sockaddr_in sa; memset(&sa, 0, sizeof sa); sa.sin_family = AF_INET; sa.sin_port = htons((uint16_t)port);
      if (!addr || !inet_pton(AF_INET, addr, &sa.sin_addr)) sa.sin_addr.s_addr = INADDR_ANY;
      if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0 || listen(fd, 128) < 0) { berr = errno; close(fd); fd = -1; }
    }
  }
  if (fd < 0) { snprintf(err, es, "تعذّر الاستماع على المنفذ %d (%s)", port, strerror(berr ? berr : errno)); return -1; }
  if (fd >= MAXFD) { close(fd); snprintf(err, es, "عدد الاتصالات كبير جدا"); return -1; }
  fd_setup(fd);
  if (cert && key) {
    if (!tls_load()) { snprintf(err, es, "التشفير غير متاح: لم أجد libssl — %s", TLS_HINT); close(fd); return -1; }
    void *ctx = tl.SSL_CTX_new(tl.TLS_server_method());
    if (!ctx || tl.SSL_CTX_use_certificate_chain_file(ctx, cert) != 1 || tl.SSL_CTX_use_PrivateKey_file(ctx, key, 1) != 1) { snprintf(err, es, "تعذّر تحميل الشهادة/المفتاح"); close(fd); return -1; }
    srv_ctx[fd] = ctx;
  }
  return fd;
}

static int accept_one(int lfd, char *ip, int ipsz) {
  if (tmo_ms[lfd] >= 0) { struct pollfd p = {lfd, POLLIN, 0}; int pr = poll(&p, 1, tmo_ms[lfd]); if (pr == 0) return -2; if (pr < 0) return -1; }
  struct sockaddr_storage ss; socklen_t cl = sizeof ss; int c = accept(lfd, (struct sockaddr *)&ss, &cl);
  if (c < 0) return (errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK) ? -2 : -1;
  if (c >= MAXFD) { close(c); return -1; }
  fd_setup(c); int one = 1; setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  if (ss.ss_family == AF_INET6) {
    struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)&ss;
    if (IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr)) inet_ntop(AF_INET, &s6->sin6_addr.s6_addr[12], ip, ipsz); else inet_ntop(AF_INET6, &s6->sin6_addr, ip, ipsz);
  } else inet_ntop(AF_INET, &((struct sockaddr_in *)&ss)->sin_addr, ip, ipsz);
  if (srv_ctx[lfd]) {
    void *ssl = tl.SSL_new(srv_ctx[lfd]); tl.SSL_set_fd(ssl, FDSYS(c));
    if (tl.SSL_accept(ssl) != 1) { tl.SSL_free(ssl); close(c); fd_setup(c); return -2; }
    ssl_of[c] = ssl;
  }
  return c;
}
static bool getfd(Value v, int *fd) { if (!IS_NUM(v) || v.as.n < 0 || v.as.n >= MAXFD || v.as.n != floor(v.as.n)) return false; *fd = (int)v.as.n; return true; }
#define FDA(i, var) int var; do { if (!getfd(a[i], &var)) FAIL("المعامل %d يجب أن يكون مقبض اتصال صالحا", (i) + 1); } while (0)
static Value optv(int argc, Value *a, int i, const char *key) { Value v = NIL; if (argc > i && IS_T(a[i], O_MAP)) mget(&AS_MAP(a[i])->d, KDYN(key), &v); return v; }

NAT(sk_connect) {
  STRA(0); NUMA(1); Value t = optv(argc, a, 2, "تشفير"), tm = optv(argc, a, 2, "مهلة"), vf = optv(argc, a, 2, "تحقق");
  int tmo = IS_NUM(tm) ? (int)(tm.as.n * 1000) : 10000; char err[300];
  int fd = tcp_connect(AS_STR(a[0])->chars, (int)a[1].as.n, tmo, err, sizeof err); if (fd < 0) FAIL("%s", err);
  if (!IS_NIL(t) && !falsey(t) && !tls_wrap_client(fd, AS_STR(a[0])->chars, IS_NIL(vf) || !falsey(vf), err, sizeof err)) { conn_close(fd); FAIL("%s", err); }
  if (IS_NUM(tm)) tmo_ms[fd] = tmo;
  *res = NUMV(fd); return true;
}
NAT(sk_listen) {
  NUMA(0); Value ad = optv(argc, a, 1, "عنوان"), ce = optv(argc, a, 1, "شهادة"), ke = optv(argc, a, 1, "مفتاح"); char err[300];
  int fd = listen_on((int)a[0].as.n, IS_T(ad, O_STR) ? AS_STR(ad)->chars : NULL, IS_T(ce, O_STR) ? AS_STR(ce)->chars : NULL, IS_T(ke, O_STR) ? AS_STR(ke)->chars : NULL, err, sizeof err);
  if (fd < 0) FAIL("%s", err);
  *res = NUMV(fd); return true;
}
NAT(sk_accept) {
  FDA(0, l); char ip[64]; int c = accept_one(l, ip, sizeof ip);
  if (c == -2) { *res = NIL; return true; } if (c < 0) FAIL("فشل قبول الاتصال");
  List *r = new_list(); varr_push(&r->a, NUMV(c)); varr_push(&r->a, mkstr(ip, (int)strlen(ip))); *res = OBJV(r); return true;
}
NAT(sk_send) {
  FDA(0, fd); Str *s = tostr(a[1]); if (conn_write(fd, s->chars, s->len) < 0) FAIL("فشل الإرسال (الاتصال مغلق؟)");
  *res = NUMV(s->len); return true;
}
NAT(sk_recv) {
  FDA(0, fd); int mx = argc > 1 && IS_NUM(a[1]) ? (int)a[1].as.n : 65536; if (mx < 1) mx = 1; if (mx > (1 << 24)) mx = 1 << 24;
  Buf *b = &rbuf[fd];
  if (b->n == 0) { int r = rb_fill(fd); if (r == 0) { *res = NIL; return true; } if (r == -2) { *res = BOOLV(false); return true; } if (r < 0) { *res = NIL; return true; } b = &rbuf[fd]; }
  int n = b->n < mx ? b->n : mx; *res = mkstr(b->p, n); rb_consume(fd, n); return true;
}
NAT(sk_exact) {
  FDA(0, fd); NUMA(1); Buf out; buf_init(&out); int r = rb_exact(fd, (int)a[1].as.n, &out);
  if (r == 1) *res = mkstr(buf_cstr(&out), out.n); else if (r == -2) *res = BOOLV(false); else *res = NIL;
  free(out.p); return true;
}
NAT(sk_line) {
  FDA(0, fd); Buf out; buf_init(&out); int r = rb_getline(fd, &out);
  if (r == 1) *res = mkstr(buf_cstr(&out), out.n); else if (r == -2) *res = BOOLV(false); else *res = NIL;
  free(out.p); return true;
}
NAT(sk_close) { FDA(0, fd); conn_close(fd); *res = NIL; return true; }
NAT(sk_timeout) { FDA(0, fd); tmo_ms[fd] = (IS_NUM(a[1]) && a[1].as.n >= 0) ? (int)(a[1].as.n * 1000) : -1; *res = NIL; return true; }
NAT(sk_nonblock) {
  FDA(0, fd); int fl = fcntl(fd, F_GETFL, 0); bool on = argc < 2 || !falsey(a[1]);
  fcntl(fd, F_SETFL, on ? fl | O_NONBLOCK : fl & ~O_NONBLOCK); if (on) fflag[fd] |= 4; else fflag[fd] &= ~4; *res = NIL; return true;
}
NAT(sk_wait) {
  if (!IS_T(a[0], O_LIST)) FAIL("انتظر تتطلب قائمة مقابض"); List *l = AS_LIST(a[0]);
  struct pollfd pf[256]; int n = 0, buffered = 0;
  for (int i = 0; i < l->a.n && n < 256; i++) { int fd; if (!getfd(l->a.v[i], &fd)) continue; pf[n].fd = fd; pf[n].events = POLLIN; pf[n].revents = 0; if (fd_buffered(fd)) buffered++; n++; }
  int tmo = buffered ? 0 : (argc > 1 && IS_NUM(a[1]) && a[1].as.n >= 0 ? (int)(a[1].as.n * 1000) : -1);
  int pr; do pr = poll(pf, n, tmo); while (pr < 0 && errno == EINTR);
  List *o = new_list(); *res = OBJV(o);
  for (int i = 0; i < n; i++) if ((pr > 0 && (pf[i].revents & (POLLIN | POLLHUP | POLLERR))) || fd_buffered(pf[i].fd)) varr_push(&o->a, NUMV(pf[i].fd));
  return true;
}
NAT(sk_resolve) {
  STRA(0); struct addrinfo h, *r; memset(&h, 0, sizeof h); h.ai_socktype = SOCK_STREAM; h.ai_family = AF_INET;
  if (getaddrinfo(AS_STR(a[0])->chars, NULL, &h, &r)) FAIL("تعذّر حل العنوان"); char ip[64]; inet_ntop(AF_INET, &((struct sockaddr_in *)r->ai_addr)->sin_addr, ip, sizeof ip);
  freeaddrinfo(r); *res = mkstr(ip, (int)strlen(ip)); return true;
}
NAT(sk_udp) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0); if (fd < 0 || fd >= MAXFD) FAIL("تعذّر إنشاء UDP"); fd_setup(fd);
  if (argc > 0 && IS_NUM(a[0])) {
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa); sa.sin_family = AF_INET; sa.sin_port = htons((uint16_t)a[0].as.n); sa.sin_addr.s_addr = INADDR_ANY;
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { close(fd); fd_setup(fd); FAIL("تعذّر ربط المنفذ %d", (int)a[0].as.n); }
  }
  *res = NUMV(fd); return true;
}
NAT(sk_sendto) {
  FDA(0, fd); STRA(1); NUMA(2); Str *s = tostr(a[3]); struct sockaddr_in sa; memset(&sa, 0, sizeof sa); sa.sin_family = AF_INET; sa.sin_port = htons((uint16_t)a[2].as.n);
  if (inet_pton(AF_INET, AS_STR(a[1])->chars, &sa.sin_addr) != 1) {
    struct addrinfo h, *r; memset(&h, 0, sizeof h); h.ai_family = AF_INET; if (getaddrinfo(AS_STR(a[1])->chars, NULL, &h, &r)) FAIL("تعذّر حل العنوان");
    sa.sin_addr = ((struct sockaddr_in *)r->ai_addr)->sin_addr; freeaddrinfo(r);
  }
  ssize_t w = sendto(fd, s->chars, s->len, 0, (struct sockaddr *)&sa, sizeof sa); if (w < 0) FAIL("فشل إرسال UDP");
  *res = NUMV((double)w); return true;
}
NAT(sk_recvfrom) {
  FDA(0, fd); if (tmo_ms[fd] >= 0) { struct pollfd p = {fd, POLLIN, 0}; if (poll(&p, 1, tmo_ms[fd]) <= 0) { *res = NIL; return true; } }
  char *buf = xalloc(65536); struct sockaddr_in sa; socklen_t sl = sizeof sa; ssize_t r = recvfrom(fd, buf, 65536, 0, (struct sockaddr *)&sa, &sl);
  if (r < 0) { free(buf); *res = NIL; return true; }
  char ip[64]; inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof ip); List *l = new_list();
  varr_push(&l->a, mkstr(buf, (int)r)); varr_push(&l->a, mkstr(ip, (int)strlen(ip))); varr_push(&l->a, NUMV(ntohs(sa.sin_port))); free(buf); *res = OBJV(l); return true;
}
