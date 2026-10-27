#define OPLIST(X) \
  X(OP_CONST) \
  X(OP_NIL) \
  X(OP_TRUE) \
  X(OP_FALSE) \
  X(OP_POP) \
  X(OP_DUP) \
  X(OP_DUP2) \
  X(OP_GET_LOCAL) \
  X(OP_SET_LOCAL) \
  X(OP_GET_GLOBAL) \
  X(OP_DEF_GLOBAL) \
  X(OP_SET_GLOBAL) \
  X(OP_GET_UPV) \
  X(OP_SET_UPV) \
  X(OP_GET_PROP) \
  X(OP_SET_PROP) \
  X(OP_GET_SUPER) \
  X(OP_GET_INDEX) \
  X(OP_SET_INDEX) \
  X(OP_EQ) \
  X(OP_GT) \
  X(OP_LT) \
  X(OP_GE) \
  X(OP_LE) \
  X(OP_IN) \
  X(OP_ADD) \
  X(OP_SUB) \
  X(OP_MUL) \
  X(OP_DIV) \
  X(OP_MOD) \
  X(OP_POW) \
  X(OP_NOT) \
  X(OP_NEG) \
  X(OP_CONCAT) \
  X(OP_JUMP) \
  X(OP_JIF) \
  X(OP_JIT) \
  X(OP_JNN) \
  X(OP_LOOP) \
  X(OP_JNE_EMPTY) \
  X(OP_CALL) \
  X(OP_INVOKE) \
  X(OP_SUPER_INVOKE) \
  X(OP_CLOSURE) \
  X(OP_CLOSE_UPV) \
  X(OP_RETURN) \
  X(OP_CLASS) \
  X(OP_INHERIT) \
  X(OP_METHOD) \
  X(OP_LIST) \
  X(OP_MAP) \
  X(OP_ITERPREP) \
  X(OP_FOREACH) \
  X(OP_TRY) \
  X(OP_ENDTRY) \
  X(OP_THROW) \
  X(OP_IMPORT) \
  X(OP_SLICE) \
  X(OP_STATIC) \
  X(OP_AWAIT) \
  X(OP_YIELD) \
  X(OP_LINE) \
  X(OP_IDIV) \
  X(OP_BAND) \
  X(OP_BOR) \
  X(OP_BXOR) \
  X(OP_SHL) \
  X(OP_SHR) \
  X(OP_JNIL) \
  X(OP_LNEW) \
  X(OP_LPUSH) \
  X(OP_LEXT) \
  X(OP_MEXT) \
  X(OP_MSET) \
  X(OP_CALLV) \
  X(OP_INVOKEV) \
  X(OP_CHECK) \
  X(OP_ADD_APPEND) \
  X(OP_Q_LP) \
  X(OP_Q_LL) \
  X(OP_Q_LK_ADD) \
  X(OP_Q_LK_SUB) \
  X(OP_Q_LK_MUL) \
  X(OP_Q_LK_LT) \
  X(OP_Q_LK_GT) \
  X(OP_Q_LK_LE) \
  X(OP_Q_LK_GE) \
  X(OP_Q_LL_ADD) \
  X(OP_Q_LL_SUB) \
  X(OP_Q_LL_MUL) \
  X(OP_Q_SETPOP)
#define X_OPENUM(o) o,
#define X_LBL(o) &&L_##o,
typedef enum { OPLIST(X_OPENUM) OP__COUNT } Op;

typedef enum {
  T_LP, T_RP, T_LB, T_RB, T_LC, T_RC, T_COMMA, T_DOT, T_SEMI, T_COLON, T_QUEST, T_QQ,
  T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PCT, T_POW, T_BANG, T_NE, T_EQ, T_EQEQ, T_GT, T_GE, T_LT, T_LE,
  T_PLUSEQ, T_MINUSEQ, T_STAREQ, T_SLASHEQ, T_PCTEQ, T_ARROW, T_ELLIPSIS, T_ANDAND, T_OROR,
  T_IDIV, T_IDIVEQ, T_POWEQ, T_BAND, T_BOR, T_BXOR, T_SHL, T_SHR, T_INC, T_DEC, T_QDOT,
  T_IDENT, T_STR, T_NUM,
  T_LET, T_CONST, T_FUN, T_IF, T_ELSE, T_WHILE, T_FOR, T_IN, T_BREAK, T_CONT, T_RET, T_TRUE, T_FALSE, T_NIL,
  T_AND, T_OR, T_NOT, T_CLASS, T_THIS, T_SUPER, T_EXTENDS, T_TRY, T_CATCH, T_THROW, T_IMPORT, T_FROM, T_AS,
  T_REPEAT, T_ELIF, T_ASYNC, T_GEN, T_AWAIT, T_YIELD, T_SWITCH, T_FINALLY, T_EOF, T_ERR
} TT;

typedef struct Token_ { TT t; const char *start; int len; int line; bool nl; Str *s; double num; } Token;
typedef struct { const char *src, *cur; int line; } Scanner;
typedef struct {
  Token cur, prev; Scanner sc; const char *file; bool quiet; jmp_buf jb;
  Str *gconst[256]; int ngconst; Str *gtn[256]; uint16_t gtm[256]; List *gti[256]; int ngt; char errmsg[400]; char errtext[300]; int errline; int last_add_pos; void *last_add_c; int rec; Str *soft[256]; int nsoft;
} Parser;
static Parser P;

static void hl_range(const char *s, int n, Buf *o, bool html, int depth);
static char *read_file(const char *path, size_t *len);
static void print_tok_snippet(struct Token_ *t);
static void cerror_at(Token *t, const char *msg) {
  snprintf(P.errmsg, sizeof P.errmsg, "%s: خطأ في الصياغة [سطر %d]: %s", P.file, t->line, msg);
  snprintf(P.errtext, sizeof P.errtext, "%s", msg); P.errline = t->line;
  if (!P.quiet) { fprintf(stderr, "%s\n", P.errmsg); print_tok_snippet(t); }
  longjmp(P.jb, 1);
}
#define CERR(m) cerror_at(&P.prev, (m))

static const char *kw_canon[256];

static void init_keywords(void) {
  static const struct { TT t; const char *words; } kws[] = {
    {T_LET, "خل|متغير|ليكن|اجعل|عرف|فليكن"},
    {T_CONST, "ثابت|دائم|قيمة_ثابتة"},
    {T_FUN, "دالة|دالّة|وظيفة|اجراء|إجراء|وظيفه"},
    {T_IF, "اذا|إذا|لو|في_حال|اذا_كان|إذا_كان|ان_كان|في_حالة"},
    {T_ELSE, "والا|وإلا|والّا|غير_ذلك|خلاف_ذلك|بخلاف_ذلك|سوى_ذلك|و_الا"},
    {T_ELIF, "والا_اذا|وإلا_إذا|والا_لو|او_اذا|او_لو|غير_ذلك_اذا"},
    {T_WHILE, "طالما|مادام|ما_دام|بينما|طول_ما"},
    {T_FOR, "لكل|لاجل|لأجل|من_اجل|من_أجل"},
    {T_REPEAT, "كرر|كرّر|كرر_مرات"},
    {T_IN, "في|ضمن"},
    {T_BREAK, "توقف|اكسر|قف|انهي_الحلقة"},
    {T_CONT, "استمر|تابع|واصل|تخطى|تجاوز"},
    {T_RET, "ارجع|اعد|أعد|رجع|رجّع|ارجاع|اعطي|أعطِ"},
    {T_TRUE, "صح|صحيح|حقيقي|نعم|صدق"},
    {T_FALSE, "غلط|خاطئ|كذب|زائف"},
    {T_NIL, "عدم|فارغ|لاشيء|لا_شيء|خالي|بلا_قيمة"},
    {T_AND, "و|وكذلك"},
    {T_OR, "او|أو"},
    {T_NOT, "ليس|ليست|نفي"},
    {T_CLASS, "صنف|فئة|كلاس|قالب"},
    {T_THIS, "هذا|هذه|نفسه|ذاتي"},
    {T_SUPER, "الاب|الأب|الاصل|الأصل"},
    {T_EXTENDS, "يرث|يمتد|يمتد_من|يشتق_من|ترث|يرث_من"},
    {T_TRY, "حاول|جرب|جرّب"},
    {T_CATCH, "امسك|التقط|عند_الخطا|عند_الخطأ"},
    {T_FINALLY, "اخيرا|أخيراً|اخيراً|في_النهاية|نهاية"},
    {T_THROW, "ارمي|اطلق|ارفع|القي|ألقِ|ارم"},
    {T_IMPORT, "استورد|استدعي|اجلب_مكتبة|ادرج_مكتبة"},
    {T_AS, "باسم|كاسم|بمسمى"},
    {T_SWITCH, "تبديل|طابق|حسب"},
    {T_ASYNC, "غير_متزامن|غير_متزامنة"},
    {T_GEN, "مولد|مولّد"},
    {T_AWAIT, "انتظر|ترقب"},
    {T_YIELD, "انتج|أنتج|ولد|ولّد"},
    {T_EOF, NULL}};
  for (int i = 0; kws[i].words; i++) {
    char buf[512]; snprintf(buf, sizeof buf, "%s", kws[i].words);
    bool first = true;
    for (char *w = strtok(buf, "|"); w; w = strtok(NULL, "|")) {
      if (first) { kw_canon[kws[i].t] = strdup(w); first = false; }
      dset(&vm.kw, OBJV(normstr(w)), NUMV(kws[i].t));
    }
  }
  static const struct { int id; const char *words; } ctx[] = {
    {1, "حالة|عندما|حين|لو_كان"}, {2, "افتراضي|غير_ذلك_افتراضي|سوى"}, {3, "ساكن|ساكنة|استاتيكي"},
    {4, "خاصية|صفة|احصل"}, {5, "تعداد|قائمة_ثابتة|تعداد_ثابت"}, {6, "من|من_مكتبة"}, {0, NULL}};
  for (int i = 0; ctx[i].words; i++) {
    char buf[256]; snprintf(buf, sizeof buf, "%s", ctx[i].words);
    for (char *w = strtok(buf, "|"); w; w = strtok(NULL, "|")) dset(&vm.ctxw, OBJV(normstr(w)), NUMV(ctx[i].id));
  }
}
static int ctx_id(Str *s) { Value v; if (s && dget(&vm.ctxw, OBJV(s), &v)) return (int)v.as.n; return 0; }

static const char *op_names[] = {
#define X_OPNAME(o) #o,
  OPLIST(X_OPNAME)
#undef X_OPNAME
};

static int op_operand_len(const Fun *f, const uint8_t *ip) {
  switch ((Op)*ip) {
    case OP_GET_LOCAL: case OP_SET_LOCAL: case OP_GET_UPV: case OP_SET_UPV: case OP_CALL: case OP_ITERPREP:
    case OP_Q_LP: case OP_Q_LL: case OP_Q_LK_ADD: case OP_Q_LK_SUB: case OP_Q_LK_MUL: case OP_Q_LK_LT: case OP_Q_LK_GT: case OP_Q_LK_LE: case OP_Q_LK_GE:
    case OP_Q_LL_ADD: case OP_Q_LL_SUB: case OP_Q_LL_MUL: case OP_Q_SETPOP: return 1;
    case OP_CONST: case OP_GET_GLOBAL: case OP_DEF_GLOBAL: case OP_SET_GLOBAL: case OP_GET_PROP: case OP_SET_PROP: case OP_GET_SUPER:
    case OP_JUMP: case OP_JIF: case OP_JIT: case OP_JNN: case OP_LOOP: case OP_CLASS: case OP_METHOD: case OP_LIST: case OP_MAP:
    case OP_TRY: case OP_IMPORT: case OP_STATIC: case OP_LINE: case OP_JNIL: case OP_INVOKEV: return 2;
    case OP_CHECK: return 4;
    case OP_JNE_EMPTY: case OP_INVOKE: case OP_SUPER_INVOKE: return 3;
    case OP_FOREACH: return 4;
    case OP_CLOSURE: {
      int ci = (ip[1] << 8) | ip[2]; Value c = f->ch.consts.v[ci];
      return 2 + 2 * (IS_T(c, O_FUN) ? AS_FUN(c)->upc : 0);
    }
    default: return 0;
  }
}
static const uint8_t *op_next(const Fun *f, const uint8_t *ip) { return ip + 1 + op_operand_len(f, ip); }

static inline uint8_t op_norm(uint8_t o) {
  if (o == OP_ADD_APPEND) return OP_ADD;
  if (o == OP_Q_SETPOP) return OP_SET_LOCAL;
  if (o >= OP_Q_LP && o <= OP_Q_LL_MUL) return OP_GET_LOCAL;
  return o;
}

static void quicken_fun(Fun *f) {
  uint8_t *code = f->ch.code; const uint8_t *end = code + f->ch.n;
  for (uint8_t *ip = code; ip < end; ip = (uint8_t *)op_next(f, ip)) {
    uint8_t op = *ip;
    if (op == OP_SET_LOCAL) { if (ip + 3 <= end && ip[2] == OP_POP) *ip = OP_Q_SETPOP; continue; }
    if (op != OP_GET_LOCAL) continue;
    uint8_t *n1 = ip + 2; if (n1 >= end) continue;
    if (*n1 == OP_CONST && n1 + 4 <= end) {
      uint8_t o3 = n1[3]; int q = 0;
      switch (o3) { case OP_ADD: case OP_ADD_APPEND: q = OP_Q_LK_ADD; break; case OP_SUB: q = OP_Q_LK_SUB; break; case OP_MUL: q = OP_Q_LK_MUL; break;
        case OP_LT: q = OP_Q_LK_LT; break; case OP_GT: q = OP_Q_LK_GT; break; case OP_LE: q = OP_Q_LK_LE; break; case OP_GE: q = OP_Q_LK_GE; break; default: break; }
      if (q) { *ip = (uint8_t)q; continue; }
    } else if (*n1 == OP_GET_PROP) { *ip = OP_Q_LP; continue; }
    else if (*n1 == OP_GET_LOCAL && n1 + 3 <= end) {
      uint8_t o3 = n1[2]; int q = 0;
      switch (o3) { case OP_ADD: case OP_ADD_APPEND: q = OP_Q_LL_ADD; break; case OP_SUB: q = OP_Q_LL_SUB; break; case OP_MUL: q = OP_Q_LL_MUL; break; default: break; }
      if (q) { *ip = (uint8_t)q; continue; }

      if (n1 + 4 <= end && n1[2] == OP_GET_PROP) continue;
      *ip = OP_Q_LL;
    }
  }
}
