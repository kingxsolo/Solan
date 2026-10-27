typedef struct { int id; double due, interval; int cb; bool alive; } Timer;
typedef struct { int fd, cb; bool alive; } Watch;
static Timer timers[512]; static int ntm = 0, next_tid = 1; static Watch watches[256]; static int nwt = 0; static bool loop_stop = false; static List *loop_cbs;
static int add_cb(Value f) { varr_push(&loop_cbs->a, f); return loop_cbs->a.n - 1; }
static bool callable(Value v) { return IS_T(v, O_CLO) || IS_T(v, O_NAT) || IS_T(v, O_BOUND); }
static bool add_timer(double secs, double interval, Value f, Value *res) {
  if (!callable(f)) { rt_error("المعامل الثاني يجب أن يكون دالة"); return false; }
  if (ntm == 512) { int k = 0; for (int i = 0; i < ntm; i++) if (timers[i].alive) timers[k++] = timers[i]; ntm = k; if (ntm == 512) { rt_error("مؤقتات كثيرة جدا"); return false; } }
  if (secs < 0) secs = 0; timers[ntm].id = next_tid++; timers[ntm].due = now_sec() + secs; timers[ntm].interval = interval; timers[ntm].cb = add_cb(f); timers[ntm].alive = true;
  *res = NUMV(timers[ntm].id); ntm++; return true;
}
NAT(lp_after) { NUMA(0); return add_timer(a[0].as.n, 0, a[1], res); }
NAT(lp_every) { NUMA(0); if (a[0].as.n <= 0) FAIL("الفاصل يجب أن يكون أكبر من صفر"); return add_timer(a[0].as.n, a[0].as.n, a[1], res); }
NAT(lp_cancel) { NUMA(0); for (int i = 0; i < ntm; i++) if (timers[i].id == (int)a[0].as.n && timers[i].alive) { timers[i].alive = false; loop_cbs->a.v[timers[i].cb] = NIL; } *res = NIL; return true; }
NAT(lp_watch) {
  FDA(0, fd); if (!callable(a[1])) FAIL("المعامل الثاني يجب أن يكون دالة");
  for (int i = 0; i < nwt; i++) if (watches[i].alive && watches[i].fd == fd) { watches[i].cb = add_cb(a[1]); *res = NIL; return true; }
  if (nwt == 256) { int k = 0; for (int i = 0; i < nwt; i++) if (watches[i].alive) watches[k++] = watches[i]; nwt = k; if (nwt == 256) FAIL("مراقبات كثيرة جدا"); }
  watches[nwt].fd = fd; watches[nwt].cb = add_cb(a[1]); watches[nwt].alive = true; nwt++; *res = NIL; return true;
}
NAT(lp_unwatch) { FDA(0, fd); for (int i = 0; i < nwt; i++) if (watches[i].alive && watches[i].fd == fd) watches[i].alive = false; *res = NIL; return true; }
NAT(lp_stop) { loop_stop = true; *res = NIL; return true; }
static int loop_iter(void) {
  double now = now_sec(), next = -1; bool any = false;
  for (int i = 0; i < ntm; i++) if (timers[i].alive) { any = true; if (next < 0 || timers[i].due < next) next = timers[i].due; }
  struct pollfd pf[256]; int map[256], np = 0; bool buffered = false;
  for (int i = 0; i < nwt; i++) if (watches[i].alive) { pf[np].fd = watches[i].fd; pf[np].events = POLLIN; pf[np].revents = 0; map[np++] = i; any = true; if (fd_buffered(watches[i].fd)) buffered = true; }
  if (!any) return 0;
  int tmo; if (buffered) tmo = 0; else if (next < 0) tmo = -1; else { double dd = (next - now) * 1000; tmo = dd <= 0 ? 0 : (int)ceil(dd); }
  int pr = poll(pf, np, tmo); if (pr < 0 && errno != EINTR) { rt_error("فشل poll"); return -1; }
  for (int i = 0; i < np; i++) {
    int wi = map[i]; if (!watches[wi].alive) continue;
    if ((pr > 0 && (pf[i].revents & (POLLIN | POLLHUP | POLLERR))) || fd_buffered(watches[wi].fd)) {
      Value cb = loop_cbs->a.v[watches[wi].cb], arg = NUMV(watches[wi].fd), out; if (!call_fn(cb, 1, &arg, &out)) return -1;
      if (loop_stop) return 1;
    }
  }
  now = now_sec(); int cnt = ntm;
  for (int i = 0; i < cnt && !loop_stop; i++) {
    if (!timers[i].alive || timers[i].due > now) continue;
    Timer t = timers[i]; if (t.interval > 0) timers[i].due = now + t.interval; else timers[i].alive = false;
    Value cb = loop_cbs->a.v[t.cb], out; if (!call_fn(cb, 0, NULL, &out)) return -1;
  }
  return 1;
}
NAT(lp_run) { loop_stop = false; while (!loop_stop) { int r = loop_iter(); if (r < 0) return false; if (r == 0) break; } *res = NIL; return true; }
NAT(lp_step) { int r = loop_iter(); if (r < 0) return false; *res = BOOLV(r > 0); return true; }
NAT(s_bg) { SAFE(); STRA(0); Buf c; buf_init(&c);
#if SOLAN_WIN
  buf_fmt(&c, "start /b \"\" cmd /c \"%s\" >NUL 2>&1", AS_STR(a[0])->chars);
#else
  buf_fmt(&c, "(%s) >/dev/null 2>&1 &", AS_STR(a[0])->chars);
#endif
   int r = system(c.p); free(c.p); *res = BOOLV(r == 0); return true; }
NAT(m_band) { NUMA(0); NUMA(1); *res = NUMV((double)((int64_t)a[0].as.n & (int64_t)a[1].as.n)); return true; }
NAT(m_bor) { NUMA(0); NUMA(1); *res = NUMV((double)((int64_t)a[0].as.n | (int64_t)a[1].as.n)); return true; }
NAT(m_bxor) { NUMA(0); NUMA(1); *res = NUMV((double)((int64_t)a[0].as.n ^ (int64_t)a[1].as.n)); return true; }
NAT(m_shl) { NUMA(0); NUMA(1); *res = NUMV((double)((int64_t)a[0].as.n << ((int)a[1].as.n & 63))); return true; }
NAT(m_shr) { NUMA(0); NUMA(1); *res = NUMV((double)((int64_t)a[0].as.n >> ((int)a[1].as.n & 63))); return true; }
NAT(m_bnot) { NUMA(0); *res = NUMV((double)(~(int64_t)a[0].as.n)); return true; }

static bool do_format(Str *f, int argc, Value *args, Value *res) {
  Buf o; buf_init(&o); int next = 0;
  for (int i = 0; i < f->len; i++) {
    char c = f->chars[i];
    if (c == '}' && i + 1 < f->len && f->chars[i + 1] == '}') { buf_addc(&o, '}'); i++; continue; }
    if (c != '{') { buf_addc(&o, c); continue; }
    if (i + 1 < f->len && f->chars[i + 1] == '{') { buf_addc(&o, '{'); i++; continue; }
    int j = i + 1; while (j < f->len && f->chars[j] != '}') j++;
    if (j >= f->len) { free(o.p); rt_error("نسق: '{' بلا إغلاق"); return false; }
    char field[128]; int fl = j - i - 1; if (fl > 126) fl = 126; memcpy(field, f->chars + i + 1, fl); field[fl] = 0; i = j;
    char *spec = strchr(field, ':'); if (spec) { *spec = 0; spec++; } else spec = field + strlen(field);
    int idx = field[0] ? atoi(field) : next++;
    if (idx < 0 || idx >= argc) { free(o.p); rt_error("نسق: لا يوجد وسيط رقم %d", idx); return false; }
    Value v = args[idx]; char fill = ' ', align = 0; const char *p = spec;
    if (p[0] && (p[1] == '<' || p[1] == '>' || p[1] == '^')) { fill = p[0]; align = p[1]; p += 2; }
    else if (p[0] == '<' || p[0] == '>' || p[0] == '^') { align = p[0]; p++; }
    bool zero = false; if (*p == '0') { zero = true; p++; }
    int width = 0; while (isdigit((unsigned char)*p)) width = width * 10 + (*p++ - '0');
    int prec = -1; if (*p == '.') { p++; prec = 0; while (isdigit((unsigned char)*p)) prec = prec * 10 + (*p++ - '0'); }
    char type = *p; Buf item; buf_init(&item); bool isnum = IS_NUM(v) && type != 's';
    if (isnum) {
      double d = v.as.n;
      if (type == 'd') buf_fmt(&item, "%lld", (long long)llround(d));
      else if (type == 'x') buf_fmt(&item, "%llx", (long long)d);
      else if (type == 'X') buf_fmt(&item, "%llX", (long long)d);
      else if (type == 'b') { unsigned long long u = (unsigned long long)(long long)d; char t[70]; int n = 0; if (!u) t[n++] = '0'; while (u) { t[n++] = '0' + (u & 1); u >>= 1; } while (n) buf_addc(&item, t[--n]); }
      else if (type == 'f' || (prec >= 0 && !type)) buf_fmt(&item, "%.*f", prec < 0 ? 6 : prec, d);
      else if (type == 'e') buf_fmt(&item, "%.*e", prec < 0 ? 6 : prec, d);
      else if (type == '%') buf_fmt(&item, "%.*f%%", prec < 0 ? 0 : prec, d * 100);
      else fmt_num(d, &item);
      if (!align) align = '>';
    } else {
      Str *s = tostr(v); int n = s->len;
      if (prec >= 0) { int off = u8_off(s->chars, s->len, prec); if (off >= 0) n = off; }
      buf_add(&item, s->chars, n); if (!align) align = '<';
    }
    int len = u8_len(buf_cstr(&item), item.n), pad = width - len;
    if (pad > 0) {
      if (zero && isnum) {
        int k = 0; if (item.p[0] == '-' || item.p[0] == '+') { buf_addc(&o, item.p[0]); k = 1; }
        for (int q = 0; q < pad; q++) buf_addc(&o, '0'); buf_add(&o, item.p + k, item.n - k);
      } else {
        int left = align == '>' ? pad : align == '^' ? pad / 2 : 0, right = pad - left;
        for (int q = 0; q < left; q++) buf_addc(&o, fill); buf_add(&o, buf_cstr(&item), item.n); for (int q = 0; q < right; q++) buf_addc(&o, fill);
      }
    } else buf_add(&o, buf_cstr(&item), item.n);
    free(item.p);
  }
  *res = mkstr(buf_cstr(&o), o.n); free(o.p); return true;
}
NAT(n_format) { STRA(0); return do_format(AS_STR(a[0]), argc - 1, a + 1, res); }
NAT(n_enum) { if (!IS_T(a[0], O_MAP)) FAIL("تعداد غير صالح"); AS_MAP(a[0])->module = true; *res = a[0]; return true; }

#define BBASE 1000000000u
