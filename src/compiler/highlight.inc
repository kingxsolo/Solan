static bool g_color_off = false;
static bool use_color(int fd) { return !g_color_off && isatty(fd) && !getenv("NO_COLOR"); }
enum { H_PLAIN, H_COMMENT, H_STRING, H_NUMBER, H_CTRL, H_DECL, H_LIT, H_OPW, H_IMPORT, H_BUILTIN, H_MODULE, H_CALL, H_CLASS, H_SELF, H_PUNCT, H_INTERP, H_COUNT };
static const char *HL_ANSI[H_COUNT] = {"", "\x1b[3;38;5;244m", "\x1b[38;5;114m", "\x1b[38;5;209m", "\x1b[1;38;5;141m", "\x1b[1;38;5;75m", "\x1b[38;5;215m", "\x1b[38;5;176m", "\x1b[38;5;139m", "\x1b[38;5;80m", "\x1b[1;38;5;79m", "\x1b[38;5;222m", "\x1b[1;4;38;5;221m", "\x1b[3;38;5;183m", "\x1b[38;5;246m", "\x1b[1;38;5;216m"};
static const char *HL_NAME[H_COUNT] = {"p", "cm", "st", "nu", "kc", "kd", "kl", "ko", "ki", "bi", "mo", "fn", "cl", "se", "pu", "in"};
static void hl_put(Buf *o, int cls, const char *s, int n, bool html) {
  if (n <= 0) return;
  if (html) {
    if (cls != H_PLAIN) buf_fmt(o, "<span class=\"%s\">", HL_NAME[cls]);
    for (int i = 0; i < n; i++) { char c = s[i]; if (c == '<') buf_add(o, "&lt;", 4); else if (c == '>') buf_add(o, "&gt;", 4); else if (c == '&') buf_add(o, "&amp;", 5); else buf_addc(o, c); }
    if (cls != H_PLAIN) buf_add(o, "</span>", 7);
  } else if (cls == H_PLAIN) buf_add(o, s, n);
  else { buf_add(o, HL_ANSI[cls], (int)strlen(HL_ANSI[cls])); buf_add(o, s, n); buf_add(o, "\x1b[0m", 4); }
}
static int hl_kw_class(TT t) {
  switch (t) {
    case T_IF: case T_ELSE: case T_ELIF: case T_WHILE: case T_FOR: case T_REPEAT: case T_BREAK: case T_CONT: case T_RET: case T_TRY: case T_CATCH:
    case T_FINALLY: case T_THROW: case T_SWITCH: case T_AWAIT: case T_YIELD: return H_CTRL;
    case T_LET: case T_CONST: case T_FUN: case T_CLASS: case T_EXTENDS: case T_ASYNC: case T_GEN: return H_DECL;
    case T_TRUE: case T_FALSE: case T_NIL: return H_LIT;
    case T_THIS: case T_SUPER: return H_SELF;
    case T_AND: case T_OR: case T_NOT: case T_IN: return H_OPW;
    case T_IMPORT: case T_AS: return H_IMPORT;
    default: return H_PLAIN;
  }
}
static void hl_range(const char *s, int n, Buf *o, bool html, int depth) {
  int i = 0; bool after_class = false;
  if (depth == 0 && n >= 2 && s[0] == '#' && s[1] == '!') { int j = 0; while (j < n && s[j] != '\n') j++; hl_put(o, H_COMMENT, s, j, html); i = j; }
  while (i < n) {
    unsigned char c = (unsigned char)s[i];
    if (c == '/' && i + 1 < n && (s[i + 1] == '/' || s[i + 1] == '*')) {
      int j;
      if (s[i + 1] == '*') { j = i + 2; while (j + 1 < n && !(s[j] == '*' && s[j + 1] == '/')) j++; j = j + 1 < n ? j + 2 : n; }
      else { j = i; while (j < n && s[j] != '\n') j++; }
      hl_put(o, H_COMMENT, s + i, j - i, html); i = j; continue;
    }
    bool guil = c == 0xC2 && i + 1 < n && (unsigned char)s[i + 1] == 0xAB;
    if (c == '"' || c == '\'' || guil) {
      int j = i + (guil ? 2 : 1), seg = i;
      while (j < n) {
        if (s[j] == '\\' && j + 1 < n) { j += 2; continue; }
        if (guil ? ((unsigned char)s[j] == 0xC2 && j + 1 < n && (unsigned char)s[j + 1] == 0xBB) : (unsigned char)s[j] == c) { j += guil ? 2 : 1; break; }
        if (s[j] == '$' && j + 1 < n && s[j + 1] == '{') {
          hl_put(o, H_STRING, s + seg, j - seg, html); hl_put(o, H_INTERP, "${", 2, html);
          int k = j + 2, d = 1;
          while (k < n && d > 0) {
            if (s[k] == '"' || s[k] == '\'') { char q = s[k++]; while (k < n && s[k] != q) { if (s[k] == '\\') k++; k++; } if (k < n) k++; continue; }
            if (s[k] == '{') d++; else if (s[k] == '}') { d--; if (d == 0) break; } k++;
          }
          hl_range(s + j + 2, k - (j + 2), o, html, depth + 1);
          if (k < n) { hl_put(o, H_INTERP, "}", 1, html); k++; }
          j = k; seg = j; continue;
        }
        j++;
      }
      if (j > n) j = n; hl_put(o, H_STRING, s + seg, j - seg, html); i = j; continue;
    }
    uint32_t cp0 = 0; if (c >= 0x80) u8_decode((const unsigned char *)s + i, &cp0);
    if ((c >= '0' && c <= '9') || (c >= 0x80 && is_digit_cp(cp0))) {
      int j = i; while (j < n) { unsigned char d = (unsigned char)s[j]; uint32_t cp = 0; if (d >= 0x80) u8_decode((const unsigned char *)s + j, &cp);
        if ((d >= '0' && d <= '9') || d == '_' || d == '.' || (d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F') || d == 'x' || (d >= 0x80 && (is_digit_cp(cp) || cp == 0x66B))) j += d >= 0x80 ? u8_charsize(s + j) : 1; else break; }
      if (j > i + 1 && s[j - 1] == '.') j--;
      hl_put(o, H_NUMBER, s + i, j - i, html); i = j; continue;
    }
    if (ident_start(s + i)) {
      int j = i; while (j < n && ident_part(s + j)) j += u8_charsize(s + j);
      Str *w = normstr_n(s + i, j - i); Value v; int cls = H_PLAIN;
      if (dget(&vm.kw, OBJV(w), &v)) { TT t = (TT)(int)v.as.n; cls = hl_kw_class(t); if (t == T_CLASS || t == T_EXTENDS) { hl_put(o, cls, s + i, j - i, html); after_class = true; i = j; continue; } }
      else if (ctx_id(w) >= 1 && ctx_id(w) <= 5) cls = (ctx_id(w) <= 2) ? H_CTRL : H_DECL;
      else if (after_class) cls = H_CLASS;
      else if (dget(&vm.natmods, OBJV(w), &v)) cls = H_MODULE;
      else if (dget(&vm.builtins, OBJV(w), &v)) cls = (IS_T(v, O_MAP)) ? H_MODULE : H_BUILTIN;
      if (cls == H_PLAIN) { int k = j; while (k < n && (s[k] == ' ' || s[k] == '\t')) k++; if (k < n && s[k] == '(') cls = H_CALL; }
      hl_put(o, cls, s + i, j - i, html); after_class = false; i = j; continue;
    }
    if (strchr("+-*/%=<>!&|^?:.,;(){}[]", (int)c) && c) { int j = i; while (j < n && s[j] && strchr("+-*/%=<>!&|^?:.,;(){}[]", (int)(unsigned char)s[j])) j++; hl_put(o, H_PUNCT, s + i, j - i, html); i = j; continue; }
    if (c >= 0x80 && (cp0 == 0x60C || cp0 == 0x61B || cp0 == 0x61F || cp0 == 0xD7 || cp0 == 0xF7)) { int l = u8_charsize(s + i); hl_put(o, H_PUNCT, s + i, l, html); i += l; continue; }
    int l = c >= 0x80 ? u8_charsize(s + i) : 1; hl_put(o, H_PLAIN, s + i, l, html); i += l;
  }
}
static void highlight_to(FILE *f, const char *src, int n, bool html) {
  Buf o; buf_init(&o);
  if (html) {
    buf_fmt(&o, "<!doctype html><html dir=\"rtl\" lang=\"ar\"><head><meta charset=\"utf-8\"><title>solan</title><style>body{background:#1e1e2e;color:#cdd6f4;margin:0;padding:1.2rem}pre{font:15px/1.7 ui-monospace,Menlo,Consolas,monospace;white-space:pre-wrap;tab-size:2}"
      ".cm{color:#808080;font-style:italic}.st{color:#87d787}.nu{color:#ff875f}.kc{color:#af87ff;font-weight:700}.kd{color:#5fafff;font-weight:700}.kl{color:#ffaf5f}.ko{color:#d787d7}.ki{color:#af87af}"
      ".bi{color:#5fd7d7}.mo{color:#5fd7af;font-weight:700}.fn{color:#ffd787}.cl{color:#ffd75f;font-weight:700;text-decoration:underline}.se{color:#d7afff;font-style:italic}.pu{color:#8a8a8a}.in{color:#ffaf87;font-weight:700}</style></head><body><pre>");
  }
  hl_range(src, n, &o, html, 0);
  if (html) buf_fmt(&o, "</pre></body></html>\n");
  fwrite(buf_cstr(&o), 1, o.n, f); free(o.p);
}
static void print_src_line(const char *ls, int len) {
  if (use_color(2)) { Buf o; buf_init(&o); hl_range(ls, len, &o, false, 0); fwrite(buf_cstr(&o), 1, o.n, stderr); free(o.p); } else fwrite(ls, 1, len, stderr);
}
static void print_tok_snippet(Token *t) {
  const char *base = P.sc.src; if (!base || !t->start || t->start < base) return;
  const char *ls = t->start; while (ls > base && ls[-1] != '\n') ls--; const char *le = t->start; while (*le && *le != '\n') le++;
  int col = u8_len(ls, (int)(t->start - ls)); char num[16]; int nw = snprintf(num, sizeof num, "%d", t->line);
  fprintf(stderr, "  %s │ ", num); print_src_line(ls, (int)(le - ls)); fputc('\n', stderr);
  fprintf(stderr, "  %*s │ %*s%s^%s\n", nw, "", col, "", use_color(2) ? "\x1b[1;38;5;203m" : "", use_color(2) ? "\x1b[0m" : "");
}
static void print_snippet_file(const char *file, int line) {
  if (!file || !file[0] || file[0] == '<' || line < 1) return;
  size_t n; char *src = read_file(file, &n); if (!src) return;
  for (int ln = line - 1; ln <= line + 1; ln++) {
    if (ln < 1) continue; const char *p = src; for (int i = 1; i < ln && *p; i++) { const char *nl = strchr(p, '\n'); if (!nl) { p = NULL; break; } p = nl + 1; }
    if (!p || (!*p && ln > line)) continue; const char *e = strchr(p, '\n'); int len = e ? (int)(e - p) : (int)strlen(p);
    fprintf(stderr, "  %s%d │ ", ln == line ? "▶ " : "  ", ln); print_src_line(p, len); fputc('\n', stderr);
  }
  free(src);
}
static int ed_dist(const char *a, const char *b) {
  uint32_t x[48], y[48]; int n = 0, m = 0; uint32_t cp;
  for (const char *p = a; *p && n < 47;) { int l = u8_decode((const unsigned char *)p, &cp); x[n++] = cp; p += l; }
  for (const char *p = b; *p && m < 47;) { int l = u8_decode((const unsigned char *)p, &cp); y[m++] = cp; p += l; }
  if (n >= 47 || m >= 47) return 99;
  int prev[48], cur[48]; for (int j = 0; j <= m; j++) prev[j] = j;
  for (int i = 1; i <= n; i++) { cur[0] = i; for (int j = 1; j <= m; j++) { int c = x[i - 1] == y[j - 1] ? 0 : 1; int v = prev[j - 1] + c; if (prev[j] + 1 < v) v = prev[j] + 1; if (cur[j - 1] + 1 < v) v = cur[j - 1] + 1; cur[j] = v; } memcpy(prev, cur, sizeof(int) * (m + 1)); }
  return prev[m];
}
static void suggest_scan(Dict *d, Str *name, const char **best, int *bd) {
  if (!d) return;
  for (int i = 0; i < d->n; i++) {
    if (!IS_T(d->e[i].k, O_STR)) continue; Str *k = AS_STR(d->e[i].k); if (k->chars[0] == '$' || k->len > 120 || k == name) continue;
    char tmp[260]; norm_into(k->chars, k->len, tmp); int dd = ed_dist(name->chars, tmp);
    if (dd < *bd) { *bd = dd; *best = k->chars; }
  }
}
static void suggest(char *out, int sz, Str *name, Dict *d1, Dict *d2, Dict *d3) {
  out[0] = 0; int L = u8_len(name->chars, name->len); int bd = L <= 3 ? 2 : (L <= 6 ? 3 : 4); const char *best = NULL;
  suggest_scan(d1, name, &best, &bd); suggest_scan(d2, name, &best, &bd); suggest_scan(d3, name, &best, &bd);
  if (best && bd <= (L <= 3 ? 1 : (L <= 6 ? 2 : 3)) && bd < L) snprintf(out, sz, " — هل تقصد '%s'؟", best);
}
static void print_value_colored(Value r) {
  Buf o; buf_init(&o); repr(r, &o, 0, true);
  if (use_color(1)) {
    if (IS_T(r, O_LIST) || IS_T(r, O_MAP)) { Buf c; buf_init(&c); hl_range(buf_cstr(&o), o.n, &c, false, 0); printf("%s\n", buf_cstr(&c)); free(c.p); }
    else { const char *k = IS_NUM(r) ? HL_ANSI[H_NUMBER] : IS_T(r, O_STR) ? HL_ANSI[H_STRING] : (r.t == V_BOOL || IS_NIL(r)) ? HL_ANSI[H_LIT] : HL_ANSI[H_BUILTIN]; printf("%s%s\x1b[0m\n", k, buf_cstr(&o)); }
  } else printf("%s\n", buf_cstr(&o));
  free(o.p);
}
