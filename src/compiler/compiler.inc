typedef struct { Str *name; int depth; bool captured, isconst; uint16_t tmask; List *tinfo; } Local;
typedef struct { uint8_t index; bool isLocal; } UpvDesc;
typedef enum { FT_SCRIPT, FT_FUNC, FT_METHOD, FT_INIT } FT;
typedef struct Loop { struct Loop *enc; int start, depth, tryDepth, nbreaks, finBase; int breaks[256]; } Loop;
typedef struct Compiler {
  struct Compiler *enc; Fun *fn; FT type; Local locals[256]; int lcount; UpvDesc upv[256]; int depth, tryDepth; Loop *loop;
  int finSlot[16], finTry[16], nfins; uint16_t rmask; List *rinfo;
} Compiler;
typedef struct ClassComp { struct ClassComp *enc; bool hasSuper; } ClassComp;
static Str *g_cur_file = NULL;
static Compiler *C; static ClassComp *CC; static bool g_getter = false; static int g_kind = 0; static Str *g_arrow_first = NULL; static bool g_skip_lp = false; static bool g_debug_compile = false;

static Chunk *chunk(void) { return &C->fn->ch; }

static void emit(uint8_t b) {
  Chunk *c = chunk();
  if (c->n == c->cap) { c->cap = c->cap ? c->cap * 2 : 64; c->code = xrealloc(c->code, c->cap); c->lines = xrealloc(c->lines, sizeof(int) * c->cap); }
  c->code[c->n] = b; c->lines[c->n] = P.prev.line; c->n++;
}
static void emit2(uint8_t a, uint8_t b) { emit(a); emit(b); }
static void emit16(int v) { emit((v >> 8) & 0xff); emit(v & 0xff); }
static int make_const(Value v) {
  Chunk *c = chunk();
  if (IS_T(v, O_STR)) for (int i = 0; i < c->consts.n; i++) if (c->consts.v[i].t == V_OBJ && c->consts.v[i].as.o == v.as.o) return i;
  if (c->consts.n >= 65535) CERR("عدد الثوابت كبير جدا");
  varr_push(&c->consts, v); return c->consts.n - 1;
}
static void emit_const(Value v) { emit(OP_CONST); emit16(make_const(v)); }
static int emit_jump(uint8_t op) { emit(op); emit(0xff); emit(0xff); return chunk()->n - 2; }
static void patch_jump(int off) {
  int j = chunk()->n - off - 2; if (j > 0xffff) CERR("القفزة كبيرة جدا");
  chunk()->code[off] = (j >> 8) & 0xff; chunk()->code[off + 1] = j & 0xff;
}
static void emit_loop(int start) {
  emit(OP_LOOP); int off = chunk()->n - start + 2; if (off > 0xffff) CERR("الحلقة كبيرة جدا"); emit16(off);
}
static void init_compiler(Compiler *c, FT type, Str *name) {
  c->enc = C; c->type = type; c->lcount = 0; c->depth = 0; c->tryDepth = 0; c->loop = NULL; c->nfins = 0; c->rmask = 0; c->rinfo = NULL;
  c->fn = new_fun(); c->fn->name = name; c->fn->file = g_cur_file; C = c;
  Local *l = &c->locals[c->lcount++]; l->depth = 0; l->captured = false; l->isconst = false; l->tmask = 0; l->tinfo = NULL;
  l->name = (type == FT_METHOD || type == FT_INIT) ? vm.s_this : NULL;
}
static void emit_check_fwd(void);
static Fun *end_compiler(void) {
  if (C->type == FT_INIT) emit2(OP_GET_LOCAL, 0); else { emit(OP_NIL); emit_check_fwd(); }
  emit(OP_RETURN);
  Fun *f = C->fn; C = C->enc;
  if (!g_debug_compile) quicken_fun(f);
  f->gc = xalloc(sizeof(int) * (2 * f->ch.consts.n + 1));
  for (int i = 0; i < 2 * f->ch.consts.n + 1; i++) f->gc[i] = 0;
  return f;
}

static void begin_scope(void) { C->depth++; }
static void end_scope(void) {
  C->depth--;
  while (C->lcount > 0 && C->locals[C->lcount - 1].depth > C->depth) { emit(C->locals[C->lcount - 1].captured ? OP_CLOSE_UPV : OP_POP); C->lcount--; }
}
static int add_local(Str *name, bool init, bool isconst) {
  if (C->lcount == 256) CERR("متغيرات محلية كثيرة جدا");
  Local *l = &C->locals[C->lcount++]; l->name = name; l->depth = init ? C->depth : -1; l->captured = false; l->isconst = isconst; l->tmask = 0; l->tinfo = NULL;
  if (g_debug_compile) {
    Fun *f = C->fn; if (f->ndbg == f->capdbg) { f->capdbg = f->capdbg ? f->capdbg * 2 : 8; f->dbgname = xrealloc(f->dbgname, sizeof(Str *) * f->capdbg); f->dbgslot = xrealloc(f->dbgslot, sizeof(int) * f->capdbg); }
    f->dbgname[f->ndbg] = name; f->dbgslot[f->ndbg++] = C->lcount - 1;
  }
  return C->lcount - 1;
}

static int resolve_local(Compiler *c, Str *name, bool *isconst) {
  for (int i = c->lcount - 1; i >= 0; i--) {
    Local *l = &c->locals[i];
    if (l->name && l->name == name) { if (l->depth == -1) CERR("لا يمكن قراءة المتغير داخل تعريفه"); *isconst = l->isconst; return i; }
  }
  return -1;
}
static int add_upvalue(Compiler *c, int index, bool isLocal) {
  int n = c->fn->upc;
  for (int i = 0; i < n; i++) if (c->upv[i].index == index && c->upv[i].isLocal == isLocal) return i;
  if (n == 256) CERR("متغيرات مغلقة كثيرة جدا");
  c->upv[n].isLocal = isLocal; c->upv[n].index = index; return c->fn->upc++;
}
static int resolve_upvalue(Compiler *c, Str *name, bool *isconst) {
  if (!c->enc) return -1;
  int l = resolve_local(c->enc, name, isconst);
  if (l >= 0) { c->enc->locals[l].captured = true; return add_upvalue(c, l, true); }
  int u = resolve_upvalue(c->enc, name, isconst);
  if (u >= 0) return add_upvalue(c, u, false);
  return -1;
}
static bool is_gconst(Str *n) { for (int i = 0; i < P.ngconst; i++) if (P.gconst[i] == n) return true; return false; }

typedef enum { PREC_NONE, PREC_ASSIGN, PREC_TERNARY, PREC_COALESCE, PREC_OR, PREC_AND, PREC_EQ, PREC_CMP, PREC_BOR, PREC_BXOR, PREC_BAND, PREC_SHIFT, PREC_TERM, PREC_FACTOR, PREC_UNARY, PREC_POW, PREC_CALL, PREC_PRIMARY } Prec;
typedef void (*ParseFn)(bool ca);
typedef struct { ParseFn prefix, infix; Prec prec; } Rule;
static void expression(void); static void declaration(void); static void statement(void); static void block(void);

static void parse_prec(Prec p);
static void unary(bool ca);

static void function(FT type, Str *name);
static void grouping(bool), unary(bool), binary(bool), number(bool), string_(bool), literal(bool), variable(bool),
    this_(bool), super_(bool), lambda(bool), list_lit(bool), map_lit(bool), call(bool), dot(bool), index_(bool),
    and_(bool), or_(bool), ternary(bool), coalesce(bool), async_prefix(bool), await_(bool), yield_(bool), qdot(bool);
static const Rule rules[T_ERR + 1] = {
  [T_LP] = {grouping, call, PREC_CALL}, [T_LB] = {list_lit, index_, PREC_CALL}, [T_LC] = {map_lit, NULL, PREC_NONE},
  [T_DOT] = {NULL, dot, PREC_CALL}, [T_QUEST] = {NULL, ternary, PREC_TERNARY}, [T_QQ] = {NULL, coalesce, PREC_COALESCE},
  [T_PLUS] = {NULL, binary, PREC_TERM}, [T_MINUS] = {unary, binary, PREC_TERM},
  [T_STAR] = {NULL, binary, PREC_FACTOR}, [T_SLASH] = {NULL, binary, PREC_FACTOR}, [T_PCT] = {NULL, binary, PREC_FACTOR}, [T_IDIV] = {NULL, binary, PREC_FACTOR},
  [T_BAND] = {NULL, binary, PREC_BAND}, [T_BOR] = {NULL, binary, PREC_BOR}, [T_BXOR] = {NULL, binary, PREC_BXOR}, [T_SHL] = {NULL, binary, PREC_SHIFT}, [T_SHR] = {NULL, binary, PREC_SHIFT},
  [T_QDOT] = {NULL, qdot, PREC_CALL},
  [T_POW] = {NULL, binary, PREC_POW}, [T_BANG] = {unary, NULL, PREC_NONE}, [T_NOT] = {unary, NULL, PREC_NONE},
  [T_NE] = {NULL, binary, PREC_EQ}, [T_EQEQ] = {NULL, binary, PREC_EQ},
  [T_GT] = {NULL, binary, PREC_CMP}, [T_GE] = {NULL, binary, PREC_CMP}, [T_LT] = {NULL, binary, PREC_CMP}, [T_LE] = {NULL, binary, PREC_CMP},
  [T_IN] = {NULL, binary, PREC_CMP},
  [T_IDENT] = {variable, NULL, PREC_NONE}, [T_STR] = {string_, NULL, PREC_NONE}, [T_NUM] = {number, NULL, PREC_NONE},
  [T_AND] = {NULL, and_, PREC_AND}, [T_ANDAND] = {NULL, and_, PREC_AND}, [T_OR] = {NULL, or_, PREC_OR}, [T_OROR] = {NULL, or_, PREC_OR},
  [T_TRUE] = {literal, NULL, PREC_NONE}, [T_FALSE] = {literal, NULL, PREC_NONE}, [T_NIL] = {literal, NULL, PREC_NONE},
  [T_ASYNC] = {async_prefix, NULL, PREC_NONE}, [T_GEN] = {async_prefix, NULL, PREC_NONE}, [T_AWAIT] = {await_, NULL, PREC_NONE}, [T_YIELD] = {yield_, NULL, PREC_NONE},
  [T_THIS] = {this_, NULL, PREC_NONE}, [T_SUPER] = {super_, NULL, PREC_NONE}, [T_FUN] = {lambda, NULL, PREC_NONE},
};
static bool is_assign_tok(TT t) { return t == T_EQ || t == T_PLUSEQ || t == T_MINUSEQ || t == T_STAREQ || t == T_SLASHEQ || t == T_PCTEQ || t == T_IDIVEQ || t == T_POWEQ || t == T_INC || t == T_DEC; }
static uint8_t compound_op(TT t) {
  switch (t) { case T_PLUSEQ: case T_INC: return OP_ADD; case T_MINUSEQ: case T_DEC: return OP_SUB; case T_STAREQ: return OP_MUL; case T_SLASHEQ: return OP_DIV; case T_IDIVEQ: return OP_IDIV; case T_POWEQ: return OP_POW; default: return OP_MOD; }
}
static void rhs_value(TT at) { if (at == T_INC || at == T_DEC) emit_const(NUMV(1)); else expression(); }
static void parse_prec_impl(Prec p) {
  advance(); ParseFn pre = rules[P.prev.t].prefix;
  if (!pre) CERR("توقعت تعبيرا");
  bool ca = p <= PREC_ASSIGN; pre(ca);
  while (p <= rules[P.cur.t].prec) {
    if (P.cur.nl && (P.cur.t == T_LP || P.cur.t == T_LB)) break;
    advance(); rules[P.prev.t].infix(ca);
  }
  if (ca && is_assign_tok(P.cur.t)) cerror_at(&P.cur, "هدف الإسناد غير صالح");
}
#define REC_MAX 500
#define REC_ENTER() do { if (++P.rec > REC_MAX) CERR("التداخل عميق جدا: تجاوز الحد الأقصى للتداخل في الشيفرة"); } while (0)
static void unary_impl(bool ca);
static void parse_prec_impl(Prec p);
static void statement_impl(void);
static void declaration_impl(void);
static void function_impl(FT type, Str *name);
static void parse_prec(Prec p) { REC_ENTER(); parse_prec_impl(p); P.rec--; }
static void unary(bool ca) { REC_ENTER(); unary_impl(ca); P.rec--; }
static void statement(void) { REC_ENTER(); statement_impl(); P.rec--; }
static void declaration(void) { REC_ENTER(); declaration_impl(); P.rec--; }
static void function(FT type, Str *name) { REC_ENTER(); function_impl(type, name); P.rec--; }
static void expression(void) { parse_prec(PREC_ASSIGN); }

static void number(bool ca) { (void)ca; emit_const(NUMV(P.prev.num)); }
static void literal(bool ca) { (void)ca; emit(P.prev.t == T_TRUE ? OP_TRUE : P.prev.t == T_FALSE ? OP_FALSE : OP_NIL); }
static bool paren_is_arrow(void);
static void grouping(bool ca) {
  (void)ca;
  if (paren_is_arrow()) { g_skip_lp = true; function(FT_FUNC, NULL); return; }
  expression(); consume(T_RP, "توقعت ')' بعد التعبير");
}

static int hexv(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; }
static void compile_expr_from(const char *txt, int len) {
  Parser saved = P; char *buf = xalloc(len + 1); memcpy(buf, txt, len); buf[len] = 0;
  P.sc.src = P.sc.cur = buf; P.sc.line = saved.prev.line;
  advance(); expression();
  if (!check(T_EOF)) cerror_at(&P.cur, "تعبير غير صالح داخل النص");
  free(buf);
  P = saved;
}

static void compile_string(const char *s, int n, bool interp) {
  Buf lit; buf_init(&lit); bool started = false;
  for (int i = 0; i < n; i++) {
    char c = s[i];
    if (c == '\\' && i + 1 < n) {
      i++;
      switch (s[i]) {
        case 'n': buf_addc(&lit, '\n'); break; case 't': buf_addc(&lit, '\t'); break; case 'r': buf_addc(&lit, '\r'); break;
        case '0': buf_addc(&lit, 0); break; case '\\': buf_addc(&lit, '\\'); break; case '"': buf_addc(&lit, '"'); break;
        case '\'': buf_addc(&lit, '\''); break; case '$': buf_addc(&lit, '$'); break; case '\n': break;
        case 'u': { uint32_t cp = 0; int k = 0; while (k < 4 && i + 1 < n && hexv(s[i + 1]) >= 0) { cp = cp * 16 + hexv(s[i + 1]); i++; k++; } char t[4]; buf_add(&lit, t, u8_encode(cp, t)); break; }
        case 'x': { uint32_t cp = 0; int k = 0; while (k < 2 && i + 1 < n && hexv(s[i + 1]) >= 0) { cp = cp * 16 + hexv(s[i + 1]); i++; k++; } buf_addc(&lit, (char)cp); break; }
        default: buf_addc(&lit, '\\'); buf_addc(&lit, s[i]);
      }
    } else if (interp && c == '$' && i + 1 < n && s[i + 1] == '{') {
      int depth = 1, j = i + 2;
      while (j < n && depth > 0) {
        if (s[j] == '{') depth++; else if (s[j] == '}') depth--;
        else if (s[j] == '"' || s[j] == '\'') { char q = s[j++]; while (j < n && s[j] != q) { if (s[j] == '\\') j++; j++; } }
        if (depth > 0) j++;
      }
      if (depth != 0) CERR("'${' غير مغلقة");
      if (!started) { emit_const(OBJV(intern(lit.p ? lit.p : "", lit.n))); started = true; }
      else if (lit.n > 0) { emit_const(OBJV(intern(lit.p, lit.n))); emit(OP_CONCAT); }
      lit.n = 0; if (lit.p) lit.p[0] = 0;
      compile_expr_from(s + i + 2, j - (i + 2)); emit(OP_CONCAT);
      i = j;
    } else buf_addc(&lit, c);
  }
  if (!started) emit_const(OBJV(intern(lit.p ? lit.p : "", lit.n)));
  else if (lit.n > 0) { emit_const(OBJV(intern(lit.p, lit.n))); emit(OP_CONCAT); }
  free(lit.p);
}
static void string_(bool ca) { (void)ca; compile_string(P.prev.start, P.prev.len, true); }

enum { TY_NIL = 1, TY_BOOL = 2, TY_NUM = 4, TY_STR = 8, TY_LIST = 16, TY_MAP = 32, TY_FUN = 64, TY_INST = 128, TY_CLS = 256, TY_CORO = 512, TY_INT = 1024, TY_CLASSNAME = 2048, TY_ANY = 4095 };
static const struct { const char *name; uint16_t mask; } TYPE_NAMES[] = {
  {"رقم", TY_NUM}, {"عدد", TY_NUM}, {"عدد_صحيح", TY_INT}, {"رقم_صحيح", TY_INT}, {"نص", TY_STR}, {"منطقي", TY_BOOL}, {"قائمة", TY_LIST}, {"قاموس", TY_MAP},
  {"دالة", TY_FUN}, {"عدم", TY_NIL}, {"اي", TY_ANY}, {"أي", TY_ANY}, {"كائن", TY_INST}, {"صنف", TY_CLS}, {"كوروتين", TY_CORO}, {NULL, 0}};
static void type_atom(uint16_t *mask, List *info, Buf *disp) {
  if (check(T_NIL)) { advance(); *mask |= TY_NIL; buf_fmt(disp, "عدم"); return; }
  if (check(T_FUN)) { advance(); *mask |= TY_FUN; buf_fmt(disp, "دالة"); return; }
  if (!check(T_IDENT) || !P.cur.s) cerror_at(&P.cur, "توقعت اسم نوع");
  advance(); Str *nm = P.prev.s; uint16_t m = 0;
  for (int i = 0; TYPE_NAMES[i].name; i++) if (normstr(TYPE_NAMES[i].name) == nm) { m = TYPE_NAMES[i].mask; break; }
  if (m) *mask |= m; else { *mask |= TY_CLASSNAME; varr_push(&info->a, OBJV(nm)); }
  buf_add(disp, P.prev.start, P.prev.len);
  if ((m == TY_LIST || m == TY_MAP) && check(T_LT)) {
    advance(); buf_addc(disp, '<'); uint16_t dm = 0; List *dl = new_list(); Buf db; buf_init(&db);
    do { type_atom(&dm, dl, &db); if (check(T_COMMA)) buf_addc(&db, ','); } while (match(T_COMMA));
    consume(T_GT, "توقعت '>' بعد نوع العناصر"); buf_add(disp, db.p ? db.p : "", db.n); free(db.p); buf_addc(disp, '>');
  }
}
static void parse_type(uint16_t *mask, List **info, const char *ctx) {
  List *l = new_list(); Buf disp; buf_init(&disp); uint16_t m = 0;
  varr_push(&l->a, NIL); varr_push(&l->a, NIL);
  do {
    if (disp.n) buf_fmt(&disp, " | ");
    type_atom(&m, l, &disp);
    if (check(T_QUEST)) { advance(); m |= TY_NIL; buf_addc(&disp, '?'); }
  } while (match(T_BOR));
  l->a.v[0] = OBJV(intern(buf_cstr(&disp), disp.n)); free(disp.p);
  l->a.v[1] = OBJV(intern_c(ctx));
  *mask = m; *info = l;
}
static void emit_check(uint16_t mask, List *info, const char *ctx) {
  if (!mask || (mask & TY_ANY) == TY_ANY) return;
  if (ctx && IS_NIL(info->a.v[1])) info->a.v[1] = OBJV(intern_c(ctx));
  emit(OP_CHECK); emit16(mask); emit16(make_const(OBJV(info)));
}
static void emit_check_fwd(void) { emit_check(C->rmask, C->rinfo, NULL); }
static bool var_type(Str *name, uint16_t *mask, List **info) {
  for (Compiler *c = C; c; c = c->enc)
    for (int i = c->lcount - 1; i >= 0; i--) if (c->locals[i].name && c->locals[i].name == name) { *mask = c->locals[i].tmask; *info = c->locals[i].tinfo; return *mask != 0; }
  for (int i = P.ngt - 1; i >= 0; i--) if (P.gtn[i] == name) { *mask = P.gtm[i]; *info = P.gti[i]; return true; }
  return false;
}
static void ctx_name(char *out, size_t n, const char *kind, Str *name) { snprintf(out, n, "%s '%s'", kind, name ? name->chars : "؟"); }
static void ctx_tok(char *out, size_t n, const char *kind, const Token *t) { snprintf(out, n, "%s '%.*s'", kind, t->len > 80 ? 80 : t->len, t->start); }

static uint8_t compound_op_a(TT t) { return t == T_PLUSEQ ? OP_ADD_APPEND : compound_op(t); }
static void named_variable(Str *name, bool ca) {
  uint8_t gop, sop; bool isconst = false; int arg = resolve_local(C, name, &isconst);
  if (arg >= 0) { gop = OP_GET_LOCAL; sop = OP_SET_LOCAL; }
  else if ((arg = resolve_upvalue(C, name, &isconst)) >= 0) { gop = OP_GET_UPV; sop = OP_SET_UPV; }
  else { arg = make_const(OBJV(name)); gop = OP_GET_GLOBAL; sop = OP_SET_GLOBAL; isconst = is_gconst(name); }
  bool glob = gop == OP_GET_GLOBAL;
#define EMIT_ARG(op) do { emit(op); if (glob) emit16(arg); else emit((uint8_t)arg); } while (0)
  if (ca && is_assign_tok(P.cur.t)) {
    if (isconst) CERR("لا يمكن تغيير قيمة ثابت");
    TT at = P.cur.t; advance();
    if (at == T_EQ) {
      int st = chunk()->n; expression();

      if (P.last_add_c == (void *)C && P.last_add_pos == chunk()->n - 1 && st + (glob ? 3 : 2) < chunk()->n && chunk()->code[st] == gop
          && (glob ? (chunk()->code[st + 1] == ((arg >> 8) & 0xff) && chunk()->code[st + 2] == (arg & 0xff)) : chunk()->code[st + 1] == (uint8_t)arg))
        chunk()->code[P.last_add_pos] = OP_ADD_APPEND;
    }
    else { EMIT_ARG(gop); rhs_value(at); emit(compound_op_a(at)); }
    { uint16_t tm; List *ti; if (var_type(name, &tm, &ti)) { char cx[200]; ctx_name(cx, sizeof cx, "المتغير", name); emit_check(tm, ti, cx); } }
    EMIT_ARG(sop);
  } else EMIT_ARG(gop);
}
static void variable(bool ca) {
  if (P.cur.t == T_ARROW && !P.cur.nl) { g_arrow_first = P.prev.s; function(FT_FUNC, NULL); return; }
  named_variable(P.prev.s, ca);
}
static void this_(bool ca) { (void)ca; if (!CC) CERR("'هذا' فقط داخل الأصناف"); named_variable(vm.s_this, false); }
static bool has_spread_until(TT close) {
  Scanner sv = P.sc; int depth = 1; Token t = P.cur; bool res = false;
  for (;;) {
    if (t.t == T_EOF) break;
    if (t.t == T_LP || t.t == T_LB || t.t == T_LC) depth++;
    else if (t.t == T_RP || t.t == T_RB || t.t == T_RC) { depth--; if (depth == 0) break; }
    else if (t.t == T_ELLIPSIS && depth == 1) { res = true; break; }
    t = scan_token();
  }
  (void)close; P.sc = sv; return res;
}
static bool paren_is_arrow(void) {
  Scanner sv = P.sc; int depth = 1; Token t = P.cur; bool res = false;
  for (;;) {
    if (t.t == T_EOF) break;
    if (t.t == T_LP || t.t == T_LB || t.t == T_LC) depth++;
    else if (t.t == T_RP || t.t == T_RB || t.t == T_RC) { depth--; if (depth == 0) {
        Token n = scan_token();
        if (n.t == T_COLON) {
          for (int k = 0; k < 24; k++) {
            n = scan_token();
            if (n.t == T_IDENT || n.t == T_NIL || n.t == T_FUN || n.t == T_BOR || n.t == T_QUEST || n.t == T_LT || n.t == T_GT || n.t == T_COMMA) continue;
            break;
          }
        }
        res = n.t == T_ARROW; break; } }
    t = scan_token();
  }
  P.sc = sv; return res;
}
static bool brace_is_map(void) {
  Scanner sv = P.sc; Token t1 = scan_token(); bool res = false;
  if (t1.t == T_RC || t1.t == T_LB) res = true;
  else if (t1.t == T_STR || t1.t == T_NUM || t1.s != NULL) { Token t2 = scan_token(); res = t2.t == T_COLON; }
  P.sc = sv; return res;
}
static uint8_t arglist(bool *spread) {
  int n = 0; *spread = false;
  if (!check(T_RP) && has_spread_until(T_RP)) {
    *spread = true; emit(OP_LNEW);
    do {
      if (check(T_RP)) break;
      if (match(T_ELLIPSIS)) { expression(); emit(OP_LEXT); } else { expression(); emit(OP_LPUSH); }
    } while (match(T_COMMA));
    consume(T_RP, "توقعت ')' بعد الوسائط"); return 0;
  }
  if (!check(T_RP)) { do { if (check(T_RP)) break; expression(); if (n == 255) CERR("وسائط كثيرة"); n++; } while (match(T_COMMA)); }
  consume(T_RP, "توقعت ')' بعد الوسائط"); return (uint8_t)n;
}
static void call(bool ca) { (void)ca; bool sp; uint8_t n = arglist(&sp); if (sp) emit(OP_CALLV); else emit2(OP_CALL, n); }
static void dot(bool ca) {
  if (P.cur.s == NULL) cerror_at(&P.cur, "توقعت اسم خاصية بعد '.'");
  advance(); Str *name = P.prev.s; int k = make_const(OBJV(name));
  if (ca && is_assign_tok(P.cur.t)) {
    TT at = P.cur.t; advance();
    if (at == T_EQ) expression();
    else { emit(OP_DUP); emit(OP_GET_PROP); emit16(k); rhs_value(at); emit(compound_op_a(at)); }
    emit(OP_SET_PROP); emit16(k);
  } else if (check(T_LP) && !P.cur.nl) { advance(); bool sp; uint8_t n = arglist(&sp); if (sp) { emit(OP_INVOKEV); emit16(k); } else { emit(OP_INVOKE); emit16(k); emit(n); } }
  else { emit(OP_GET_PROP); emit16(k); }
}
static void index_(bool ca) {
  if (check(T_COLON)) emit(OP_NIL); else expression();
  if (match(T_COLON)) {
    if (check(T_RB)) emit(OP_NIL); else expression();
    consume(T_RB, "توقعت ']'"); emit(OP_SLICE); return;
  }
  consume(T_RB, "توقعت ']'");
  if (ca && is_assign_tok(P.cur.t)) {
    TT at = P.cur.t; advance();
    if (at == T_EQ) expression();
    else { emit(OP_DUP2); emit(OP_GET_INDEX); rhs_value(at); emit(compound_op_a(at)); }
    emit(OP_SET_INDEX);
  } else emit(OP_GET_INDEX);
}
static void unary_impl(bool ca) {
  (void)ca; TT op = P.prev.t; parse_prec(PREC_UNARY); emit(op == T_MINUS ? OP_NEG : OP_NOT);
}

static void binary(bool ca) {
  (void)ca; TT op = P.prev.t; Prec pr = rules[op].prec;
  parse_prec(op == T_POW ? PREC_POW : (Prec)(pr + 1));
  switch (op) {
    case T_PLUS: emit(OP_ADD); P.last_add_pos = chunk()->n - 1; P.last_add_c = C; break; case T_MINUS: emit(OP_SUB); break; case T_STAR: emit(OP_MUL); break;
    case T_SLASH: emit(OP_DIV); break; case T_PCT: emit(OP_MOD); break; case T_POW: emit(OP_POW); break;
    case T_EQEQ: emit(OP_EQ); break; case T_NE: emit2(OP_EQ, OP_NOT); break;
    case T_GT: emit(OP_GT); break; case T_GE: emit(OP_GE); break; case T_LT: emit(OP_LT); break; case T_LE: emit(OP_LE); break;
    case T_IN: emit(OP_IN); break;
    case T_IDIV: emit(OP_IDIV); break; case T_BAND: emit(OP_BAND); break; case T_BOR: emit(OP_BOR); break; case T_BXOR: emit(OP_BXOR); break;
    case T_SHL: emit(OP_SHL); break; case T_SHR: emit(OP_SHR); break; default: break;
  }
}
static void qdot(bool ca) { (void)ca; int j = emit_jump(OP_JNIL); dot(false); patch_jump(j); }
static void and_(bool ca) { (void)ca; int j = emit_jump(OP_JIF); emit(OP_POP); parse_prec(PREC_AND + 1); patch_jump(j); }
static void or_(bool ca) { (void)ca; int j = emit_jump(OP_JIT); emit(OP_POP); parse_prec(PREC_OR + 1); patch_jump(j); }
static void coalesce(bool ca) { (void)ca; int j = emit_jump(OP_JNN); emit(OP_POP); parse_prec(PREC_COALESCE + 1); patch_jump(j); }
static void ternary(bool ca) {
  (void)ca; int e = emit_jump(OP_JIF); emit(OP_POP); expression();
  consume(T_COLON, "توقعت ':' في التعبير الشرطي");
  int j = emit_jump(OP_JUMP); patch_jump(e); emit(OP_POP); parse_prec(PREC_TERNARY); patch_jump(j);
}
static void list_lit(bool ca) {
  (void)ca; int n = 0;
  if (!check(T_RB) && has_spread_until(T_RB)) {
    emit(OP_LNEW);
    do { if (check(T_RB)) break; if (match(T_ELLIPSIS)) { expression(); emit(OP_LEXT); } else { expression(); emit(OP_LPUSH); } } while (match(T_COMMA));
    consume(T_RB, "توقعت ']'"); return;
  }
  if (!check(T_RB)) { do { if (check(T_RB)) break; expression(); n++; } while (match(T_COMMA)); }
  consume(T_RB, "توقعت ']'"); emit(OP_LIST); emit16(n);
}
static void map_lit(bool ca) {
  (void)ca; int n = 0;
  if (!check(T_RC) && has_spread_until(T_RC)) {
    emit(OP_MAP); emit16(0);
    do {
      if (check(T_RC)) break;
      if (match(T_ELLIPSIS)) { expression(); emit(OP_MEXT); continue; }
      if (check(T_LB)) { advance(); expression(); consume(T_RB, "توقعت ']'"); }
      else if (check(T_STR)) { advance(); compile_string(P.prev.start, P.prev.len, false); }
      else if (check(T_NUM)) { advance(); emit_const(NUMV(P.prev.num)); }
      else if (P.cur.s) { advance(); emit_const(OBJV(intern(P.prev.start, P.prev.len))); }
      else cerror_at(&P.cur, "مفتاح غير صالح في القاموس");
      consume(T_COLON, "توقعت ':' بعد المفتاح"); expression(); emit(OP_MSET);
    } while (match(T_COMMA));
    consume(T_RC, "توقعت '}'"); return;
  }
  if (!check(T_RC)) {
    do {
      if (check(T_RC)) break;
      if (check(T_LB)) { advance(); expression(); consume(T_RB, "توقعت ']'"); }
      else if (check(T_STR)) { advance(); compile_string(P.prev.start, P.prev.len, false); }
      else if (check(T_NUM)) { advance(); emit_const(NUMV(P.prev.num)); }
      else if (P.cur.s) { advance(); emit_const(OBJV(intern(P.prev.start, P.prev.len))); }
      else cerror_at(&P.cur, "مفتاح غير صالح في القاموس");
      consume(T_COLON, "توقعت ':' بعد المفتاح"); expression(); n++;
    } while (match(T_COMMA));
  }
  consume(T_RC, "توقعت '}'"); emit(OP_MAP); emit16(n);
}
static void lambda(bool ca) { (void)ca; function(FT_FUNC, NULL); }
static void async_prefix(bool ca) { (void)ca; int k = P.prev.t == T_ASYNC ? 1 : 2; consume(T_FUN, "توقعت 'دالة'"); g_kind = k; function(FT_FUNC, NULL); }
static void await_(bool ca) { (void)ca; parse_prec(PREC_UNARY); emit(OP_AWAIT); }
static void yield_(bool ca) {
  (void)ca;
  if (check(T_RC) || check(T_SEMI) || check(T_RP) || check(T_RB) || check(T_COMMA) || P.cur.nl) emit(OP_NIL); else parse_prec(PREC_TERNARY);
  emit(OP_YIELD);
}
static void super_(bool ca) {
  (void)ca; if (!CC) CERR("'الاب' فقط داخل الأصناف"); if (!CC->hasSuper) CERR("الصنف ليس له أب");
  consume(T_DOT, "توقعت '.' بعد 'الاب'");
  if (!P.cur.s) cerror_at(&P.cur, "توقعت اسم دالة"); advance();
  int k = make_const(OBJV(P.prev.s));
  named_variable(vm.s_this, false);
  if (check(T_LP) && !P.cur.nl) { advance(); bool sp; uint8_t n = arglist(&sp); if (sp) CERR("الانتشار غير مدعوم في استدعاء الاب"); named_variable(vm.s_super, false); emit(OP_SUPER_INVOKE); emit16(k); emit(n); }
  else { named_variable(vm.s_super, false); emit(OP_GET_SUPER); emit16(k); }
}

static void function_impl(FT type, Str *name) {
  Compiler c; init_compiler(&c, type, name); begin_scope();
  c.fn->getter = g_getter; g_getter = false; c.fn->kind = g_kind; g_kind = 0;
  bool hasdef = false;
  if (g_arrow_first) { add_local(g_arrow_first, true, false); C->fn->arity = 1; C->fn->minar = 1; g_arrow_first = NULL; }
  else {
  if (g_skip_lp) g_skip_lp = false; else consume(T_LP, "توقعت '(' بعد اسم الدالة");
  if (!check(T_RP)) {
    do {
      if (check(T_RP)) break;
      bool rest = match(T_ELLIPSIS);
      soft_ident(); if (!P.cur.s || P.cur.t != T_IDENT) cerror_at(&P.cur, "توقعت اسم معامل"); advance();
      Str *pname = P.prev.s; char pcx[200]; ctx_tok(pcx, sizeof pcx, "المعامل", &P.prev); add_local(pname, true, false); int slot = C->lcount - 1; C->fn->arity++;
      uint16_t ptm = 0; List *pti = NULL;
      if (match(T_COLON)) { parse_type(&ptm, &pti, pcx); C->locals[slot].tmask = ptm; C->locals[slot].tinfo = pti; }
      if (rest) { C->fn->variadic = true; if (!check(T_RP)) CERR("المعامل المتبقي يجب أن يكون الأخير"); if (ptm) { emit2(OP_GET_LOCAL, (uint8_t)slot); emit_check(ptm, pti, NULL); emit(OP_POP); } break; }
      if (match(T_EQ)) {
        hasdef = true; emit(OP_JNE_EMPTY); emit((uint8_t)slot); emit(0xff); emit(0xff); int j = chunk()->n - 2;
        expression(); emit2(OP_SET_LOCAL, (uint8_t)slot); emit(OP_POP); patch_jump(j);
      } else { if (hasdef) CERR("المعاملات ذات القيمة الافتراضية يجب أن تكون أخيرة"); C->fn->minar++; }
      if (ptm) { emit2(OP_GET_LOCAL, (uint8_t)slot); emit_check(ptm, pti, NULL); emit(OP_POP); }
    } while (match(T_COMMA));
  }
  consume(T_RP, "توقعت ')' بعد المعاملات");
  if (match(T_COLON)) { char cx[200]; ctx_name(cx, sizeof cx, "قيمة الإرجاع من", name); if (!name) snprintf(cx, sizeof cx, "قيمة الإرجاع من الدالة"); parse_type(&C->rmask, &C->rinfo, cx); }
  }
  if (match(T_ARROW)) {
    if (check(T_LC) && !brace_is_map()) { advance(); while (!check(T_RC) && !check(T_EOF)) declaration(); consume(T_RC, "توقعت '}' بعد جسم الدالة"); }
    else { expression(); emit_check(C->rmask, C->rinfo, NULL); emit(OP_RETURN); }
  }
  else { consume(T_LC, "توقعت '{' قبل جسم الدالة"); while (!check(T_RC) && !check(T_EOF)) declaration(); consume(T_RC, "توقعت '}' بعد جسم الدالة"); }
  Fun *fn = end_compiler();
  emit(OP_CLOSURE); emit16(make_const(OBJV(fn)));
  for (int i = 0; i < fn->upc; i++) { emit(c.upv[i].isLocal ? 1 : 0); emit(c.upv[i].index); }
}

static void block(void) {
  consume(T_LC, "توقعت '{'"); begin_scope();
  while (!check(T_RC) && !check(T_EOF)) declaration();
  consume(T_RC, "توقعت '}'"); end_scope();
}
static void declare_local_var(Str *name, bool isconst) {
  for (int i = C->lcount - 1; i >= 0; i--) {
    Local *l = &C->locals[i]; if (l->depth != -1 && l->depth < C->depth) break;
    if (l->name == name) CERR("المتغير معرّف مسبقا في هذا النطاق");
  }
  add_local(name, false, isconst);
}
static int parse_var_name(bool isconst, Str **out) {
  soft_ident();
  if (P.cur.t != T_IDENT) cerror_at(&P.cur, "توقعت اسم متغير");
  advance(); Str *name = P.prev.s; *out = name;
  if (C->depth > 0) { declare_local_var(name, isconst); return 0; }
  if (isconst && P.ngconst < 256) P.gconst[P.ngconst++] = name;
  return make_const(OBJV(name));
}
static void define_var(int g) {
  if (C->depth > 0) { C->locals[C->lcount - 1].depth = C->depth; return; }
  emit(OP_DEF_GLOBAL); emit16(g);
}
static void destructure(bool isconst) {
  bool list = check(T_LB); advance(); TT close = list ? T_RB : T_RC; Str *names[32]; int n = 0;
  do { if (check(close)) break; soft_ident(); if (P.cur.t != T_IDENT) cerror_at(&P.cur, "توقعت اسم متغير"); advance(); if (n >= 32) CERR("عناصر كثيرة"); names[n++] = P.prev.s; } while (match(T_COMMA));
  consume(close, "توقعت إغلاق النمط"); consume(T_EQ, "توقعت '=' بعد النمط"); expression();
  int tmp = add_local(intern_c("$d"), true, false);
  for (int i = 0; i < n; i++) {
    emit2(OP_GET_LOCAL, (uint8_t)tmp);
    if (list) { emit_const(NUMV(i)); emit(OP_GET_INDEX); } else { emit(OP_GET_PROP); emit16(make_const(OBJV(names[i]))); }
    if (C->depth > 0) { declare_local_var(names[i], isconst); C->locals[C->lcount - 1].depth = C->depth; }
    else { if (isconst && P.ngconst < 256) P.gconst[P.ngconst++] = names[i]; emit(OP_DEF_GLOBAL); emit16(make_const(OBJV(names[i]))); }
  }
}
static void var_decl(bool isconst) {
  do {
    if (check(T_LB) || check(T_LC)) { destructure(isconst); continue; }
    Str *nm; int g = parse_var_name(isconst, &nm);
    uint16_t tm = 0; List *ti = NULL; char cx[200]; ctx_tok(cx, sizeof cx, "المتغير", &P.prev);
    if (match(T_COLON)) parse_type(&tm, &ti, cx);
    if (match(T_EQ)) { expression(); if (tm) emit_check(tm, ti, cx); }
    else { if (isconst) CERR("الثابت يحتاج قيمة"); emit(OP_NIL); }
    if (tm) {
      if (C->depth > 0) { C->locals[C->lcount - 1].tmask = tm; C->locals[C->lcount - 1].tinfo = ti; }
      else if (P.ngt < 256) { P.gtn[P.ngt] = nm; P.gtm[P.ngt] = tm; P.gti[P.ngt++] = ti; }
    }
    define_var(g);
  } while (match(T_COMMA));
  match(T_SEMI);
}
static void fun_decl(void) {
  Str *nm; int g = parse_var_name(false, &nm);
  if (C->depth > 0) C->locals[C->lcount - 1].depth = C->depth;
  function(FT_FUNC, nm); if (C->depth == 0) define_var(g);
}

static void class_decl(void) {
  Str *cname; int g = parse_var_name(false, &cname);
  emit(OP_CLASS); emit16(make_const(OBJV(cname))); define_var(g);
  ClassComp cc; cc.enc = CC; cc.hasSuper = false; CC = &cc;
  if (match(T_EXTENDS) || match(T_COLON)) {
    if (P.cur.t != T_IDENT) cerror_at(&P.cur, "توقعت اسم الصنف الأب"); advance();
    if (P.prev.s == cname) CERR("الصنف لا يرث من نفسه");
    named_variable(P.prev.s, false);
    begin_scope(); add_local(vm.s_super, true, false);
    named_variable(cname, false); emit(OP_INHERIT); cc.hasSuper = true;
  }
  named_variable(cname, false);
  consume(T_LC, "توقعت '{' قبل جسم الصنف");
  while (!check(T_RC) && !check(T_EOF)) {
    bool isStatic = false, isGetter = false;
    if (P.cur.t == T_IDENT && (ctx_id(P.cur.s) == 3 || ctx_id(P.cur.s) == 4)) {
      Scanner sv = P.sc; Token nx = scan_token(); P.sc = sv;
      if (nx.s != NULL) { if (ctx_id(P.cur.s) == 3) isStatic = true; else isGetter = true; advance(); }
    }
    int mkind = 0; if (match(T_ASYNC)) mkind = 1; else if (match(T_GEN)) mkind = 2;
    match(T_FUN);
    if (!P.cur.s) cerror_at(&P.cur, "توقعت اسم دالة"); advance();
    Str *mn = P.prev.s; int k = make_const(OBJV(mn));
    if (isStatic && check(T_EQ)) { advance(); expression(); match(T_SEMI); emit(OP_STATIC); emit16(k); continue; }
    g_getter = isGetter; g_kind = mkind;
    function(isStatic ? FT_FUNC : (mn == vm.init_str ? FT_INIT : FT_METHOD), mn);
    emit(isStatic ? OP_STATIC : OP_METHOD); emit16(k);
  }
  consume(T_RC, "توقعت '}' بعد جسم الصنف"); emit(OP_POP);
  if (cc.hasSuper) end_scope();
  CC = cc.enc;
}

static void emit_set_name(Str *name) {
  bool isconst = false; int arg = resolve_local(C, name, &isconst); uint8_t sop; bool glob = false;
  if (arg >= 0) sop = OP_SET_LOCAL; else if ((arg = resolve_upvalue(C, name, &isconst)) >= 0) sop = OP_SET_UPV;
  else { arg = make_const(OBJV(name)); sop = OP_SET_GLOBAL; glob = true; isconst = is_gconst(name); }
  if (isconst) CERR("لا يمكن تغيير قيمة ثابت");
  emit(sop); if (glob) emit16(arg); else emit((uint8_t)arg);
}
static bool looks_like_destructure_assign(void) {
  Scanner sv = P.sc; bool ok = false; Token t = scan_token();
  for (;;) {
    if (t.t != T_IDENT) break; t = scan_token();
    if (t.t == T_COMMA) { t = scan_token(); continue; }
    if (t.t == T_RB) { t = scan_token(); ok = t.t == T_EQ; }
    break;
  }
  P.sc = sv; return ok;
}
static void destructure_assign(void) {
  advance(); Str *names[32]; int n = 0;
  do { soft_ident(); if (P.cur.t != T_IDENT) cerror_at(&P.cur, "توقعت اسم متغير"); advance(); if (n >= 32) CERR("عناصر كثيرة"); names[n++] = P.prev.s; } while (match(T_COMMA));
  consume(T_RB, "توقعت ']'"); consume(T_EQ, "توقعت '='"); expression(); match(T_SEMI);
  begin_scope(); int tmp = add_local(intern_c("$d"), true, false);
  for (int i = 0; i < n; i++) { emit2(OP_GET_LOCAL, (uint8_t)tmp); emit_const(NUMV(i)); emit(OP_GET_INDEX); emit_set_name(names[i]); emit(OP_POP); }
  end_scope();
}
static void expr_stmt(void) {
  if (check(T_LB) && looks_like_destructure_assign()) { destructure_assign(); return; }
  expression(); match(T_SEMI); emit(OP_POP);
}

static void if_stmt(void) {
  expression(); int t = emit_jump(OP_JIF); emit(OP_POP); block();
  int e = emit_jump(OP_JUMP); patch_jump(t); emit(OP_POP);
  if (match(T_ELIF)) if_stmt();
  else if (match(T_ELSE)) { if (match(T_IF)) if_stmt(); else block(); }
  patch_jump(e);
}
static void loop_begin(Loop *l, int start) { l->finBase = C->nfins; l->enc = C->loop; l->start = start; l->depth = C->depth; l->tryDepth = C->tryDepth; l->nbreaks = 0; C->loop = l; }
static void loop_end(Loop *l) { for (int i = 0; i < l->nbreaks; i++) patch_jump(l->breaks[i]); C->loop = l->enc; }
static void while_stmt(void) {
  Loop lp; int start = chunk()->n; loop_begin(&lp, start);
  expression(); int ex = emit_jump(OP_JIF); emit(OP_POP); block(); emit_loop(start);
  patch_jump(ex); emit(OP_POP); loop_end(&lp);
}
static int range_loop_expr(int *ra, int *rs) {
  static Str *rng = NULL; if (!rng) rng = normstr("مدى");
  *ra = *rs = -1;
  if (P.cur.t != T_IDENT || P.cur.s != rng) return 0;
  bool isconst = false;
  if (resolve_local(C, rng, &isconst) >= 0 || resolve_upvalue(C, rng, &isconst) >= 0) return 0;
  Scanner sv = P.sc; Token t = scan_token();
  if (t.t != T_LP) { P.sc = sv; return 0; }
  int depth = 1, commas = 0;
  while (depth > 0) {
    t = scan_token();
    if (t.t == T_EOF || t.t == T_ERR) { P.sc = sv; return 0; }
    if (t.t == T_LP || t.t == T_LB || t.t == T_LC) depth++;
    else if (t.t == T_RP || t.t == T_RB || t.t == T_RC) depth--;
    else if (t.t == T_COMMA && depth == 1) commas++;
  }
  t = scan_token(); P.sc = sv;
  if (commas > 2 || t.t != T_LC) return 0;
  advance(); consume(T_LP, "توقعت '('");
  if (commas == 0) { expression(); consume(T_RP, "توقعت ')'"); return 1; }

  expression(); *ra = add_local(intern_c("$ra"), true, false);
  consume(T_COMMA, "توقعت '،'"); expression(); int rb = add_local(intern_c("$rb"), true, false);
  if (commas == 2) { consume(T_COMMA, "توقعت '،'"); expression(); } else emit_const(NUMV(1));
  *rs = add_local(intern_c("$rs"), true, false);
  consume(T_RP, "توقعت ')'");
  named_variable(normstr("_عدد_المدى"), false);
  emit2(OP_GET_LOCAL, (uint8_t)*ra); emit2(OP_GET_LOCAL, (uint8_t)rb); emit2(OP_GET_LOCAL, (uint8_t)*rs); emit2(OP_CALL, 3);
  return 2;
}
static void for_stmt(void) {
  begin_scope();
  soft_ident(); if (P.cur.t != T_IDENT) cerror_at(&P.cur, "توقعت اسم متغير بعد 'لكل'"); advance(); Str *v1 = P.prev.s, *v2 = NULL;
  if (match(T_COMMA)) { soft_ident(); if (P.cur.t != T_IDENT) cerror_at(&P.cur, "توقعت اسم متغير"); advance(); v2 = P.prev.s; }
  consume(T_IN, "توقعت 'في' في حلقة 'لكل'");
  int ra, rs; int rk = range_loop_expr(&ra, &rs); if (!rk) expression();
  int base = add_local(intern_c("$c"), true, false);
  emit_const(NUMV(0)); add_local(intern_c("$i"), true, false);
  emit_const(NUMV(0)); add_local(intern_c("$m"), true, false);
  int nv = v2 ? 2 : 1; emit2(OP_ITERPREP, (uint8_t)nv);
  Loop lp; int start = chunk()->n; loop_begin(&lp, start);
  emit(OP_FOREACH); emit((uint8_t)base); emit((uint8_t)nv); emit(0xff); emit(0xff); int ex = chunk()->n - 2;
  begin_scope(); int s1 = add_local(v1, true, false); int s2 = v2 ? add_local(v2, true, false) : -1;
  if (rk == 2) { int tv = v2 ? s2 : s1; emit2(OP_GET_LOCAL, (uint8_t)ra); emit2(OP_GET_LOCAL, (uint8_t)tv); emit2(OP_GET_LOCAL, (uint8_t)rs); emit(OP_MUL); emit(OP_ADD); emit2(OP_SET_LOCAL, (uint8_t)tv); emit(OP_POP); }
  block(); end_scope(); emit_loop(start);
  patch_jump(ex); loop_end(&lp); end_scope();
}
static void repeat_stmt(void) {
  begin_scope(); expression();
  int base = add_local(intern_c("$c"), true, false);
  emit_const(NUMV(0)); add_local(intern_c("$i"), true, false);
  emit_const(NUMV(0)); add_local(intern_c("$m"), true, false);
  emit2(OP_ITERPREP, 1);
  Loop lp; int start = chunk()->n; loop_begin(&lp, start);
  emit(OP_FOREACH); emit((uint8_t)base); emit(1); emit(0xff); emit(0xff); int ex = chunk()->n - 2;
  begin_scope(); add_local(intern_c("$r"), true, false);
  block(); end_scope(); emit_loop(start);
  patch_jump(ex); loop_end(&lp); end_scope();
}
static int emit_fin_calls(int low) {
  int cur = C->tryDepth;
  for (int i = C->nfins - 1; i >= low; i--) {
    for (; cur > C->finTry[i]; cur--) emit(OP_ENDTRY);
    emit2(OP_GET_LOCAL, (uint8_t)C->finSlot[i]); emit2(OP_CALL, 0); emit(OP_POP);
  }
  return cur;
}
static void pop_to_loop(void) {
  int cur = emit_fin_calls(C->loop->finBase);
  for (; cur > C->loop->tryDepth; cur--) emit(OP_ENDTRY);
  for (int i = C->lcount - 1; i >= 0 && C->locals[i].depth > C->loop->depth; i--) emit(C->locals[i].captured ? OP_CLOSE_UPV : OP_POP);
}
static void break_stmt(void) {
  if (!C->loop) CERR("'توقف' خارج حلقة"); match(T_SEMI); pop_to_loop();
  if (C->loop->nbreaks >= 256) CERR("'توقف' كثيرة"); C->loop->breaks[C->loop->nbreaks++] = emit_jump(OP_JUMP);
}
static void continue_stmt(void) {
  if (!C->loop) CERR("'استمر' خارج حلقة"); match(T_SEMI); pop_to_loop(); emit_loop(C->loop->start);
}
static void return_stmt(void) {
  if (C->type == FT_INIT && !(check(T_RC) || check(T_EOF) || check(T_SEMI) || P.cur.nl)) CERR("لا يمكن إرجاع قيمة من البناء");
  if (check(T_RC) || check(T_EOF) || check(T_SEMI) || P.cur.nl) { if (C->type == FT_INIT) emit2(OP_GET_LOCAL, 0); else emit(OP_NIL); }
  else expression();
  if (C->type != FT_INIT) emit_check(C->rmask, C->rinfo, NULL);
  match(T_SEMI); emit_fin_calls(0); emit(OP_RETURN);
}
static Parser finP;
static bool try_has_finally(void) {
  Scanner sv = P.sc; bool r = false; int depth = 1; Token t;
  while (depth > 0) { t = scan_token(); if (t.t == T_EOF) goto out; if (t.t == T_LC) depth++; else if (t.t == T_RC) depth--; }
  t = scan_token();
  if (t.t == T_CATCH) {
    do { t = scan_token(); } while (t.t != T_LC && t.t != T_EOF);
    if (t.t == T_EOF) goto out;
    depth = 1;
    while (depth > 0) { t = scan_token(); if (t.t == T_EOF) goto out; if (t.t == T_LC) depth++; else if (t.t == T_RC) depth--; }
    t = scan_token();
  }
  if (t.t == T_FINALLY) {
    Token lb = scan_token();
    if (lb.t == T_LC) { finP = P; finP.cur = lb; finP.prev = t; r = true; }
  }
out:
  P.sc = sv; return r;
}

static void try_stmt(void) {
  if (!check(T_LC)) cerror_at(&P.cur, "توقعت '{' بعد 'حاول'");
  bool fin = try_has_finally(); int outer = -1, finslot = -1; Parser after;
  begin_scope();
  if (fin) {
    Parser saved = P; P = finP;
    Compiler c; init_compiler(&c, FT_FUNC, NULL); begin_scope();
    consume(T_LC, "توقعت '{'"); while (!check(T_RC) && !check(T_EOF)) declaration(); consume(T_RC, "توقعت '}' بعد اخيرا");
    Fun *fn = end_compiler(); emit(OP_CLOSURE); emit16(make_const(OBJV(fn)));
    for (int i = 0; i < fn->upc; i++) { emit(c.upv[i].isLocal ? 1 : 0); emit(c.upv[i].index); }
    after = P; P = saved;
    finslot = add_local(intern_c("$fin"), true, false);
    if (C->nfins >= 16) CERR("اخيرا متداخلة أكثر من اللازم");
    C->finSlot[C->nfins] = finslot; C->finTry[C->nfins] = C->tryDepth; C->nfins++;
    outer = emit_jump(OP_TRY); C->tryDepth++;
  }
  int t = emit_jump(OP_TRY); C->tryDepth++; block(); emit(OP_ENDTRY); C->tryDepth--;
  int skip = emit_jump(OP_JUMP); patch_jump(t);
  if (match(T_CATCH)) {
    begin_scope(); bool paren = match(T_LP);
    soft_ident(); if (P.cur.t == T_IDENT) { advance(); add_local(P.prev.s, true, false); } else add_local(intern_c("$e"), true, false);
    if (paren) consume(T_RP, "توقعت ')'");
    block(); end_scope();
  } else {
    if (!fin) cerror_at(&P.cur, "توقعت 'امسك' بعد كتلة 'حاول'");
    emit(OP_THROW);
  }
  patch_jump(skip);
  if (fin) {
    emit(OP_ENDTRY); C->tryDepth--; C->nfins--;
    consume(T_FINALLY, "توقعت 'اخيرا'"); P = after;
    emit2(OP_GET_LOCAL, (uint8_t)finslot); emit2(OP_CALL, 0); emit(OP_POP);
    int skipf = emit_jump(OP_JUMP); patch_jump(outer);
    begin_scope(); int slot = add_local(intern_c("$e"), true, false);
    emit2(OP_GET_LOCAL, (uint8_t)finslot); emit2(OP_CALL, 0); emit(OP_POP);
    emit2(OP_GET_LOCAL, (uint8_t)slot); emit(OP_THROW); end_scope();
    patch_jump(skipf);
  }
  end_scope();
}
static Str *import_path(void) {
  if (!check(T_STR)) cerror_at(&P.cur, "توقعت مسار المكتبة كنص"); advance();
  Str *p = intern(P.prev.start, P.prev.len);
  emit(OP_IMPORT); emit16(make_const(OBJV(p)));
  return p;
}
static Str *import_basename(Str *path) {
  const char *p = path->chars; const char *sl = strrchr(p, '/'); if (sl) p = sl + 1;
  int n = (int)strlen(p); const char *dot = NULL;
  for (int i = n - 1; i > 0; i--) if (p[i] == '.') { dot = p + i; break; }
  if (dot) n = (int)(dot - p);
  return normstr_n(p, n);
}
static void import_stmt(void) {
  if (C->depth > 0 || C->enc) CERR("الاستيراد فقط في المستوى الأعلى للملف");
  Str *path = import_path();
  Str *name = import_basename(path);
  if (match(T_AS)) { if (P.cur.t != T_IDENT) cerror_at(&P.cur, "توقعت اسما"); advance(); name = P.prev.s; }
  emit(OP_DEF_GLOBAL); emit16(make_const(OBJV(name))); match(T_SEMI);
}
static void from_stmt(void) {
  if (C->depth > 0 || C->enc) CERR("الاستيراد فقط في المستوى الأعلى للملف");
  import_path(); consume(T_IMPORT, "توقعت 'استورد' بعد المسار");
  do {
    if (!P.cur.s) cerror_at(&P.cur, "توقعت اسما"); advance(); Str *n = P.prev.s, *alias = n;
    if (match(T_AS)) { if (P.cur.t != T_IDENT) cerror_at(&P.cur, "توقعت اسما"); advance(); alias = P.prev.s; }
    emit(OP_DUP); emit(OP_GET_PROP); emit16(make_const(OBJV(n)));
    emit(OP_DEF_GLOBAL); emit16(make_const(OBJV(alias)));
  } while (match(T_COMMA));
  emit(OP_POP); match(T_SEMI);
}
static void switch_stmt(void) {
  expression(); consume(T_LC, "توقعت '{' بعد تبديل"); begin_scope();
  int sub = add_local(intern_c("$s"), true, false); int ends[64], ne = 0;
  while (!check(T_RC) && !check(T_EOF)) {
    if (P.cur.t == T_IDENT && ctx_id(P.cur.s) == 1) {
      advance(); int bj[32], nb = 0;
      do {
        if (nb >= 32) CERR("قيم كثيرة في حالة واحدة");
        emit2(OP_GET_LOCAL, (uint8_t)sub); expression(); emit(OP_EQ); bj[nb++] = emit_jump(OP_JIT); emit(OP_POP);
      } while (match(T_COMMA));
      int skip = emit_jump(OP_JUMP);
      for (int i = 0; i < nb; i++) patch_jump(bj[i]);
      emit(OP_POP); block();
      if (ne >= 64) CERR("حالات كثيرة"); ends[ne++] = emit_jump(OP_JUMP); patch_jump(skip);
    } else if ((P.cur.t == T_IDENT && ctx_id(P.cur.s) == 2) || P.cur.t == T_ELSE) { advance(); block(); }
    else cerror_at(&P.cur, "توقعت 'حالة' أو 'افتراضي'");
  }
  consume(T_RC, "توقعت '}'");
  for (int i = 0; i < ne; i++) patch_jump(ends[i]);
  end_scope();
}
static void enum_decl(void) {
  Str *nm; int g = parse_var_name(true, &nm);
  emit(OP_GET_GLOBAL); emit16(make_const(OBJV(normstr("_تعداد"))));
  consume(T_LC, "توقعت '{' بعد اسم التعداد"); int n = 0; double counter = 0;
  while (!check(T_RC) && !check(T_EOF)) {
    if (!P.cur.s) cerror_at(&P.cur, "توقعت اسم عنصر"); advance(); emit_const(OBJV(P.prev.s));
    if (match(T_EQ)) { if (check(T_NUM)) counter = P.cur.num + 1; expression(); } else { emit_const(NUMV(counter)); counter++; }
    n++; match(T_COMMA);
  }
  consume(T_RC, "توقعت '}'"); emit(OP_MAP); emit16(n); emit2(OP_CALL, 1); define_var(g);
}
static void statement_impl(void) {
  if (match(T_IF)) if_stmt();
  else if (match(T_SWITCH)) switch_stmt();
  else if (match(T_REPEAT)) repeat_stmt();
  else if (match(T_WHILE)) while_stmt();
  else if (match(T_FOR)) for_stmt();
  else if (match(T_RET)) return_stmt();
  else if (match(T_BREAK)) break_stmt();
  else if (match(T_CONT)) continue_stmt();
  else if (match(T_TRY)) try_stmt();
  else if (match(T_THROW)) { expression(); match(T_SEMI); emit(OP_THROW); }
  else if (match(T_IMPORT)) import_stmt();
  else if (check(T_LC)) block();
  else if (match(T_SEMI)) {}
  else expr_stmt();
}
static void declaration_impl(void) {
  if (g_debug_compile && C) { emit(OP_LINE); emit16(P.cur.line); }
  if (check(T_IDENT) && ctx_id(P.cur.s) == 5) {
    Scanner sv = P.sc; Token nx = scan_token(); P.sc = sv;
    if (nx.t == T_IDENT) { advance(); enum_decl(); return; }
  }
  if (check(T_IDENT) && ctx_id(P.cur.s) == 6) {
    Scanner sv = P.sc; Token nx = scan_token(); P.sc = sv;
    if (nx.t == T_STR) { advance(); from_stmt(); return; }
  }
  if (check(T_ASYNC) || check(T_GEN)) {
    int k = P.cur.t == T_ASYNC ? 1 : 2; advance(); consume(T_FUN, "توقعت 'دالة'"); g_kind = k;
    soft_ident(); if (P.cur.t == T_IDENT) fun_decl(); else { function(FT_FUNC, NULL); match(T_SEMI); emit(OP_POP); }
    return;
  }
  if (match(T_CLASS)) class_decl();
  else if (match(T_FUN)) {
    soft_ident();
    if (P.cur.t == T_IDENT) fun_decl();
    else { function(FT_FUNC, NULL); match(T_SEMI); emit(OP_POP); }
  }
  else if (match(T_LET)) var_decl(false);
  else if (match(T_CONST)) var_decl(true);
  else statement();
}

static Fun *compile(const char *src, const char *file, bool quiet) {
  memset(&P.sc, 0, sizeof P.sc); P.sc.src = P.sc.cur = src; P.sc.line = 1; P.file = file; g_cur_file = intern_c(file); P.quiet = quiet; P.ngconst = 0; P.ngt = 0; P.last_add_c = NULL; P.last_add_pos = -1;
  C = NULL; CC = NULL;
  P.rec = 0; P.nsoft = 0;
  if (setjmp(P.jb)) { C = NULL; CC = NULL; return NULL; }
  Compiler c; init_compiler(&c, FT_SCRIPT, NULL);
  P.cur = mk_tok(T_EOF, src, 0, 1, false); advance();
  while (!check(T_EOF)) declaration();
  return end_compiler();
}

static void push(Value v) { *vm.sp++ = v; }
static Value pop(void) { return *--vm.sp; }
static Value peek(int n) { return vm.sp[-1 - n]; }

static Upv *capture_upvalue(Value *local) {
  Upv *prev = NULL, *u = vm.open;
  while (u && u->loc > local) { prev = u; u = u->next; }
  if (u && u->loc == local) return u;
  Upv *n = new_upv(local); n->next = u;
  if (prev) prev->next = n; else vm.open = n;
  return n;
}
static void close_upvalues(Value *last) {
  while (vm.open && vm.open->loc >= last) { Upv *u = vm.open; u->closed = *u->loc; u->loc = &u->closed; vm.open = u->next; }
}

static bool call_closure(Closure *c, int argc);
static bool call_value(Value callee, int argc);
