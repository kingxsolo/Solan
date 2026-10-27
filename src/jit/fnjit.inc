#if SOLAN_JIT
#define FJ_TH 40
#define FJ_MAXD 40
#define FJ_MAXI 1500
#define FJ_MAXARGS 6
#define FJ_MAXDEPS 8
#define FJ_MAXFIX 6000
enum { FJT_NONE = 0, FJT_NUM = 1, FJT_BOOL = 2, FJT_FN = 3 };

typedef struct FjGuard { Dict *d; int gi; Str *name; Fun *fun; } FjGuard;
typedef struct FjInfo { void *mem; size_t msz; void *entry; int ng; FjGuard g[FJ_MAXDEPS]; int nd; Fun *deps[FJ_MAXDEPS]; int bails; } FjInfo;

static jmp_buf g_fj_jb;
static int g_fj_depth, g_fj_limit;
static void fj_bail(void) { _longjmp(g_fj_jb, 1); }

typedef struct { const uint8_t *ip; uint8_t op; int a; int len; int tgt; } FI;
typedef struct { uint8_t kind; double imm; int ref; } FV;
typedef struct {
  Fun *f; Closure *clo; int n; FI *ins; int *idx_of;
  int8_t *depth; uint8_t *ty; uint8_t *fnx; uint8_t *reach; uint8_t *islab;
  int nf; Fun *fns[FJ_MAXDEPS]; int fgi[FJ_MAXDEPS]; Str *fname[FJ_MAXDEPS]; void *fentry[FJ_MAXDEPS];
  XB x; int *lpos; int nlab; struct { int pos, lab; } *fix; int nfix;
  int maxd; const char *why; FV vs[FJ_MAXD];
} FC;
#define TYP(c, i, k) ((c)->ty[(size_t)(i) * FJ_MAXD + (k)])
#define FNX(c, i, k) ((c)->fnx[(size_t)(i) * FJ_MAXD + (k)])
#define CELL(i) RSP, (int)(8 * (i))

static bool fj_compile(Closure *clo);

static void fc_fix(FC *c, int lab) {
  if (c->nfix >= FJ_MAXFIX) { c->why = "fixups"; xb_u32(&c->x, 0); return; }
  c->fix[c->nfix].pos = c->x.n; c->fix[c->nfix].lab = lab; c->nfix++; xb_u32(&c->x, 0);
}
static void fc_jmp(FC *c, int lab) { jx_rel32_jmp(&c->x); fc_fix(c, lab); }
static void fc_jcc(FC *c, int cc, int lab) { jx_rel32_jcc(&c->x, cc); fc_fix(c, lab); }
static void fc_call(FC *c, int lab) { xb_b(&c->x, 0xE8); fc_fix(c, lab); }
static void fc_bind(FC *c, int lab) { c->lpos[lab] = c->x.n; }
#define LB_ENTRY(c) ((c)->n)
#define LB_BAIL(c) ((c)->n + 1)

static void fc_ld(FC *c, int xr, int k) {
  FV *v = &c->vs[k]; XB *x = &c->x;
  if (v->kind == 1) { uint64_t b; memcpy(&b, &v->imm, 8); jx_mov_ri(x, RAX, b); jx_movq_xr(x, xr, RAX); }
  else jx_movsd_rm(x, xr, CELL(v->kind == 2 ? v->ref : k));
}

static void fc_ldq(FC *c, int k) {
  FV *v = &c->vs[k]; XB *x = &c->x;
  if (v->kind == 1) { uint64_t b; memcpy(&b, &v->imm, 8); jx_mov_ri(x, RAX, b); }
  else jx_mov_rm(x, RAX, CELL(v->kind == 2 ? v->ref : k));
}
static void fc_flush(FC *c, int d) {
  for (int k = 0; k < d; k++) {
    if (c->vs[k].kind == 0) continue;
    fc_ldq(c, k); jx_mov_mr(&c->x, CELL(k), RAX); c->vs[k].kind = 0;
  }
}

static bool fc_resolve_global(FC *c, Str *name, int *out) {
  Dict *d = &c->clo->globals->d; int gi = dfind(d, OBJV(name), name->hash);
  if (gi < 0) { c->why = "global"; return false; }
  Value v = d->e[gi].v;
  if (!IS_T(v, O_CLO)) { c->why = "global-not-fn"; return false; }
  Closure *g = AS_CLO(v); Fun *gf = g->f;
  if (g->globals != c->clo->globals || gf->upc != 0 || gf->variadic || gf->kind || gf->getter || gf->arity > FJ_MAXARGS) { c->why = "callee"; return false; }
  for (int i = 0; i < c->nf; i++) if (c->fname[i] == name) { *out = i; return true; }
  if (c->nf >= FJ_MAXDEPS) { c->why = "deps"; return false; }
  void *entry = NULL;
  if (gf != c->f) {
    if (gf->fjst == 0) fj_compile(g);
    if (gf->fjst != 1) { c->why = "callee-not-compiled"; return false; }
    entry = gf->fj->entry;
  }
  c->fns[c->nf] = gf; c->fgi[c->nf] = gi; c->fname[c->nf] = name; c->fentry[c->nf] = entry; *out = c->nf++; return true;
}

static bool fc_step(FC *c, int i, uint8_t *ty, uint8_t *fx, int *dp) {
  FI *in = &c->ins[i]; int d = *dp;
#define NEEDN(n) if (d < (n)) { c->why = "underflow"; return false; }
#define PUSHT(t, f) do { if (d >= FJ_MAXD - 2) { c->why = "deep"; return false; } ty[d] = (t); fx[d] = (f); d++; } while (0)
  switch ((Op)in->op) {
    case OP_CONST: {
      Value k = c->f->ch.consts.v[in->a]; if (!IS_NUM(k)) { c->why = "const"; return false; } PUSHT(FJT_NUM, 0); break;
    }
    case OP_TRUE: case OP_FALSE: PUSHT(FJT_BOOL, 0); break;
    case OP_GET_LOCAL: if (in->a == 0 || in->a >= d || ty[in->a] == FJT_FN) { c->why = "local"; return false; } PUSHT(ty[in->a], 0); break;
    case OP_SET_LOCAL: NEEDN(1); if (in->a == 0 || in->a >= d - 1 || ty[d - 1] != ty[in->a] || ty[d - 1] == FJT_FN) { c->why = "setlocal"; return false; } break;
    case OP_POP: NEEDN(1); d--; break;
    case OP_DUP: NEEDN(1); if (ty[d - 1] == FJT_FN) { c->why = "dup"; return false; } PUSHT(ty[d - 1], 0); break;
    case OP_ADD: case OP_ADD_APPEND: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_IDIV: case OP_POW:
      NEEDN(2); if (ty[d - 1] != FJT_NUM || ty[d - 2] != FJT_NUM) { c->why = "arith"; return false; } d--; break;
    case OP_LT: case OP_GT: case OP_LE: case OP_GE:
      NEEDN(2); if (ty[d - 1] != FJT_NUM || ty[d - 2] != FJT_NUM) { c->why = "cmp"; return false; } d--; ty[d - 1] = FJT_BOOL; break;
    case OP_EQ:
      NEEDN(2); if (ty[d - 1] != ty[d - 2] || ty[d - 1] == FJT_FN) { c->why = "eq"; return false; } d--; ty[d - 1] = FJT_BOOL; break;
    case OP_NOT: NEEDN(1); if (ty[d - 1] == FJT_FN) { c->why = "not"; return false; } ty[d - 1] = FJT_BOOL; break;
    case OP_NEG: NEEDN(1); if (ty[d - 1] != FJT_NUM) { c->why = "neg"; return false; } break;
    case OP_JUMP: case OP_LOOP: break;
    case OP_JIF: case OP_JIT: NEEDN(1); if (ty[d - 1] == FJT_FN) { c->why = "jif"; return false; } break;
    case OP_CHECK: NEEDN(1); if (ty[d - 1] != FJT_NUM || !(in->a & TY_NUM)) { c->why = "check"; return false; } break;
    case OP_RETURN: NEEDN(1); if (ty[d - 1] != FJT_NUM) { c->why = "return-type"; return false; } break;
    case OP_GET_GLOBAL: {
      int fi; if (!fc_resolve_global(c, AS_STR(c->f->ch.consts.v[in->a]), &fi)) return false; PUSHT(FJT_FN, (uint8_t)fi); break;
    }
    case OP_CALL: {
      int n = in->a; NEEDN(n + 1);
      if (ty[d - 1 - n] != FJT_FN) { c->why = "call-target"; return false; }
      Fun *cf = c->fns[fx[d - 1 - n]]; if (cf->arity != n) { c->why = "arity"; return false; }
      for (int k = 0; k < n; k++) if (ty[d - n + k] != FJT_NUM) { c->why = "call-arg"; return false; }
      d -= n; ty[d - 1] = FJT_NUM; break;
    }
    default: c->why = op_names[in->op]; return false;
  }
  if (d > c->maxd) c->maxd = d;
  *dp = d; return true;
#undef NEEDN
#undef PUSHT
}

static bool fc_analyze(FC *c) {
  const uint8_t *code = c->f->ch.code, *end = code + c->f->ch.n, *ip = code; c->n = 0;
  c->idx_of = calloc((size_t)c->f->ch.n + 1, sizeof(int));
  for (int i = 0; i <= c->f->ch.n; i++) c->idx_of[i] = -1;
  while (ip < end) {
    if (c->n >= FJ_MAXI) { c->why = "size"; return false; }
    FI *in = &c->ins[c->n]; in->ip = ip; in->op = op_norm(*ip); in->len = 1 + op_operand_len(c->f, ip); in->a = 0; in->tgt = -1;
    const uint8_t *ar = ip + 1;
    switch ((Op)in->op) {
      case OP_CONST: case OP_GET_GLOBAL: in->a = (ar[0] << 8) | ar[1]; break;
      case OP_GET_LOCAL: case OP_SET_LOCAL: case OP_CALL: in->a = ar[0]; break;
      case OP_CHECK: in->a = (ar[0] << 8) | ar[1]; break;
      case OP_JUMP: case OP_JIF: case OP_JIT: in->a = (int)(ip + 3 + ((ar[0] << 8) | ar[1]) - code); break;
      case OP_LOOP: in->a = (int)(ip + 3 - ((ar[0] << 8) | ar[1]) - code); break;
      default: break;
    }
    c->idx_of[ip - code] = c->n++; ip += in->len;
  }
  if (ip != end) { c->why = "decode"; return false; }
  for (int i = 0; i < c->n; i++) {
    FI *in = &c->ins[i];
    if (in->op == OP_JUMP || in->op == OP_JIF || in->op == OP_JIT || in->op == OP_LOOP) {
      if (in->a < 0 || in->a > c->f->ch.n || c->idx_of[in->a] < 0) { c->why = "jump-target"; return false; }
      in->tgt = c->idx_of[in->a]; c->islab[in->tgt] = 1;
    }
  }
  int ar = c->f->arity;
  if (ar + 1 >= FJ_MAXD - 4) { c->why = "arity-big"; return false; }
  int *work = malloc(sizeof(int) * (size_t)c->n * 2 + 16), nw = 0;
  for (int i = 0; i < c->n; i++) c->depth[i] = -1;
  c->depth[0] = (int8_t)(ar + 1); c->reach[0] = 1;
  for (int k = 0; k <= ar; k++) { TYP(c, 0, k) = FJT_NUM; FNX(c, 0, k) = 0; }
  c->maxd = ar + 1; work[nw++] = 0;
  bool ok = true;
  while (nw > 0 && ok) {
    int i = work[--nw]; uint8_t ty[FJ_MAXD], fx[FJ_MAXD]; int d = c->depth[i];
    memcpy(ty, &TYP(c, i, 0), FJ_MAXD); memcpy(fx, &FNX(c, i, 0), FJ_MAXD);
    if (!fc_step(c, i, ty, fx, &d)) { ok = false; break; }
    FI *in = &c->ins[i]; int succ[2], ns = 0;
    switch ((Op)in->op) {
      case OP_RETURN: break;
      case OP_JUMP: case OP_LOOP: succ[ns++] = in->tgt; break;
      case OP_JIF: case OP_JIT: if (i + 1 < c->n) succ[ns++] = i + 1; succ[ns++] = in->tgt; break;
      default: if (i + 1 < c->n) succ[ns++] = i + 1; else { c->why = "fallthrough"; ok = false; }
    }
    for (int k = 0; k < ns && ok; k++) {
      int s = succ[k];
      if (c->depth[s] == -1) { c->depth[s] = (int8_t)d; c->reach[s] = 1; memcpy(&TYP(c, s, 0), ty, FJ_MAXD); memcpy(&FNX(c, s, 0), fx, FJ_MAXD); work[nw++] = s; }
      else {
        if (c->depth[s] != d) { c->why = "depth-merge"; ok = false; break; }
        for (int q = 0; q < d; q++) if (TYP(c, s, q) != ty[q] || (ty[q] == FJT_FN && FNX(c, s, q) != fx[q])) { c->why = "type-merge"; ok = false; break; }
      }
    }
  }
  free(work); return ok;
}

static void fc_call_helper(FC *c, void *fn) { jx_mov_ri(&c->x, RAX, (uint64_t)(uintptr_t)fn); jx_call_rax(&c->x); }
static void fc_store_xmm0(FC *c, int k) { jx_movsd_mr(&c->x, CELL(k), 0); c->vs[k].kind = 0; }
static void fc_store_bool(FC *c, int k) { jx_movzx8_rr(&c->x, RAX, RAX); jx_mov_mr(&c->x, CELL(k), RAX); c->vs[k].kind = 0; }

static void fc_emit(FC *c, int frame_size) {
  XB *x = &c->x; int ar = c->f->arity;
  fc_bind(c, LB_ENTRY(c));
  jx_push(x, RBP); jx_mov_rr(x, RBP, RSP);
  jx_rex(x, 1, 0, 0, RSP); xb_b(x, 0x81); xb_b(x, 0xE8 | (RSP & 7)); xb_u32(x, (uint32_t)frame_size);
  jx_mov_ri(x, RAX, (uint64_t)(uintptr_t)&g_fj_depth); jx_mov32_rm(x, RCX, RAX, 0); jx_add32_ri(x, RCX, 1); jx_mov32_mr(x, RAX, 0, RCX);
  jx_mov_ri(x, RDX, (uint64_t)(uintptr_t)&g_fj_limit); jx_cmp32_rm(x, RCX, RDX, 0); fc_jcc(c, CC_G, LB_BAIL(c));
  for (int k = 0; k < ar; k++) jx_movsd_mr(x, CELL(k + 1), k);
  for (int k = 0; k < FJ_MAXD; k++) c->vs[k].kind = 0;
  for (int i = 0; i < c->n; i++) {
    FI *in = &c->ins[i];
    if (!c->reach[i]) { fc_bind(c, i); continue; }
    int d = c->depth[i];
    if (c->islab[i]) { fc_flush(c, d); }
    fc_bind(c, i);
    switch ((Op)in->op) {
      case OP_CONST: { double v = c->f->ch.consts.v[in->a].as.n; c->vs[d].kind = 1; c->vs[d].imm = v; break; }
      case OP_TRUE: case OP_FALSE: jx_mov_ri(x, RAX, in->op == OP_TRUE); jx_mov_mr(x, CELL(d), RAX); c->vs[d].kind = 0; break;
      case OP_GET_LOCAL: c->vs[d].kind = 2; c->vs[d].ref = in->a; break;
      case OP_SET_LOCAL: fc_flush(c, d); fc_ldq(c, d - 1); jx_mov_mr(x, CELL(in->a), RAX); break;
      case OP_POP: break;
      case OP_DUP: c->vs[d] = c->vs[d - 1]; if (c->vs[d].kind == 0) { jx_mov_rm(x, RAX, CELL(d - 1)); jx_mov_mr(x, CELL(d), RAX); } break;
      case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: {
        fc_ld(c, 0, d - 2); fc_ld(c, 1, d - 1);
        if (in->op == OP_DIV) {
          jx_xorpd(x, 2, 2); jx_ucomisd(x, 1, 2); xb_b(x, 0x7A); xb_b(x, 6); fc_jcc(c, CC_E, LB_BAIL(c));
          jx_divsd(x, 0, 1);
        } else if (in->op == OP_ADD) jx_addsd(x, 0, 1); else if (in->op == OP_SUB) jx_subsd(x, 0, 1); else jx_mulsd(x, 0, 1);
        fc_store_xmm0(c, d - 2); break;
      }
      case OP_IDIV: {
        fc_ld(c, 0, d - 2); fc_ld(c, 1, d - 1);
        jx_xorpd(x, 2, 2); jx_ucomisd(x, 1, 2); xb_b(x, 0x7A); xb_b(x, 6); fc_jcc(c, CC_E, LB_BAIL(c));
        jx_divsd(x, 0, 1); jx_roundsd(x, 0, 0, 9); fc_store_xmm0(c, d - 2); break;
      }
      case OP_MOD: {
        fc_ld(c, 0, d - 2); fc_ld(c, 1, d - 1);
        jx_xorpd(x, 2, 2); jx_ucomisd(x, 1, 2); xb_b(x, 0x7A); xb_b(x, 6); fc_jcc(c, CC_E, LB_BAIL(c));
        fc_call_helper(c, (void *)jit_h_mod); fc_store_xmm0(c, d - 2); break;
      }
      case OP_POW: fc_ld(c, 0, d - 2); fc_ld(c, 1, d - 1); fc_call_helper(c, (void *)jit_h_pow); fc_store_xmm0(c, d - 2); break;
      case OP_LT: case OP_LE: fc_ld(c, 0, d - 2); fc_ld(c, 1, d - 1); jx_ucomisd(x, 1, 0); jx_setcc(x, in->op == OP_LT ? CC_A : CC_AE, RAX); fc_store_bool(c, d - 2); break;
      case OP_GT: case OP_GE: fc_ld(c, 0, d - 2); fc_ld(c, 1, d - 1); jx_ucomisd(x, 0, 1); jx_setcc(x, in->op == OP_GT ? CC_A : CC_AE, RAX); fc_store_bool(c, d - 2); break;
      case OP_EQ:
        if (TYP(c, i, d - 1) == FJT_NUM) { fc_ld(c, 0, d - 2); fc_ld(c, 1, d - 1); jx_ucomisd(x, 0, 1); jx_setcc(x, CC_E, RAX); jx_setcc(x, CC_NP, RCX); jx_and8_rr(x, RAX, RCX); }
        else { fc_ldq(c, d - 2); jx_mov_rr(x, RDX, RAX); fc_ldq(c, d - 1); jx_cmp_rr(x, RDX, RAX); jx_setcc(x, CC_E, RAX); }
        fc_store_bool(c, d - 2); break;
      case OP_NOT:
        if (TYP(c, i, d - 1) == FJT_BOOL) { fc_ldq(c, d - 1); jx_mov_ri(x, RCX, 1); jx_alu_rr(x, 0x31, RAX, RCX); jx_mov_mr(x, CELL(d - 1), RAX); c->vs[d - 1].kind = 0; }
        else { fc_ld(c, 0, d - 1); jx_xorpd(x, 1, 1); jx_ucomisd(x, 0, 1); jx_setcc(x, CC_E, RAX); jx_setcc(x, CC_NP, RCX); jx_and8_rr(x, RAX, RCX); fc_store_bool(c, d - 1); }
        break;
      case OP_NEG: fc_ldq(c, d - 1); jx_mov_ri(x, RCX, 0x8000000000000000ull); jx_alu_rr(x, 0x31, RAX, RCX); jx_mov_mr(x, CELL(d - 1), RAX); c->vs[d - 1].kind = 0; break;
      case OP_JUMP: case OP_LOOP: fc_flush(c, d); fc_jmp(c, in->tgt); break;
      case OP_JIF: case OP_JIT: {
        bool jf = in->op == OP_JIF; fc_flush(c, d);
        if (TYP(c, i, d - 1) == FJT_BOOL) { jx_cmp32_mi(x, CELL(d - 1), 0); fc_jcc(c, jf ? CC_E : CC_NE, in->tgt); }
        else {
          jx_movsd_rm(x, 0, CELL(d - 1)); jx_xorpd(x, 1, 1); jx_ucomisd(x, 0, 1);
          if (jf) { xb_b(x, 0x7A); xb_b(x, 6); fc_jcc(c, CC_E, in->tgt); } else { fc_jcc(c, CC_P, in->tgt); fc_jcc(c, CC_NE, in->tgt); }
        }
        break;
      }
      case OP_CHECK: break;
      case OP_GET_GLOBAL: c->vs[d].kind = 0; break;
      case OP_CALL: {
        int n = in->a, base = d - 1 - n; int fi = FNX(c, i, base); Fun *cf = c->fns[fi];
        for (int k = 0; k < n; k++) fc_ld(c, k, base + 1 + k);
        if (cf == c->f) fc_call(c, LB_ENTRY(c)); else { jx_mov_ri(x, RAX, (uint64_t)(uintptr_t)c->fentry[fi]); jx_call_rax(x); }
        fc_store_xmm0(c, base); break;
      }
      case OP_RETURN: {
        fc_ld(c, 0, d - 1);
        jx_mov_ri(x, RAX, (uint64_t)(uintptr_t)&g_fj_depth); jx_rm(x, 0, 0, 0x83, 5, RAX, -1, 0, 0); xb_b(x, 1);
        xb_b(x, 0xC9); jx_ret(x); break;
      }
      default: c->why = "emit"; return;
    }
  }
  fc_bind(c, LB_BAIL(c));
  fc_call_helper(c, (void *)fj_bail);
}

static void fj_free(Fun *f) {
  if (f->fj) { if (f->fj->mem) { munmap(f->fj->mem, f->fj->msz); g_jit_bytes -= f->fj->msz; } free(f->fj); f->fj = NULL; }
}
static void fj_mark(Fun *f) { if (f->fj) for (int i = 0; i < f->fj->nd; i++) mark_obj((Obj *)f->fj->deps[i]); }

static bool fj_compile(Closure *clo) {
  Fun *f = clo->f;
  if (f->fjst != 0) return f->fjst == 1;
  f->fjst = 3;
  FC *c = calloc(1, sizeof(FC)); bool ok = false; FjInfo *fj = NULL;
  c->f = f; c->clo = clo;
  if (f->upc != 0 || f->variadic || f->kind || f->getter || f->arity > FJ_MAXARGS || f->ch.n < 3 || f->ch.n > 6000 || vm.dbg_mode) goto out;
  size_t N = (size_t)f->ch.n / 1 + 4; (void)N;
  c->ins = calloc(FJ_MAXI, sizeof(FI)); c->depth = calloc(FJ_MAXI, 1); c->ty = calloc((size_t)FJ_MAXI * FJ_MAXD, 1); c->fnx = calloc((size_t)FJ_MAXI * FJ_MAXD, 1);
  c->reach = calloc(FJ_MAXI, 1); c->islab = calloc(FJ_MAXI, 1);
  c->nf = 0;
  if (!fc_analyze(c)) { if (g_jit_trace) fprintf(stderr, "[fnjit] رُفضت '%s': %s\n", f->name ? f->name->chars : "?", c->why ? c->why : "?"); goto out; }
  c->x.cap = 1 << 17; c->x.p = malloc((size_t)c->x.cap); c->x.n = 0;
  c->lpos = malloc(sizeof(int) * (size_t)(c->n + 4)); for (int i = 0; i < c->n + 4; i++) c->lpos[i] = -1;
  c->fix = malloc(sizeof(c->fix[0]) * FJ_MAXFIX);
  int fs = ((c->maxd + 2) * 8 + 15) & ~15;
  fc_emit(c, fs);
  if (c->why || c->x.n > c->x.cap) { if (g_jit_trace) fprintf(stderr, "[fnjit] فشل التوليد: %s\n", c->why ? c->why : "size"); goto out; }
  for (int k = 0; k < c->nfix; k++) {
    int tp = c->lpos[c->fix[k].lab]; if (tp < 0) goto out;
    int32_t rel = tp - (c->fix[k].pos + 4); memcpy(c->x.p + c->fix[k].pos, &rel, 4);
  }
  size_t sz = ((size_t)c->x.n + 4095) & ~(size_t)4095;
  if (g_jit_bytes + sz > (64u << 20)) goto out;
  void *mem = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); if (mem == MAP_FAILED) goto out;
  memcpy(mem, c->x.p, (size_t)c->x.n);
  if (mprotect(mem, sz, PROT_READ | PROT_EXEC) != 0) { munmap(mem, sz); goto out; }
  fj = calloc(1, sizeof(FjInfo)); fj->mem = mem; fj->msz = sz; fj->entry = mem; g_jit_bytes += sz;
  for (int i = 0; i < c->nf; i++) {
    fj->deps[fj->nd++] = c->fns[i];
    fj->g[fj->ng].d = &clo->globals->d; fj->g[fj->ng].gi = c->fgi[i]; fj->g[fj->ng].name = c->fname[i]; fj->g[fj->ng].fun = c->fns[i]; fj->ng++;
  }
  f->fj = fj; ok = true;
  if (g_jit_trace) fprintf(stderr, "[fnjit] جُمّعت الدالة '%s': %d تعليمة، %d بايت\n", f->name ? f->name->chars : "?", c->n, c->x.n);
out:
  if (c->idx_of) free(c->idx_of);
  free(c->ins); free(c->depth); free(c->ty); free(c->fnx); free(c->reach); free(c->islab); free(c->x.p); free(c->lpos); free(c->fix); free(c);
  f->fjst = ok ? 1 : 2; return ok;
}

static bool fj_run(Closure *clo, Value *args, int argc, double *out) {
  FjInfo *fj = clo->f->fj;
  for (int i = 0; i < fj->ng; i++) {
    FjGuard *g = &fj->g[i]; Dict *d = g->d;
    if (!(g->gi < d->n && d->e[g->gi].k.t == V_OBJ && d->e[g->gi].k.as.o == (Obj *)g->name && d->e[g->gi].v.t == V_OBJ && d->e[g->gi].v.as.o->t == O_CLO && ((Closure *)d->e[g->gi].v.as.o)->f == g->fun)) { fj->bails++; return false; }
  }
  double a[FJ_MAXARGS];
  for (int i = 0; i < argc; i++) { if (!IS_NUM(args[i])) return false; a[i] = args[i].as.n; }
  g_fj_depth = 0; g_fj_limit = vm.frames_max - vm.fcount - 4; if (g_fj_limit < 16) return false;
  if (_setjmp(g_fj_jb)) { fj->bails++; return false; }
  double r;
  switch (argc) {
    case 0: r = ((double (*)(void))fj->entry)(); break;
    case 1: r = ((double (*)(double))fj->entry)(a[0]); break;
    case 2: r = ((double (*)(double, double))fj->entry)(a[0], a[1]); break;
    case 3: r = ((double (*)(double, double, double))fj->entry)(a[0], a[1], a[2]); break;
    case 4: r = ((double (*)(double, double, double, double))fj->entry)(a[0], a[1], a[2], a[3]); break;
    case 5: r = ((double (*)(double, double, double, double, double))fj->entry)(a[0], a[1], a[2], a[3], a[4]); break;
    default: r = ((double (*)(double, double, double, double, double, double))fj->entry)(a[0], a[1], a[2], a[3], a[4], a[5]); break;
  }
  *out = r; return true;
}
#else
static void fj_free(Fun *f) { (void)f; }
static void fj_mark(Fun *f) { (void)f; }
#endif
