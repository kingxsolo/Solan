enum { RX_CHAR, RX_ANY, RX_CLASS, RX_SPLIT, RX_JMP, RX_SAVE, RX_BOL, RX_EOL, RX_WB, RX_NWB, RX_MATCH, RX_PSAVE, RX_PCHK };
typedef struct { int op, x, y; } RxIns;
typedef struct { int lo, hi; } RxRange;
typedef struct { int pred; uint32_t lo, hi; } RxItem;
typedef struct { bool neg; int n; RxItem *items; } RxClass;
typedef struct { RxIns *ins; int n, cap; RxClass *cls; int ncls; int ncap, nslots; char **names; int nnames; int *nameidx; unsigned flags; int fstate; unsigned char fb[256]; } Rx;
#define RXF_ICASE 1
#define RXF_NORM 2
#define RXF_MULTI 4
#define RXF_DOTALL 8
typedef struct RxNode { int t; uint32_t c; int lo, hi, lazy, idx, cls; struct RxNode *a, *b, *next; } RxNode;
enum { N_CHAR, N_ANY, N_CLASS, N_CAT, N_ALT, N_REP, N_GROUP, N_NOCAP, N_BOL, N_EOL, N_WB, N_NWB, N_EMPTY };
typedef struct { const char *p, *end; Rx *rx; unsigned flags; char err[120]; RxNode **pool; int npool, cappool; } RxParse;

static RxNode *rn_new(RxParse *ps, int t) {
  RxNode *n = xalloc(sizeof(RxNode)); memset(n, 0, sizeof *n); n->t = t;
  if (ps->npool == ps->cappool) { ps->cappool = ps->cappool ? ps->cappool * 2 : 64; ps->pool = xrealloc(ps->pool, sizeof(RxNode *) * ps->cappool); }
  ps->pool[ps->npool++] = n; return n;
}
static uint32_t rx_normcp(uint32_t c, unsigned fl) {
  if (fl & RXF_NORM) c = norm_cp(c);
  if ((fl & RXF_ICASE) && c < 128) c = (uint32_t)tolower((int)c);
  return c;
}
static uint32_t rx_next_cp(RxParse *ps) { uint32_t cp; int l = u8_decode((const unsigned char *)ps->p, &cp); ps->p += l; return cp; }
static RxNode *rx_alt(RxParse *ps);
static bool rx_is_word(uint32_t c) {
  if (c < 128) return isalnum((int)c) || c == '_';
  return !is_special_cp(c) && !(c >= 0x2000 && c <= 0x206F) && !(c >= 0x3000 && c <= 0x303F) && c != 0xD7 && c != 0xF7;
}
static bool rx_is_mark(uint32_t c) {
  return (c >= 0x300 && c <= 0x36F) || (c >= 0x483 && c <= 0x489) || (c >= 0x591 && c <= 0x5BD) || (c >= 0x64B && c <= 0x65F) || c == 0x670 || (c >= 0x6D6 && c <= 0x6DC) || (c >= 0x6DF && c <= 0x6E4) || c == 0x6E7 || c == 0x6E8 || (c >= 0x6EA && c <= 0x6ED);
}
static bool rx_is_punct(uint32_t c) {
  if (c < 128) return c && strchr("!\"#%&'()*,-./:;?@[\\]_{}", (int)c) != NULL;
  return c == 0xA1 || c == 0xA7 || c == 0xAB || c == 0xB6 || c == 0xB7 || c == 0xBB || c == 0xBF || c == 0x60C || c == 0x61B || c == 0x61F || (c >= 0x66A && c <= 0x66D) || c == 0x6D4 || (c >= 0x2010 && c <= 0x2027) || (c >= 0x2030 && c <= 0x205E) || (c >= 0x3001 && c <= 0x3003) || (c >= 0x3008 && c <= 0x3011);
}
static bool rx_is_symbol(uint32_t c) {
  if (c < 128) return c && strchr("$+<=>^`|~", (int)c) != NULL;
  return (c >= 0xA2 && c <= 0xA9 && c != 0xA7) || c == 0xAC || (c >= 0xAE && c <= 0xB1) || c == 0xB4 || c == 0xB8 || c == 0xD7 || c == 0xF7 || (c >= 0x20A0 && c <= 0x20CF) || (c >= 0x2100 && c <= 0x214F) || (c >= 0x2190 && c <= 0x23FF) || (c >= 0x2600 && c <= 0x27BF) || (c >= 0x1F300 && c <= 0x1FAFF);
}
static bool rx_is_number(uint32_t c) { return is_digit_cp(c) || c == 0xB2 || c == 0xB3 || c == 0xB9 || (c >= 0xBC && c <= 0xBE); }
static bool rx_is_letter(uint32_t c) {
  if (c < 128) return isalpha((int)c) != 0;
  return rx_is_word(c) && c != '_' && !rx_is_number(c) && !rx_is_mark(c) && !rx_is_punct(c) && !rx_is_symbol(c);
}
static bool rx_is_upper(uint32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= 0x391 && c <= 0x3A9) || (c >= 0x400 && c <= 0x42F); }
static bool rx_is_lower(uint32_t c) { return (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7) || (c >= 0x3B1 && c <= 0x3C9) || (c >= 0x430 && c <= 0x45F); }
static bool rx_prop_match(int id, uint32_t c) {
  switch (id) {
    case 0: return rx_is_letter(c);
    case 1: return rx_is_upper(c);
    case 2: return rx_is_lower(c);
    case 3: return rx_is_number(c);
    case 4: return rx_is_punct(c);
    case 5: return rx_is_symbol(c);
    case 6: return c == ' ' || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
    case 7: return (c >= 0x600 && c <= 0x6FF && !rx_is_punct(c) && !rx_is_number(c)) || (c >= 0x750 && c <= 0x77F) || (c >= 0x8A0 && c <= 0x8FF) || (c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF);
    case 8: return (c < 128 && isalpha((int)c)) || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7);
    case 9: return c >= 0x400 && c <= 0x52F;
    case 10: return (c >= 0x370 && c <= 0x3FF) || (c >= 0x1F00 && c <= 0x1FFF);
    case 11: return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF);
    case 12: return c >= 0x5D0 && c <= 0x5EA;
    case 13: return c >= 0x590 && c <= 0x5FF;
  }
  return false;
}
static int rx_prop_id(const char *n, int len) {
  static const struct { const char *name; int id; } t[] = {
    {"L", 0}, {"Letter", 0}, {"Alphabetic", 0}, {"Lu", 1}, {"Uppercase", 1}, {"Ll", 2}, {"Lowercase", 2},
    {"N", 3}, {"Nd", 3}, {"Number", 3}, {"Digit", 3}, {"P", 4}, {"Punctuation", 4}, {"S", 5}, {"Symbol", 5},
    {"Z", 6}, {"Zs", 6}, {"Separator", 6}, {"Space", 6}, {"Arabic", 7}, {"Latin", 8}, {"Cyrillic", 9}, {"Greek", 10}, {"Han", 11}, {"Hebrew", 12}};
  for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) if ((int)strlen(t[i].name) == len && memcmp(t[i].name, n, (size_t)len) == 0) return t[i].id;
  return -1;
}
static int rx_parse_prop(RxParse *ps) {
  char e = *ps->p; ps->p++;
  const char *nm; int len;
  if (ps->p < ps->end && *ps->p == '{') {
    const char *q = memchr(ps->p, '}', (size_t)(ps->end - ps->p));
    if (!q) { snprintf(ps->err, sizeof ps->err, "'}' مفقودة في \\p{...}"); return -1; }
    nm = ps->p + 1; len = (int)(q - nm); ps->p = q + 1;
  } else if (ps->p < ps->end) { nm = ps->p; len = 1; ps->p++; }
  else { snprintf(ps->err, sizeof ps->err, "اسم الخاصية مفقود بعد \\p"); return -1; }
  int neg = e == 'P';
  if (len > 0 && nm[0] == '^') { neg = !neg; nm++; len--; }
  int id = rx_prop_id(nm, len);
  if (id < 0) { snprintf(ps->err, sizeof ps->err, "خاصية يونيكود غير معروفة: %.*s", len, nm); return -1; }
  return 100 + id * 2 + neg;
}
static bool rx_pred(int pred, uint32_t c) {
  if (pred >= 100) return rx_prop_match((pred - 100) >> 1, c) != (bool)((pred - 100) & 1);
  switch (pred) {
    case 1: return is_digit_cp(c); case 2: return !is_digit_cp(c);
    case 3: return rx_is_word(c); case 4: return !rx_is_word(c);
    case 5: return c == ' ' || (c >= 9 && c <= 13) || c == 0xA0; case 6: return !(c == ' ' || (c >= 9 && c <= 13) || c == 0xA0);
  }
  return false;
}
static int rx_new_class(RxParse *ps, RxClass *c) { Rx *rx = ps->rx; rx->cls = xrealloc(rx->cls, sizeof(RxClass) * (rx->ncls + 1)); rx->cls[rx->ncls] = *c; return rx->ncls++; }
static int rx_escape_pred(char e) { switch (e) { case 'd': return 1; case 'D': return 2; case 'w': return 3; case 'W': return 4; case 's': return 5; case 'S': return 6; } return 0; }
static uint32_t rx_escape_char(char e) { switch (e) { case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r'; case 'f': return '\f'; case 'v': return '\v'; case '0': return 0; default: return (uint32_t)(unsigned char)e; } }
static RxNode *rx_class(RxParse *ps) {
  RxClass c; c.neg = false; c.n = 0; c.items = NULL; int cap = 0;
  if (ps->p < ps->end && *ps->p == '^') { c.neg = true; ps->p++; }
  bool first = true;
  while (ps->p < ps->end && (*ps->p != ']' || first)) {
    first = false; uint32_t lo; RxItem it; it.pred = 0;
    if (*ps->p == '\\' && ps->p + 1 < ps->end) {
      ps->p++; char e = *ps->p; int pr = rx_escape_pred(e);
      if (e == 'p' || e == 'P') { pr = rx_parse_prop(ps); if (pr < 0) { free(c.items); return NULL; } it.pred = pr; it.lo = it.hi = 0; if (c.n == cap) { cap = cap ? cap * 2 : 8; c.items = xrealloc(c.items, sizeof(RxItem) * cap); } c.items[c.n++] = it; continue; }
      if (pr) { ps->p++; it.pred = pr; it.lo = it.hi = 0; if (c.n == cap) { cap = cap ? cap * 2 : 8; c.items = xrealloc(c.items, sizeof(RxItem) * cap); } c.items[c.n++] = it; continue; }
      if ((unsigned char)e < 128) { lo = rx_escape_char(e); ps->p++; } else lo = rx_next_cp(ps);
    } else lo = rx_next_cp(ps);
    uint32_t hi = lo;
    if (ps->p + 1 < ps->end && *ps->p == '-' && ps->p[1] != ']') {
      ps->p++;
      if (*ps->p == '\\' && ps->p + 1 < ps->end) { ps->p++; hi = rx_escape_char(*ps->p); ps->p++; } else hi = rx_next_cp(ps);
    }
    it.lo = rx_normcp(lo, ps->flags); it.hi = rx_normcp(hi, ps->flags);
    if (c.n == cap) { cap = cap ? cap * 2 : 8; c.items = xrealloc(c.items, sizeof(RxItem) * cap); }
    c.items[c.n++] = it;
    if ((ps->flags & RXF_ICASE) && lo < 128 && hi < 128) { it.lo = (uint32_t)toupper((int)lo); it.hi = (uint32_t)toupper((int)hi); if (c.n == cap) { cap = cap ? cap * 2 : 8; c.items = xrealloc(c.items, sizeof(RxItem) * cap); } c.items[c.n++] = it; }
  }
  if (ps->p >= ps->end) { snprintf(ps->err, sizeof ps->err, "']' مفقودة"); return NULL; }
  ps->p++; RxNode *n = rn_new(ps, N_CLASS); n->cls = rx_new_class(ps, &c); return n;
}
static RxNode *rx_atom(RxParse *ps) {
  if (ps->p >= ps->end) return NULL;
  char ch = *ps->p;
  if (ch == '(') {
    ps->p++; int idx = -1; char name[64]; name[0] = 0;
    if (ps->p + 1 < ps->end && ps->p[0] == '?') {
      if (ps->p[1] == ':') { ps->p += 2; }
      else if (ps->p[1] == '<' || (ps->p[1] == 'P' && ps->p + 2 < ps->end && ps->p[2] == '<')) {
        ps->p += ps->p[1] == 'P' ? 3 : 2; int k = 0; while (ps->p < ps->end && *ps->p != '>' && k < 62) name[k++] = *ps->p++;
        name[k] = 0; if (ps->p < ps->end) ps->p++; idx = ++ps->rx->ncap;
        ps->rx->names = xrealloc(ps->rx->names, sizeof(char *) * (ps->rx->nnames + 1)); ps->rx->nameidx = xrealloc(ps->rx->nameidx, sizeof(int) * (ps->rx->nnames + 1));
        ps->rx->names[ps->rx->nnames] = strdup(name); ps->rx->nameidx[ps->rx->nnames++] = idx;
      } else { snprintf(ps->err, sizeof ps->err, "صيغة مجموعة غير مدعومة"); return NULL; }
    } else idx = ++ps->rx->ncap;
    RxNode *inner = rx_alt(ps); if (!inner) return NULL;
    if (ps->p >= ps->end || *ps->p != ')') { snprintf(ps->err, sizeof ps->err, "')' مفقودة"); return NULL; }
    ps->p++; RxNode *g = rn_new(ps, idx > 0 ? N_GROUP : N_NOCAP); g->idx = idx; g->a = inner; return g;
  }
  if (ch == '[') { ps->p++; return rx_class(ps); }
  if (ch == '.') { ps->p++; return rn_new(ps, N_ANY); }
  if (ch == '^') { ps->p++; return rn_new(ps, N_BOL); }
  if (ch == '$') { ps->p++; return rn_new(ps, N_EOL); }
  if (ch == '\\') {
    if (ps->p + 1 >= ps->end) { snprintf(ps->err, sizeof ps->err, "\\ في النهاية"); return NULL; }
    ps->p++; char e = *ps->p;
    if (e == 'b') { ps->p++; return rn_new(ps, N_WB); } if (e == 'B') { ps->p++; return rn_new(ps, N_NWB); }
    int pr = rx_escape_pred(e);
    if (e == 'p' || e == 'P') { pr = rx_parse_prop(ps); if (pr < 0) return NULL; RxClass c; c.neg = false; c.n = 1; c.items = xalloc(sizeof(RxItem)); c.items[0].pred = pr; c.items[0].lo = c.items[0].hi = 0; RxNode *n = rn_new(ps, N_CLASS); n->cls = rx_new_class(ps, &c); return n; }
    if (pr) { ps->p++; RxClass c; c.neg = false; c.n = 1; c.items = xalloc(sizeof(RxItem)); c.items[0].pred = pr; c.items[0].lo = c.items[0].hi = 0; RxNode *n = rn_new(ps, N_CLASS); n->cls = rx_new_class(ps, &c); return n; }
    RxNode *n = rn_new(ps, N_CHAR);
    if ((unsigned char)e < 128) { n->c = rx_normcp(rx_escape_char(e), ps->flags); ps->p++; } else n->c = rx_normcp(rx_next_cp(ps), ps->flags);
    return n;
  }
  RxNode *n = rn_new(ps, N_CHAR); n->c = rx_normcp(rx_next_cp(ps), ps->flags); return n;
}
static RxNode *rx_quant(RxParse *ps) {
  RxNode *a = rx_atom(ps); if (!a) return NULL;
  for (;;) {
    if (ps->p >= ps->end) break; char q = *ps->p; int lo, hi;
    if (q == '*') { lo = 0; hi = -1; ps->p++; } else if (q == '+') { lo = 1; hi = -1; ps->p++; } else if (q == '?') { lo = 0; hi = 1; ps->p++; }
    else if (q == '{' && ps->p + 1 < ps->end && isdigit((unsigned char)ps->p[1])) {
      const char *s = ps->p + 1; lo = 0; while (s < ps->end && isdigit((unsigned char)*s)) lo = lo * 10 + (*s++ - '0'); hi = lo;
      if (s < ps->end && *s == ',') { s++; if (s < ps->end && isdigit((unsigned char)*s)) { hi = 0; while (s < ps->end && isdigit((unsigned char)*s)) hi = hi * 10 + (*s++ - '0'); } else hi = -1; }
      if (s >= ps->end || *s != '}' || lo > 1000 || hi > 1000) { snprintf(ps->err, sizeof ps->err, "تكرار {n,m} غير صالح"); return NULL; }
      ps->p = s + 1;
    } else break;
    RxNode *r = rn_new(ps, N_REP); r->lo = lo; r->hi = hi; r->a = a; r->lazy = 0;
    if (ps->p < ps->end && *ps->p == '?') { r->lazy = 1; ps->p++; }
    a = r;
  }
  return a;
}
static RxNode *rx_cat(RxParse *ps) {
  RxNode *head = NULL, *tail = NULL;
  while (ps->p < ps->end && *ps->p != '|' && *ps->p != ')') {
    RxNode *q = rx_quant(ps); if (!q) return NULL;
    if (!head) head = tail = q; else { tail->next = q; tail = q; }
  }
  RxNode *c = rn_new(ps, head ? N_CAT : N_EMPTY); c->a = head; return c;
}
static RxNode *rx_alt(RxParse *ps) {
  RxNode *first = rx_cat(ps); if (!first) return NULL;
  if (ps->p >= ps->end || *ps->p != '|') return first;
  RxNode *alt = rn_new(ps, N_ALT); alt->a = first; RxNode *tail = first;
  while (ps->p < ps->end && *ps->p == '|') { ps->p++; RxNode *n = rx_cat(ps); if (!n) return NULL; tail->next = n; tail = n; }
  return alt;
}
static int rx_emit(Rx *rx, int op, int x, int y) {
  if (rx->n == rx->cap) { rx->cap = rx->cap ? rx->cap * 2 : 64; rx->ins = xrealloc(rx->ins, sizeof(RxIns) * rx->cap); }
  rx->ins[rx->n].op = op; rx->ins[rx->n].x = x; rx->ins[rx->n].y = y; return rx->n++;
}
static bool rx_can_empty(RxNode *n) {
  switch (n->t) {
    case N_CHAR: case N_ANY: case N_CLASS: return false;
    case N_CAT: { for (RxNode *c = n->a; c; c = c->next) if (!rx_can_empty(c)) return false; return true; }
    case N_ALT: { for (RxNode *c = n->a; c; c = c->next) if (rx_can_empty(c)) return true; return false; }
    case N_REP: return n->lo == 0 || rx_can_empty(n->a);
    case N_GROUP: case N_NOCAP: return rx_can_empty(n->a);
    default: return true;
  }
}
static bool rx_gen(Rx *rx, RxNode *n) {
  if (rx->n > 60000) return false;
  switch (n->t) {
    case N_CHAR: rx_emit(rx, RX_CHAR, (int)n->c, 0); return true;
    case N_ANY: rx_emit(rx, RX_ANY, 0, 0); return true;
    case N_CLASS: rx_emit(rx, RX_CLASS, n->cls, 0); return true;
    case N_BOL: rx_emit(rx, RX_BOL, 0, 0); return true; case N_EOL: rx_emit(rx, RX_EOL, 0, 0); return true;
    case N_WB: rx_emit(rx, RX_WB, 0, 0); return true; case N_NWB: rx_emit(rx, RX_NWB, 0, 0); return true;
    case N_EMPTY: return true;
    case N_CAT: for (RxNode *c = n->a; c; c = c->next) if (!rx_gen(rx, c)) return false; return true;
    case N_NOCAP: return rx_gen(rx, n->a);
    case N_GROUP: rx_emit(rx, RX_SAVE, 2 * n->idx, 0); if (!rx_gen(rx, n->a)) return false; rx_emit(rx, RX_SAVE, 2 * n->idx + 1, 0); return true;
    case N_ALT: {
      int jmps[64], nj = 0; RxNode *c = n->a;
      while (c) {
        if (c->next) {
          int sp = rx_emit(rx, RX_SPLIT, 0, 0); rx->ins[sp].x = rx->n; if (!rx_gen(rx, c)) return false;
          if (nj >= 64) return false; jmps[nj++] = rx_emit(rx, RX_JMP, 0, 0); rx->ins[sp].y = rx->n;
        } else if (!rx_gen(rx, c)) return false;
        c = c->next;
      }
      for (int i = 0; i < nj; i++) rx->ins[jmps[i]].x = rx->n; return true;
    }
    case N_REP: {
      RxNode *a = n->a; int lo = n->lo, hi = n->hi;
      for (int i = 0; i < lo; i++) if (!rx_gen(rx, a)) return false;
      if (hi == -1) {
        bool guard = rx_can_empty(a); int slot = guard ? rx->nslots++ : 0;
        int sp = rx_emit(rx, RX_SPLIT, 0, 0); int body = rx->n;
        if (guard) rx_emit(rx, RX_PSAVE, slot, 0);
        if (!rx_gen(rx, a)) return false;
        if (guard) rx_emit(rx, RX_PCHK, slot, 0);
        rx_emit(rx, RX_JMP, sp, 0); int out = rx->n;
        if (n->lazy) { rx->ins[sp].x = out; rx->ins[sp].y = body; } else { rx->ins[sp].x = body; rx->ins[sp].y = out; }
      } else {
        int splits[1001], ns = 0;
        for (int i = lo; i < hi; i++) { int sp = rx_emit(rx, RX_SPLIT, 0, 0); splits[ns++] = sp; rx->ins[sp].x = rx->n; if (!rx_gen(rx, a)) return false; }
        for (int i = 0; i < ns; i++) { if (n->lazy) { rx->ins[splits[i]].y = rx->ins[splits[i]].x; rx->ins[splits[i]].x = rx->n; } else rx->ins[splits[i]].y = rx->n; }
      }
      return true;
    }
  }
  return false;
}
static void rx_free(Rx *rx) {
  if (!rx) return; free(rx->ins); for (int i = 0; i < rx->ncls; i++) free(rx->cls[i].items); free(rx->cls);
  for (int i = 0; i < rx->nnames; i++) free(rx->names[i]); free(rx->names); free(rx->nameidx); free(rx);
}
static Rx *rx_compile(const char *pat, int len, unsigned flags, char *err, int es) {
  Rx *rx = xalloc(sizeof(Rx)); memset(rx, 0, sizeof *rx); rx->flags = flags;
  RxParse ps; memset(&ps, 0, sizeof ps); ps.p = pat; ps.end = pat + len; ps.rx = rx; ps.flags = flags;
  RxNode *root = rx_alt(&ps);
  if (root && ps.p < ps.end) { snprintf(ps.err, sizeof ps.err, "')' زائدة"); root = NULL; }
  bool ok = false;
  if (root) { rx->nslots = 0; rx_emit(rx, RX_SAVE, 0, 0); ok = rx_gen(rx, root); rx_emit(rx, RX_SAVE, 1, 0); rx_emit(rx, RX_MATCH, 0, 0); if (!ok) snprintf(ps.err, sizeof ps.err, "النمط كبير جدا"); }
  for (int i = 0; i < ps.npool; i++) free(ps.pool[i]); free(ps.pool);
  if (!ok) { snprintf(err, es, "%s", ps.err[0] ? ps.err : "نمط غير صالح"); rx_free(rx); return NULL; }
  return rx;
}
typedef struct { int kind, a, b; } RxBt;
static bool rx_class_match(Rx *rx, int ci, uint32_t c) {
  RxClass *k = &rx->cls[ci]; bool in = false;
  for (int i = 0; i < k->n && !in; i++) { RxItem *it = &k->items[i]; if (it->pred) in = rx_pred(it->pred, c); else in = c >= it->lo && c <= it->hi; }
  return in != k->neg;
}

static RxBt *g_rxst; static int g_rxsc;
static int rx_run(Rx *rx, const char *s, int len, int start, int *caps) {
  int total = 2 * (rx->ncap + 1) + rx->nslots; for (int i = 0; i < total; i++) caps[i] = -1;
  RxBt *st = g_rxst; int sn = 0, sc = g_rxsc; long steps = 0; int pc = 0, pos = start; unsigned fl = rx->flags; int result = 0;
#define PUSHBT(k, x, y) do { if (sn == sc) { sc = sc ? sc * 2 : 256; st = xrealloc(st, sizeof(RxBt) * sc); } st[sn].kind = (k); st[sn].a = (x); st[sn].b = (y); sn++; } while (0)
  for (;;) {
    if (++steps > 20000000) { result = -1; break; }
    RxIns *in = &rx->ins[pc]; bool fail = false;
    switch (in->op) {
      case RX_CHAR: case RX_ANY: case RX_CLASS: {
        if (pos >= len) { fail = true; break; }
        uint32_t c; int l = u8_decode((const unsigned char *)s + pos, &c);
        if (in->op == RX_ANY) { if (c == '\n' && !(fl & RXF_DOTALL)) { fail = true; break; } }
        else {
          uint32_t cc = rx_normcp(c, fl);
          if (in->op == RX_CHAR) { if (cc != (uint32_t)in->x) { fail = true; break; } }
          else if (!rx_class_match(rx, in->x, cc) && !(cc != c && rx_class_match(rx, in->x, c))) { fail = true; break; }
        }
        pos += l; pc++; break;
      }
      case RX_SPLIT: PUSHBT(0, in->y, pos); pc = in->x; break;
      case RX_JMP: pc = in->x; break;
      case RX_SAVE: PUSHBT(1, in->x, caps[in->x]); caps[in->x] = pos; pc++; break;
      case RX_PSAVE: { int sl = 2 * (rx->ncap + 1) + in->x; PUSHBT(1, sl, caps[sl]); caps[sl] = pos; pc++; break; }
      case RX_PCHK: { int sl = 2 * (rx->ncap + 1) + in->x; if (caps[sl] == pos) fail = true; else pc++; break; }
      case RX_BOL: if (pos == 0 || ((fl & RXF_MULTI) && s[pos - 1] == '\n')) pc++; else fail = true; break;
      case RX_EOL: if (pos == len || ((fl & RXF_MULTI) && s[pos] == '\n')) pc++; else fail = true; break;
      case RX_WB: case RX_NWB: {
        bool a = false, b = false; uint32_t c;
        if (pos > 0) { int q = pos - 1; while (q > 0 && ((unsigned char)s[q] & 0xC0) == 0x80) q--; u8_decode((const unsigned char *)s + q, &c); a = rx_is_word(c); }
        if (pos < len) { u8_decode((const unsigned char *)s + pos, &c); b = rx_is_word(c); }
        if ((a != b) == (in->op == RX_WB)) pc++; else fail = true; break;
      }
      case RX_MATCH: result = 1; goto done;
    }
    if (fail) {
      for (;;) {
        if (sn == 0) { result = 0; goto done; }
        RxBt b = st[--sn];
        if (b.kind == 1) caps[b.a] = b.b; else { pc = b.a; pos = b.b; break; }
      }
    }
  }
done:
  g_rxst = st; g_rxsc = sc; return result;
}

static void rx_first(Rx *rx) {
  rx->fstate = 1; memset(rx->fb, 0, sizeof rx->fb);
  if (rx->flags & (RXF_ICASE | RXF_NORM)) { rx->fstate = 2; return; }
  int *stk = xalloc(sizeof(int) * (rx->n * 2 + 4)); char *seen = xalloc((size_t)rx->n + 1); memset(seen, 0, (size_t)rx->n + 1);
  int sp = 0; stk[sp++] = 0;
  while (sp > 0 && rx->fstate == 1) {
    int pc = stk[--sp]; if (pc < 0 || pc >= rx->n || seen[pc]) continue; seen[pc] = 1;
    RxIns *in = &rx->ins[pc];
    switch (in->op) {
      case RX_SAVE: case RX_PSAVE: case RX_PCHK: stk[sp++] = pc + 1; break;
      case RX_JMP: stk[sp++] = in->x; break;
      case RX_SPLIT: stk[sp++] = in->x; stk[sp++] = in->y; break;
      case RX_CHAR: { uint32_t c = (uint32_t)in->x; unsigned b = c < 128 ? c : c < 0x800 ? (0xC0 | (c >> 6)) : c < 0x10000 ? (0xE0 | (c >> 12)) : (0xF0 | (c >> 18)); rx->fb[b & 255] = 1; break; }
      case RX_CLASS: for (int b = 0; b < 128; b++) if (rx_class_match(rx, in->x, (uint32_t)b)) rx->fb[b] = 1; for (int b = 128; b < 256; b++) rx->fb[b] = 1; break;
      default: rx->fstate = 2; break;
    }
  }
  free(stk); free(seen);
}
static int rx_search(Rx *rx, const char *s, int len, int from, int *caps) {
  if (rx->fstate == 0) rx_first(rx);
  for (int st = from; st <= len; ) {
    if (rx->fstate == 1) { while (st < len && !rx->fb[(unsigned char)s[st]]) st += u8_charsize(s + st); if (st >= len) return 0; }
    int r = rx_run(rx, s, len, st, caps); if (r != 0) return r;
    if (st >= len) break; st += u8_charsize(s + st);
  }
  return 0;
}

typedef struct { char *key; Rx *rx; } RxCache;
static RxCache rxc[32]; static int rxc_n = 0, rxc_next = 0;
static unsigned rx_flags_from(Value opt) {
  unsigned f = 0; if (!IS_T(opt, O_MAP)) return 0; Value v; Dict *d = &AS_MAP(opt)->d;
  if (mget(d, KN("تجاهل_الحالة"), &v) && !falsey(v)) f |= RXF_ICASE; if (mget(d, KN("تطبيع"), &v) && !falsey(v)) f |= RXF_NORM;
  if (mget(d, KN("متعدد"), &v) && !falsey(v)) f |= RXF_MULTI; if (mget(d, KN("نقطة_سطر"), &v) && !falsey(v)) f |= RXF_DOTALL;
  return f;
}
static Rx *rx_get(Str *pat, unsigned flags, char *err, int es) {
  char key[600]; if (pat->len > 500) { snprintf(err, es, "النمط طويل جدا"); return NULL; }
  snprintf(key, sizeof key, "%u:%s", flags, pat->chars);
  for (int i = 0; i < rxc_n; i++) if (!strcmp(rxc[i].key, key)) return rxc[i].rx;
  Rx *rx = rx_compile(pat->chars, pat->len, flags, err, es); if (!rx) return NULL;
  int slot; if (rxc_n < 32) slot = rxc_n++; else { slot = rxc_next; rxc_next = (rxc_next + 1) % 32; free(rxc[slot].key); rx_free(rxc[slot].rx); }
  rxc[slot].key = strdup(key); rxc[slot].rx = rx; return rx;
}
static Value rx_match_map(Rx *rx, Str *text, int *caps) {
  Map *m = new_map(); push(OBJV(m));
  dset(&m->d, KN("نص"), mkstr(text->chars + caps[0], caps[1] - caps[0]));
  dset(&m->d, KX("بداية"), NUMV(u8_len(text->chars, caps[0]))); dset(&m->d, KX("نهاية"), NUMV(u8_len(text->chars, caps[1])));
  List *g = new_list(); dset(&m->d, KN("مجموعات"), OBJV(g));
  for (int i = 1; i <= rx->ncap; i++) varr_push(&g->a, caps[2 * i] >= 0 && caps[2 * i + 1] >= 0 ? mkstr(text->chars + caps[2 * i], caps[2 * i + 1] - caps[2 * i]) : NIL);
  if (rx->nnames) { Map *nm = new_map(); dset(&m->d, KN("مسميات"), OBJV(nm)); for (int i = 0; i < rx->nnames; i++) { int ix = rx->nameidx[i]; dset(&nm->d, mkstr(rx->names[i], (int)strlen(rx->names[i])), g->a.v[ix - 1]); } }
  vm.sp--; return OBJV(m);
}
#define RXARGS(i) STRA(0); STRA(1); char err[160]; Rx *rx = rx_get(AS_STR(a[0]), rx_flags_from(argc > (i) ? a[i] : NIL), err, sizeof err); if (!rx) FAIL("نمط غير صالح: %s", err); \
  Str *text = AS_STR(a[1]); int *caps = xalloc(sizeof(int) * (2 * (rx->ncap + 1) + rx->nslots + 2))
NAT(rx_find) { RXARGS(2); int r = rx_search(rx, text->chars, text->len, 0, caps); if (r < 0) { free(caps); FAIL("النمط معقد جدا"); } *res = r ? rx_match_map(rx, text, caps) : NIL; free(caps); return true; }
NAT(rx_test) { RXARGS(2); int r = rx_search(rx, text->chars, text->len, 0, caps); free(caps); if (r < 0) FAIL("النمط معقد جدا"); *res = BOOLV(r > 0); return true; }
NAT(rx_full) {
  RXARGS(2); int r = rx_search(rx, text->chars, text->len, 0, caps); bool ok = r > 0 && caps[0] == 0 && caps[1] == text->len;
  if (r > 0 && !ok) {
    char *pp = xalloc(AS_STR(a[0])->len + 8); snprintf(pp, AS_STR(a[0])->len + 8, "^(?:%s)$", AS_STR(a[0])->chars); char e2[160];
    Rx *r2 = rx_compile(pp, (int)strlen(pp), rx->flags, e2, sizeof e2); free(pp);
    if (r2) { int *c2 = xalloc(sizeof(int) * (2 * (r2->ncap + 1) + r2->nslots + 2)); ok = rx_search(r2, text->chars, text->len, 0, c2) > 0; free(c2); rx_free(r2); }
  }
  free(caps); if (r < 0) FAIL("النمط معقد جدا"); *res = BOOLV(ok); return true;
}
NAT(rx_all) {
  RXARGS(2); List *l = new_list(); push(OBJV(l)); int from = 0;
  while (from <= text->len) {
    int r = rx_search(rx, text->chars, text->len, from, caps); if (r < 0) { free(caps); vm.sp--; FAIL("النمط معقد جدا"); } if (!r) break;
    varr_push(&l->a, rx_match_map(rx, text, caps)); from = caps[1] > caps[0] ? caps[1] : caps[1] + (caps[1] < text->len ? u8_charsize(text->chars + caps[1]) : 1);
  }
  free(caps); vm.sp--; *res = OBJV(l); return true;
}
NAT(rx_split) {
  RXARGS(2); List *l = new_list(); push(OBJV(l)); int from = 0, last = 0;
  while (from <= text->len) {
    int r = rx_search(rx, text->chars, text->len, from, caps); if (r < 0) { free(caps); vm.sp--; FAIL("النمط معقد جدا"); } if (!r) break;
    if (caps[1] == caps[0]) { from = caps[1] + (caps[1] < text->len ? u8_charsize(text->chars + caps[1]) : 1); if (caps[0] == 0 || caps[0] >= text->len) continue; }
    else from = caps[1];
    varr_push(&l->a, mkstr(text->chars + last, caps[0] - last)); last = caps[1];
  }
  varr_push(&l->a, mkstr(text->chars + last, text->len - last)); free(caps); vm.sp--; *res = OBJV(l); return true;
}
NAT(rx_replace) {
  STRA(0); STRA(1); char err[160]; Rx *rx = rx_get(AS_STR(a[0]), rx_flags_from(argc > 3 ? a[3] : NIL), err, sizeof err); if (!rx) FAIL("نمط غير صالح: %s", err);
  Str *text = AS_STR(a[1]); Value rep = a[2]; bool fn = callable(rep); if (!fn && !IS_T(rep, O_STR)) FAIL("البديل يجب أن يكون نصا أو دالة");
  int *caps = xalloc(sizeof(int) * (2 * (rx->ncap + 1) + rx->nslots + 2)); Buf out; buf_init(&out); int from = 0, last = 0;
  while (from <= text->len) {
    int r = rx_search(rx, text->chars, text->len, from, caps); if (r < 0) { free(caps); free(out.p); FAIL("النمط معقد جدا"); } if (!r) break;
    buf_add(&out, text->chars + last, caps[0] - last);
    if (fn) {
      Value mm = rx_match_map(rx, text, caps); push(mm); Value ret; if (!call_fn(rep, 1, &mm, &ret)) { free(caps); free(out.p); return false; }
      vm.sp--; Str *rs = tostr(ret); buf_add(&out, rs->chars, rs->len);
    } else {
      Str *t = AS_STR(rep);
      for (int i = 0; i < t->len; i++) {
        char c = t->chars[i];
        if ((c == '$' || c == '\\') && i + 1 < t->len && (isdigit((unsigned char)t->chars[i + 1]) || (c == '$' && t->chars[i + 1] == '&'))) {
          int g = t->chars[i + 1] == '&' ? 0 : t->chars[i + 1] - '0'; i++;
          if (g <= rx->ncap && caps[2 * g] >= 0 && caps[2 * g + 1] >= 0) buf_add(&out, text->chars + caps[2 * g], caps[2 * g + 1] - caps[2 * g]);
        } else buf_addc(&out, c);
      }
    }
    last = caps[1];
    if (caps[1] > caps[0]) from = caps[1]; else { from = caps[1] + (caps[1] < text->len ? u8_charsize(text->chars + caps[1]) : 1); if (caps[1] < text->len) { buf_add(&out, text->chars + caps[1], from - caps[1]); last = from; } }
  }
  if (last < text->len) buf_add(&out, text->chars + last, text->len - last);
  free(caps); *res = mkstr(buf_cstr(&out), out.n); free(out.p); return true;
}

NAT(s_spawn) {
  SAFE();
#if !SOLAN_FORK
  FAIL("هذه الدالة غير متاحة على هذا النظام (تتطلب fork): استخدم WSL أو Linux");
#else
  STRA(0); int in[2], out[2], er[2]; if (pipe(in) < 0 || pipe(out) < 0 || pipe(er) < 0) FAIL("تعذّر إنشاء أنابيب");
  if (in[1] >= MAXFD || out[0] >= MAXFD || er[0] >= MAXFD) FAIL("عدد الاتصالات كبير جدا");
  fflush(stdout); pid_t pid = fork(); if (pid < 0) FAIL("فشل fork");
  if (pid == 0) {
    dup2(in[0], 0); dup2(out[1], 1); dup2(er[1], 2);
    int fds[6] = {in[0], in[1], out[0], out[1], er[0], er[1]}; for (int i = 0; i < 6; i++) close(fds[i]);
    execlp("sh", "sh", "-c", AS_STR(a[0])->chars, (char *)NULL); _exit(127);
  }
  close(in[0]); close(out[1]); close(er[1]); int ws[3] = {in[1], out[0], er[0]};
  for (int i = 0; i < 3; i++) { fd_setup(ws[i]); fflag[ws[i]] |= 8; }
  Map *r = new_map(); dset(&r->d, KN("pid"), NUMV(pid)); dset(&r->d, KN("دخل"), NUMV(in[1])); dset(&r->d, KN("خرج"), NUMV(out[0])); dset(&r->d, KX("خطأ"), NUMV(er[0]));
  *res = OBJV(r); return true;
#endif
}
NAT(s_exec_argv) {
  SAFE();
  if (!IS_T(a[0], O_LIST)) FAIL("شغل_قائمة تتطلب قائمة وسائط");
  List *l = AS_LIST(a[0]); if (l->a.n < 1 || l->a.n > 64) FAIL("عدد الوسائط غير صالح");
  char *av[66];
  for (int i = 0; i < l->a.n; i++) { if (!IS_T(l->a.v[i], O_STR)) FAIL("كل وسيط يجب أن يكون نصا"); av[i] = AS_STR(l->a.v[i])->chars; }
  av[l->a.n] = NULL;
  char *outp = NULL; size_t outn = 0;
  int code = os_run(av, NULL, &outp, &outn);
  if (code < 0) FAIL("تعذّر تشغيل البرنامج");
  Map *r = new_map(); dset(&r->d, KX("رمز"), NUMV(code)); dset(&r->d, KX("ناتج"), mkstr(outp ? outp : "", (int)outn)); free(outp);
  *res = OBJV(r); return true;
}
NAT(s_solan_home) {
  const char *root = NULL;
  if (argc >= 1 && IS_T(a[0], O_STR)) root = AS_STR(a[0])->chars;
  char out[PATH_MAX]; const char *why = "";
  pk_home_dir(root, out, sizeof out, &why);
  *res = mkstr(out, (int)strlen(out)); return true;
}
NAT(s_is_std) {
  STRA(0); Str *k = normstr(AS_STR(a[0])->chars); Value v;
  *res = BOOLV(dget(&vm.natmods, OBJV(k), &v) || dget(&vm.stdlibd, OBJV(k), &v)); return true;
}
NAT(s_kill) { NUMA(0); *res = BOOLV(kill((pid_t)a[0].as.n, argc > 1 && IS_NUM(a[1]) ? (int)a[1].as.n : SIGTERM) == 0); return true; }
NAT(n_hl_ansi) { STRA(0); Buf o; buf_init(&o); hl_range(AS_STR(a[0])->chars, AS_STR(a[0])->len, &o, false, 0); *res = mkstr(buf_cstr(&o), o.n); free(o.p); return true; }
NAT(n_hl_html) { STRA(0); Buf o; buf_init(&o); hl_range(AS_STR(a[0])->chars, AS_STR(a[0])->len, &o, true, 0); *res = mkstr(buf_cstr(&o), o.n); free(o.p); return true; }

static Value convert_digits(Str *s, int target) {
  Buf o; buf_init(&o);
  for (int i = 0; i < s->len;) {
    uint32_t cp; int l = u8_decode((const unsigned char *)s->chars + i, &cp); int d = -1;
    if (cp >= '0' && cp <= '9') d = (int)(cp - '0'); else if (cp >= 0x660 && cp <= 0x669) d = (int)(cp - 0x660); else if (cp >= 0x6F0 && cp <= 0x6F9) d = (int)(cp - 0x6F0);
    if (d >= 0) { uint32_t nc = target == 0 ? (uint32_t)('0' + d) : (target == 1 ? 0x660u + d : 0x6F0u + d); char u[4]; buf_add(&o, u, u8_encode(nc, u)); }
    else if (cp == '.' && target == 1 && i > 0 && i + 1 < s->len && isdigit((unsigned char)s->chars[i + 1]) && isdigit((unsigned char)s->chars[i - 1])) buf_add(&o, "\xD9\xAB", 2);
    else if (cp == 0x66B && target != 1) buf_addc(&o, '.');
    else if (cp == 0x66C && target == 0) buf_addc(&o, ',');
    else buf_add(&o, s->chars + i, l);
    i += l;
  }
  Value v = mkstr(buf_cstr(&o), o.n); free(o.p); return v;
}
NAT(n_digits_ar) { *res = convert_digits(tostr(a[0]), 1); return true; }
NAT(n_digits_en) { *res = convert_digits(tostr(a[0]), 0); return true; }
NAT(n_digits_fa) { *res = convert_digits(tostr(a[0]), 2); return true; }
NAT(n_digits_mode) {
  STRA(0); Str *m = normstr(AS_STR(a[0])->chars); const char *c = m->chars;
  if (strstr(c, "عرب") || !strcmp(c, "ar")) g_digits = 1; else if (strstr(c, "فارس") || !strcmp(c, "fa")) g_digits = 2; else g_digits = 0;
  *res = NIL; return true;
}
NAT(f_size) { STRA(0); PERMP(PM_READ, AS_STR(a[0])->chars); sstat_t st; if (sstat(AS_STR(a[0])->chars, &st) != 0) { *res = NIL; return true; } *res = NUMV((double)st.st_size); return true; }
NAT(f_isdir) { STRA(0); PERMP(PM_READ, AS_STR(a[0])->chars); sstat_t st; *res = BOOLV(sstat(AS_STR(a[0])->chars, &st) == 0 && S_ISDIR(st.st_mode)); return true; }
NAT(f_abs) { STRA(0); char b[PATH_MAX]; if (!realpath(AS_STR(a[0])->chars, b)) { *res = NIL; return true; } *res = mkstr(b, (int)strlen(b)); return true; }
static bool rt_mkdirs(const char *path) {
  char d[PATH_MAX]; if (strlen(path) >= sizeof d) return false;
  snprintf(d, sizeof d, "%s", path);
  for (char *c = d + 1; *c; c++) if (*c == '/') { *c = 0; sstat_t st; if (sstat(d, &st) != 0) mkdir(d, 0755); *c = '/'; }
  sstat_t st; if (sstat(d, &st) == 0) return S_ISDIR(st.st_mode);
  return mkdir(d, 0755) == 0;
}
static bool rt_rmtree(const char *path, int depth) {
  if (depth > 64) return false;
  if (os_is_link(path)) return os_rm_file(path) == 0;
  sstat_t st; if (sstat(path, &st) != 0) return true;
  if (!S_ISDIR(st.st_mode)) return os_rm_file(path) == 0;
  DIR *d = opendir(path); if (!d) return false;
  bool ok = true; struct dirent *e;
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    char sub[PATH_MAX]; if (snprintf(sub, sizeof sub, "%s/%s", path, e->d_name) >= (int)sizeof sub) { ok = false; continue; }
    if (!rt_rmtree(sub, depth + 1)) ok = false;
  }
  closedir(d);
  return rmdir(path) == 0 && ok;
}
NAT(f_mkdirs) { STRA(0); PERMP(PM_WRITE, AS_STR(a[0])->chars); *res = BOOLV(rt_mkdirs(AS_STR(a[0])->chars)); return true; }
NAT(f_rmtree) {
  STRA(0); const char *p = AS_STR(a[0])->chars; PERMP(PM_WRITE, p);
  if (strlen(p) < 4 || !strcmp(p, "/") || !strcmp(p, ".") || !strcmp(p, "..")) FAIL("مسار الحذف غير آمن: '%s'", p);
  *res = BOOLV(rt_rmtree(p, 0)); return true;
}
NAT(f_islink) { STRA(0); PERMP(PM_READ, AS_STR(a[0])->chars); *res = BOOLV(os_is_link(AS_STR(a[0])->chars)); return true; }
NAT(f_move) { STRA(0); STRA(1); PERMP(PM_READ, AS_STR(a[0])->chars); PERMP(PM_WRITE, AS_STR(a[0])->chars); PERMP(PM_WRITE, AS_STR(a[1])->chars); *res = BOOLV(rename(AS_STR(a[0])->chars, AS_STR(a[1])->chars) == 0); return true; }
NAT(f_copy) {
  STRA(0); STRA(1); PERMP(PM_READ, AS_STR(a[0])->chars); PERMP(PM_WRITE, AS_STR(a[1])->chars); size_t n; char *d = read_file(AS_STR(a[0])->chars, &n); if (!d) FAIL("تعذّرت قراءة '%s'", AS_STR(a[0])->chars);
  FILE *f = fopen(AS_STR(a[1])->chars, "wb"); if (!f) { free(d); FAIL("تعذّرت الكتابة في '%s'", AS_STR(a[1])->chars); } fwrite(d, 1, n, f); fclose(f); free(d); *res = BOOLV(true); return true;
}
NAT(f_lines) {
  STRA(0); PERMP(PM_READ, AS_STR(a[0])->chars); size_t n; char *d = read_file(AS_STR(a[0])->chars, &n); if (!d) FAIL("تعذّرت قراءة '%s'", AS_STR(a[0])->chars);
  Str tmp; (void)tmp; List *l = new_list(); const char *p = d, *end = d + n;
  while (p < end) { const char *q = memchr(p, '\n', end - p); int len = q ? (int)(q - p) : (int)(end - p); int m = len; if (m > 0 && p[m - 1] == '\r') m--; varr_push(&l->a, mkstr(p, m)); if (!q) break; p = q + 1; }
  free(d); *res = OBJV(l); return true;
}
NAT(f_home) { const char *h = getenv("HOME"); *res = h ? mkstr(h, (int)strlen(h)) : NIL; return true; }
NAT(f_rmdir) { STRA(0); PERMP(PM_WRITE, AS_STR(a[0])->chars); *res = BOOLV(rmdir(AS_STR(a[0])->chars) == 0); return true; }

NAT(co_new) { if (!IS_T(a[0], O_CLO)) FAIL("كوروتين تتطلب دالة"); *res = OBJV(new_coro(AS_CLO(a[0]), argc - 1, a)); return true; }
NAT(co_resume_n) {
  if (!IS_T(a[0], O_CORO)) FAIL("استأنف تتطلب كوروتين"); Value out;
  int r = coro_resume(AS_CORO(a[0]), argc > 1 ? a[1] : NIL, false, &out); if (r < 0) return false; *res = out; return true;
}
NAT(co_done) { if (!IS_T(a[0], O_CORO)) FAIL("منتهي تتطلب كوروتين"); *res = BOOLV(AS_CORO(a[0])->state == 3); return true; }
NAT(co_step) {
  if (!IS_T(a[0], O_CORO)) FAIL("_استأنف تتطلب كوروتين"); Value out;
  int r = coro_resume(AS_CORO(a[0]), argc > 1 ? a[1] : NIL, argc > 2 && !falsey(a[2]), &out); List *l = new_list();
  if (r < 0) { varr_push(&l->a, NUMV(2)); varr_push(&l->a, vm.exc); vm.exc = NIL; } else { varr_push(&l->a, NUMV(r)); varr_push(&l->a, out); }
  *res = OBJV(l); return true;
}
NAT(s_fork) {
  SAFE();
#if !SOLAN_FORK
  FAIL("هذه الدالة غير متاحة على هذا النظام (تتطلب fork): استخدم WSL أو Linux");
#else
  if (!callable(a[0])) FAIL("فرع يتطلب دالة"); int p[2]; if (pipe(p) < 0) FAIL("تعذّر إنشاء أنبوب");
  if (p[0] >= MAXFD) { close(p[0]); close(p[1]); FAIL("عدد الاتصالات كبير جدا"); }
  fflush(stdout); pid_t pid = fork(); if (pid < 0) { close(p[0]); close(p[1]); FAIL("فشل fork"); }
  if (pid == 0) {
    close(p[0]); Value out; Map *m = new_map(); push(OBJV(m));
    if (call_fn(a[0], 0, NULL, &out)) dset(&m->d, KX("قيمة"), out); else dset(&m->d, KX("خطأ"), OBJV(tostr(vm.exc)));
    Buf b; buf_init(&b); json_enc(OBJV(m), &b, 0, 0); size_t off = 0;
    while (off < (size_t)b.n) { ssize_t w = write(p[1], b.p + off, b.n - off); if (w <= 0) break; off += w; }
    fflush(stdout); _exit(0);
  }
  close(p[1]); fd_setup(p[0]); fflag[p[0]] |= 8; Map *r = new_map(); dset(&r->d, KN("مقبض"), NUMV(p[0])); dset(&r->d, KN("pid"), NUMV(pid)); *res = OBJV(r); return true;
#endif
}
static void report_error(void);
NAT(s_worker) {
  SAFE();
#if !SOLAN_FORK
  FAIL("هذه الدالة غير متاحة على هذا النظام (تتطلب fork): استخدم WSL أو Linux");
#else
  if (!callable(a[0])) FAIL("عامل يتطلب دالة"); int sp[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0) FAIL("تعذّر إنشاء قناة");
  if (sp[0] >= MAXFD || sp[1] >= MAXFD) { close(sp[0]); close(sp[1]); FAIL("عدد الاتصالات كبير جدا"); }
  fflush(stdout); fflush(stderr); pid_t pid = fork(); if (pid < 0) { close(sp[0]); close(sp[1]); FAIL("فشل fork"); }
  if (pid == 0) {
    close(sp[0]); fd_setup(sp[1]); fflag[sp[1]] |= 8; Value out, arg = NUMV(sp[1]); push(arg);
    bool ok = call_fn(a[0], 1, &arg, &out);
    if (!ok) { fflush(stdout); report_error(); }
    close(sp[1]); fflush(stdout); fflush(stderr); _exit(ok ? 0 : 70);
  }
  close(sp[1]); fd_setup(sp[0]); fflag[sp[0]] |= 8; Map *r = new_map(); dset(&r->d, KN("مقبض"), NUMV(sp[0])); dset(&r->d, KN("pid"), NUMV(pid)); *res = OBJV(r); return true;
#endif
}

NAT(s_readbytes) {
  NUMA(0); int n = (int)a[0].as.n; if (n < 0 || n > (64 << 20)) FAIL("حجم غير صالح");
  char *b = xalloc(n + 1); size_t got = 0;
  while ((int)got < n) { size_t r = fread(b + got, 1, n - got, stdin); if (r == 0) break; got += r; }
  if ((int)got < n) { free(b); *res = NIL; return true; }
  *res = mkstr(b, n); free(b); return true;
}
NAT(s_writeraw) { STRA(0); fwrite(AS_STR(a[0])->chars, 1, AS_STR(a[0])->len, stdout); fflush(stdout); *res = NIL; return true; }
NAT(s_norm) { STRA(0); *res = OBJV(normstr_n(AS_STR(a[0])->chars, AS_STR(a[0])->len)); return true; }
NAT(s_lsp_send) {
  STRA(0); Str *s = AS_STR(a[0]); fprintf(stdout, "Content-Length: %d\r\n\r\n", s->len); fwrite(s->chars, 1, s->len, stdout); fflush(stdout); *res = NIL; return true;
}
NAT(s_syntax_check) {
  STRA(0); int saved_nest = vm.nest; (void)saved_nest;
  Fun *fn = compile(AS_STR(a[0])->chars, "<lsp>", true);
  if (fn) { *res = NIL; return true; }
  List *l = new_list(); varr_push(&l->a, NUMV(P.errline)); varr_push(&l->a, mkstr(P.errtext, (int)strlen(P.errtext))); *res = OBJV(l); return true;
}
NAT(s_symbols) {
  Map *r = new_map(); push(OBJV(r));
  List *kw = new_list(); dset(&r->d, KN("كلمات"), OBJV(kw));
  for (int i = 0; i < vm.kw.n; i++) { Value k = vm.kw.e[i].k; if (k.t == V_EMPTY || !IS_T(k, O_STR)) continue; varr_push(&kw->a, k); }
  { static const struct { const char *k; int t; } defs[] = {{"دالة", T_FUN}, {"صنف", T_CLASS}, {"خل", T_LET}, {"ثابت", T_CONST}, {NULL, 0}};
    Map *dm = new_map(); dset(&r->d, KN("تعريفات"), OBJV(dm));
    for (int d = 0; defs[d].k; d++) {
      List *l = new_list(); dset(&dm->d, OBJV(intern_c(defs[d].k)), OBJV(l));
      for (int i = 0; i < vm.kw.n; i++) { Value k = vm.kw.e[i].k; if (k.t == V_EMPTY || !IS_T(k, O_STR) || (int)vm.kw.e[i].v.as.n != defs[d].t) continue; varr_push(&l->a, k); }
    } }
  { List *l = new_list(); dset(&r->d, KX("قياسية"), OBJV(l)); for (int t = T_LET; t <= T_YIELD; t++) if (kw_canon[t]) varr_push(&l->a, mkstr(kw_canon[t], (int)strlen(kw_canon[t]))); }
  { List *l = new_list(); dset(&r->d, KN("طرق"), OBJV(l)); for (int i = 0; i < M_COUNT; i++) varr_push(&l->a, mkstr(method_names[i], (int)strlen(method_names[i]))); }
  List *bi = new_list(); dset(&r->d, KX("مدمجة"), OBJV(bi));
  for (int i = 0; i < vm.builtins.n; i++) { Value k = vm.builtins.e[i].k; if (k.t == V_EMPTY || !IS_T(k, O_STR) || AS_STR(k)->chars[0] == '_') continue; varr_push(&bi->a, k); }
  Map *mods = new_map(); dset(&r->d, KN("وحدات"), OBJV(mods));
  for (int i = 0; i < vm.natmods.n; i++) {
    Value k = vm.natmods.e[i].k, v = vm.natmods.e[i].v; if (k.t == V_EMPTY || !IS_T(v, O_MAP)) continue;
    List *fl = new_list(); dset(&mods->d, k, OBJV(fl)); Map *mm = AS_MAP(v);
    for (int j = 0; j < mm->d.n; j++) { Value fk = mm->d.e[j].k; if (fk.t != V_EMPTY && IS_T(fk, O_STR)) varr_push(&fl->a, fk); }
  }
  List *sl = new_list(); dset(&r->d, KN("مكتبات"), OBJV(sl));
  for (int i = 0; STDLIB[i].name; i++) varr_push(&sl->a, mkstr(STDLIB[i].name, (int)strlen(STDLIB[i].name)));
  vm.sp--; *res = OBJV(r); return true;
}
NAT(s_waitpid) { NUMA(0); int st = 0;
#if !SOLAN_FORK
  FAIL("waitpid غير متاحة على هذا النظام");
#else
  pid_t r = waitpid((pid_t)a[0].as.n, &st, 0); *res = NUMV(r < 0 ? -1 : (WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st))); return true;
#endif
}

static void defnat(Dict *d, const char *name, NativeFn fn, int mn, int mx) {
  Str *n = normstr(name); dset(d, OBJV(n), OBJV(new_native(n, fn, mn, mx)));
}
static Map *mkmod(const char *name) { Map *m = new_map(); m->module = true; dset(&vm.natmods, OBJV(normstr(name)), OBJV(m)); return m; }
#define MF(m, name, fn, mn, mx) defnat(&(m)->d, name, fn, mn, mx)
static void init_natives(void) {
  Dict *g = &vm.builtins;
  defnat(g, "اطبع", n_print, 0, -1); defnat(g, "اكتب", n_write, 0, -1); defnat(g, "ادخل", n_input, 0, 1);
  defnat(g, "طول", n_len, 1, 1); defnat(g, "نوع", n_type, 1, 1); defnat(g, "نص", n_str, 1, 1); defnat(g, "رقم", n_num, 1, 1);
  defnat(g, "مدى", n_range, 1, 3); defnat(g, "_عدد_المدى", n_range_count, 3, 3); defnat(g, "تأكد", n_assert, 1, 2); defnat(g, "حرف", n_chr, 1, 1); defnat(g, "رمز", n_ord, 1, 1);
  defnat(g, "هل_فرع", n_isa, 2, 2); defnat(g, "هل_نص", n_is_str, 1, 1); defnat(g, "هل_رقم", n_is_num, 1, 1);
  defnat(g, "هل_قائمة", n_is_list, 1, 1); defnat(g, "هل_قاموس", n_is_map, 1, 1); defnat(g, "هل_عدم", n_is_nil, 1, 1);
  defnat(g, "هل_دالة", n_is_fn, 1, 1); defnat(g, "نسق", n_format, 1, -1); defnat(g, "لون_الكود", n_hl_ansi, 1, 1); defnat(g, "كود_html", n_hl_html, 1, 1); defnat(g, "ارقام_عربية", n_digits_ar, 1, 1); defnat(g, "ارقام_لاتينية", n_digits_en, 1, 1); defnat(g, "ارقام_فارسية", n_digits_fa, 1, 1); defnat(g, "ارقام_الاخراج", n_digits_mode, 1, 1); defnat(g, "كوروتين", co_new, 1, -1); defnat(g, "استأنف", co_resume_n, 1, 2); defnat(g, "منتهي", co_done, 1, 1); defnat(g, "_استأنف", co_step, 1, 3); defnat(g, "_تعداد", n_enum, 1, 1); defnat(g, "لديه", n_has, 2, 2);
  dset(g, KN("النسخة"), mkstr(SOLAN_VERSION, (int)strlen(SOLAN_VERSION)));

  Map *m = mkmod("رياضيات");
  MF(m, "جذر", m_sqrt, 1, 1); MF(m, "جيب", m_sin, 1, 1); MF(m, "جتا", m_cos, 1, 1); MF(m, "ظل", m_tan, 1, 1);
  MF(m, "ارضي", m_floor, 1, 1); MF(m, "سقف", m_ceil, 1, 1); MF(m, "مطلق", m_abs, 1, 1); MF(m, "قرب", m_round, 1, 1);
  MF(m, "اس", m_exp, 1, 1); MF(m, "قوة", m_pow, 2, 2); MF(m, "لوغ", m_log, 1, 2); MF(m, "لوغ10", m_log10, 1, 1);
  MF(m, "اكبر", m_max, 0, -1); MF(m, "اصغر", m_min, 0, -1); MF(m, "جيب_عكسي", m_asin, 1, 1); MF(m, "جتا_عكسي", m_acos, 1, 1);
  MF(m, "ظل_عكسي", m_atan, 1, 1); MF(m, "ظل2", m_atan2, 2, 2); MF(m, "بتر", m_trunc, 1, 1);
  dset(&m->d, KN("باي"), NUMV(M_PI)); dset(&m->d, KN("اي"), NUMV(M_E)); dset(&m->d, KX("لانهاية"), NUMV(INFINITY));
  m = mkmod("ضخم");
  MF(m, "جمع", bg_add, 2, 2); MF(m, "طرح", bg_sub, 2, 2); MF(m, "ضرب", bg_mul, 2, 2); MF(m, "قسمة", bg_div_, 2, 2); MF(m, "باقي", bg_mod_, 2, 2);
  MF(m, "قوة", bg_pow, 2, 2); MF(m, "قارن", bg_cmp, 2, 2); MF(m, "مضروب", bg_fact, 1, 1); MF(m, "رقم", bg_num, 1, 1);
  m = mkmod("عشوائي");
  MF(m, "رقم", r_num, 0, 0); MF(m, "صحيح", r_int, 2, 2); MF(m, "اختر", r_pick, 1, 1); MF(m, "اخلط", r_shuffle, 1, 1); MF(m, "بذرة", r_seed, 1, 1);
  rng_state ^= (uint64_t)(now_sec() * 1e6) * 2654435761ULL; if (!rng_state) rng_state = 1;
  m = mkmod("زمن");
  MF(m, "الان", t_now, 0, 0); MF(m, "تاريخ", t_date, 0, 2); MF(m, "من_تاريخ", t_mk, 1, 2); MF(m, "تحليل", t_parse, 2, 3); MF(m, "ساعة", t_clock, 0, 0); MF(m, "نوم", t_sleep, 1, 1); MF(m, "تنسيق", t_format, 1, 2);
  m = mkmod("نظام");
  MF(m, "بيئة", s_env, 1, 2); MF(m, "ضبط_بيئة", s_setenv, 2, 2); MF(m, "طرفية", s_tty, 0, 0); MF(m, "حمل_env", s_loadenv, 0, 1); MF(m, "اخرج", s_exit, 0, 1); MF(m, "نفذ", s_exec, 1, 1); MF(m, "شغل", s_run, 1, 1); MF(m, "مسار", s_cwd, 0, 0);
#ifdef __ANDROID__
  dset(&m->d, KX("منصة"), mkstr("android", 7));
#else
  dset(&m->d, KX("منصة"), mkstr("linux", 5));
#endif
  m = mkmod("ملفات");
  MF(m, "اقرا", f_read, 1, 1); MF(m, "اكتب", f_write, 2, 2); MF(m, "الحق", f_append, 2, 2); MF(m, "موجود", f_exists, 1, 1);
  MF(m, "احذف", f_remove, 1, 1); MF(m, "حجم", f_size, 1, 1); MF(m, "هل_مجلد", f_isdir, 1, 1); MF(m, "مسار_مطلق", f_abs, 1, 1); MF(m, "انقل", f_move, 2, 2); MF(m, "انسخ", f_copy, 2, 2); MF(m, "سطور", f_lines, 1, 1); MF(m, "مسار_البيت", f_home, 0, 0); MF(m, "احذف_مجلد", f_rmdir, 1, 1); MF(m, "انشئ_مجلدات", f_mkdirs, 1, 1); MF(m, "احذف_شجرة", f_rmtree, 1, 1); MF(m, "هل_رابط", f_islink, 1, 1); MF(m, "انشئ_مجلد", f_mkdir, 1, 1); MF(m, "قائمة", f_list, 1, 1);
  m = mkmod("جيسون"); MF(m, "ترميز", j_enc, 1, 2); MF(m, "فك", j_dec, 1, 1);
  m = mkmod("شبكة"); MF(m, "جلب", net_fetch, 1, 2); MF(m, "خادم", net_serve, 2, 3); MF(m, "اقرا_طلب", net_readreq, 2, 2); MF(m, "اكتب_رد", net_writeresp, 2, 2); MF(m, "ترميز_رابط", net_urlenc, 1, 1); MF(m, "فك_رابط", net_urldec, 1, 1);
  m = mkmod("سوكت");
  MF(m, "اتصل", sk_connect, 2, 3); MF(m, "استمع", sk_listen, 1, 2); MF(m, "اقبل", sk_accept, 1, 1); MF(m, "ارسل", sk_send, 2, 2);
  MF(m, "استقبل", sk_recv, 1, 2); MF(m, "بالضبط", sk_exact, 2, 2); MF(m, "سطر", sk_line, 1, 1); MF(m, "اغلق", sk_close, 1, 1);
  MF(m, "مهلة", sk_timeout, 2, 2); MF(m, "غير_حاجز", sk_nonblock, 1, 2); MF(m, "انتظر", sk_wait, 1, 2); MF(m, "عنوان", sk_resolve, 1, 1);
  MF(m, "udp", sk_udp, 0, 1); MF(m, "ارسل_الى", sk_sendto, 4, 4); MF(m, "استقبل_من", sk_recvfrom, 1, 1);
  m = mkmod("ويبسوكت");
  MF(m, "اتصل", ws_connect, 1, 2); MF(m, "ترقية", ws_upgrade, 1, 1); MF(m, "ارسل", ws_send, 2, 3); MF(m, "استقبل", ws_recv_n, 1, 2);
  MF(m, "نبض", ws_ping, 1, 1); MF(m, "اغلق", ws_close, 1, 1);
  m = mkmod("تشفير");
  MF(m, "sha1", c_sha1, 1, 2); MF(m, "sha256", c_sha256, 1, 2); MF(m, "hmac_sha256", c_hmac, 2, 3); MF(m, "base64", c_b64, 1, 1);
  MF(m, "فك_base64", c_unb64, 1, 1); MF(m, "hex", c_hex, 1, 1); MF(m, "فك_hex", c_unhex, 1, 1); MF(m, "عشوائي_بايتات", c_rand, 1, 1);
  m = mkmod("نمط");
  MF(m, "ابحث", rx_find, 2, 3); MF(m, "هل", rx_test, 2, 3); MF(m, "مطابق_كامل", rx_full, 2, 3); MF(m, "كل", rx_all, 2, 3); MF(m, "قسم", rx_split, 2, 3); MF(m, "ابدل", rx_replace, 3, 4);
#if SOLAN_FFI
  m = mkmod("ffi"); MF(m, "افتح", ffi_open, 0, 1); MF(m, "ربط", ffi_bind, 3, 3); MF(m, "ذاكرة", ffi_alloc, 1, 1); MF(m, "حرر", ffi_free, 1, 1);
  MF(m, "اقرأ_نص", ffi_read_str, 1, 2); MF(m, "اكتب_نص", ffi_write_str, 2, 2);
#endif
  m = mkmod("قاعدة"); MF(m, "افتح", db_open, 1, 1); MF(m, "اغلق", db_close, 1, 1); MF(m, "نفذ", db_exec, 2, 3); MF(m, "استعلم", db_query, 2, 3);
  m = mkmod("ضغط"); MF(m, "gzip", z_gzip, 1, 1); MF(m, "gunzip", z_gunzip, 1, 1); MF(m, "zlib", z_zlib, 1, 1); MF(m, "فك", z_gunzip, 1, 1);
  m = mkmod("حلقة"); loop_cbs = new_list(); pin((Obj *)loop_cbs);
  MF(m, "بعد", lp_after, 2, 2); MF(m, "كل", lp_every, 2, 2); MF(m, "الغ", lp_cancel, 1, 1); MF(m, "راقب", lp_watch, 2, 2);
  MF(m, "الغ_مراقبة", lp_unwatch, 1, 1); MF(m, "خطوة", lp_step, 0, 0); MF(m, "شغل", lp_run, 0, 0); MF(m, "اوقف", lp_stop, 0, 0);
  m = AS_MAP(({ Value t; mget(&vm.natmods, KN("نظام"), &t); t; })); MF(m, "خلفية", s_bg, 1, 1); MF(m, "منصة", s_platform, 0, 0); MF(m, "فرع", s_fork, 1, 1); MF(m, "عامل", s_worker, 1, 1); MF(m, "اقرا_بايتات", s_readbytes, 1, 1); MF(m, "اكتب_خام", s_writeraw, 1, 1); MF(m, "ارسل_lsp", s_lsp_send, 1, 1); MF(m, "طبّع", s_norm, 1, 1); MF(m, "افحص_صياغة", s_syntax_check, 1, 1); MF(m, "رموز", s_symbols, 0, 0); MF(m, "عملية", s_spawn, 1, 1); MF(m, "شغل_قائمة", s_exec_argv, 1, 1); MF(m, "مكتبة_قياسية", s_is_std, 1, 1); MF(m, "بيت_سولان", s_solan_home, 0, 1); MF(m, "اقتل", s_kill, 1, 2); MF(m, "انتظر_عملية", s_waitpid, 1, 1);
  m = AS_MAP(({ Value t; mget(&vm.natmods, KN("رياضيات"), &t); t; }));
  MF(m, "بت_و", m_band, 2, 2); MF(m, "بت_او", m_bor, 2, 2); MF(m, "بت_خالف", m_bxor, 2, 2); MF(m, "ازح_يسار", m_shl, 2, 2); MF(m, "ازح_يمين", m_shr, 2, 2); MF(m, "بت_نفي", m_bnot, 1, 1);
}

static const struct { const char *scope, *canon, *alts; } ALIASES[] = {
  {"g", "اطبع", "اعرض|اظهر|طباعة|اطبع_سطر|اكتب_سطر"},
  {"g", "اكتب", "اطبع_بلا_سطر|اكتب_بلا_سطر|كتابة"},
  {"g", "ادخل", "اقرا|اقرأ|سؤال|مدخل|ادخال|استقبل_نص"},
  {"g", "طول", "حجم|عدد|قياس|عدد_العناصر"},
  {"g", "نوع", "نوع_القيمة|ماهو|اي_نوع"},
  {"g", "نص", "سلسلة|حرفي|كنص"},
  {"g", "رقم", "حول_لرقم|كرقم|الى_رقم|عددي"},
  {"g", "مدى", "نطاق|تسلسل|سلسلة_ارقام"},
  {"g", "تأكد", "اكد|تحقق|اثبت|تاكد"},
  {"g", "حرف", "حرف_من_رمز|رمز_الى_حرف"},
  {"g", "رمز", "رمز_الحرف|كود"},
  {"g", "هل_فرع", "هل_من_صنف|هل_نسخة_من"},
  {"g", "هل_نص", "هل_سلسلة"}, {"g", "هل_رقم", "هل_عدد"}, {"g", "هل_قائمة", "هل_مصفوفة|هل_لائحة"},
  {"g", "هل_قاموس", "هل_خريطة|هل_سجل"}, {"g", "هل_عدم", "هل_فارغ|هل_لاشيء"}, {"g", "هل_دالة", "هل_وظيفة"},
  {"g", "لديه", "يملك|هل_يملك"},
  {"g", "نسق", "نسّق|صيغ|قالب_نص"},
  {"g", "كوروتين", "روتين|مهمة_متعاونة"},
  {"g", "استأنف", "واصل_الروتين|استكمل"},
  {"g", "منتهي", "انتهى|هل_انتهى"},
  {"g", "نم", "نوم|سبات|تمهل"},
  {"g", "شغل", "ابدأ_البرنامج|شغّل"},
  {"g", "كل_الوعود", "انتظر_الكل|جمع_الوعود"},
  {"g", "سباق", "اول_وعد|الاسرع"},
  {"g", "عملية", "عملية_فرعية|مهمة_متوازية"},
  {"g", "جلب_غير_متزامن", "اطلب_غير_متزامن|جلب_متوازي"},
  {"m", "طول", "حجم|عدد"}, {"m", "قسم", "قطع|فصل|جزّئ"}, {"m", "نظف", "قص|شذب|احذف_الفراغات"},
  {"m", "كبير", "حروف_كبيرة|كبّر"}, {"m", "صغير", "حروف_صغيرة|صغّر"}, {"m", "يبدأ_ب", "يبتدئ_ب|يبدا_بـ"},
  {"m", "يحتوي", "يشمل|فيه|يتضمن|تضمن"}, {"m", "ابدل", "استبدل|بدّل|غيّر"}, {"m", "جزء", "شريحة|اقتطع"},
  {"m", "ابحث", "موضع|فهرس|اين"}, {"m", "كرر", "كرّر|ضاعف"}, {"m", "اعكس", "اقلب|عكس"}, {"m", "حروف", "احرف"},
  {"m", "اضف", "ألحق|الحق|ادفع|أضف|اضافة|زد"}, {"m", "ادرج", "اقحم|ادخل_في"}, {"m", "احذف", "ازل|امحي|امح|انزع"},
  {"m", "اسحب", "انزع_الاخير|ابرز"}, {"m", "ادمج", "اربط|ضم|صل"}, {"m", "رتب", "افرز|فرز|صنّف"},
  {"m", "حول", "طبق|خرّط|حوّل|خريطة"}, {"m", "رشح", "صفي|فلتر|اختر_حسب"}, {"m", "اطوي", "قلص|جمّع|اختزل"},
  {"m", "نفذ", "لكل_عنصر|كرر_على|مرّ_على"}, {"m", "انسخ", "استنسخ|نسخة"}, {"m", "امسح", "افرغ|فرّغ"},
  {"m", "مجموع", "اجمع|جمع_العناصر"}, {"m", "مفاتيح", "المفاتيح|اسماء"}, {"m", "قيم", "القيم|محتويات"},
  {"m", "ازواج", "عناصر|ازواج_القيم"}, {"m", "دمج", "ادمج_مع|حدّث|حدث"}, {"m", "جد", "اوجد|ابحث_عن|جد_اول"},
  {"m", "اي", "بعض|اي_منها|يوجد"}, {"m", "كل", "جميع|الكل|كلها"}, {"m", "سطور", "اسطر|الاسطر"}, {"m", "نسق", "صيغ"},
  {"M", "رياضيات", "حساب|رياضة|ارقام"}, {"M", "عشوائي", "عشوائية|حظ|صدفة"}, {"M", "زمن", "وقت|توقيت"},
  {"M", "نظام", "نظام_التشغيل|نظام_الكمبيوتر"}, {"M", "ملفات", "ملف|ملفات_النظام"}, {"M", "جيسون", "json|ترميز_جيسون"},
  {"M", "شبكة", "ويب|انترنت|انترنيت|اتصالات"}, {"M", "سوكت", "مقبس|مآخذ"}, {"M", "ويبسوكت", "مقبس_ويب|ويب_سوكت"},
  {"M", "تشفير", "تعمية|كريبتو"}, {"M", "نمط", "تعبير_نمطي|ريجكس|تعابير"}, {"M", "قاعدة", "قاعدة_بيانات|قواعد"},
  {"M", "ضغط", "زيب|ارشفة"}, {"M", "حلقة", "احداث|حلقة_الاحداث"}, {"M", "ضخم", "اعداد_كبيرة|عدد_ضخم"},
  {"M", "مسارات", "مسار|مسارات_الملفات"}, {"M", "ألوان", "الوان|تلوين"}, {"M", "سجل", "تسجيل|لوغ|سجلات"},
  {"M", "اختبار", "اختبارات|فحص"}, {"M", "وسائط", "معاملات_الاوامر|سطر_الاوامر"}, {"M", "جداول", "csv|جدول"},
  {"رياضيات", "جذر", "جذر_تربيعي"}, {"رياضيات", "ارضي", "سفلي|ارضية|قاع"}, {"رياضيات", "سقف", "علوي|سقفي"},
  {"رياضيات", "مطلق", "قيمة_مطلقة|معياري"}, {"رياضيات", "قرب", "دور|تقريب"}, {"رياضيات", "قوة", "اس_قوة|اسّ"},
  {"رياضيات", "اكبر", "اعلى|اقصى|اكبر_قيمة"}, {"رياضيات", "اصغر", "ادنى|اقل|اصغر_قيمة"}, {"رياضيات", "باي", "ط"},
  {"رياضيات", "لوغ", "لوغاريتم"}, {"رياضيات", "جيب", "جا"}, {"رياضيات", "جتا", "جتا_زاوية"}, {"رياضيات", "ظل", "ظا"},
  {"عشوائي", "رقم", "عشوائي_عشري"}, {"عشوائي", "صحيح", "عدد_صحيح|عدد_عشوائي"}, {"عشوائي", "اختر", "التقط_عشوائيا|اخذ"},
  {"عشوائي", "اخلط", "بعثر|خلط"}, {"عشوائي", "بذرة", "بذر"},
  {"زمن", "الان", "حاليا|الوقت_الان"}, {"زمن", "نوم", "تمهل|انتظر_مدة"}, {"زمن", "تاريخ", "تقويم"},
  {"زمن", "تنسيق", "صيغة|نسق_وقت"}, {"زمن", "ساعة", "عداد|مؤقت_دقيق"}, {"زمن", "من_تاريخ", "الى_طابع_زمني"},
  {"نظام", "بيئة", "متغير_بيئة|متغير"}, {"نظام", "اخرج", "انهي|انتهاء|اغلق_البرنامج"}, {"نظام", "نفذ", "شغل_امر|امر"},
  {"نظام", "شغل", "تشغيل|شغل_امر_بلا_ناتج"}, {"نظام", "عملية", "ابدأ_عملية|شغل_عملية"}, {"نظام", "مسار", "المجلد_الحالي"},
  {"ملفات", "اقرا", "اقرأ|قراءة|حمل"}, {"ملفات", "اكتب", "احفظ|كتابة|خزن"}, {"ملفات", "الحق", "ضيف|الحاق"},
  {"ملفات", "موجود", "هل_موجود|يوجد"}, {"ملفات", "احذف", "امسح_ملف|ازل"}, {"ملفات", "قائمة", "محتويات|اعرض_المجلد"},
  {"ملفات", "انشئ_مجلد", "مجلد_جديد|اصنع_مجلد"},
  {"جيسون", "ترميز", "الى_جيسون|اكتب"}, {"جيسون", "فك", "من_جيسون|حلل|اقرا"},
  {"شبكة", "جلب", "اطلب|احضر|طلب|جلب_رابط"}, {"شبكة", "خادم", "شغل_خادم|خدمة"}, {"شبكة", "ترميز_رابط", "ترميز_url"},
  {"سوكت", "اتصل", "وصل"}, {"سوكت", "استمع", "اصغ"}, {"سوكت", "اقبل", "استقبل_اتصال"}, {"سوكت", "ارسل", "ابعث"},
  {"سوكت", "استقبل", "تسلم"}, {"سوكت", "اغلق", "اقفل|انه"}, {"سوكت", "سطر", "اقرا_سطر"},
  {"ويبسوكت", "اتصل", "وصل"}, {"ويبسوكت", "ارسل", "ابعث"}, {"ويبسوكت", "استقبل", "تسلم"}, {"ويبسوكت", "اغلق", "اقفل"},
  {"نمط", "ابحث", "جد|اعثر"}, {"نمط", "هل", "يطابق|مطابقة"}, {"نمط", "كل", "جميع"}, {"نمط", "ابدل", "استبدل"}, {"نمط", "قسم", "فصل"},
  {"قاعدة", "افتح", "صل|اتصل"}, {"قاعدة", "نفذ", "شغل|امر"}, {"قاعدة", "استعلم", "اسأل|اجلب|اختر"}, {"قاعدة", "اغلق", "اقفل"},
  {"حلقة", "بعد", "بعد_مدة|مؤقت"}, {"حلقة", "كل", "كل_مدة|دوري"}, {"حلقة", "شغل", "ابدأ|شغل_الاحداث"}, {"حلقة", "اوقف", "قف|انهي"},
  {"ضغط", "فك", "فك_الضغط"},
  {NULL, NULL, NULL}};
static void alias_dict(Dict *d, const char *canon, const char *alts) {
  Value v; if (!dget(d, OBJV(normstr(canon)), &v)) return;
  char buf[512]; snprintf(buf, sizeof buf, "%s", alts);
  for (char *w = strtok(buf, "|"); w; w = strtok(NULL, "|")) { Value ex; Str *n = normstr(w); if (!dget(d, OBJV(n), &ex)) dset(d, OBJV(n), v); }
}
static void apply_aliases(void) {
  for (int i = 0; ALIASES[i].scope; i++) {
    const char *sc = ALIASES[i].scope;
    if (!strcmp(sc, "g")) alias_dict(&vm.builtins, ALIASES[i].canon, ALIASES[i].alts);
    else if (!strcmp(sc, "m")) alias_dict(&vm.mid, ALIASES[i].canon, ALIASES[i].alts);
    else if (!strcmp(sc, "M")) { alias_dict(&vm.natmods, ALIASES[i].canon, ALIASES[i].alts); alias_dict(&vm.stdlibd, ALIASES[i].canon, ALIASES[i].alts); }
    else { Value m; if (dget(&vm.natmods, OBJV(normstr(sc)), &m) && IS_T(m, O_MAP)) alias_dict(&AS_MAP(m)->d, ALIASES[i].canon, ALIASES[i].alts); }
  }
}
