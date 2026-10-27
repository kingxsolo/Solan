static const char *g_lexerr;

static bool is_special_cp(uint32_t c) {
  return c == 0x60C || c == 0x61B || c == 0x61F || c == 0x66B || c == 0xAB || c == 0xBB || c == 0xD7 || c == 0xF7 || c == 0xA0 ||
         c == 0xFEFF || (c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069);
}
static bool is_digit_cp(uint32_t c) { return (c >= '0' && c <= '9') || (c >= 0x660 && c <= 0x669) || (c >= 0x6F0 && c <= 0x6F9); }

static bool ident_start(const char *p) {
  unsigned char c = *p;
  if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  uint32_t cp; u8_decode((const unsigned char *)p, &cp);
  return !is_special_cp(cp) && !is_digit_cp(cp) && cp >= 0x80;
}
static bool ident_part(const char *p) {
  unsigned char c = *p;
  if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c >= '0' && c <= '9');
  uint32_t cp; u8_decode((const unsigned char *)p, &cp);
  return !is_special_cp(cp);
}

static Token mk_tok(TT t, const char *s, int len, int line, bool nl) {
  Token k; k.t = t; k.start = s; k.len = len; k.line = line; k.nl = nl; k.s = NULL; k.num = 0; return k;
}

static void skip_ws(Scanner *s, bool *nl) {
  for (;;) {
    unsigned char c = *s->cur;
    if (c == ' ' || c == '\t' || c == '\r') s->cur++;
    else if (c == '\n') { s->line++; *nl = true; s->cur++; }
    else if (c == '#' && s->cur[1] == '!' && s->cur == s->src) { while (*s->cur && *s->cur != '\n') s->cur++; }
    else if (c == '/' && s->cur[1] == '/') {

      while (*s->cur && *s->cur != '\n') s->cur++;
    } else if (c == '/' && s->cur[1] == '*') {

      s->cur += 2;
      while (*s->cur && !(s->cur[0] == '*' && s->cur[1] == '/')) { if (*s->cur == '\n') { s->line++; *nl = true; } s->cur++; }
      if (*s->cur) s->cur += 2;
    } else if (c >= 0x80) {
      uint32_t cp; int l = u8_decode((const unsigned char *)s->cur, &cp);
      if (cp == 0xA0 || cp == 0xFEFF || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069)) s->cur += l;
      else break;
    } else break;
  }
}

static Token scan_token(void) {
  Scanner *s = &P.sc; bool nl = false;
  skip_ws(s, &nl);
  const char *st = s->cur; int line = s->line;
  unsigned char c = *s->cur;
  if (!c) return mk_tok(T_EOF, st, 0, line, nl);

  uint32_t cp0 = 0; if (c >= 0x80) u8_decode((const unsigned char *)st, &cp0);
  if ((c >= '0' && c <= '9') || (c >= 0x80 && is_digit_cp(cp0))) {
    char buf[128]; int n = 0; bool hex = false;
    if (c == '0' && (s->cur[1] == 'x' || s->cur[1] == 'X')) { hex = true; buf[n++] = '0'; buf[n++] = 'x'; s->cur += 2; }
    for (;;) {
      unsigned char d = *s->cur; uint32_t cp = 0; int l = 1; if (d >= 0x80) l = u8_decode((const unsigned char *)s->cur, &cp);
      if (n > 100) break;
      if ((d >= '0' && d <= '9') || (hex && ((d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F')))) { buf[n++] = d; s->cur++; }
      else if (d >= 0x80 && is_digit_cp(cp)) { buf[n++] = (char)norm_cp(cp); s->cur += l; }
      else if (d == '_') s->cur++;
      else if (!hex && (d == '.' || (d >= 0x80 && cp == 0x66B)) && ((s->cur[l] >= '0' && s->cur[l] <= '9') || (unsigned char)s->cur[l] >= 0xD9)) {
        if ((unsigned char)s->cur[l] >= 0x80) { uint32_t c2; u8_decode((const unsigned char *)s->cur + l, &c2); if (!is_digit_cp(c2)) break; }
        buf[n++] = '.'; s->cur += l;
      }
      else if (!hex && (d == 'e' || d == 'E') && ((s->cur[1] >= '0' && s->cur[1] <= '9') || ((s->cur[1] == '-' || s->cur[1] == '+') && s->cur[2] >= '0' && s->cur[2] <= '9'))) {
        buf[n++] = 'e'; s->cur++; if (*s->cur == '-' || *s->cur == '+') buf[n++] = *s->cur++;
      } else break;
    }
    buf[n] = 0;
    if (!hex) {
      const char *dg = buf; while (*dg == '0') dg++;
      int dl = (int)strlen(dg); bool plain = true; for (int k = 0; k < dl; k++) if (dg[k] < '0' || dg[k] > '9') { plain = false; break; }
      if (plain && (dl > 16 || (dl == 16 && strcmp(dg, "9007199254740992") > 0))) {
        g_lexerr = "الرقم الصحيح أكبر من 9007199254740992 ولا يمكن تمثيله بدقة (الأرقام عشرية مزدوجة)؛ اكتبه نصا واستخدم مكتبة ضخم مثل ضخم.جمع(\"..\", \"..\")";
        return mk_tok(T_ERR, st, (int)(s->cur - st), line, nl);
      }
    }
    Token t = mk_tok(T_NUM, st, (int)(s->cur - st), line, nl); t.num = strtod(buf, NULL); return t;
  }
  if (ident_start(st)) {
    while (ident_part(s->cur)) s->cur += u8_charsize(s->cur);
    Token t = mk_tok(T_IDENT, st, (int)(s->cur - st), line, nl);
    t.s = normstr_n(st, t.len);
    Value kv; if (dget(&vm.kw, OBJV(t.s), &kv)) { t.t = (TT)(int)kv.as.n; for (int k = 0; k < P.nsoft; k++) if (P.soft[k] == t.s) { t.t = T_IDENT; break; } }
    return t;
  }

  if (c == '"' || c == '\'' || (c == 0xC2 && (unsigned char)s->cur[1] == 0xAB)) {
    bool guil = c == 0xC2; s->cur += guil ? 2 : 1; const char *cs = s->cur;
    for (;;) {
      if (!*s->cur) { P.prev = mk_tok(T_STR, st, 1, line, nl); cerror_at(&P.prev, "نص غير مغلق"); }
      if (guil) { if ((unsigned char)s->cur[0] == 0xC2 && (unsigned char)s->cur[1] == 0xBB) break; }
      else if (*s->cur == (char)c) break;
      if (*s->cur == '\\' && s->cur[1]) { if (s->cur[1] == '\n') s->line++; s->cur += 2; continue; }
      if (*s->cur == '$' && s->cur[1] == '{') {
        s->cur += 2; int depth = 1;
        while (*s->cur && depth > 0) {
          char ch = *s->cur;
          if (ch == '"' || ch == '\'') { char q = ch; s->cur++; while (*s->cur && *s->cur != q) { if (*s->cur == '\\' && s->cur[1]) s->cur++; if (*s->cur == '\n') s->line++; s->cur++; } if (*s->cur) s->cur++; continue; }
          if (ch == '{') depth++; else if (ch == '}') depth--;
          if (ch == '\n') s->line++;
          s->cur++;
        }
        continue;
      }
      if (*s->cur == '\n') s->line++;
      s->cur++;
    }
    Token t = mk_tok(T_STR, cs, (int)(s->cur - cs), line, nl); s->cur += guil ? 2 : 1; return t;
  }

  if (c >= 0x80) {
    uint32_t cp; int l = u8_decode((const unsigned char *)st, &cp);
    TT tt = T_ERR; int len = l;
    if (cp == 0x60C) tt = T_COMMA; else if (cp == 0x61B) tt = T_SEMI;
    else if (cp == 0x61F) { if ((unsigned char)st[l] == 0xD8 && (unsigned char)st[l + 1] == 0x9F) { tt = T_QQ; len = l * 2; } else if (st[l] == '.' && !isdigit((unsigned char)st[l + 1])) { tt = T_QDOT; len = l + 1; } else tt = T_QUEST; }
    else if (cp == 0xD7) tt = T_STAR; else if (cp == 0xF7) tt = T_SLASH;
    s->cur += len; return mk_tok(tt, st, len, line, nl);
  }
  s->cur++;
#define M2(ch, yes, no) (*s->cur == (ch) ? (s->cur++, (yes)) : (no))
  TT t;
  switch (c) {
    case '(': t = T_LP; break; case ')': t = T_RP; break; case '[': t = T_LB; break; case ']': t = T_RB; break;
    case '{': t = T_LC; break; case '}': t = T_RC; break; case ',': t = T_COMMA; break; case ';': t = T_SEMI; break;
    case ':': t = T_COLON; break;
    case '.': if (s->cur[0] == '.' && s->cur[1] == '.') { s->cur += 2; t = T_ELLIPSIS; } else t = T_DOT; break;
    case '?': if (*s->cur == '?') { s->cur++; t = T_QQ; } else if (*s->cur == '.' && !isdigit((unsigned char)s->cur[1])) { s->cur++; t = T_QDOT; } else t = T_QUEST; break;
    case '+': if (*s->cur == '+') { s->cur++; t = T_INC; } else t = M2('=', T_PLUSEQ, T_PLUS); break;
    case '-': if (*s->cur == '-') { s->cur++; t = T_DEC; } else t = M2('=', T_MINUSEQ, T_MINUS); break;
    case '*': if (*s->cur == '*') { s->cur++; t = M2('=', T_POWEQ, T_POW); } else t = M2('=', T_STAREQ, T_STAR); break;
    case '/': t = M2('=', T_SLASHEQ, T_SLASH); break;
    case '\\': t = M2('=', T_IDIVEQ, T_IDIV); break;
    case '%': t = M2('=', T_PCTEQ, T_PCT); break;
    case '!': t = M2('=', T_NE, T_BANG); break;
    case '=': if (*s->cur == '>') { s->cur++; t = T_ARROW; } else t = M2('=', T_EQEQ, T_EQ); break;
    case '<': if (*s->cur == '<') { s->cur++; t = T_SHL; } else t = M2('=', T_LE, T_LT); break;
    case '>': if (*s->cur == '>') { s->cur++; t = T_SHR; } else t = M2('=', T_GE, T_GT); break;
    case '&': t = M2('&', T_ANDAND, T_BAND); break;
    case '|': t = M2('|', T_OROR, T_BOR); break;
    case '^': t = T_BXOR; break;
    default: t = T_ERR;
  }
  return mk_tok(t, st, (int)(s->cur - st), line, nl);
}
static void advance(void) {
  P.prev = P.cur; P.cur = scan_token();
  if (P.cur.t == T_ERR) { const char *lm = g_lexerr ? g_lexerr : "رمز غير معروف"; g_lexerr = NULL; cerror_at(&P.cur, lm); }
}
static bool check(TT t) { return P.cur.t == t; }
static bool match(TT t) { if (!check(t)) return false; advance(); return true; }
static Str *g_kwc[T_ERR + 1];
static void soft_ident(void) {
  Token *t = &P.cur;
  if (!t->s || t->t == T_IDENT || t->t == T_STR || t->t == T_NUM || t->t == T_EOF || t->t == T_ERR || !kw_canon[t->t]) return;
  if (!g_kwc[t->t]) g_kwc[t->t] = normstr(kw_canon[t->t]);
  if (g_kwc[t->t] == t->s) return;
  if (P.nsoft < 256) P.soft[P.nsoft++] = t->s;
  t->t = T_IDENT;
}
static void consume(TT t, const char *msg) { if (check(t)) { advance(); return; } cerror_at(&P.cur, msg); }
