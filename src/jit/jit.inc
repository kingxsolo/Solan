#define JIT_TH 300
#define JIT_READY 0xFFFD
#define JIT_OFF 0xFFFE
#define JIT_MAX_DEOPT 200

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
#define SAVE_AREA 112

typedef struct { const uint8_t *ip; uint8_t op; int a, b, c; int len; int tgt; const uint8_t *tip; } JI;
typedef struct { int pos, lab; } JF;
typedef struct { const uint8_t *ip; int depth; int lab; int code; } JX;
enum { SK_MEM, SK_XMM, SK_VAR, SK_CONST, SK_HOME };
typedef struct { uint8_t k, r, n; int8_t hb; int hd; int var; uint64_t bits; } SD;
typedef struct { int g, idx, gi, uses, ok, reg; } PV;
typedef struct { int lab, n; SD *s; const uint8_t *ip; } JS;

typedef struct {
  Fun *fn; Closure *clo; Value *slots; const uint8_t *head, *tail;
  JI ins[JMAXI]; int n; int *idx_of;
  int depth[JMAXI]; uint8_t reach[JMAXI], islabel[JMAXI];
  XB x; int lpos[JMAXLAB]; int nlab; JF fix[JMAXFIX]; int nfix;
  JX ex[JMAXI * 2]; int nex;
  int dl[JMAXI];
  Str *gnames[JMAXV]; int ggi[JMAXV]; int ng;
  JS *sx; int nsx, csx;
  PV pv[64]; int npv; int usepromo; int spoff;
  SD sd[JMAXD + 8];
  uint64_t pool[JMAXPOOL]; int poollab[JMAXPOOL]; int npool;
  int failed; const char *why;
} JC;

#define EM(p) R12, (int)((p) * 16)
#define EMP(p) R12, (int)((p) * 16 + 8)

static double jit_h_mod(double a, double b) {
  double r = fmod(a, b);
  if (r != 0 && ((r < 0) != (b < 0))) r += b;
  return r;
}
static double jit_h_pow(double a, double b) { return sol_pow(a, b); }

static int jc_new_label(JC *c) { if (c->nlab >= JMAXLAB) { c->failed = 1; c->why = "labels"; return 0; } c->lpos[c->nlab] = -1; return c->nlab++; }
static void jc_bind(JC *c, int l) { c->lpos[l] = c->x.n; }
static void jc_fixup(JC *c, int l) {
  if (c->nfix >= JMAXFIX) { c->failed = 1; c->why = "fixups"; return; }
  c->fix[c->nfix].pos = c->x.n; c->fix[c->nfix].lab = l; c->nfix++; xb_u32(&c->x, 0);
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
  if (c->dl[i] >= 0) {
    JS *o = &c->sx[c->dl[i]]; int same = o->n == depth;
    for (int k = 0; same && k < depth; k++) if (!sd_eq(&o->s[k], &c->sd[k])) same = 0;
    if (same) return o->lab;
  }
  if (c->nsx == c->csx) { c->csx = c->csx ? c->csx * 2 : 256; c->sx = realloc(c->sx, (size_t)c->csx * sizeof(JS)); }
  JS *s = &c->sx[c->nsx]; s->lab = jc_new_label(c); s->n = depth; s->ip = c->ins[i].ip;
  s->s = malloc((size_t)(depth + 1) * sizeof(SD)); memcpy(s->s, c->sd, (size_t)depth * sizeof(SD));
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
    default: (void)c; return 0;
  }
}

static int jc_analyze(JC *c) {
  const uint8_t *code = c->fn->ch.code;
  const uint8_t *ip = c->head;
  c->idx_of = calloc((size_t)(c->tail - c->head) + 1, sizeof(int));
  for (int i = 0; i < (int)(c->tail - c->head) + 1; i++) c->idx_of[i] = -1;
  c->n = 0;
  while (ip < c->tail) {
    if (c->n >= JMAXI) return 0;
    JI *in = &c->ins[c->n]; in->ip = ip; in->op = op_norm(*ip); in->len = 1 + op_operand_len(c->fn, ip); in->tgt = -1; in->tip = NULL;
    const uint8_t *ar = ip + 1;
    in->a = in->b = in->c = 0;
    switch ((Op)in->op) {
      case OP_CONST: case OP_GET_GLOBAL: case OP_SET_GLOBAL: case OP_GET_PROP: case OP_CHECK: in->a = (ar[0] << 8) | ar[1]; break;
      case OP_GET_LOCAL: case OP_SET_LOCAL: case OP_GET_UPV: case OP_SET_UPV: in->a = ar[0]; break;
      case OP_JUMP: case OP_JIF: case OP_JIT: case OP_JNN: case OP_JNIL: in->tip = ip + 3 + ((ar[0] << 8) | ar[1]); break;
      case OP_LOOP: in->tip = ip + 3 - ((ar[0] << 8) | ar[1]); break;
      case OP_FOREACH: in->a = ar[0]; in->b = ar[1]; in->tip = ip + 5 + ((ar[2] << 8) | ar[3]); break;
      default: break;
    }
    int pop, push;
    if (!jc_effect(c, in, &pop, &push)) { c->why = op_names[in->op]; return 0; }
    c->idx_of[ip - c->head] = c->n++;
    ip += in->len;
  }
  if (ip != c->tail) return 0;
  (void)code;
  for (int i = 0; i < c->n; i++) {
    JI *in = &c->ins[i];
    if (in->tip) {
      in->tgt = jc_target_idx(c, in->tip); if (in->tgt >= 0) c->islabel[in->tgt] = 1;
      if ((in->op == OP_JIF || in->op == OP_JIT) && *in->tip == OP_POP) { int t2 = jc_target_idx(c, in->tip + 1); if (t2 >= 0) c->islabel[t2] = 1; }
    }
  }
  for (int i = 0; i < c->n; i++) c->depth[i] = -1;
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
  if (pfx) xb_b(x, pfx);
  jx_rex(x, 0, reg, 0, 0); jx_opc(x, op); xb_b(x, ((reg & 7) << 3) | 5); jc_fixup(c, lab);
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
static void jc_save_regs(JC *c) { for (int r = 3; r <= 15; r++) jx_movsd_mr(&c->x, RSP, (r - 3) * 8, r); }
static void jc_restore_regs(JC *c) { for (int r = 3; r <= 15; r++) jx_movsd_rm(&c->x, r, RSP, (r - 3) * 8); }
static void jc_call_helper(JC *c, void *fn) {
  jc_save_regs(c);
  jx_mov_ri(&c->x, RAX, (uint64_t)(uintptr_t)fn); jx_call_rax(&c->x);
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
    jx_cdq(x); jx_idiv32(x, RCX);
    jx_test32_rr(x, RDX, RDX); jc_jcc(c, CC_E, res);
    jx_alu32_rr(x, 0x89, RAX, RDX); jx_alu32_rr(x, 0x31, RAX, RCX); jc_jcc(c, CC_NS, res);
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
      Str *nm = AS_STR(kc[in->a]); Dict *dd = &c->clo->globals->d;
      gi = dfind(dd, OBJV(nm), nm->hash); if (gi < 0) continue; g = 1; idx = gi;
    } else continue;
    int f = -1; for (int k = 0; k < nc; k++) if (cand[k].g == g && cand[k].idx == idx) { f = k; break; }
    if (f < 0) { if (nc >= 64) continue; f = nc++; cand[f].g = g; cand[f].idx = idx; cand[f].gi = gi; cand[f].uses = 0; cand[f].ok = 1; cand[f].reg = 0;
      Value v = g ? c->clo->globals->d.e[gi].v : c->slots[idx];
      cand[f].ok = IS_NUM(v); }
    cand[f].uses++;
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
        case OP_CONST: ty[d] = IS_NUM(kc[in->a]); break;
        case OP_NIL: case OP_TRUE: case OP_FALSE: case OP_GET_UPV: ty[d] = 0; break;
        case OP_GET_LOCAL: { int k = in->a < c->spoff ? jc_find_pv(c, 0, in->a) : -1; ty[d] = k >= 0 && c->pv[k].ok; break; }
        case OP_GET_GLOBAL: {
          Str *nm = AS_STR(kc[in->a]); int gi = dfind(&c->clo->globals->d, OBJV(nm), nm->hash);
          int k = gi >= 0 ? jc_find_pv(c, 1, gi) : -1; ty[d] = k >= 0 && c->pv[k].ok; break;
        }
        case OP_DUP: ty[d] = ty[d - 1]; break;
        case OP_DUP2: ty[d] = ty[d - 2]; ty[d + 1] = ty[d - 1]; break;
        case OP_SET_LOCAL: case OP_SET_GLOBAL: {
          int k;
          if (in->op == OP_SET_LOCAL) k = in->a < c->spoff ? jc_find_pv(c, 0, in->a) : -1;
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
        case OP_SET_INDEX: ty[d - 3] = ty[d - 1]; break;
        case OP_FOREACH: for (int k = 0; k < in->b; k++) ty[d + k] = 0; break;
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
        Value v = c->fn->ch.consts.v[in->a];
        if (IS_NUM(v)) { c->sd[d] = sd_mk(SK_CONST); memcpy(&c->sd[d].bits, &v.as.n, 8); }
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
        if (in->op == OP_GET_LOCAL) { if (in->a < c->spoff) k = jc_find_pv(c, 0, in->a); }
        else { Str *nm = AS_STR(c->fn->ch.consts.v[in->a]); int gi = jc_global_index(c, nm); k = jc_find_pv(c, 1, gi); }
        if (k >= 0) { c->sd[d] = sd_mk(SK_VAR); c->sd[d].r = (uint8_t)c->pv[k].reg; c->sd[d].var = k; break; }
        int b, dd;
        if (in->op == OP_GET_LOCAL) {
          if (in->a >= c->spoff) { int p = in->a - c->spoff; if (p >= d) { c->failed = 1; c->why = "slot"; break; } jc_mat(c, p); }
          jc_local_home(c, in->a, &b, &dd);
        } else {
          Str *nm = AS_STR(c->fn->ch.consts.v[in->a]); int gi = jc_global_index(c, nm);
          b = R13; dd = gi * (int)sizeof(Ent) + (int)offsetof(Ent, v);
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
        if (in->op == OP_SET_LOCAL) { if (in->a < c->spoff) k = jc_find_pv(c, 0, in->a); }
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
      default: c->failed = 1; c->why = "op"; break;
    }
  }
}

static JitRegion *jit_compile(Fun *fn, const uint8_t *head, const uint8_t *tail, Closure *clo, Value *slots, Value *sp) {
  JC *c = calloc(1, sizeof(JC));
  JitRegion *res = NULL;
  c->fn = fn; c->clo = clo; c->slots = slots; c->head = head; c->tail = tail;
  c->spoff = (int)(sp - slots); c->usepromo = 1;
  c->x.cap = 1 << 20; c->x.p = malloc(c->x.cap);
  if (!jc_analyze(c)) { if (g_jit_trace) fprintf(stderr, "[jit] تحليل فشل: %s\n", c->why ? c->why : "?"); goto out; }
  c->nlab = c->n;
  for (int i = 0; i < c->n; i++) { c->lpos[i] = -1; c->dl[i] = -1; }
  jc_promote(c);
  XB *x = &c->x;
  jx_push(x, RBX); jx_push(x, R12); jx_push(x, R13); jx_push(x, R14); jx_push(x, R15);
  jx_sub_rsp(x, SAVE_AREA);
  jx_mov_rr(x, R15, RDI);
  jx_mov_rm(x, RBX, R15, (int)offsetof(JitCtx, slots)); jx_mov_rm(x, R12, R15, (int)offsetof(JitCtx, sp)); jx_mov_rm(x, R14, R15, (int)offsetof(JitCtx, clo));
  int lguards = jc_new_label(c), lbody = jc_new_label(c), lepi = jc_new_label(c);
  jc_jmp(c, lguards);
  jc_bind(c, lbody);
  jc_gen_body(c);
  if (c->failed) { if (g_jit_trace) fprintf(stderr, "[jit] توليد فشل: %s\n", c->why ? c->why : "?"); goto out; }
  jc_bind(c, lguards);
  {
    int fail = jc_exit_label2(c, head, 0, 2);
    if (c->ng > 0) {
      jx_mov_rm(x, RAX, R14, (int)offsetof(Closure, globals));
      jx_lea(x, RSI, RAX, -1, 0, (int)offsetof(Map, d));
      jx_mov_rm(x, RDI, RSI, (int)offsetof(Dict, e));
      for (int k = 0; k < c->ng; k++) {
        int gi = c->ggi[k];
        jx_cmp32_mi(x, RSI, (int)offsetof(Dict, n), gi + 1); jc_jcc(c, CC_L, fail);
        jx_cmp32_mi(x, RDI, gi * (int)sizeof(Ent) + (int)offsetof(Ent, k), V_OBJ); jc_jcc(c, CC_NE, fail);
        jx_mov_ri(x, RAX, (uint64_t)(uintptr_t)c->gnames[k]);
        jx_cmp_rm(x, RAX, RDI, gi * (int)sizeof(Ent) + (int)offsetof(Ent, k) + 8); jc_jcc(c, CC_NE, fail);
      }
      jx_mov_rr(x, R13, RDI);
    }
    jx_lea(x, RAX, RBX, -1, 0, c->spoff * 16); jx_cmp_rr(x, RAX, R12); jc_jcc(c, CC_NE, fail);
    for (int k = 0; k < c->npv; k++) { int b, d; jc_pv_home(&c->pv[k], &b, &d); jx_cmp32_mi(x, b, d, V_NUM); jc_jcc(c, CC_NE, fail); }
    for (int k = 0; k < c->npv; k++) { int b, d; jc_pv_home(&c->pv[k], &b, &d); jx_movsd_rm(x, c->pv[k].reg, b, d + 8); }
  }
  jc_jmp(c, lbody);
  for (int k = 0; k < c->nsx; k++) {
    JS *s = &c->sx[k]; jc_bind(c, s->lab);
    for (int p = 0; p < s->n; p++) jc_emit_mat(c, &s->s[p], p);
    jc_jmp(c, jc_exit_label2(c, s->ip, s->n, 1));
  }
  for (int k = 0; k < c->nex; k++) {
    JX *e = &c->ex[k]; jc_bind(c, e->lab);
    if (e->code != 2) jc_flush_vars(c);
    jx_mov_ri(x, RAX, (uint64_t)(uintptr_t)e->ip); jx_mov_mr(x, R15, (int)offsetof(JitCtx, ip), RAX);
    jx_lea(x, RAX, R12, -1, 0, e->depth * 16); jx_mov_mr(x, R15, (int)offsetof(JitCtx, sp), RAX);
    jx_mov32_mi(x, R15, (int)offsetof(JitCtx, code), (uint32_t)e->code);
    jc_jmp(c, lepi);
  }
  jc_bind(c, lepi);
  jx_add_rsp(x, SAVE_AREA);
  jx_pop(x, R15); jx_pop(x, R14); jx_pop(x, R13); jx_pop(x, R12); jx_pop(x, RBX); jx_ret(x);
  for (int k = 0; k < c->npool; k++) { jc_bind(c, c->poollab[k]); xb_u64(x, c->pool[k]); }
  if (c->x.n > c->x.cap || c->failed) { if (g_jit_trace) fprintf(stderr, "[jit] فشل: %s\n", c->why ? c->why : "حجم"); goto out; }
  for (int k = 0; k < c->nfix; k++) {
    int tp = c->lpos[c->fix[k].lab];
    if (tp < 0) { if (g_jit_trace) fprintf(stderr, "[jit] تسمية غير مربوطة\n"); goto out; }
    int32_t rel = tp - (c->fix[k].pos + 4); memcpy(c->x.p + c->fix[k].pos, &rel, 4);
  }
  {
    size_t sz = ((size_t)c->x.n + 4095) & ~(size_t)4095;
    if (g_jit_bytes + sz > (64u << 20)) goto out;
    void *mem = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) goto out;
    memcpy(mem, c->x.p, (size_t)c->x.n);
    if (mprotect(mem, sz, PROT_READ | PROT_EXEC) != 0) { munmap(mem, sz); g_jit = 0; goto out; }
    res = calloc(1, sizeof(JitRegion));
    res->tail = tail; res->fn = (JitFn)mem; res->mem = mem; res->msz = sz; res->next = fn->jreg; fn->jreg = res; g_jit_bytes += sz;
    if (g_jit_trace) fprintf(stderr, "[jit] جُمّعت حلقة: %d تعليمة، %d بايت، متغيرات في سجلات: %d\n", c->n, c->x.n, c->npv);
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
  __builtin_cpu_init();
  if (!__builtin_cpu_supports("sse4.1")) g_jit = 0;
}
#else
static void jit_init(void) { g_jit = 0; }
#endif
