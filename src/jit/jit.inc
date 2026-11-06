#define JIT_TH 300
#define JIT_READY 0xFFFD
#define JIT_OFF 0xFFFE
#define JIT_MAX_DEOPT 200
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
#include <pthread.h>
extern void pthread_jit_write_protect_np(int) __attribute__((weak_import)); /* ماك 11.3+ فقط — يُفحص وجوده وقت التشغيل */
#endif

typedef struct JitCtx { Value *slots; Value *sp; Closure *clo; const uint8_t *ip; int code; } JitCtx;
typedef void (*JitFn)(JitCtx *);
typedef struct JitRegion { const uint8_t *tail; JitFn fn; void *mem; size_t msz; int deopts; struct JitRegion *next; } JitRegion;

static int g_jit = 0;
static int g_jit_trace = 0;
static size_t g_jit_bytes = 0;

static void jit_free_regions(Fun *f) {
#if SOLAN_JIT
  JitRegion *r = f->jreg;
  while (r) { JitRegion *n = r->next; if (r->mem) munmap(r->mem, r->msz); g_jit_bytes -= r->msz; free(r); r = n; }
  f->jreg = NULL;
#else
  (void)f;
#endif
}

#if SOLAN_JIT
#define JMAXI 3000
#define JMAXD 40
#define JMAXV 128
#define JMAXFIX 20000
#define JMAXLAB 12000
#define JMAXPOOL 512
#define JMAXPV 8
#define TMP_LO 3
#define TMP_HI 7
#define VAR_LO 8
/* مساحة الحفظ تُعرَّف داخل jit_compile حسب المنصة (JX_SAVE_AREA) */

/* pf: مجمع ثوابت المستدعى المُدرج (NULL = ثوابت المنطقة) — rd: فهرس تعليمة إعادة توجيه التخفي (-1 = بلا) */
typedef struct { const uint8_t *ip; uint8_t op; int a, b, c; int len; int tgt; const uint8_t *tip; Fun *pf; int rd; } JI;
typedef struct { int pos, lab; } JF;
typedef struct { const uint8_t *ip; int depth; int lab; int code; } JX;
enum { SK_MEM, SK_XMM, SK_VAR, SK_CONST, SK_HOME };
typedef struct { uint8_t k, r, n; int8_t hb; int hd; int var; uint64_t bits; } SD;
typedef struct { int g, idx, gi, uses, ok, reg, noc; } PV; /* noc: سلوت مُنشأ داخل المنطقة — بلا فحص وسم عند الدخول */
typedef struct { int lab, n; SD *s; const uint8_t *ip; } JS;

typedef struct {
  Fun *fn; Closure *clo; Value *slots; const uint8_t *head, *tail;
  JI ins[JMAXI]; int n; int *idx_of;
  int depth[JMAXI]; uint8_t reach[JMAXI], islabel[JMAXI];
  XB x; int lpos[JMAXLAB]; int nlab; JF fix[JMAXFIX]; int nfix;
  JX ex[JMAXI * 2]; int nex;
  int dl[JMAXI];
  Str *gnames[JMAXV]; int ggi[JMAXV]; Closure *gexp[JMAXV]; int ng; /* gexp: قيمة عام متوقعة لمستدعٍ مُدرج (NULL = بلا حراسة قيمة) */
  int nbi; int ggen; /* nbi: مواقع قراءة عام مدمج (تحتاج R13)؛ ggen: جيل قاموس العوالم عند الترجمة */
  JS *sx; int nsx, csx;
  PV pv[64]; int npv; int usepromo; int spoff;
  SD sd[JMAXD + 8];
  uint64_t pool[JMAXPOOL]; int poollab[JMAXPOOL]; int npool;
  int failed; int ninl; const char *why;
} JC;

#define EM(p) R12, (int)((p) * 16)
#define EMP(p) R12, (int)((p) * 16 + 8)

static double jit_h_mod(double a, double b) {
  double r = fmod(a, b);
  if (r != 0 && ((r < 0) != (b < 0))) r += b;
  return r;
}
static double jit_h_pow(double a, double b) { return sol_pow(a, b); }

/* مساعد call-out لـ OP_CALL: ينفّذ الاستدعاءَ غير القابل للإدراج عبر آلية المفسّر كاملةً
   ثم تعود المنطقة للعمل. vm.sp = قمة المكدس عند الاستدعاء (تحت المنطقة كلها مُفرَّغة مسبقاً).
   عند الفشل يضبط code=2 (استثناء: vm.exc مضبوط والآلة متسقة تحت sp) بلا تفريغ سجلات. */
static Value *jit_h_call(JitCtx *cx, Value *sp, int argc, const uint8_t *call_ip) {
  vm.sp = sp;
  vm.frames[vm.fcount - 1].ip = (uint8_t *)(uintptr_t)call_ip; /* أسطر الأخطاء داخل call_value تشير لموقع الاستدعاء */
  if (vm.gc_pending) collect();
  if (!call_sync(argc)) cx->code = 2;
  return vm.sp;
}

static int jc_new_label(JC *c) { if (c->nlab >= JMAXLAB) { c->failed = 1; c->why = "labels"; return 0; } c->lpos[c->nlab] = -1; return c->nlab++; }
static void jc_bind(JC *c, int l) { c->lpos[l] = c->x.n; }
static void jc_fixup(JC *c, int l) {
  if (c->nfix >= JMAXFIX) { c->failed = 1; c->why = "fixups"; return; }
  c->fix[c->nfix].pos = c->x.n; c->fix[c->nfix].lab = l; c->nfix++;
#if JX_ARM
  /* التعليمة نفسها (4 بايت صادرة للتو) هي العنصر النائب — حلقة الترقيع تعدّل حقولها */
#else
  xb_u32(&c->x, 0);
#endif
}
static void jc_jmp(JC *c, int l) { jx_rel32_jmp(&c->x); jc_fixup(c, l); }
static void jc_jcc(JC *c, int cc, int l) { jx_rel32_jcc(&c->x, cc); jc_fixup(c, l); }

static int jc_exit_label2(JC *c, const uint8_t *ip, int depth, int code) {
  for (int i = 0; i < c->nex; i++) if (c->ex[i].ip == ip && c->ex[i].depth == depth && c->ex[i].code == code) return c->ex[i].lab;
  if (c->nex >= JMAXI * 2) { c->failed = 1; c->why = "exits"; return 0; }
  c->ex[c->nex].ip = ip; c->ex[c->nex].depth = depth; c->ex[c->nex].code = code; c->ex[c->nex].lab = jc_new_label(c); return c->ex[c->nex++].lab;
}
static int jc_exit_label(JC *c, const uint8_t *ip, int depth) { return jc_exit_label2(c, ip, depth, 0); }
static int sd_eq(const SD *a, const SD *b) {
  return a->k == b->k && a->r == b->r && a->n == b->n && a->hb == b->hb && a->hd == b->hd && a->var == b->var && a->bits == b->bits;
}
static int jc_deopt(JC *c, int i, int depth) {
  JI *in = &c->ins[i];
  const uint8_t *sip = in->ip; int sdep = depth;
  if (in->rd >= 0) { sip = c->ins[in->rd].ip; sdep = c->depth[in->rd]; } /* تعليمات جسم مُدرج: تُخفي إلى ip/عمق الاستدعاء الأصلي */
  if (c->dl[i] >= 0) {
    JS *o = &c->sx[c->dl[i]]; int same = o->n == sdep;
    for (int k = 0; same && k < sdep; k++) if (!sd_eq(&o->s[k], &c->sd[k])) same = 0;
    if (same) return o->lab;
  }
  if (c->nsx == c->csx) { c->csx = c->csx ? c->csx * 2 : 256; c->sx = realloc(c->sx, (size_t)c->csx * sizeof(JS)); }
  JS *s = &c->sx[c->nsx]; s->lab = jc_new_label(c); s->n = sdep; s->ip = sip;
  s->s = malloc((size_t)(sdep + 1) * sizeof(SD)); memcpy(s->s, c->sd, (size_t)sdep * sizeof(SD));
  c->dl[i] = c->nsx++;
  return s->lab;
}

static int jc_global_index(JC *c, Str *name) {
  for (int i = 0; i < c->ng; i++) if (c->gnames[i] == name) return c->ggi[i];
  Dict *d = &c->clo->globals->d;
  int gi = dfind(d, OBJV(name), name->hash);
  if (gi < 0 || c->ng >= JMAXV) { c->failed = 1; c->why = "global"; return 0; }
  c->gnames[c->ng] = name; c->ggi[c->ng++] = gi; return gi;
}

static int jc_target_idx(JC *c, const uint8_t *p) {
  if (p < c->head || p >= c->tail) return -1;
  int i = c->idx_of[p - c->head];
  return i;
}

static int jc_effect(JC *c, JI *in, int *pop, int *push) {
  *pop = 0; *push = 0;
  switch ((Op)in->op) {
    case OP_CONST: case OP_NIL: case OP_TRUE: case OP_FALSE: case OP_GET_LOCAL: case OP_GET_GLOBAL: case OP_GET_UPV: case OP_DUP: *push = 1; if (in->op == OP_DUP) *pop = 0; return 1;
    case OP_DUP2: *push = 2; return 1;
    case OP_POP: *pop = 1; return 1;
    case OP_SET_LOCAL: case OP_SET_GLOBAL: case OP_SET_UPV: case OP_NOT: case OP_NEG: case OP_JUMP: case OP_JIF: case OP_JIT: case OP_JNN: case OP_JNIL: case OP_LOOP: return 1;
    case OP_GET_PROP: return 1;
    case OP_CHECK: return 1;
    case OP_GET_INDEX: case OP_EQ: case OP_GT: case OP_LT: case OP_GE: case OP_LE: case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_POW: case OP_IDIV:
    case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR: *pop = 1; return 1;
    case OP_SET_INDEX: *pop = 2; return 1;
    case OP_FOREACH: *push = in->b; return 1;
    case OP_RFOREP: *pop = 0; return 1;
    case OP_RFOR: *push = in->c; return 1;
    case OP_CALL: *pop = in->a + 1; *push = 1; return 1; /* المستدعى + الوسائط ← النتيجة */
    case OP_JMOVE: *pop = in->a + 1; *push = 1; return 1; /* انهيار إطار مُدرج: انسخ القمة إلى d-1-k */
    default: (void)c; return 0;
  }
}

static int jc_flow(JC *c);

static int jc_analyze(JC *c) {
  const uint8_t *code = c->fn->ch.code;
  const uint8_t *ip = c->head;
  c->idx_of = calloc((size_t)(c->tail - c->head) + 1, sizeof(int));
  for (int i = 0; i < (int)(c->tail - c->head) + 1; i++) c->idx_of[i] = -1;
  c->n = 0;
  while (ip < c->tail) {
    if (c->n >= JMAXI) return 0;
    JI *in = &c->ins[c->n]; in->ip = ip; in->op = op_norm(*ip); in->len = 1 + op_operand_len(c->fn, ip); in->tgt = -1; in->tip = NULL; in->rd = -1; /* calloc يتركه 0 — بلا هذا يُعاد كل تخفي إلى ins[0] */
    const uint8_t *ar = ip + 1;
    in->a = in->b = in->c = 0;
    switch ((Op)in->op) {
      case OP_CONST: case OP_GET_GLOBAL: case OP_SET_GLOBAL: case OP_GET_PROP: case OP_CHECK: in->a = (ar[0] << 8) | ar[1]; break;
      case OP_GET_LOCAL: case OP_SET_LOCAL: case OP_GET_UPV: case OP_SET_UPV: case OP_CALL: in->a = ar[0]; break;
      case OP_JUMP: case OP_JIF: case OP_JIT: case OP_JNN: case OP_JNIL: in->tip = ip + 3 + ((ar[0] << 8) | ar[1]); break;
      case OP_LOOP: in->tip = ip + 3 - ((ar[0] << 8) | ar[1]); break;
      case OP_FOREACH: in->a = ar[0]; in->b = ar[1]; in->tip = ip + 5 + ((ar[2] << 8) | ar[3]); break;
      case OP_RFOREP: in->a = ar[0]; in->b = ar[1]; in->c = ar[2]; break;
      case OP_RFOR: in->a = ar[0]; in->b = ar[1]; in->c = ar[2]; in->tip = ip + 6 + ((ar[3] << 8) | ar[4]); break;
      default: break;
    }
    int pop, push;
    if (!jc_effect(c, in, &pop, &push)) { c->why = op_names[in->op]; return 0; }
    c->idx_of[ip - c->head] = c->n++;
    ip += in->len;
  }
  if (ip != c->tail) return 0;
  (void)code;
  return jc_flow(c);
}

/* جولة التدفق: أهداف القفز + أعماق المكدس — تُعاد تشغيلها بعد إدراج الأجسام */
static int jc_flow(JC *c) {
  for (int i = 0; i < c->n; i++) c->islabel[i] = 0;
  for (int i = 0; i < c->n; i++) {
    JI *in = &c->ins[i];
    if (in->tip) {
      in->tgt = jc_target_idx(c, in->tip); if (in->tgt >= 0) c->islabel[in->tgt] = 1;
      if ((in->op == OP_JIF || in->op == OP_JIT) && *in->tip == OP_POP) { int t2 = jc_target_idx(c, in->tip + 1); if (t2 >= 0) c->islabel[t2] = 1; }
    }
  }
  for (int i = 0; i < c->n; i++) { c->depth[i] = -1; c->reach[i] = 0; }
  int work[JMAXI * 2], nw = 0;
  c->depth[0] = 0; c->reach[0] = 1; work[nw++] = 0;
  while (nw > 0) {
    int i = work[--nw]; JI *in = &c->ins[i]; int d = c->depth[i];
    int pop, push; jc_effect(c, in, &pop, &push);
    if (d - pop < 0) return 0;
    int nd = d - pop + push;
    if (nd >= JMAXD - 4) return 0;
    int succ[2], ns = 0, sd[2];
    switch ((Op)in->op) {
      case OP_JUMP: case OP_LOOP: if (in->tgt >= 0) { succ[ns] = in->tgt; sd[ns++] = nd; } break;
      case OP_JIF: case OP_JIT: case OP_JNN: case OP_JNIL:
        if (i + 1 < c->n) { succ[ns] = i + 1; sd[ns++] = nd; }
        if (in->tgt >= 0) { succ[ns] = in->tgt; sd[ns++] = nd; }
        break;
      case OP_FOREACH:
        if (i + 1 < c->n) { succ[ns] = i + 1; sd[ns++] = nd; }
        if (in->tgt >= 0) { succ[ns] = in->tgt; sd[ns++] = d; }
        break;
      case OP_RFOR: /* خروج الحلقة الشرطي قد يقع داخل المنطقة (حلقة متداخلة): عمقه = عمق الدخول بلا دفع قيم الحلقة */
        if (i + 1 < c->n) { succ[ns] = i + 1; sd[ns++] = nd; }
        if (in->tgt >= 0) { succ[ns] = in->tgt; sd[ns++] = d; }
        break;
      default: if (i + 1 < c->n) { succ[ns] = i + 1; sd[ns++] = nd; }
    }
    for (int k = 0; k < ns; k++) {
      int s = succ[k];
      if (c->depth[s] == -1) { c->depth[s] = sd[k]; c->reach[s] = 1; work[nw++] = s; }
      else if (c->depth[s] != sd[k]) { c->why = "depth"; return 0; }
    }
  }
  return 1;
}

/* ═══════ إدراج أجسام الدوال (inlining) ═══════
   يستبدل الاستدعاء المباشر لدالة عامة معروفة بجسمها المفكوك فوق الوسائط في مكانها، ثم ينهار
   المكدس بـ OP_JMOVE. حارس هوية المستدعى (وسم V_OBJ + مؤشر الـ Closure المتوقع) يُرفع إلى
   مقدمة المنطقة، والمرجع يُثبَّت في vm.pins لمنع إعادة استخدام العنوان (ABA). التخفي داخل
   الجسم يُعاد توجيهه إلى ip/عمق الاستدعاء الأصلي عبر rd — فيستأنف المفسّر الاستدعاء كما هو. */
typedef struct { const uint8_t *ip; uint8_t op; int a; uint8_t len; } BOP;
typedef struct { int ic, d0, argc, blen, b; Fun *pf; BOP *bp; } ISITE;
#define JMAXSITE 64
#define JMAXBODY 16

static int jc_inline_walk(JC *c, Fun *pf, int argc, int D_call, BOP *bp, int *blenout, int *bout) {
  const uint8_t *ip = pf->ch.code, *end = pf->ch.code + pf->ch.n;
  int bd = 0, bmax = 0, cnt = 0, b = -1;
  const char *rej = NULL;
  while (ip < end) {
    uint8_t op = op_norm(*ip);
    int len = 1 + op_operand_len(pf, ip);
    const uint8_t *ar = ip + 1;
    int a = 0;
    switch (op) {
      case OP_CONST: case OP_GET_GLOBAL: case OP_SET_GLOBAL: case OP_GET_PROP: case OP_CHECK: a = (ar[0] << 8) | ar[1]; break;
      case OP_GET_LOCAL: case OP_SET_LOCAL: case OP_GET_UPV: case OP_SET_UPV: case OP_CALL: a = ar[0]; break;
      default: break;
    }
    if (op == OP_RETURN) {
      if (bd < 1) { rej = "رجع بلا قيمة"; goto fail; }
      b = bd; break; /* ما بعد أول رجع غير قابل للوصول (لا قفزات في الجسم) — كالنمط الضمني NIL/RETURN */
    }
    switch (op) {
      case OP_CONST: {
        if (a >= pf->ch.consts.n) { rej = "ثابت خارج النطاق"; goto fail; }
        Value v = pf->ch.consts.v[a];
        if (!IS_NUM(v) && !IS_T(v, O_STR)) { rej = "ثابت ليس رقماً/نصاً"; goto fail; }
        bd++; break;
      }
      case OP_GET_LOCAL: if (a < 1 || a > argc) { rej = "محلي خارج الوسائط"; goto fail; } bd++; break; /* الوسائط فقط: 0 = المستدعى نفسه مرفوض */
      case OP_TRUE: case OP_FALSE: case OP_NIL: case OP_DUP: bd++; break;
      case OP_GET_GLOBAL: {
        if (a >= pf->ch.consts.n || !IS_T(pf->ch.consts.v[a], O_STR)) { rej = "اسم عام غير نصي"; goto fail; }
        Str *nm = AS_STR(pf->ch.consts.v[a]);
        /* عام الجسم يُقبل إن وُجد في عوالم المنطقة أو كان مدمجاً — تعليمته تمرّ بحالة
           OP_GET_GLOBAL الرئيسية فتُرقّى أو تُقرأ حديثاً من vm.builtins بحارس الظل */
        if (dfind(&c->clo->globals->d, OBJV(nm), nm->hash) < 0 &&
            dfind(&vm.builtins, OBJV(nm), nm->hash) < 0) { rej = "عام غير معرف بعد"; goto fail; }
        bd++; break;
      }
      case OP_CALL: { /* استدعاء داخل الجسم (مدمج غالباً): يُنفَّذ call-out عبر حالة OP_CALL الرئيسية */
        if (bd < a + 1) { rej = "مكدس سالب"; goto fail; }
        bd -= a; break; /* المستدعى + الوسائط تُستبدل بالنتيجة */
      }
      case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_POW: case OP_IDIV:
      case OP_EQ: case OP_LT: case OP_LE: case OP_GT: case OP_GE:
      case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR: bd--; break;
      case OP_NEG: case OP_NOT: break;
      default: rej = op_names[op]; goto fail;
    }
    if (bd < 0) { rej = "مكدس سالب"; goto fail; }
    if (bd > bmax) bmax = bd;
    if (cnt >= JMAXBODY) { rej = "جسم طويل"; goto fail; }
    bp[cnt].ip = ip; bp[cnt].op = op; bp[cnt].a = a; bp[cnt].len = (uint8_t)len; cnt++;
    ip += len;
  }
  if (b < 0) { rej = "بلا رجع"; goto fail; }
  if (D_call + bmax > JMAXD - 5) { rej = "عمق زائد"; goto fail; }
  *blenout = cnt; *bout = b;
  return 1;
  fail:
  if (g_jit_trace) fprintf(stderr, "[inline] جسم %s رُفض عند %s (bd=%d)\n", pf->name ? pf->name->chars : "?", rej ? rej : "?", bd);
  return 0;
}

static void jc_inline(JC *c) {
  ISITE st[JMAXSITE]; BOP bops[JMAXSITE * JMAXBODY];
  int ns = 0, nbo = 0, add = 0;
  Value *kc = c->fn->ch.consts.v;
  Dict *gd = &c->clo->globals->d;
  const char *rej = NULL; int ric = -1;
  /* جولة الفحص: كل المواقع تُدقَّق على المصفوفة الأصلية قبل أي تحريف */
  for (int ic = 0; ic < c->n; ic++) {
    if (ns >= JMAXSITE || nbo + JMAXBODY > JMAXSITE * JMAXBODY) break;
    JI *in = &c->ins[ic];
    rej = NULL;
    if (in->op != OP_CALL) continue;
    ric = ic;
    if (!c->reach[ic]) rej = "غير مُشمولة";
    else if (c->islabel[ic]) rej = "وسم قفز";
    else if (ic + 1 >= c->n) rej = "آخر تعليمة";
    if (rej) goto rejlog;
    int argc = in->a, D = c->depth[ic], d0 = D - argc - 1;
    if (d0 < 0) { rej = "عمق سالب"; goto rejlog; }
    int j = -1; /* دفع المستدعى: أقرب تعليمة سابقة عند العمق d0 */
    for (int q = ic - 1; q >= 0; q--) {
      int dq = c->depth[q];
      if (dq == d0) { j = q; break; }
      if (dq < d0) break;
    }
    if (j < 0) { rej = "لا دافع"; goto rejlog; }
    JI *cj = &c->ins[j];
    if (cj->op != OP_GET_GLOBAL) { rej = "الدافع ليس عاماً"; goto rejlog; }
    if (cj->a >= c->fn->ch.consts.n || !IS_T(kc[cj->a], O_STR)) { rej = "اسم غير نصي"; goto rejlog; }
    Str *nm = AS_STR(kc[cj->a]);
    int gi = dfind(gd, OBJV(nm), nm->hash);
    if (gi < 0) { rej = "عام مفقود"; goto rejlog; }
    Value gv = gd->e[gi].v;
    if (!IS_T(gv, O_CLO)) { rej = "ليس دالة"; goto rejlog; }
    Fun *pf = AS_CLO(gv)->f;
    if (!pf || pf->variadic || pf->kind || pf->arity != argc || pf == c->fn) { rej = "توقيع غير مطابق"; goto rejlog; }
    int dirty = 0; /* لا كتابة لهذا العام داخل المنطقة — وإلا ينكار حارس الهوية أثناء التنفيذ */
    for (int q = 0; q < c->n && !dirty; q++) {
      JI *w = &c->ins[q];
      if (w->op == OP_SET_GLOBAL) { if (w->a < c->fn->ch.consts.n && IS_T(kc[w->a], O_STR) && AS_STR(kc[w->a]) == nm) dirty = 1; }
      else if (w->op == OP_DEF_GLOBAL && w->ip + 2 < c->tail) {
        int ci = (w->ip[1] << 8) | w->ip[2];
        if (ci < c->fn->ch.consts.n && IS_T(kc[ci], O_STR) && AS_STR(kc[ci]) == nm) dirty = 1;
      }
    }
    if (dirty) { rej = "عام مكتوب داخل المنطقة"; goto rejlog; }
    BOP *bp = &bops[nbo];
    int blen = 0, b = 0;
    if (!jc_inline_walk(c, pf, argc, D, bp, &blen, &b)) { rej = "جسم غير مؤهل"; goto rejlog; }
    int slot = -1; /* تسجيل اسم المستدعى + القيمة المتوقعة + تثبيت المرجع ضد الـ GC */
    for (int q = 0; q < c->ng; q++) if (c->gnames[q] == nm) { slot = q; break; }
    if (slot < 0) {
      jc_global_index(c, nm);
      if (c->failed) { c->failed = 0; c->why = NULL; rej = "سجل عام"; goto rejlog; }
      slot = c->ng - 1;
      pin((Obj *)AS_CLO(gv));
    }
    c->gexp[slot] = AS_CLO(gv);
    nbo += blen; add += blen;
    st[ns].ic = ic; st[ns].d0 = d0; st[ns].argc = argc; st[ns].blen = blen; st[ns].b = b; st[ns].pf = pf; st[ns].bp = bp;
    ns++;
    continue;
    rejlog:
    if (g_jit_trace) fprintf(stderr, "[inline] رفض ic=%d عمق=%d: %s\n", ric, c->depth[ric], rej);
  }
  if (ns == 0 || c->n + add >= JMAXI) return;
  /* جولة الإدراج: تصاعدياً؛ كل إدراج يزيح مواقع المواقع الأعلى غير المعالجة فقط */
  for (int s = 0; s < ns; s++) {
    int ic = st[s].ic, blen = st[s].blen, argc = st[s].argc, b = st[s].b;
    Fun *pf = st[s].pf;
    const uint8_t *call_ip = c->ins[ic].ip;
    memmove(&c->ins[ic + 1 + blen], &c->ins[ic + 1], (size_t)(c->n - ic - 1) * sizeof(JI));
    c->n += blen;
    for (int k = 0; k < blen; k++) {
      BOP *bo = &st[s].bp[k];
      JI *ni = &c->ins[ic + k];
      ni->ip = k == 0 ? call_ip : bo->ip; /* الأولى تحمل ip الاستدعاء: تخفيها الذاتي يصيب الموضع الصحيح */
      ni->op = bo->op;
      /* ترجمة الوسيط: محلي الجسم a (1..argc) هو الوسيط a في عمق المتصل d0+a → سلوت مطلق spoff+d0+a */
      ni->a = bo->op == OP_GET_LOCAL ? bo->a + c->spoff + st[s].d0 : bo->a;
      ni->b = 0; ni->c = 0; ni->len = bo->len;
      ni->tgt = -1; ni->tip = NULL;
      ni->pf = (bo->op == OP_CONST || bo->op == OP_GET_GLOBAL) ? pf : NULL;
      ni->rd = k == 0 ? -1 : ic;
    }
    JI *jm = &c->ins[ic + blen];
    jm->ip = call_ip; jm->op = OP_JMOVE; jm->a = argc + b; jm->b = 0; jm->c = 0;
    jm->len = 1; jm->tgt = -1; jm->tip = NULL; jm->pf = NULL; jm->rd = ic;
    for (int t = s + 1; t < ns; t++) st[t].ic += blen;
    c->ninl++;
  }
  /* إعادة بناء idx_of ثم جولة تدفق جديدة على التسلسل المُدرج */
  for (int i = 0; i < (int)(c->tail - c->head) + 1; i++) c->idx_of[i] = -1;
  for (int i = 0; i < c->n; i++) { const uint8_t *p = c->ins[i].ip; if (p >= c->head && p < c->tail) c->idx_of[p - c->head] = i; }
  if (!jc_flow(c)) { c->failed = 1; c->why = "flow2"; }
}

static void jc_copyx(JC *c, int db, int di, int dd, int sb, int si, int sdisp) {
  XB *x = &c->x;
  jx_rm(x, 0, 0, 0x8B, R8, sb, si, 0, sdisp); jx_rm(x, 0, 0, 0x89, R8, db, di, 0, dd);
  jx_rm(x, 0, 1, 0x8B, R9, sb, si, 0, sdisp + 8); jx_rm(x, 0, 1, 0x89, R9, db, di, 0, dd + 8);
}
static void jc_copy16(JC *c, int dbase, int ddisp, int sbase, int sdisp) { jc_copyx(c, dbase, -1, ddisp, sbase, -1, sdisp); }

static SD sd_mk(int k) { SD s; memset(&s, 0, sizeof s); s.k = (uint8_t)k; s.var = -1; return s; }

static int jc_pool(JC *c, uint64_t bits) {
  for (int i = 0; i < c->npool; i++) if (c->pool[i] == bits) return c->poollab[i];
  if (c->npool >= JMAXPOOL) { c->failed = 1; c->why = "pool"; return 0; }
  c->pool[c->npool] = bits; c->poollab[c->npool] = jc_new_label(c);
  return c->poollab[c->npool++];
}
static void jc_rip(JC *c, int pfx, int op, int reg, int lab) {
  XB *x = &c->x;
#if JX_ARM
  /* تحميل ثابت من البركة: LDR literal (imm19 عنصر نائب تُرقَّعه حلقة الترقيع) */
  if (op == 0x0F10) jx_emit(x, 0x5C000000u | (uint32_t)reg); /* ldr d{reg}, lit */
  else { jx_emit(x, 0x5C000000u | 16u); jx_sse_rr(x, pfx, op, reg, 16); } /* ldr d16, lit ثم العملية */
  jc_fixup(c, lab);
#else
  if (pfx) xb_b(x, pfx);
  jx_rex(x, 0, reg, 0, 0); jx_opc(x, op); xb_b(x, ((reg & 7) << 3) | 5); jc_fixup(c, lab);
#endif
}
static void jc_settag(JC *c, int pos, int tag) { jx_mov32_mi(&c->x, EM(pos), tag); }

static void jc_mem_addr(JC *c, int pos, int *base, int *disp) {
  SD *s = &c->sd[pos];
  if (s->k == SK_HOME) { *base = s->hb; *disp = s->hd; } else { *base = R12; *disp = pos * 16; }
}
static void jc_local_home(JC *c, int a, int *base, int *disp) {
  if (a >= c->spoff) { *base = R12; *disp = (a - c->spoff) * 16; } else { *base = RBX; *disp = a * 16; }
}
static void jc_pv_home(PV *p, int *base, int *disp) {
  if (p->g) { *base = R13; *disp = p->gi * (int)sizeof(Ent) + (int)offsetof(Ent, v); }
  else { *base = RBX; *disp = p->idx * 16; }
}
static int jc_find_pv(JC *c, int g, int idx) {
  for (int k = 0; k < c->npv; k++) if (c->pv[k].g == g && c->pv[k].idx == idx) return k;
  return -1;
}

static void jc_emit_mat(JC *c, SD *s, int pos) {
  XB *x = &c->x;
  switch (s->k) {
    case SK_XMM: case SK_VAR: jx_movsd_mr(x, R12, pos * 16 + 8, s->r); jc_settag(c, pos, V_NUM); break;
    case SK_CONST: jx_mov_ri(x, RAX, s->bits); jx_mov_mr(x, R12, pos * 16 + 8, RAX); jc_settag(c, pos, V_NUM); break;
    case SK_HOME: if (!(s->hb == R12 && s->hd == pos * 16)) jc_copy16(c, R12, pos * 16, s->hb, s->hd); break;
    default: break;
  }
}
static void jc_mat(JC *c, int pos) {
  SD *s = &c->sd[pos];
  if (s->k == SK_MEM) return;
  int n = s->k == SK_HOME ? s->n : 1;
  jc_emit_mat(c, s, pos);
  *s = sd_mk(SK_MEM); s->n = (uint8_t)n;
}
static void jc_canon(JC *c, int lo, int hi) { for (int p = lo; p < hi; p++) jc_mat(c, p); }
static void jc_forget(JC *c, int d) { for (int p = 0; p < d; p++) { SD *s = &c->sd[p]; if (s->k == SK_MEM) s->n = 0; } }

static int jc_alloc_tmp(JC *c, int live) {
  for (int r = TMP_LO; r <= TMP_HI; r++) {
    int used = 0;
    for (int p = 0; p < live; p++) if (c->sd[p].k == SK_XMM && c->sd[p].r == r) { used = 1; break; }
    if (!used) return r;
  }
  for (int p = 0; p < live; p++) if (c->sd[p].k == SK_XMM) { int r = c->sd[p].r; jc_mat(c, p); return r; }
  c->failed = 1; c->why = "regs"; return TMP_LO;
}

static void jc_load_num(JC *c, int pos, int xr) {
  XB *x = &c->x; SD *s = &c->sd[pos];
  switch (s->k) {
    case SK_XMM: case SK_VAR: jx_movaps(x, xr, s->r); break;
    case SK_CONST: if (s->bits == 0) jx_xorpd(x, xr, xr); else jc_rip(c, 0xF2, 0x0F10, xr, jc_pool(c, s->bits)); break;
    default: { int b, d; jc_mem_addr(c, pos, &b, &d); jx_movsd_rm(x, xr, b, d + 8); }
  }
}
static int jc_reg_for(JC *c, int pos, int scr) {
  SD *s = &c->sd[pos];
  if (s->k == SK_XMM || s->k == SK_VAR) return s->r;
  jc_load_num(c, pos, scr); return scr;
}
static void jc_need_num(JC *c, int i, int d, int pos) {
  SD *s = &c->sd[pos];
  if (s->k == SK_XMM || s->k == SK_VAR || s->k == SK_CONST || s->n) return;
  int b, dd; jc_mem_addr(c, pos, &b, &dd);
  int dl = jc_deopt(c, i, d);
  jx_cmp32_mi(&c->x, b, dd, V_NUM); jc_jcc(c, CC_NE, dl);
  c->sd[pos].n = 1;
}
static void jc_op_rm(JC *c, int pfx, int op, int dst, int pos) {
  XB *x = &c->x; SD *s = &c->sd[pos];
  switch (s->k) {
    case SK_XMM: case SK_VAR: jx_sse_rr(x, pfx, op, dst, s->r); break;
    case SK_CONST: jc_rip(c, pfx, op, dst, jc_pool(c, s->bits)); break;
    default: { int b, d; jc_mem_addr(c, pos, &b, &d); jx_rm(x, pfx, 0, op, dst, b, -1, 0, d + 8); }
  }
}

static void jc_flush_vars(JC *c) {
  for (int k = 0; k < c->npv; k++) { int b, d; jc_pv_home(&c->pv[k], &b, &d); jx_movsd_mr(&c->x, b, d + 8, c->pv[k].reg); }
}
#if JX_ARM
/* d0-d2 مخطوطات صرفة، d3-d7 مؤقتات/متغيرات caller-saved تُنقذ حول المساعدين،
   d8-d15 محفوظة-للمستدعى أصلاً فلا حاجة لنقذها هنا (البرولوج ينقذها للمستدعى الأعلى).
   منطقة الإطار [16..55] مخصصة لهذا الغرض (انظر البرولوج). */
static void jc_save_regs(JC *c) { for (int r = 3; r <= 7; r++) jx_movsd_mr(&c->x, RSP, 16 + (r - 3) * 8, r); }
static void jc_restore_regs(JC *c) { for (int r = 3; r <= 7; r++) jx_movsd_rm(&c->x, r, RSP, 16 + (r - 3) * 8); }
#else
static void jc_save_regs(JC *c) { for (int r = 3; r <= 15; r++) jx_movsd_mr(&c->x, RSP, (r - 3) * 8, r); }
static void jc_restore_regs(JC *c) { for (int r = 3; r <= 15; r++) jx_movsd_rm(&c->x, r, RSP, (r - 3) * 8); }
#endif
static void jc_call_helper(JC *c, void *fn) {
  jc_save_regs(c);
#if JX_ARM
  jx_ldi64(&c->x, 15, (uint64_t)(uintptr_t)fn); /* x15 = عنوان المساعد (عابرة، لا x16/x17 المحجوزتين للقشور) */
  jx_blr(&c->x, 15); /* sp متحاذى 16 أصلاً (الإطار 160) — لا مساحة ظل على ARM */
#elif JX_WIN
  jx_sub_rsp(&c->x, 32); /* مساحة الظل التي يتوقعها المستدعى على ويندوز */
  jx_mov_ri(&c->x, RAX, (uint64_t)(uintptr_t)fn); jx_call_rax(&c->x);
  jx_add_rsp(&c->x, 32);
#else
  jx_mov_ri(&c->x, RAX, (uint64_t)(uintptr_t)fn); jx_call_rax(&c->x);
#endif
  jc_restore_regs(c);
}

static void jc_unalias_var(JC *c, int lim, int live, int var) {
  for (int p = 0; p < lim; p++) {
    if (c->sd[p].k == SK_VAR && c->sd[p].var == var) {
      int t = jc_alloc_tmp(c, live);
      jx_movaps(&c->x, t, c->sd[p].r);
      c->sd[p] = sd_mk(SK_XMM); c->sd[p].r = (uint8_t)t;
    }
  }
}
static void jc_unalias_home(JC *c, int d, int hb, int hd) {
  for (int p = 0; p < d; p++) if (c->sd[p].k == SK_HOME && c->sd[p].hb == hb && c->sd[p].hd == hd) jc_mat(c, p);
}

static void jc_cond_jump(JC *c, int i, int d, int pos, int jump_if_falsey, int lab) {
  XB *x = &c->x; SD *s = &c->sd[pos];
  int dl = jc_deopt(c, i, d);
  int done = jc_new_label(c), l1 = jc_new_label(c), l2 = jc_new_label(c);
  if (s->k == SK_XMM || s->k == SK_VAR || s->k == SK_CONST) { jc_load_num(c, pos, 0); goto numpath; }
  {
    int b, dd; jc_mem_addr(c, pos, &b, &dd);
    if (s->n) { jx_movsd_rm(x, 0, b, dd + 8); goto numpath; }
    jx_mov32_rm(x, RAX, b, dd);
    jx_cmp32_ri(x, RAX, V_NIL); jc_jcc(c, CC_NE, l1);
    if (jump_if_falsey) jc_jmp(c, lab); else jc_jmp(c, done);
    jc_bind(c, l1);
    jx_cmp32_ri(x, RAX, V_BOOL); jc_jcc(c, CC_NE, l2);
    jx_cmp8_mi(x, b, dd + 8, 0);
    jc_jcc(c, jump_if_falsey ? CC_E : CC_NE, lab); jc_jmp(c, done);
    jc_bind(c, l2);
    jx_cmp32_ri(x, RAX, V_NUM); jc_jcc(c, CC_NE, dl);
    jx_movsd_rm(x, 0, b, dd + 8);
  }
numpath:
  jx_xorpd(x, 1, 1); jx_ucomisd(x, 0, 1);
  if (jump_if_falsey) { jc_jcc(c, CC_P, done); jc_jcc(c, CC_E, lab); }
  else { jc_jcc(c, CC_P, lab); jc_jcc(c, CC_NE, lab); }
  jc_bind(c, done);
}

static int jc_jump_label(JC *c, JI *in, int depth, int skip_pop) {
  const uint8_t *tp = in->tip + (skip_pop ? 1 : 0);
  int t = jc_target_idx(c, tp);
  if (t >= 0) return t;
  return jc_exit_label(c, tp, depth);
}

static int jc_fusable_pop(JC *c, int i) {
  JI *in = &c->ins[i];
  if (i + 1 >= c->n || c->ins[i + 1].op != OP_POP || c->islabel[i + 1]) return 0;
  if (!in->tip || *in->tip != OP_POP) return 0;
  return 1;
}

static void jc_list_ptr(JC *c, int i, int d, int lp) {
  XB *x = &c->x; SD *s = &c->sd[lp];
  if (s->k != SK_MEM && s->k != SK_HOME) { c->failed = 1; c->why = "listop"; return; }
  int dl = jc_deopt(c, i, d), b, dd; jc_mem_addr(c, lp, &b, &dd);
  jx_cmp32_mi(x, b, dd, V_OBJ); jc_jcc(c, CC_NE, dl);
  jx_mov_rm(x, RAX, b, dd + 8);
  jx_cmp32_mi(x, RAX, (int)offsetof(Obj, t), O_LIST); jc_jcc(c, CC_NE, dl);
}
static void jc_list_index(JC *c, int i, int d, int qp) {
  XB *x = &c->x; int dl = jc_deopt(c, i, d);
  jc_load_num(c, qp, 0);
  jx_cvttsd2si64(x, RCX, 0); jx_cvtsi2sd64(x, 1, RCX); jx_ucomisd(x, 1, 0);
  jc_jcc(c, CC_P, dl); jc_jcc(c, CC_NE, dl);
  jx_mov32_rm(x, RDX, RAX, (int)(offsetof(List, a) + offsetof(VArr, n)));
  jx_cmp_rr(x, RCX, RDX); jc_jcc(c, CC_AE, dl);
  jx_mov_rm(x, RDX, RAX, (int)(offsetof(List, a) + offsetof(VArr, v)));
  jx_shl_ri(x, RCX, 4);
}

static void jc_arith(JC *c, int i, int d, int op) {
  XB *x = &c->x; int a = d - 2, b = d - 1;
  jc_need_num(c, i, d, a); jc_need_num(c, i, d, b);
  int comm = op == 0x0F58 || op == 0x0F59;
  int divop = op == 0x0F5E;
  int dpos = a, spos = b;
  if (c->sd[a].k != SK_XMM && comm && c->sd[b].k == SK_XMM) { dpos = b; spos = a; }
  int dst;
  if (c->sd[dpos].k == SK_XMM) dst = c->sd[dpos].r;
  else { dst = jc_alloc_tmp(c, d); }
  if (divop) {
    SD *sb = &c->sd[spos];
    int skip = sb->k == SK_CONST && (sb->bits << 1) != 0 && ((sb->bits >> 52) & 0x7FF) != 0x7FF;
    if (!skip) {
      int dl = jc_deopt(c, i, d), ok = jc_new_label(c);
      int rb = jc_reg_for(c, spos, 1);
      jx_xorpd(x, 2, 2); jx_ucomisd(x, rb, 2); jc_jcc(c, CC_P, ok); jc_jcc(c, CC_E, dl); jc_bind(c, ok);
    }
  }
  if (c->sd[dpos].k != SK_XMM) jc_load_num(c, dpos, dst);
  jc_op_rm(c, 0xF2, op, dst, spos);
  c->sd[a] = sd_mk(SK_XMM); c->sd[a].r = (uint8_t)dst;
}

static void jc_mod(JC *c, int i, int d) {
  XB *x = &c->x; int a = d - 2, b = d - 1;
  jc_need_num(c, i, d, a); jc_need_num(c, i, d, b);
  int t = jc_alloc_tmp(c, d);
  int dl = jc_deopt(c, i, d);
  SD sb = c->sd[b];
  double cv = 0; int isc = 0;
  if (sb.k == SK_CONST) { memcpy(&cv, &sb.bits, 8); if (cv >= 1 && cv <= 1073741824.0 && cv == (double)(int32_t)cv) isc = 1; }
  jc_load_num(c, a, 0);
  int done = jc_new_label(c), slow = jc_new_label(c);
  if (isc) {
    int ci = (int)cv;
    jx_cvttsd2si32(x, RAX, 0); jx_cvtsi2sd32(x, 2, RAX); jx_ucomisd(x, 0, 2);
    jc_jcc(c, CC_NE, slow); jc_jcc(c, CC_P, slow);
    jx_test32_rr(x, RAX, RAX); jc_jcc(c, CC_S, slow);
    if ((ci & (ci - 1)) == 0) {
      jx_and32_ri(x, RAX, ci - 1);
    } else {
      int L = 0; while ((1ll << L) < ci) L++;
      int k = 31 + L; uint64_t M = ((1ull << k) + (uint64_t)ci - 1) / (uint64_t)ci;
      jx_alu32_rr(x, 0x89, RCX, RAX);
      jx_mov_ri(x, RDX, M); jx_imul64_rr(x, RCX, RDX); jx_shr64_ri(x, RCX, k);
      jx_imul32_rri(x, RCX, RCX, ci); jx_alu32_rr(x, 0x29, RAX, RCX);
    }
    jx_cvtsi2sd32(x, t, RAX); jc_jmp(c, done);
    jc_bind(c, slow);
    jc_load_num(c, b, 1);
    jc_call_helper(c, (void *)&jit_h_mod);
    jx_movaps(x, t, 0);
    jc_bind(c, done);
  } else {
    int ok = jc_new_label(c), k = jc_new_label(c), res = jc_new_label(c);
    jc_load_num(c, b, 1);
    jx_xorpd(x, 2, 2); jx_ucomisd(x, 1, 2); jc_jcc(c, CC_P, ok); jc_jcc(c, CC_E, dl); jc_bind(c, ok);
    jx_cvttsd2si32(x, RAX, 0); jx_cvtsi2sd32(x, 2, RAX); jx_ucomisd(x, 2, 0); jc_jcc(c, CC_NE, slow); jc_jcc(c, CC_P, slow);
    jx_cvttsd2si32(x, RCX, 1); jx_cvtsi2sd32(x, 2, RCX); jx_ucomisd(x, 2, 1); jc_jcc(c, CC_NE, slow); jc_jcc(c, CC_P, slow);
    jx_cmp32_ri(x, RAX, (int32_t)0x80000000); jc_jcc(c, CC_E, slow);
    jx_cmp32_ri(x, RCX, (int32_t)0x80000000); jc_jcc(c, CC_E, slow);
    jx_cmp32_ri(x, RCX, -1); jc_jcc(c, CC_NE, k);
    jx_xorpd(x, t, t); jc_jmp(c, done);
    jc_bind(c, k);
#if JX_ARM
    jx_sdiv32(x, RDX, RAX, RCX);      /* w2 = w0 / w1 (يقتطع كالـidiv تماماً) */
    jx_msub32(x, RDX, RDX, RCX, RAX); /* w2 = w0 - w2*w1 = الباقي المقتطع */
#else
    jx_cdq(x); jx_idiv32(x, RCX);
#endif
    jx_test32_rr(x, RDX, RDX); jc_jcc(c, CC_E, res);
#if JX_ARM
    jx_alu32_rr(x, 0x89, RAX, RDX); /* mov */
    jx_alu32_rr(x, 0x31, RAX, RCX); /* eor — على ARM لا يضبط الأعلام فنُعيد الفحص بـtst */
    jx_test32_rr(x, RAX, RAX);      /* N = إشارة (rem ^ b) */
    jc_jcc(c, CC_NS, res);
#else
    jx_alu32_rr(x, 0x89, RAX, RDX); jx_alu32_rr(x, 0x31, RAX, RCX); jc_jcc(c, CC_NS, res);
#endif
    jx_alu32_rr(x, 0x01, RDX, RCX);
    jc_bind(c, res);
    jx_cvtsi2sd32(x, t, RDX); jc_jmp(c, done);
    jc_bind(c, slow);
    jc_call_helper(c, (void *)&jit_h_mod);
    jx_movaps(x, t, 0);
    jc_bind(c, done);
  }
  c->sd[a] = sd_mk(SK_XMM); c->sd[a].r = (uint8_t)t;
}

static void jc_pow(JC *c, int i, int d) {
  XB *x = &c->x; int a = d - 2, b = d - 1;
  jc_need_num(c, i, d, a); jc_need_num(c, i, d, b);
  int t = jc_alloc_tmp(c, d);
  SD sb = c->sd[b]; double cv = 0;
  if (sb.k == SK_CONST) memcpy(&cv, &sb.bits, 8);
  if (sb.k == SK_CONST && cv == 2.0) {
    jc_load_num(c, a, t); jx_mulsd(x, t, t);
  } else {
    jc_load_num(c, a, 0); jc_load_num(c, b, 1);
    jc_call_helper(c, (void *)&jit_h_pow);
    jx_movaps(x, t, 0);
  }
  c->sd[a] = sd_mk(SK_XMM); c->sd[a].r = (uint8_t)t;
}

static void jc_bitop(JC *c, int i, int d, int op) {
  XB *x = &c->x; int a = d - 2, b = d - 1;
  jc_need_num(c, i, d, a); jc_need_num(c, i, d, b);
  int t = jc_alloc_tmp(c, d);
  int ra = jc_reg_for(c, a, 0), rb = jc_reg_for(c, b, 1);
  jx_cvttsd2si64(x, RAX, ra); jx_cvttsd2si64(x, RCX, rb);
  if (op == OP_BAND) jx_alu_rr(x, 0x21, RAX, RCX);
  else if (op == OP_BOR) jx_alu_rr(x, 0x09, RAX, RCX);
  else if (op == OP_BXOR) jx_alu_rr(x, 0x31, RAX, RCX);
  else { jx_and32_ri(x, RCX, 63); if (op == OP_SHL) jx_shl_cl(x, RAX); else jx_sar_cl(x, RAX); }
  jx_cvtsi2sd64(x, t, RAX);
  c->sd[a] = sd_mk(SK_XMM); c->sd[a].r = (uint8_t)t;
}

static void jc_store_bool_eax(JC *c, int pos) {
  jx_movzx8_rr(&c->x, RAX, RAX); jx_mov_mr(&c->x, R12, pos * 16 + 8, RAX); jc_settag(c, pos, V_BOOL);
  c->sd[pos] = sd_mk(SK_MEM);
}

/* ثوابت التعليمة: ثوابت المستدعى المُدرج إن وُجد وإلا ثوابت المنطقة */
static Value *jc_kc(JC *c, JI *in) { return (in->pf ? in->pf : c->fn)->ch.consts.v; }

static void jc_promote(JC *c) {
  c->npv = 0;
  if (!c->usepromo) return;
  PV cand[64]; int nc = 0;
  Value *kc = c->fn->ch.consts.v;
  for (int i = 0; i < c->n; i++) {
    if (!c->reach[i]) continue;
    JI *in = &c->ins[i]; int g, idx, gi = -1;
    if (in->op == OP_GET_LOCAL || in->op == OP_SET_LOCAL) { if (in->a >= c->spoff) continue; g = 0; idx = in->a; }
    else if (in->op == OP_GET_GLOBAL || in->op == OP_SET_GLOBAL) {
      Str *nm = AS_STR(jc_kc(c, in)[in->a]); Dict *dd = &c->clo->globals->d; /* jc_kc: تعليمة مُدرجة تقرأ مسبار دالتها لا مسبار المنطقة */
      gi = dfind(dd, OBJV(nm), nm->hash); if (gi < 0) continue; g = 1; idx = gi;
    } else continue;
    int f = -1; for (int k = 0; k < nc; k++) if (cand[k].g == g && cand[k].idx == idx) { f = k; break; }
    if (f < 0) { if (nc >= 64) continue; f = nc++; cand[f].g = g; cand[f].idx = idx; cand[f].gi = gi; cand[f].uses = 0; cand[f].ok = 1; cand[f].reg = 0; cand[f].noc = 0;
      Value v = g ? c->clo->globals->d.e[gi].v : c->slots[idx];
      cand[f].ok = IS_NUM(v); }
    cand[f].uses++;
  }
  /* مرشحو حلقة المدى: آلية الحلقة (العدّاد/العدد/أ) بوسوم صحيحة من خارج المنطقة، ووجهات القيم (سلوتات تُنشأ داخلها — بلا فحص وسم عند الدخول) */
  for (int i = 0; i < c->n; i++) {
    if (!c->reach[i]) continue;
    JI *in = &c->ins[i];
    if (in->op != OP_RFOR) continue;
    int d = c->depth[i], base = in->a, flag = in->b, nvr = in->c;
    int mach[3], nm = 0;
    mach[nm++] = flag ? base + 2 : base + 1; /* العدّاد */
    mach[nm++] = flag ? base + 1 : base + 0; /* العدد */
    if (flag) mach[nm++] = base;             /* أ */
    for (int m = 0; m < nm; m++) {
      int idx = mach[m], f = -1;
      for (int k = 0; k < nc; k++) if (!cand[k].g && cand[k].idx == idx) { f = k; break; }
      if (f < 0) { if (nc >= 64) continue; f = nc++; cand[f].g = 0; cand[f].idx = idx; cand[f].gi = 0; cand[f].uses = 0; cand[f].reg = 0; cand[f].noc = 0; cand[f].ok = IS_NUM(c->slots[idx]); }
      cand[f].uses += 100;
    }
    for (int k = 0; k < nvr; k++) {
      int idx = c->spoff + d + k, f = -1; /* العمق d+k يوافق السلوت المحلي spoff+d+k (R12 = RBX + spoff*16) */
      /* وجهة لا تُقرأ في الجسم إطلاقاً: بلا سجل — الكتابة للذاكرة تكفي، والسجل يُحرَّر لمتغير حارق
         تُكلف إقامته في الذاكرة أغلى بكثير (عالم بقراءة+كتابة ≈ 8 عمليات ذاكرة كل تكرار) */
      int reads = 0;
      for (int q = 0; q < c->n; q++) { if (c->reach[q] && c->ins[q].op == OP_GET_LOCAL && c->ins[q].a == idx) reads++; }
      for (int q = 0; q < nc; q++) if (!cand[q].g && cand[q].idx == idx) { f = q; break; }
      if (f < 0) { if (nc >= 64) continue; f = nc++; cand[f].g = 0; cand[f].idx = idx; cand[f].gi = 0; cand[f].uses = 0; cand[f].reg = 0; cand[f].noc = 1; cand[f].ok = 1; }
      cand[f].uses += reads > 0 ? 50 : 0;
    }
  }
  for (int k = 0; k < nc; k++) if (!cand[k].ok) { cand[k] = cand[nc - 1]; nc--; k--; }
  for (int a = 0; a < nc; a++) for (int b = a + 1; b < nc; b++) if (cand[b].uses > cand[a].uses) { PV t = cand[a]; cand[a] = cand[b]; cand[b] = t; }
  if (nc > JMAXPV) nc = JMAXPV;
  for (int k = 0; k < nc; k++) { c->pv[k] = cand[k]; c->pv[k].ok = 1; }
  c->npv = nc;
  for (int iter = 0; iter < 12; iter++) {
    int changed = 0; uint8_t ty[JMAXD + 8]; memset(ty, 0, sizeof ty);
    for (int i = 0; i < c->n; i++) {
      if (!c->reach[i]) continue;
      JI *in = &c->ins[i]; int d = c->depth[i];
      if (c->islabel[i]) memset(ty, 0, sizeof ty);
      switch ((Op)in->op) {
        case OP_CONST: ty[d] = IS_NUM(jc_kc(c, in)[in->a]); break;
        case OP_NIL: case OP_TRUE: case OP_FALSE: case OP_GET_UPV: ty[d] = 0; break;
        case OP_GET_LOCAL: {
          int k = jc_find_pv(c, 0, in->a); ty[d] = k >= 0 && c->pv[k].ok;
          if (in->pf && k < 0 && in->a >= c->spoff) { int p = in->a - c->spoff; if (p >= 0 && p < d) ty[d] = ty[p]; } /* وسيط مُدرج يرث نوع موقعه */
          break;
        }
        case OP_GET_GLOBAL: {
          Str *nm = AS_STR(jc_kc(c, in)[in->a]); int gi = dfind(&c->clo->globals->d, OBJV(nm), nm->hash);
          int k = gi >= 0 ? jc_find_pv(c, 1, gi) : -1; ty[d] = k >= 0 && c->pv[k].ok; break;
        }
        case OP_DUP: ty[d] = ty[d - 1]; break;
        case OP_DUP2: ty[d] = ty[d - 2]; ty[d + 1] = ty[d - 1]; break;
        case OP_SET_LOCAL: case OP_SET_GLOBAL: {
          int k;
          if (in->op == OP_SET_LOCAL) k = jc_find_pv(c, 0, in->a);
          else { Str *nm = AS_STR(kc[in->a]); int gi = dfind(&c->clo->globals->d, OBJV(nm), nm->hash); k = gi >= 0 ? jc_find_pv(c, 1, gi) : -1; }
          if (k >= 0 && c->pv[k].ok && ty[d - 1] != 1) { c->pv[k].ok = 0; changed = 1; }
          break;
        }
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_POW: case OP_IDIV:
        case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR: ty[d - 2] = 1; break;
        case OP_EQ: case OP_LT: case OP_LE: case OP_GT: case OP_GE: ty[d - 2] = 0; break;
        case OP_NEG: case OP_GET_PROP: ty[d - 1] = 1; break;
        case OP_NOT: ty[d - 1] = 0; break;
        case OP_GET_INDEX: ty[d - 2] = 0; break;
        case OP_CALL: ty[d - 1 - in->a] = 0; break; /* نتيجة استدعاء بنوع مجهول — لا ترقية عليها */
        case OP_SET_INDEX: ty[d - 3] = ty[d - 1]; break;
        case OP_FOREACH: for (int k = 0; k < in->b; k++) ty[d + k] = 0; break;
        case OP_RFOR: for (int k = 0; k < in->c; k++) ty[d + k] = 1; break; /* قيم المدى أرقام دائماً */
        case OP_JMOVE: { /* انهيار مُدرج: نوع الوجهة = نوع القمة، وما فوقها يموت */
          int dst = d - 1 - in->a;
          if (dst >= 0) { ty[dst] = ty[d - 1]; for (int q = dst + 1; q < d; q++) ty[q] = 0; }
          break;
        }
        case OP_CHECK: if (in->a & TY_NUM) ty[d - 1] = 1; break;
        default: break;
      }
    }
    if (!changed) break;
  }
  int m = 0;
  for (int k = 0; k < c->npv; k++) if (c->pv[k].ok) c->pv[m++] = c->pv[k];
  c->npv = m;
  for (int k = 0; k < m; k++) {
    c->pv[k].reg = VAR_LO + k;
    if (c->pv[k].g) {
      Dict *dd = &c->clo->globals->d; Value key = dd->e[c->pv[k].gi].k;
      if (!IS_T(key, O_STR)) { c->npv = 0; return; }
      jc_global_index(c, AS_STR(key));
    }
  }
}

static void jc_gen_body(JC *c) {
  XB *x = &c->x;
  for (int i = 0; i < c->n && !c->failed; i++) {
    JI *in = &c->ins[i];
    if (!c->reach[i]) continue;
    int d = c->depth[i];
    if (c->islabel[i]) { jc_canon(c, 0, d); jc_forget(c, d); }
    c->lpos[i] = x->n;
    switch ((Op)in->op) {
      case OP_CONST: {
        Value v = jc_kc(c, in)[in->a];
        if (IS_NUM(v)) {
          /* ثابت آلية حلقة داخل المنطقة (عدّاد/عدد/أ): حدّث سجل المتغير المُرقّى أيضاً —
             RFOR/RFOREP يقرآن السجل لا الذاكرة، وترك السجل قديماً يُبطل تصفير العدّاد كل تكرار خارجي */
          int pk = jc_find_pv(c, 0, c->spoff + d);
          if (pk >= 0) {
            uint64_t bts; memcpy(&bts, &v.as.n, 8);
            if (bts == 0) jx_xorpd(x, c->pv[pk].reg, c->pv[pk].reg);
            else { jx_mov_ri(x, RAX, bts); jx_movq_xr(x, c->pv[pk].reg, RAX); }
            c->sd[d] = sd_mk(SK_VAR); c->sd[d].r = (uint8_t)c->pv[pk].reg; c->sd[d].var = pk;
          } else { c->sd[d] = sd_mk(SK_CONST); memcpy(&c->sd[d].bits, &v.as.n, 8); }
        }
        else if (IS_T(v, O_STR)) { jx_mov_ri(x, RAX, (uint64_t)(uintptr_t)v.as.o); jx_mov_mr(x, R12, d * 16 + 8, RAX); jc_settag(c, d, V_OBJ); c->sd[d] = sd_mk(SK_MEM); }
        else { c->failed = 1; c->why = "const"; }
        break;
      }
      case OP_CHECK:
        if (in->a & TY_NUM) jc_need_num(c, i, d, d - 1); else { c->failed = 1; c->why = "check"; }
        break;
      case OP_NIL: jc_settag(c, d, V_NIL); c->sd[d] = sd_mk(SK_MEM); break;
      case OP_TRUE: case OP_FALSE:
        jx_mov_ri(x, RAX, in->op == OP_TRUE ? 1 : 0); jx_mov_mr(x, R12, d * 16 + 8, RAX); jc_settag(c, d, V_BOOL); c->sd[d] = sd_mk(SK_MEM); break;
      case OP_POP: break;
      case OP_DUP: case OP_DUP2: {
        int cnt = in->op == OP_DUP ? 1 : 2;
        for (int k = 0; k < cnt; k++) {
          int sp = d - cnt + k, dp = d + k;
          SD s = c->sd[sp];
          if (s.k == SK_XMM) { int t = jc_alloc_tmp(c, dp); s = c->sd[sp]; if (s.k == SK_XMM) { jx_movaps(x, t, s.r); c->sd[dp] = sd_mk(SK_XMM); c->sd[dp].r = (uint8_t)t; } else { jc_copy16(c, R12, dp * 16, R12, sp * 16); c->sd[dp] = s; } }
          else if (s.k == SK_MEM) { jc_copy16(c, R12, dp * 16, R12, sp * 16); c->sd[dp] = s; }
          else c->sd[dp] = s;
        }
        break;
      }
      case OP_GET_LOCAL: case OP_GET_GLOBAL: {
        int k = -1;
        if (in->op == OP_GET_LOCAL) k = jc_find_pv(c, 0, in->a);
        else { /* الترقية لأعالم العوالم فقط — المدمجة لا تُرقّى (ليست أعداداً) */
          Str *nm = AS_STR(jc_kc(c, in)[in->a]);
          if (dfind(&c->clo->globals->d, OBJV(nm), nm->hash) >= 0) { int gi = jc_global_index(c, nm); k = jc_find_pv(c, 1, gi); }
        }
        if (k >= 0) { c->sd[d] = sd_mk(SK_VAR); c->sd[d].r = (uint8_t)c->pv[k].reg; c->sd[d].var = k; break; }
        int b, dd;
        if (in->op == OP_GET_LOCAL) {
          if (in->a >= c->spoff) { int p = in->a - c->spoff; if (p >= d) { c->failed = 1; c->why = "slot"; break; } jc_mat(c, p); }
          jc_local_home(c, in->a, &b, &dd);
        } else {
          Str *nm = AS_STR(jc_kc(c, in)[in->a]);
          int gi = dfind(&c->clo->globals->d, OBJV(nm), nm->hash);
          if (gi >= 0) {
            gi = jc_global_index(c, nm);
            b = R13; dd = gi * (int)sizeof(Ent) + (int)offsetof(Ent, v);
            /* فحص هوية لكل موقع إدراج: المستدعى المُدرج يجب أن يظل هو نفسه عند كل تنفيذ —
               تغيّر العام ⇒ تخفي إلى ip التعليمة نفسها فيعيد المفسّر قراءته بالقيمة الجديدة */
            for (int z = 0; z < c->ng; z++) if (c->gnames[z] == nm && c->gexp[z]) {
              int dl = jc_deopt(c, i, d);
              jx_mov32_rm(x, RAX, b, dd); jx_cmp32_ri(x, RAX, V_OBJ); jc_jcc(c, CC_NE, dl);
              jx_mov_ri(x, RAX, (uint64_t)(uintptr_t)c->gexp[z]);
              jx_cmp_rm(x, RAX, b, dd + 8); jc_jcc(c, CC_NE, dl);
              break;
            }
          } else {
            /* عام مدمج (طول، اطبع...): يُقرأ حديثاً من vm.builtins في كل تنفيذ فلا حارس قيمة.
               حارس الظل: جيل قاموس العوالم لم يتغير منذ الترجمة ⇒ لم يُلحَق أي عام جديد
               ⇒ الاسم غير مظلَّل؛ أي إلحاق (حتى من استدعاء وسط التنفيذ) يزيد الجيل ⇒
               تخفي إلى ip التعليمة فيقرأه المفسّر بالمسار الطبيعي (ظلّ أو مدمج). */
            int bi = dfind(&vm.builtins, OBJV(nm), nm->hash);
            if (bi < 0) { c->failed = 1; c->why = "global"; break; }
            c->nbi++;
            int dl = jc_deopt(c, i, d);
            jx_cmp32_mi(x, R13, (int)offsetof(Dict, gen) - (int)offsetof(Dict, e), c->ggen); jc_jcc(c, CC_NE, dl);
            jx_mov_ri(x, RAX, (uint64_t)(uintptr_t)&vm.builtins.e);
            jx_mov_rm(x, RSI, RAX, 0);
            jx_mov_ri(x, RAX, (uint64_t)(uintptr_t)nm);
            jx_cmp_rm(x, RAX, RSI, bi * (int)sizeof(Ent) + (int)offsetof(Ent, k) + 8); jc_jcc(c, CC_NE, dl);
            jc_copy16(c, R12, d * 16, RSI, bi * (int)sizeof(Ent) + (int)offsetof(Ent, v));
            c->sd[d] = sd_mk(SK_MEM);
            break;
          }
        }
        c->sd[d] = sd_mk(SK_HOME); c->sd[d].hb = (int8_t)b; c->sd[d].hd = dd;
        if (in->op == OP_GET_LOCAL && in->a >= c->spoff) c->sd[d].n = c->sd[in->a - c->spoff].n;
        break;
      }
      case OP_GET_UPV: {
        jx_mov_rm(x, RSI, R14, (int)offsetof(Closure, upv)); jx_mov_rm(x, RSI, RSI, in->a * 8); jx_mov_rm(x, RSI, RSI, (int)offsetof(Upv, loc));
        jc_copy16(c, R12, d * 16, RSI, 0); c->sd[d] = sd_mk(SK_MEM); break;
      }
      case OP_SET_UPV: {
        jc_mat(c, d - 1);
        jx_mov_rm(x, RSI, R14, (int)offsetof(Closure, upv)); jx_mov_rm(x, RSI, RSI, in->a * 8); jx_mov_rm(x, RSI, RSI, (int)offsetof(Upv, loc));
        jc_copy16(c, RSI, 0, R12, (d - 1) * 16); break;
      }
      case OP_SET_LOCAL: case OP_SET_GLOBAL: {
        int k = -1, gi = -1;
        if (in->op == OP_SET_LOCAL) k = jc_find_pv(c, 0, in->a);
        else { Str *nm = AS_STR(c->fn->ch.consts.v[in->a]); gi = jc_global_index(c, nm); k = jc_find_pv(c, 1, gi); }
        if (k >= 0) {
          jc_need_num(c, i, d, d - 1);
          jc_unalias_var(c, d - 1, d, k);
          jc_load_num(c, d - 1, c->pv[k].reg);
          break;
        }
        int b, dd;
        if (in->op == OP_SET_LOCAL) {
          if (in->a >= c->spoff) { int p = in->a - c->spoff; if (p >= d - 1) { c->failed = 1; c->why = "slot"; break; } }
          jc_local_home(c, in->a, &b, &dd);
        } else { b = R13; dd = gi * (int)sizeof(Ent) + (int)offsetof(Ent, v); }
        { SD *tp = &c->sd[d - 1]; if (tp->k == SK_HOME && tp->hb == b && tp->hd == dd) break; }
        jc_unalias_home(c, d - 1, b, dd);
        jc_mat(c, d - 1);
        jc_copy16(c, b, dd, R12, (d - 1) * 16);
        if (in->op == OP_SET_LOCAL && in->a >= c->spoff) { int p = in->a - c->spoff; c->sd[p] = sd_mk(SK_MEM); c->sd[p].n = c->sd[d - 1].n; }
        break;
      }
      case OP_GET_PROP: {
        Value nm = c->fn->ch.consts.v[in->a];
        if (!IS_T(nm, O_STR) || AS_STR(nm) != vm.s_len) { c->failed = 1; c->why = "prop"; break; }
        int t = jc_alloc_tmp(c, d);
        jc_list_ptr(c, i, d, d - 1);
        if (c->failed) break;
        jx_cvtsi2sd32_m(x, t, RAX, (int)(offsetof(List, a) + offsetof(VArr, n)));
        c->sd[d - 1] = sd_mk(SK_XMM); c->sd[d - 1].r = (uint8_t)t;
        break;
      }
      case OP_GET_INDEX: {
        jc_need_num(c, i, d, d - 1);
        jc_list_ptr(c, i, d, d - 2);
        if (c->failed) break;
        jc_list_index(c, i, d, d - 1);
        jc_copyx(c, R12, -1, (d - 2) * 16, RDX, RCX, 0);
        c->sd[d - 2] = sd_mk(SK_MEM);
        break;
      }
      case OP_SET_INDEX: {
        jc_need_num(c, i, d, d - 2);
        jc_list_ptr(c, i, d, d - 3);
        if (c->failed) break;
        jc_list_index(c, i, d, d - 2);
        SD v = c->sd[d - 1];
        jx_alu_rr(x, 0x01, RDX, RCX);
        switch (v.k) {
          case SK_XMM: case SK_VAR: jx_movsd_mr(x, RDX, 8, v.r); jx_mov32_mi(x, RDX, 0, V_NUM); break;
          case SK_CONST: jx_mov_ri(x, RAX, v.bits); jx_mov_mr(x, RDX, 8, RAX); jx_mov32_mi(x, RDX, 0, V_NUM); break;
          case SK_HOME: jc_copy16(c, RDX, 0, v.hb, v.hd); break;
          default: jc_copy16(c, RDX, 0, R12, (d - 1) * 16); break;
        }
        if (v.k == SK_MEM) { jc_copy16(c, R12, (d - 3) * 16, R12, (d - 1) * 16); }
        c->sd[d - 3] = v;
        break;
      }
      case OP_ADD: jc_arith(c, i, d, 0x0F58); break;
      case OP_SUB: jc_arith(c, i, d, 0x0F5C); break;
      case OP_MUL: jc_arith(c, i, d, 0x0F59); break;
      case OP_DIV: jc_arith(c, i, d, 0x0F5E); break;
      case OP_IDIV: {
        jc_arith(c, i, d, 0x0F5E);
        jx_roundsd(x, c->sd[d - 2].r, c->sd[d - 2].r, 9);
        break;
      }
      case OP_MOD: jc_mod(c, i, d); break;
      case OP_POW: jc_pow(c, i, d); break;
      case OP_NEG: {
        jc_need_num(c, i, d, d - 1);
        int t = c->sd[d - 1].k == SK_XMM ? c->sd[d - 1].r : jc_alloc_tmp(c, d);
        if (c->sd[d - 1].k != SK_XMM) jc_load_num(c, d - 1, t);
        jx_mov_ri(x, RAX, 0x8000000000000000ull); jx_movq_xr(x, 2, RAX); jx_xorpd(x, t, 2);
        c->sd[d - 1] = sd_mk(SK_XMM); c->sd[d - 1].r = (uint8_t)t;
        break;
      }
      case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR: jc_bitop(c, i, d, in->op); break;
      case OP_EQ: case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
        int a = d - 2, b = d - 1;
        jc_need_num(c, i, d, a); jc_need_num(c, i, d, b);
        int fuse = 0;
        if (i + 2 < c->n && (c->ins[i + 1].op == OP_JIF || c->ins[i + 1].op == OP_JIT) && !c->islabel[i + 1] && c->ins[i + 2].op == OP_POP && !c->islabel[i + 2] && jc_fusable_pop(c, i + 1)) fuse = 1;
        int lt = in->op == OP_LT, le = in->op == OP_LE;
        if (fuse) jc_canon(c, 0, d - 2);
        if (lt || le) { int rb = jc_reg_for(c, b, 1); jc_op_rm(c, 0x66, 0x0F2E, rb, a); }
        else { int ra = jc_reg_for(c, a, 0); jc_op_rm(c, 0x66, 0x0F2E, ra, b); }
        if (fuse) {
          JI *jj = &c->ins[i + 1]; int on_true = jj->op == OP_JIT;
          int lab = jc_jump_label(c, jj, d - 2, 1);
          if (in->op == OP_EQ) {
            if (!on_true) { jc_jcc(c, CC_NE, lab); jc_jcc(c, CC_P, lab); }
            else { int sk = jc_new_label(c); jc_jcc(c, CC_P, sk); jc_jcc(c, CC_E, lab); jc_bind(c, sk); }
          } else {
            int strict = in->op == OP_LT || in->op == OP_GT;
            int cct = strict ? CC_A : CC_AE, ccf = strict ? CC_BE : CC_B;
            jc_jcc(c, on_true ? cct : ccf, lab);
          }
          i += 2;
          break;
        }
        if (in->op == OP_EQ) { jx_setcc(x, CC_E, RAX); jx_setcc(x, CC_NP, RCX); jx_and8_rr(x, RAX, RCX); }
        else jx_setcc(x, (in->op == OP_LT || in->op == OP_GT) ? CC_A : CC_AE, RAX);
        jc_store_bool_eax(c, a);
        break;
      }
      case OP_NOT: {
        jc_mat(c, d - 1);
        int lf = jc_new_label(c), ld = jc_new_label(c);
        jc_cond_jump(c, i, d, d - 1, 1, lf);
        jx_mov_ri(x, RAX, 0); jx_mov_mr(x, R12, (d - 1) * 16 + 8, RAX); jc_settag(c, d - 1, V_BOOL); jc_jmp(c, ld);
        jc_bind(c, lf);
        jx_mov_ri(x, RAX, 1); jx_mov_mr(x, R12, (d - 1) * 16 + 8, RAX); jc_settag(c, d - 1, V_BOOL);
        jc_bind(c, ld); c->sd[d - 1] = sd_mk(SK_MEM);
        break;
      }
      case OP_JUMP: case OP_LOOP: jc_canon(c, 0, d); jc_jmp(c, jc_jump_label(c, in, d, 0)); break;
      case OP_JIF: case OP_JIT: {
        int on_false = in->op == OP_JIF;
        if (jc_fusable_pop(c, i)) {
          jc_canon(c, 0, d - 1);
          int lab = jc_jump_label(c, in, d - 1, 1);
          jc_cond_jump(c, i, d, d - 1, on_false, lab); i += 1;
        } else {
          jc_canon(c, 0, d - 1);
          jc_mat(c, d - 1);
          jc_cond_jump(c, i, d, d - 1, on_false, jc_jump_label(c, in, d, 0));
        }
        break;
      }
      case OP_JNN: case OP_JNIL:
        jc_canon(c, 0, d);
        jx_cmp32_mi(x, R12, (d - 1) * 16, V_NIL); jc_jcc(c, in->op == OP_JNIL ? CC_E : CC_NE, jc_jump_label(c, in, d, 0)); break;
      case OP_FOREACH: {
        jc_canon(c, 0, d);
        int sl = in->a * 16, nv = in->b, dl = jc_deopt(c, i, d);
        int lnum = jc_new_label(c), done = jc_new_label(c), ex = jc_jump_label(c, in, d, 0);
        jx_mov32_rm(x, RAX, RBX, sl); jx_cmp32_ri(x, RAX, V_NUM); jc_jcc(c, CC_E, lnum);
        jx_cmp32_ri(x, RAX, V_OBJ); jc_jcc(c, CC_NE, dl);
        jx_mov_rm(x, RDX, RBX, sl + 8);
        jx_cmp32_mi(x, RDX, (int)offsetof(Obj, t), O_LIST); jc_jcc(c, CC_NE, dl);
        jx_mov_ri(x, RAX, 0x4000000000000000ull); jx_cmp_rm(x, RAX, RBX, sl + 40); jc_jcc(c, CC_E, dl);
        jx_movsd_rm(x, 3, RBX, sl + 24); jx_cvttsd2si64(x, RCX, 3);
        jx_mov32_rm(x, RAX, RDX, (int)(offsetof(List, a) + offsetof(VArr, n)));
        jx_cmp_rr(x, RCX, RAX); jc_jcc(c, CC_GE, ex);
        jx_mov_rm(x, RAX, RDX, (int)(offsetof(List, a) + offsetof(VArr, v)));
        jx_shl_ri(x, RCX, 4); jc_copyx(c, R12, -1, d * 16 + (nv == 2 ? 16 : 0), RAX, RCX, 0);
        jx_mov_ri(x, RAX, 0x3FF0000000000000ull); jx_movq_xr(x, 2, RAX); jx_movaps(x, 0, 3); jx_addsd(x, 0, 2); jx_movsd_mr(x, RBX, sl + 24, 0);
        if (nv == 2) { jx_movsd_mr(x, R12, d * 16 + 8, 3); jc_settag(c, d, V_NUM); }
        jc_jmp(c, done);
        jc_bind(c, lnum);
        jx_movsd_rm(x, 3, RBX, sl + 24);
        jx_rm(x, 0x66, 0, 0x0F2E, 3, RBX, -1, 0, sl + 8); jc_jcc(c, CC_AE, ex);
        jx_mov_ri(x, RAX, 0x3FF0000000000000ull); jx_movq_xr(x, 2, RAX); jx_movaps(x, 0, 3); jx_addsd(x, 0, 2); jx_movsd_mr(x, RBX, sl + 24, 0);
        for (int k = 0; k < nv; k++) { jx_movsd_mr(x, R12, (d + k) * 16 + 8, 3); jc_settag(c, d + k, V_NUM); }
        jc_bind(c, done);
        for (int k = 0; k < nv; k++) c->sd[d + k] = sd_mk(SK_MEM);
        break;
      }
      case OP_RFOREP: {
        /* تهيئة حلقة مدى متداخلة داخل المنطقة (كالمفسّر): flag=1: n=ceil(ب−أ) تُكتب في base+1 — flag=0: تنقيح العدد في base */
        jc_canon(c, 0, d);
        int base = in->a, flag = in->b;
        int dl = jc_deopt(c, i, d);
        int ak = jc_find_pv(c, 0, base), wk = jc_find_pv(c, 0, base + 1); /* أ/العدد و ب/العدّاد */
        /* فحصا الوسم كلاهما كالمفسّر: المُرقّى في سجل pv عددٌ ضمناً — وإلا فحص الوسم في ذاكرة الإطار */
        if (ak < 0) { jx_cmp32_mi(x, RBX, base * 16, V_NUM); jc_jcc(c, CC_NE, dl); }
        if (wk < 0) { jx_cmp32_mi(x, RBX, (base + 1) * 16, V_NUM); jc_jcc(c, CC_NE, dl); }
        int t = 3, z = 4; /* بعد التمهيد كل المؤقتات حرّة — نمط RFOR نفسه (XMM3/XMM4) */
        if (ak >= 0) jx_movaps(x, t, c->pv[ak].reg);
        else jx_movsd_rm(x, t, RBX, base * 16 + 8);
        if (flag) { /* n = ceil(ب − أ) */
          if (wk >= 0) jx_movaps(x, z, c->pv[wk].reg);
          else jx_movsd_rm(x, z, RBX, (base + 1) * 16 + 8);
          jx_subsd(x, z, t);
          jx_roundsd(x, z, z, 0x0A); /* تقريب للسقف */
        } else jx_movaps(x, z, t); /* n = العدد نفسه */
        jx_xorpd(x, t, t); /* 0.0 */
        jx_ucomisd(x, z, t);
        int ok = jc_new_label(c);
        jc_jcc(c, CC_A, ok); /* n > 0 ترتيبياً: أبقِه — inf يبقى حلقة لا نهائية كالمفسّر */
        jx_movaps(x, z, t);  /* NaN والسالب والصفر → 0 */
        jc_bind(c, ok);
        int dst = flag ? base + 1 : base, dk = jc_find_pv(c, 0, dst);
        { /* السلوت يوافق عمقاً في المكدس (القيم دُفعت داخل المنطقة): حدّث الحالة الرمزية وإلا
             أعاد canon في RFOR التالية كتابة القيمة القديمة فوق العدد المحسوب */
          int dd2 = dst - c->spoff;
          if (dd2 >= 0 && dd2 < d) {
            if (dk >= 0) { c->sd[dd2] = sd_mk(SK_VAR); c->sd[dd2].r = (uint8_t)c->pv[dk].reg; c->sd[dd2].var = dk; }
            else c->sd[dd2] = sd_mk(SK_MEM);
            c->sd[dd2].n = 1;
          }
        }
        if (dk >= 0) jx_movaps(x, c->pv[dk].reg, z); /* العدد في سجل المتغير المُرقّى */
        else jx_movsd_mr(x, RBX, dst * 16 + 8, z);   /* الوسم V_NUM ثابت قبل وبعد — بلا كتابة وسم */
        break;
      }
      case OP_RFOR: {
        /* حلقة المدى المتخصصة: عدّاد من 0 حتى العدد، القيمة = أ + العدّاد — عند الترقية كل شيء في سجلات pv بلا ذاكرة لكل تكرار */
        jc_canon(c, 0, d);
        int base = in->a, flag = in->b, nvr = in->c;
        int coff = flag ? 40 : 24, loff = flag ? 24 : 8; /* بايت: داخل كل سلوت 16 بايت الوسم عند +0 والعدد عند +8 */
        int ck = jc_find_pv(c, 0, flag ? base + 2 : base + 1); /* العدّاد */
        int lk = jc_find_pv(c, 0, flag ? base + 1 : base + 0); /* العدد */
        int ak = flag ? jc_find_pv(c, 0, base) : -1;           /* أ */
        int ex = jc_jump_label(c, in, d, 0);
        int cold; /* السجل الحامل للعدّاد القديم */
        if (ck >= 0) {
          cold = c->pv[ck].reg;
          if (lk >= 0) jx_ucomisd(x, cold, c->pv[lk].reg);
          else jx_rm(x, 0x66, 0, 0x0F2E, cold, RBX, -1, 0, base * 16 + loff);
        } else {
          jx_movsd_rm(x, 3, RBX, base * 16 + coff); cold = 3; /* XMM3 = العدّاد */
          if (lk >= 0) jx_ucomisd(x, 3, c->pv[lk].reg);
          else jx_rm(x, 0x66, 0, 0x0F2E, 3, RBX, -1, 0, base * 16 + loff);
        }
        jc_jcc(c, CC_AE, ex); /* انتهى → خروج (NaN مستحيل: RFOREP يصفّر العدد) */
        int vx = cold;        /* القيمة = العدّاد القديم (flag=0) */
        if (flag) {           /* القيمة = أ + العدّاد القديم → XMM4 */
          if (ak >= 0) jx_movaps(x, 4, c->pv[ak].reg);
          else jx_movsd_rm(x, 4, RBX, base * 16 + 8);
          jx_addsd(x, 4, cold); vx = 4;
        }
        /* كتابة الوجهات: nv=2 → ي=الفهرس (العدّاد القديم) ثم ت=القيمة؛ nv=1 → ي=القيمة */
        for (int k = 0; k < nvr; k++) {
          int src = (nvr == 2 && k == 0) ? cold : vx;
          int dk = jc_find_pv(c, 0, c->spoff + d + k);
          if (dk >= 0) {
            jx_movaps(x, c->pv[dk].reg, src);
            c->sd[d + k] = sd_mk(SK_VAR); c->sd[d + k].r = (uint8_t)c->pv[dk].reg; c->sd[d + k].var = dk;
          } else {
            jx_movsd_mr(x, R12, (d + k) * 16 + 8, src); jc_settag(c, d + k, V_NUM);
            c->sd[d + k] = sd_mk(SK_MEM);
          }
        }
        /* زيادة العدّاد: في سجله عند الترقية وإلا في الذاكرة (عبر مجمع الثوابت) */
        if (ck >= 0) jc_rip(c, 0xF2, 0x0F58, c->pv[ck].reg, jc_pool(c, 0x3FF0000000000000ull));
        else { jc_rip(c, 0xF2, 0x0F58, 3, jc_pool(c, 0x3FF0000000000000ull)); jx_movsd_mr(x, RBX, base * 16 + coff, 3); }
        break;
      }
      case OP_JMOVE: {
        /* انهيار إطار مُدرج: انسخ قيمة القمة (d-1) إلى العمق d-1-k — نقل رمزي بصفر تعليمات متى أمكن */
        int k = in->a, src = d - 1, dst = d - 1 - k;
        SD s = c->sd[src];
        switch (s.k) {
          case SK_XMM: case SK_VAR: case SK_CONST: c->sd[dst] = s; break;
          case SK_HOME:
            if (!(s.hb == R12 && s.hd == dst * 16)) jc_copy16(c, R12, dst * 16, s.hb, s.hd);
            c->sd[dst] = sd_mk(SK_MEM); c->sd[dst].n = s.n; break;
          default: /* SK_MEM */
            if (src != dst) jc_copy16(c, R12, dst * 16, R12, src * 16);
            c->sd[dst] = sd_mk(SK_MEM); c->sd[dst].n = s.n; break;
        }
        break;
      }
      case OP_CALL: {
        /* call-out: استدعاء غير قابل للإدراج يُنفَّذ عبر مساعد C ثم تكمل المنطقة.
           المستدعى عند d-1-argc والوسائط حتى d-1؛ النتيجة تعود حتمياً في ذاكرة d-1-argc. */
        int argc = in->a;
        if (d - 1 - argc < 0) { c->failed = 1; c->why = "call"; break; }
        jc_canon(c, 0, d); /* كل ما تحت القمة إلى الذاكرة: المستدعى والوسائط يُقرأون من المكدس الحقيقي */
        jc_flush_vars(c);  /* قيم السجلات إلى بيوتها قبل الـ GC والاستدعاء */
        const uint8_t *cip = in->ip, *rip = in->ip + in->len; int cdep = d - argc;
        if (in->rd >= 0) { /* استدعاء داخل جسم مُدرج: الأسطر والاستئناف عند موقع الاستدعاء الأصلي —
          المستدعى والوسائط ما زالت في أماكنها (الجسم يعمل فوقها) فيُعاد تنفيذ الاستدعاء الأصلي كاملاً */
          cip = rip = c->ins[in->rd].ip; cdep = c->depth[in->rd];
        }
        int ex2 = jc_exit_label2(c, cip, cdep, 2); /* استثناء: بلا تفريغ */
        int ex3 = jc_exit_label2(c, rip, cdep, 3); /* تخفي بعد الاستدعاء: المكدس صحيح */
#if JX_ARM
        /* SysV ARM64: x0-x3 = (cx, sp, argc, cip) */
        jx_mov_rr(x, RAX, R15);
        jx_lea(x, RCX, R12, -1, 0, d * 16);
        jx_mov_ri(x, RDX, (uint64_t)argc);
        jx_mov_ri(x, RSI, (uint64_t)(uintptr_t)cip);
#elif JX_WIN
        jx_mov_rr(x, RCX, R15);
        jx_lea(x, RDX, R12, -1, 0, d * 16);
        jx_mov_ri(x, R8, (uint64_t)argc);
        jx_mov_ri(x, R9, (uint64_t)(uintptr_t)cip);
#else
        /* SysV x64: RDI, RSI, RDX, RCX (إصلاح خلل كامن: كان يمرر ترتيب ويندوز دائماً) */
        jx_mov_rr(x, RDI, R15);
        jx_lea(x, RSI, R12, -1, 0, d * 16);
        jx_mov_ri(x, RDX, (uint64_t)argc);
        jx_mov_ri(x, RCX, (uint64_t)(uintptr_t)cip);
#endif
        jc_call_helper(c, (void *)jit_h_call);
        jx_cmp32_mi(x, R15, (int)offsetof(JitCtx, code), 0); jc_jcc(c, CC_NE, ex2);
        if (c->ng > 0 || c->nbi > 0) { /* إعادة اشتقاق R13: القاموس قد نما (فحص الجيل يقرأ من R13) */
          jx_mov_rm(x, RAX, R14, (int)offsetof(Closure, globals));
          jx_lea(x, RSI, RAX, -1, 0, (int)offsetof(Map, d));
          jx_mov_rm(x, R13, RSI, (int)offsetof(Dict, e));
        }
        for (int k = 0; k < c->npv; k++) { /* إعادة تحميل pv مع فحص الوسم: العوالم تتغير والمحليات عبر upvalues */
          PV *p = &c->pv[k];
          if (p->noc && !p->g) continue; /* سلوت حلقة داخلية: لا مستدعى يصل إليه */
          int b, dd; jc_pv_home(p, &b, &dd);
          jx_mov32_rm(x, RAX, b, dd); jx_cmp32_ri(x, RAX, V_NUM); jc_jcc(c, CC_NE, ex3);
          jx_movsd_rm(x, p->reg, b, dd + 8);
        }
        c->sd[d - 1 - argc] = sd_mk(SK_MEM); /* النتيجة في الذاكرة حتمياً */
        break;
      }
      default: c->failed = 1; c->why = "op"; break;
    }
  }
}

static JitRegion *jit_compile(Fun *fn, const uint8_t *head, const uint8_t *tail, Closure *clo, Value *slots, Value *sp) {
  JC *c = calloc(1, sizeof(JC));
  JitRegion *res = NULL;
  c->fn = fn; c->clo = clo; c->slots = slots; c->head = head; c->tail = tail;
  c->spoff = (int)(sp - slots); c->usepromo = 1;
  c->ggen = (&c->clo->globals->d)->gen; /* جيل العوالم عند الترجمة: أساس حارس ظلّ المدمج */
  c->x.cap = 1 << 20; c->x.p = malloc(c->x.cap);
  if (!jc_analyze(c)) { if (g_jit_trace) fprintf(stderr, "[jit] تحليل فشل: %s\n", c->why ? c->why : "?"); goto out; }
  jc_inline(c); /* إدراج أجسام الدوال قبل الترقية والتوليد */
  if (c->failed) { if (g_jit_trace) fprintf(stderr, "[jit] إدراج فشل: %s\n", c->why ? c->why : "?"); goto out; }
  c->nlab = c->n;
  for (int i = 0; i < c->n; i++) { c->lpos[i] = -1; c->dl[i] = -1; }
  jc_promote(c);
  XB *x = &c->x;
#if JX_ARM
  /* إطار 160 بايت: [0..15] x19,x20 (pre-index) — [16..55] منطقة d3-d7 العابرة —
     [64..87] x21-x23 — [88..151] d8-d15 الواردة — [152..159] x30 (LR) */
  jx_stp_pre64(x, 19, 20, 160);
  jx_stp64(x, 21, 22, 31, 64);
  jx_str64_raw(x, 23, 31, 80);
  for (int p = 0; p < 4; p++) jx_stpd(x, 8 + 2 * p, 9 + 2 * p, 31, 88 + 16 * p);
  jx_str64_raw(x, 30, 31, 152); /* LR: blr المساعدين يسحقها — بلا إنقاذ يعيد ret القفز إلى داخل الكود نفسه (حلقة لانهائية) */
  jx_mov_rr(x, R15, RAX); /* وسيط JitCtx* الأول يصل في x0 */
#else
  jx_push(x, RBX); jx_push(x, R12); jx_push(x, R13); jx_push(x, R14); jx_push(x, R15);
#if JX_WIN
  /* على ويندوز RSI وRDI محفوظتان (callee-saved) يجب إرجاعهما كما استُقبلتا */
  jx_push(x, RDI); jx_push(x, RSI);
#define JX_SAVE_AREA 192 /* 112 لعمليات الحفظ المؤقتة + 80 لقيم XMM6-15 الواردة */
#else
#define JX_SAVE_AREA 112
#endif
  jx_sub_rsp(x, JX_SAVE_AREA);
#if JX_WIN
  /* حفظ XMM6-15 الواردة من الـ VM (محفوظة على ويندوز) لاستعادتها عند الخروج */
  for (int r = 6; r <= 15; r++) jx_movsd_mr(x, RSP, 112 + (r - 6) * 8, r);
  /* وسيط JitCtx* الأول يصل في RCX على ويندوز (بدل RDI في لينكس) */
  jx_mov_rr(x, R15, RCX);
#else
  jx_mov_rr(x, R15, RDI);
#endif
#endif
  jx_mov_rm(x, RBX, R15, (int)offsetof(JitCtx, slots)); jx_mov_rm(x, R12, R15, (int)offsetof(JitCtx, sp)); jx_mov_rm(x, R14, R15, (int)offsetof(JitCtx, clo));
  int lguards = jc_new_label(c), lbody = jc_new_label(c), lepi = jc_new_label(c);
  jc_jmp(c, lguards);
  jc_bind(c, lbody);
  jc_gen_body(c);
  if (c->failed) { if (g_jit_trace) fprintf(stderr, "[jit] توليد فشل: %s\n", c->why ? c->why : "?"); goto out; }
  jc_bind(c, lguards);
  {
    int fail = jc_exit_label2(c, head, 0, 3); /* تخفي بلا تفريغ: سجلات pv غير محملة بعد والاستئناف من رأس المنطقة */
    if (c->ng > 0 || c->nbi > 0) { /* R13 يلزم أيضاً لمواقع قراءة المدمج (فحص جيل العوالم) */
      jx_mov_rm(x, RAX, R14, (int)offsetof(Closure, globals));
      jx_lea(x, RSI, RAX, -1, 0, (int)offsetof(Map, d));
      jx_mov_rm(x, RDI, RSI, (int)offsetof(Dict, e));
      if (c->ng > 0) {
        for (int k = 0; k < c->ng; k++) {
          int gi = c->ggi[k];
          jx_cmp32_mi(x, RSI, (int)offsetof(Dict, n), gi + 1); jc_jcc(c, CC_L, fail);
          jx_cmp32_mi(x, RDI, gi * (int)sizeof(Ent) + (int)offsetof(Ent, k), V_OBJ); jc_jcc(c, CC_NE, fail);
          jx_mov_ri(x, RAX, (uint64_t)(uintptr_t)c->gnames[k]);
          jx_cmp_rm(x, RAX, RDI, gi * (int)sizeof(Ent) + (int)offsetof(Ent, k) + 8); jc_jcc(c, CC_NE, fail);
        }
        /* حراسة قيم المستدعين المُدرجين: وسم V_OBJ ومؤشر الـ Closure المتوقع */
        for (int k = 0; k < c->ng; k++) {
          if (!c->gexp[k]) continue;
          int vo = c->ggi[k] * (int)sizeof(Ent) + (int)offsetof(Ent, v);
          jx_cmp32_mi(x, RDI, vo, V_OBJ); jc_jcc(c, CC_NE, fail);
          jx_mov_ri(x, RAX, (uint64_t)(uintptr_t)c->gexp[k]);
          jx_cmp_rm(x, RAX, RDI, vo + 8); jc_jcc(c, CC_NE, fail);
        }
      }
      jx_mov_rr(x, R13, RDI);
    }
    jx_lea(x, RAX, RBX, -1, 0, c->spoff * 16); jx_cmp_rr(x, RAX, R12); jc_jcc(c, CC_NE, fail);
    for (int k = 0; k < c->npv; k++) { int b, d; jc_pv_home(&c->pv[k], &b, &d); if (c->pv[k].noc && !c->pv[k].g) continue; jx_cmp32_mi(x, b, d, V_NUM); jc_jcc(c, CC_NE, fail); }
    for (int k = 0; k < c->npv; k++) {
      int b, d; jc_pv_home(&c->pv[k], &b, &d);
      if (c->pv[k].noc && !c->pv[k].g) jx_mov32_mi(x, b, d, V_NUM); /* سلوت مُنشأ داخل المنطقة: وسمه قد يكون قديماً — يُكتب V_NUM مرة واحدة هنا (قبل أي تفريغ) */
      jx_movsd_rm(x, c->pv[k].reg, b, d + 8);
    }
  }
  jc_jmp(c, lbody);
  for (int k = 0; k < c->nsx; k++) {
    JS *s = &c->sx[k]; jc_bind(c, s->lab);
    for (int p = 0; p < s->n; p++) jc_emit_mat(c, &s->s[p], p);
    jc_jmp(c, jc_exit_label2(c, s->ip, s->n, 1));
  }
  for (int k = 0; k < c->nex; k++) {
    JX *e = &c->ex[k]; jc_bind(c, e->lab);
    if (e->code == 0 || e->code == 1) jc_flush_vars(c); /* code=3: السجلات بائتة جزئياً — التفريغ يفسد البيوت */
    jx_mov_ri(x, RAX, (uint64_t)(uintptr_t)e->ip); jx_mov_mr(x, R15, (int)offsetof(JitCtx, ip), RAX);
    jx_lea(x, RAX, R12, -1, 0, e->depth * 16); jx_mov_mr(x, R15, (int)offsetof(JitCtx, sp), RAX);
    jx_mov32_mi(x, R15, (int)offsetof(JitCtx, code), (uint32_t)e->code);
    jc_jmp(c, lepi);
  }
  jc_bind(c, lepi);
#if JX_ARM
  for (int p = 3; p >= 0; p--) jx_ldpd(x, 8 + 2 * p, 9 + 2 * p, 31, 88 + 16 * p);
  jx_ldp64(x, 21, 22, 31, 64);
  jx_ldr64_raw(x, 23, 31, 80);
  jx_ldp64(x, 19, 20, 31, 0);
  jx_ldr64_raw(x, 30, 31, 152); /* استعادة LR قبل ret */
  jx_add_rsp(x, 160);
  jx_ret(x);
#else
#if JX_WIN
  /* استعادة XMM6-15 كما استُقبلت من الـ VM */
  for (int r = 6; r <= 15; r++) jx_movsd_rm(x, r, RSP, 112 + (r - 6) * 8);
#endif
  jx_add_rsp(x, JX_SAVE_AREA);
#if JX_WIN
  jx_pop(x, RSI); jx_pop(x, RDI);
#endif
  jx_pop(x, R15); jx_pop(x, R14); jx_pop(x, R13); jx_pop(x, R12); jx_pop(x, RBX); jx_ret(x);
#endif
#if JX_ARM
  if (c->x.n & 7) xb_u32(x, 0); /* محاذاة 8 للبركة: LDR literal D يتطلب محاذاة طبيعية */
#endif
  for (int k = 0; k < c->npool; k++) { jc_bind(c, c->poollab[k]); xb_u64(x, c->pool[k]); }
  if (c->x.n > c->x.cap || c->failed) { if (g_jit_trace) fprintf(stderr, "[jit] فشل: %s\n", c->why ? c->why : "حجم"); goto out; }
#if JX_ARM
  for (int k = 0; k < c->nfix; k++) {
    int tp = c->lpos[c->fix[k].lab];
    if (tp < 0) { if (g_jit_trace) fprintf(stderr, "[jit] تسمية غير مربوطة\n"); goto out; }
    /* pos-4 = بداية تعليمة الترقيع (b/b.cond/ldr literal)؛ rel بالتعليمات = (tp - ipos)/4 */
    int ipos = c->fix[k].pos - 4;
    int32_t rel = tp - ipos;
    uint32_t w; memcpy(&w, c->x.p + ipos, 4);
    if ((w >> 26) == 5u) w = 0x14000000u | ((uint32_t)(rel / 4) & 0x3FFFFFFu);             /* b: imm26 */
    else w = (w & ~0x00FFFFE0u) | (((uint32_t)(rel / 4) & 0x7FFFFu) << 5);                 /* b.cond: imm19 مع حفظ cond / ldr literal: حفظ Rt */
    memcpy(c->x.p + ipos, &w, 4);
  }
#else
  for (int k = 0; k < c->nfix; k++) {
    int tp = c->lpos[c->fix[k].lab];
    if (tp < 0) { if (g_jit_trace) fprintf(stderr, "[jit] تسمية غير مربوطة\n"); goto out; }
    int32_t rel = tp - (c->fix[k].pos + 4); memcpy(c->x.p + c->fix[k].pos, &rel, 4);
  }
#endif
  {
    size_t sz = ((size_t)c->x.n + 4095) & ~(size_t)4095;
    if (g_jit_bytes + sz > (64u << 20)) goto out;
    int mflags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    mflags |= MAP_JIT; /* Apple Silicon: تنفيذ من صفحات قابلة للكتابة يتطلب MAP_JIT */
#endif
    void *mem = mmap(NULL, sz, PROT_READ | PROT_WRITE, mflags, -1, 0);
    if (mem == MAP_FAILED) goto out;
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    if (pthread_jit_write_protect_np) pthread_jit_write_protect_np(0); /* فتح الكتابة مؤقتاً */
#endif
    memcpy(mem, c->x.p, (size_t)c->x.n);
#if JX_ARM
    __builtin___clear_cache((char *)mem, (char *)mem + sz); /* إبطال ذاكرة التعليمات بعد النسخ */
#endif
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    if (pthread_jit_write_protect_np) pthread_jit_write_protect_np(1); /* إغلاق الكتابة مجدداً */
#endif
    if (mprotect(mem, sz, PROT_READ | PROT_EXEC) != 0) { munmap(mem, sz); g_jit = 0; goto out; }
    res = calloc(1, sizeof(JitRegion));
    res->tail = tail; res->fn = (JitFn)mem; res->mem = mem; res->msz = sz; res->next = fn->jreg; fn->jreg = res; g_jit_bytes += sz;
    if (g_jit_trace) fprintf(stderr, "[jit] جُمّعت حلقة: %d تعليمة، %d بايت، متغيرات في سجلات: %d، إدراج: %d\n", c->n, c->x.n, c->npv, c->ninl);
    const char *dump = getenv("SOLAN_JIT_DUMP");
    if (dump) { FILE *df = fopen(dump, "wb"); if (df) { fwrite(c->x.p, 1, (size_t)c->x.n, df); fclose(df); } }
  }
out:
  for (int k = 0; k < c->nsx; k++) free(c->sx[k].s);
  free(c->sx); free(c->idx_of); free(c->x.p); free(c);
  return res;
}

static JitRegion *jit_find(Fun *fn, const uint8_t *tail) {
  for (JitRegion *r = fn->jreg; r; r = r->next) if (r->tail == tail) return r;
  return NULL;
}

static void jit_init(void) {
  const char *e = getenv("SOLAN_JIT");
  g_jit = !(e && e[0] == '0');
  g_jit_trace = getenv("SOLAN_JIT_TRACE") != NULL;
#if JX_ARM
  /* AArch64: FP/NEON مضمونة في المستوى الأساسي للمعمارية — لا فحص قدرات.
     وأمان: إن أُجبر SOLAN_JIT=2 على مضيف غير ARM (فحص ترجمة) فاعقله —
     تنفيذ بايتات ARM على x86 مستحيل. */
#if !defined(__aarch64__) && !defined(__arm64__)
  g_jit = 0;
#endif
#else
  __builtin_cpu_init();
  if (!__builtin_cpu_supports("sse4.1")) g_jit = 0;
#endif
}
#else
static void jit_init(void) { g_jit = 0; }
#endif
