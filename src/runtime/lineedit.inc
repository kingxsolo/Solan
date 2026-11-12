#if !SOLAN_WIN
#include <termios.h>
#include <sys/ioctl.h>
#endif

#define LE_MAX 262144

enum { LE_NONE = 0, LE_CHAR, LE_ENTER, LE_EOF, LE_CTRLC, LE_LEFT, LE_RIGHT, LE_UP, LE_DOWN, LE_HOME, LE_END, LE_BSP, LE_DEL, LE_WLEFT, LE_WRIGHT, LE_WDEL, LE_CLRALL, LE_CLRSCR, LE_KILL };

typedef struct { char *buf; int n, cap, cur; } LeLine;

static char *le_dup(const char *s, int n) {
  char *p = (char *)xalloc((size_t)n + 1);
  memcpy(p, s, (size_t)n);
  p[n] = 0;
  return p;
}

static int le_u8len(unsigned char c) { return c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1; }

static int le_u8enc(unsigned int cp, char *out) {
  if (cp < 0x80) { out[0] = (char)cp; return 1; }
  if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
  if (cp < 0x10000) { out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
  out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F)); return 4;
}

static int le_cwidth(unsigned int cp) {
  if (cp == 0) return 0;
  if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x064B && cp <= 0x0652) || cp == 0x0670 || (cp >= 0x06D6 && cp <= 0x06ED) || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0x00AD) return 0;
  if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F000 && cp <= 0x1FAFF)) return 2;
  return 1;
}

static int le_cols(const char *s, int n) {
  int w = 0, i = 0;
  while (i < n) {
    if (s[i] == 0x1b) {
      i++;
      if (i < n && s[i] == '[') { i++; while (i < n && !(s[i] >= 0x40 && s[i] <= 0x7E)) i++; if (i < n) i++; }
      else if (i < n) i++;
      continue;
    }
    int b = le_u8len((unsigned char)s[i]);
    if (i + b > n) b = n - i;
    unsigned int cp = 0xFFFD;
    if (b == 1) cp = (unsigned char)s[i];
    else if (b == 2) cp = ((unsigned int)(s[i] & 0x1F) << 6) | (s[i + 1] & 0x3F);
    else if (b == 3) cp = ((unsigned int)(s[i] & 0x0F) << 12) | (((unsigned int)s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
    else if (b == 4) cp = ((unsigned int)(s[i] & 0x07) << 18) | (((unsigned int)s[i + 1] & 0x3F) << 12) | (((unsigned int)s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
    w += le_cwidth(cp);
    i += b;
  }
  return w;
}

static int le_term_width(void) {
#if SOLAN_WIN
  CONSOLE_SCREEN_BUFFER_INFO i;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &i) && i.dwSize.X > 4) return i.dwSize.X;
#else
  struct winsize ws;
  if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 4) return ws.ws_col;
#endif
  return 80;
}

static void le_grow(LeLine *l, int need) {
  if (need <= l->cap) return;
  int c = l->cap ? l->cap : 256;
  while (c < need) c *= 2;
  l->buf = (char *)xrealloc(l->buf, (size_t)c);
  l->cap = c;
}

static void le_left(LeLine *l) {
  if (!l->cur) return;
  int i = l->cur - 1;
  while (i > 0 && ((unsigned char)l->buf[i] & 0xC0) == 0x80) i--;
  l->cur = i;
}

static void le_right(LeLine *l) {
  if (l->cur >= l->n) return;
  int s = le_u8len((unsigned char)l->buf[l->cur]);
  l->cur += s;
  if (l->cur > l->n) l->cur = l->n;
}

static bool le_ws(char c) { return c == ' ' || c == '\t'; }

static void le_wleft(LeLine *l) {
  while (l->cur > 0) { int s = l->cur; le_left(l); if (!le_ws(l->buf[l->cur])) { l->cur = s; break; } }
  while (l->cur > 0) { int s = l->cur; le_left(l); if (le_ws(l->buf[l->cur])) { l->cur = s; break; } }
}

static void le_wright(LeLine *l) {
  while (l->cur < l->n) { le_right(l); if (l->cur < l->n && !le_ws(l->buf[l->cur])) break; }
  while (l->cur < l->n) { int s = l->cur; le_right(l); if (le_ws(l->buf[l->cur])) { l->cur = s; break; } }
}

static void le_ins(LeLine *l, const char *s, int sn) {
  if (l->n + sn > LE_MAX) return;
  le_grow(l, l->n + sn + 1);
  memmove(l->buf + l->cur + sn, l->buf + l->cur, (size_t)(l->n - l->cur));
  memcpy(l->buf + l->cur, s, (size_t)sn);
  l->cur += sn;
  l->n += sn;
  l->buf[l->n] = 0;
}

static void le_bdel(LeLine *l) {
  if (!l->cur) return;
  int s = l->cur;
  le_left(l);
  int d = s - l->cur;
  memmove(l->buf + l->cur, l->buf + s, (size_t)(l->n - s));
  l->n -= d;
  l->buf[l->n] = 0;
}

static void le_fdel(LeLine *l) {
  if (l->cur >= l->n) return;
  int s = le_u8len((unsigned char)l->buf[l->cur]);
  if (l->cur + s > l->n) s = l->n - l->cur;
  memmove(l->buf + l->cur, l->buf + l->cur + s, (size_t)(l->n - l->cur - s));
  l->n -= s;
  l->buf[l->n] = 0;
}

static void le_wdel(LeLine *l) {
  int s = l->cur;
  le_wleft(l);
  int d = s - l->cur;
  if (d <= 0) return;
  memmove(l->buf + l->cur, l->buf + s, (size_t)(l->n - s));
  l->n -= d;
  l->buf[l->n] = 0;
}

static void le_set(LeLine *l, const char *s) {
  int n = (int)strlen(s);
  le_grow(l, n + 1);
  memcpy(l->buf, s, (size_t)n);
  l->n = n;
  l->buf[l->n] = 0;
  l->cur = n;
}

#define LE_HIST_KEEP 500
#define LE_HIST_TRIM 1000
static char **le_hist;
static int le_hn, le_hcap;

static const char *le_hist_path(void) {
  static char p[1024];
  const char *h = getenv("HOME");
  if (!h || !*h || strlen(h) + 20 >= sizeof p) return NULL;
  snprintf(p, sizeof p, "%s/.solan_history", h);
  return p;
}

static void le_hpush(const char *s) {
  if (le_hn && !strcmp(le_hist[le_hn - 1], s)) return;
  if (le_hn == le_hcap) { le_hcap = le_hcap ? le_hcap * 2 : 64; le_hist = (char **)xrealloc(le_hist, sizeof(char *) * (size_t)le_hcap); }
  le_hist[le_hn++] = le_dup(s, (int)strlen(s));
}

static void le_hload(void) {
  if (le_hist) return;
  le_hcap = 64;
  le_hist = (char **)xalloc(sizeof(char *) * (size_t)le_hcap);
  le_hn = 0;
  const char *p = le_hist_path();
  if (!p) return;
  FILE *f = fopen(p, "rb");
  if (!f) return;
  char line[8192];
  while (fgets(line, sizeof line, f)) {
    int n = (int)strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if (n) le_hpush(line);
  }
  fclose(f);
}

static void le_htrim(void) {
  if (le_hn < LE_HIST_TRIM) return;
  int start = le_hn - LE_HIST_KEEP;
  for (int i = 0; i < start; i++) free(le_hist[i]);
  memmove(le_hist, le_hist + start, sizeof(char *) * (size_t)(le_hn - start));
  le_hn -= start;
  const char *p = le_hist_path();
  if (!p) return;
  FILE *f = fopen(p, "wb");
  if (!f) return;
  for (int i = 0; i < le_hn; i++) fprintf(f, "%s\n", le_hist[i]);
  fclose(f);
}

static void le_history_add(const char *s) {
  if (!s || !*s) return;
  le_hload();
  int had = le_hn;
  le_hpush(s);
  if (le_hn == had) return;
  const char *p = le_hist_path();
  if (p) {
    FILE *f = fopen(p, "ab");
    if (f) { fprintf(f, "%s\n", s); fclose(f); }
  }
  le_htrim();
}

static int le_prev_total = 0;
static int le_prev_atend = 0;

static void le_render(const char *prompt, LeLine *l) {
  int w = le_term_width();
  if (le_prev_atend && le_prev_total > 1) {
    int up = (le_prev_total - 1) / w;
    if (up > 0) fprintf(stdout, "\x1b[%dA", up);
  }
  fputs("\r", stdout);
  fputs(prompt, stdout);
  fwrite(l->buf, 1, (size_t)l->cur, stdout);
  fputs("\x1b[s", stdout);
  fwrite(l->buf + l->cur, 1, (size_t)(l->n - l->cur), stdout);
  fputs("\x1b[J", stdout);
  if (l->cur < l->n) fputs("\x1b[u", stdout);
  fflush(stdout);
  le_prev_total = le_cols(prompt, (int)strlen(prompt)) + le_cols(l->buf, l->n);
  le_prev_atend = (l->cur == l->n);
}

#if SOLAN_WIN

static HANDLE le_hin = NULL;
static DWORD le_oldmode = 0;
static int le_have_mode = 0;
static int le_inited = 0;
static int le_hisur = 0;

static int le_key(char *out, int *outn) {
  if (!le_inited) {
    le_inited = 1;
    le_hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD m;
    if (GetConsoleMode(le_hin, &m)) { le_oldmode = m; le_have_mode = 1; SetConsoleMode(le_hin, 0); }
    else le_hin = NULL;
  }
  for (;;) {
    INPUT_RECORD r;
    DWORD got = 0;
    if (!le_hin || !ReadConsoleInputW(le_hin, &r, 1, &got) || got != 1) return LE_EOF;
    if (r.EventType != KEY_EVENT || !r.Event.KeyEvent.bKeyDown) continue;
    KEY_EVENT_RECORD *k = &r.Event.KeyEvent;
    WCHAR u = k->uChar.UnicodeChar;
    WORD vk = k->wVirtualKeyCode;
    bool ctrl = (k->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    if (!u) {
      switch (vk) {
        case VK_LEFT: return ctrl ? LE_WLEFT : LE_LEFT;
        case VK_RIGHT: return ctrl ? LE_WRIGHT : LE_RIGHT;
        case VK_UP: return LE_UP;
        case VK_DOWN: return LE_DOWN;
        case VK_HOME: return LE_HOME;
        case VK_END: return LE_END;
        case VK_DELETE: return LE_DEL;
        default: continue;
      }
    }
    if (u == L'\r' || u == L'\n') return LE_ENTER;
    if (u == 8) return LE_BSP;
    if (u == 127) return LE_WDEL;
    if (u == 27) return LE_CLRALL;
    if (u < 32) {
      switch (u) {
        case 3: return LE_CTRLC;
        case 4: return LE_EOF;
        case 1: return LE_HOME;
        case 5: return LE_END;
        case 2: return LE_LEFT;
        case 6: return LE_RIGHT;
        case 21: return LE_CLRALL;
        case 12: return LE_CLRSCR;
        case 23: return LE_WDEL;
        case 11: return LE_KILL;
        default: continue;
      }
    }
    if (u >= 0xD800 && u < 0xDC00) { le_hisur = u; continue; }
    unsigned int cp = u;
    if (le_hisur) {
      if (u >= 0xDC00 && u < 0xE000) cp = 0x10000 + (((unsigned int)le_hisur - 0xD800) << 10) + (u - 0xDC00);
      else { *outn = le_u8enc(0xFFFD, out); le_hisur = 0; return LE_CHAR; }
      le_hisur = 0;
    }
    *outn = le_u8enc(cp, out);
    return LE_CHAR;
  }
}

static void le_restore(void) {
  DWORD m;
  if (le_have_mode && GetConsoleMode(le_hin, &m)) SetConsoleMode(le_hin, le_oldmode);
}

#else

static struct termios le_saved;
static int le_raw_on = 0;

static void le_raw(void) {
  if (le_raw_on || !isatty(0)) return;
  struct termios t;
  if (tcgetattr(0, &t) != 0) return;
  le_saved = t;
  t.c_lflag &= ~(unsigned)(ECHO | ICANON | ISIG | IEXTEN);
  t.c_iflag &= ~(unsigned)(IXON | ICRNL | INLCR | BRKINT);
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  tcsetattr(0, TCSANOW, &t);
  le_raw_on = 1;
}

static void le_restore(void) {
  if (le_raw_on) { tcsetattr(0, TCSANOW, &le_saved); le_raw_on = 0; }
}

static int le_esc_seq(char *out, int *outn) {
  unsigned char b[16];
  int n = 0;
  while (n < 16) {
    if (read(0, b + n, 1) != 1) return LE_NONE;
    if (b[n] >= 0x40 && b[n] <= 0x7E) { n++; break; }
    n++;
  }
  char final = (char)b[n - 1];
  bool ctrl5 = false;
  for (int i = 0; i < n - 1; i++) if (b[i] == '5') ctrl5 = true;
  if (final == 'A') return LE_UP;
  if (final == 'B') return LE_DOWN;
  if (final == 'C') return ctrl5 ? LE_WRIGHT : LE_RIGHT;
  if (final == 'D') return ctrl5 ? LE_WLEFT : LE_LEFT;
  if (final == 'H') return LE_HOME;
  if (final == 'F') return LE_END;
  if (final == '~') {
    int d = -1;
    for (int i = 0; i < n - 1; i++) if (b[i] >= '0' && b[i] <= '9') { d = b[i] - '0'; break; }
    if (d == 1 || d == 7) return LE_HOME;
    if (d == 3) return LE_DEL;
    if (d == 4 || d == 8) return LE_END;
  }
  return LE_NONE;
}

static int le_key(char *out, int *outn) {
  le_raw();
  unsigned char c;
  if (read(0, &c, 1) != 1) return LE_EOF;
  if (c == 127 || c == 8) return LE_BSP;
  if (c == '\r' || c == '\n') return LE_ENTER;
  if (c == 27) return le_esc_seq(out, outn);
  if (c == 3) return LE_CTRLC;
  if (c == 4) return LE_EOF;
  if (c == 1) return LE_HOME;
  if (c == 5) return LE_END;
  if (c == 2) return LE_LEFT;
  if (c == 6) return LE_RIGHT;
  if (c == 21) return LE_CLRALL;
  if (c == 12) return LE_CLRSCR;
  if (c == 23) return LE_WDEL;
  if (c == 11) return LE_KILL;
  if (c == 9) { out[0] = '\t'; *outn = 1; return LE_CHAR; }
  if (c < 32) return LE_NONE;
  int need = le_u8len(c) - 1;
  out[0] = (char)c;
  for (int i = 1; i <= need; i++) {
    unsigned char d;
    if (read(0, &d, 1) != 1) break;
    out[i] = (char)d;
  }
  *outn = need + 1;
  return LE_CHAR;
}

#endif

static char *le_readline(const char *prompt) {
  le_hload();
  LeLine l;
  memset(&l, 0, sizeof l);
  char *draft = NULL;
  int hp = le_hn;
  le_prev_total = 0;
  le_prev_atend = 0;
  le_render(prompt, &l);
  char *result = NULL;
  for (;;) {
    char ch[8];
    int cn = 0;
    int k = le_key(ch, &cn);
    bool dirty = false;
    switch (k) {
      case LE_NONE: continue;
      case LE_CHAR: le_ins(&l, ch, cn); dirty = true; break;
      case LE_ENTER: fputs("\r\n", stdout); fflush(stdout); result = le_dup(l.buf ? l.buf : "", l.n); goto done;
      case LE_BSP: if (l.cur) { le_bdel(&l); dirty = true; } break;
      case LE_DEL: if (l.cur < l.n) { le_fdel(&l); dirty = true; } break;
      case LE_LEFT: if (l.cur) { le_left(&l); dirty = true; } break;
      case LE_RIGHT: if (l.cur < l.n) { le_right(&l); dirty = true; } break;
      case LE_HOME: if (l.cur) { l.cur = 0; dirty = true; } break;
      case LE_END: if (l.cur != l.n) { l.cur = l.n; dirty = true; } break;
      case LE_WLEFT: if (l.cur) { le_wleft(&l); dirty = true; } break;
      case LE_WRIGHT: if (l.cur < l.n) { le_wright(&l); dirty = true; } break;
      case LE_WDEL: if (l.cur < l.n) { le_wdel(&l); dirty = true; } break;
      case LE_KILL: if (l.cur < l.n) { l.n = l.cur; l.buf[l.n] = 0; dirty = true; } break;
      case LE_CLRALL: if (l.n) { l.n = 0; l.cur = 0; l.buf[0] = 0; dirty = true; } break;
      case LE_CLRSCR: fputs("\x1b[2J\x1b[H", stdout); fflush(stdout); break;
      case LE_UP:
        if (hp > 0) {
          if (hp == le_hn) { free(draft); draft = le_dup(l.buf ? l.buf : "", l.n); }
          hp--;
          le_set(&l, le_hist[hp]);
          dirty = true;
        }
        break;
      case LE_DOWN:
        if (hp < le_hn) {
          hp++;
          if (hp == le_hn) { if (draft) le_set(&l, draft); else { l.n = 0; l.cur = 0; if (l.buf) l.buf[0] = 0; } }
          else le_set(&l, le_hist[hp]);
          dirty = true;
        }
        break;
      case LE_CTRLC:
        fputs("^C\r\n", stdout);
        fflush(stdout);
        result = le_dup("", 0);
        goto done;
      case LE_EOF:
        if (l.n == 0) { fputs("\r\n", stdout); fflush(stdout); goto done; }
        le_fdel(&l);
        dirty = true;
        break;
      default: break;
    }
    if (dirty) le_render(prompt, &l);
  }
done:
  free(draft);
  free(l.buf);
  le_restore();
  return result;
}

static void le_cleanup(void) {
  for (int i = 0; i < le_hn; i++) free(le_hist[i]);
  free(le_hist);
  le_hist = NULL;
  le_hn = le_hcap = 0;
}
