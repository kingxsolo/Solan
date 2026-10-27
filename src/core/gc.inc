static void pin(Obj *o) { varr_push(&vm.pins->a, OBJV(o)); }

static void mark_obj(Obj *o) {
  if (!o || o->mark) return;
  o->mark = true;
  if (vm.gn == vm.gcap) { vm.gcap = vm.gcap ? vm.gcap * 2 : 256; vm.gray = xrealloc(vm.gray, sizeof(Obj *) * vm.gcap); }
  vm.gray[vm.gn++] = o;
}
static void mark_val(Value v) { if (v.t == V_OBJ) mark_obj(v.as.o); }
static void mark_dict(Dict *d) { for (int i = 0; i < d->n; i++) { mark_val(d->e[i].k); mark_val(d->e[i].v); } }

static void blacken(Obj *o) {
  switch (o->t) {
    case O_STR: break;
    case O_NAT: mark_obj((Obj *)((Native *)o)->name); break;
    case O_FUN: { Fun *f = (Fun *)o; mark_obj((Obj *)f->name); mark_obj((Obj *)f->file); for (int i = 0; i < f->ndbg; i++) mark_obj((Obj *)f->dbgname[i]); for (int i = 0; i < f->ch.consts.n; i++) mark_val(f->ch.consts.v[i]); fj_mark(f); break; }
    case O_CLO: { Closure *c = (Closure *)o; mark_obj((Obj *)c->f); mark_obj((Obj *)c->globals); for (int i = 0; i < c->upc; i++) mark_obj((Obj *)c->upv[i]); break; }
    case O_UPV: { Upv *u = (Upv *)o; mark_val(u->closed); if (u->loc != &u->closed) mark_val(*u->loc); break; }
    case O_CLS: { Class *c = (Class *)o; mark_obj((Obj *)c->name); mark_obj((Obj *)c->super); mark_dict(&c->methods); mark_dict(&c->statics); break; }
    case O_INST: { Instance *i = (Instance *)o; mark_obj((Obj *)i->cls); mark_dict(&i->fields); break; }
    case O_BOUND: mark_val(((Bound *)o)->recv); mark_val(((Bound *)o)->method); break;
    case O_LIST: { List *l = (List *)o; for (int i = 0; i < l->a.n; i++) mark_val(l->a.v[i]); break; }
    case O_MAP: mark_dict(&((Map *)o)->d); break;
    case O_CORO: {
      Coro *c = (Coro *)o; mark_obj((Obj *)c->fn); mark_obj((Obj *)c->args);
      if (c->state != 2) {
        for (Value *p = c->stack; p < c->sp; p++) mark_val(*p);
        for (int i = 0; i < c->fcount; i++) mark_obj((Obj *)c->frames[i].clo);
        for (Upv *u = c->open; u; u = u->next) mark_obj((Obj *)u);
      }
      break;
    }
  }
}

static size_t obj_size(Obj *o) {
  switch (o->t) {
    case O_STR: { Str *s = (Str *)o; return s->chars == (char *)(s + 1) ? sizeof(Str) + (size_t)s->len + 1 : sizeof(Str); }
    case O_FUN: return sizeof(Fun); case O_CLO: return sizeof(Closure); case O_UPV: return sizeof(Upv); case O_NAT: return sizeof(Native);
    case O_CLS: return sizeof(Class); case O_INST: return sizeof(Instance); case O_BOUND: return sizeof(Bound); case O_LIST: return sizeof(List);
    case O_MAP: return sizeof(Map); case O_CORO: return sizeof(Coro);
  }
  return sizeof(Obj);
}
static void free_obj(Obj *o) {
  size_t osz = obj_size(o);
  switch (o->t) {
    case O_STR: {
      Str *s = (Str *)o; uint32_t b = s->hash & (vm.sbcap - 1);
      Str **pp = &vm.sb[b];
      while (*pp && *pp != s) pp = &(*pp)->hnext;
      if (*pp) *pp = s->hnext;
      sbuf_unref(s->sb);
      vm.sbcount--; break;
    }
    case O_FUN: { Fun *f = (Fun *)o; free(f->ch.code); free(f->ch.lines); free(f->ch.consts.v); free(f->gc); free(f->dbgname); free(f->dbgslot); free(f->lc); jit_free_regions(f); fj_free(f); break; }
    case O_CLO: free(((Closure *)o)->upv); break;
    case O_CLS: dfree(&((Class *)o)->methods); dfree(&((Class *)o)->statics); break;
    case O_INST: dfree(&((Instance *)o)->fields); break;
    case O_LIST: free(((List *)o)->a.v); break;
    case O_MAP: dfree(&((Map *)o)->d); break;
    case O_CORO: { Coro *c = (Coro *)o; free(c->stack); free(c->frames); free(c->handlers); break; }
    default: break;
  }
  vm.nobj--; pool_free(o, osz);
}

static void collect(void) {
  vm.gc_pending = false; vm.sbytes = 0; g_mfn_memo.d = NULL;
  for (int i = 0; i < g_nkroots; i++) if (*g_kroots[i]) mark_obj((Obj *)*g_kroots[i]);
  for (Value *s = vm.stack; s < vm.sp; s++) mark_val(*s);
  for (int i = 0; i < vm.fcount; i++) mark_obj((Obj *)vm.frames[i].clo);
  for (Upv *u = vm.open; u; u = u->next) mark_obj((Obj *)u);
  mark_dict(&vm.builtins); mark_dict(&vm.natmods); mark_dict(&vm.modcache); mark_dict(&vm.kw); mark_dict(&vm.mid); mark_dict(&vm.ctxw); mark_dict(&vm.stdlibd);
  mark_val(vm.exc); mark_obj((Obj *)vm.pins); mark_val(vm.yielded); mark_obj((Obj *)vm.cur_co); mark_obj((Obj *)vm.promise_cls); mark_val(vm.fn_block); mark_val(vm.fn_start);
  for (CtxNode *n = vm.ctxs; n; n = n->next) {
    for (Value *p = n->c.stack; p < n->c.sp; p++) mark_val(*p);
    for (int i = 0; i < n->c.fcount; i++) mark_obj((Obj *)n->c.frames[i].clo);
    for (Upv *u = n->c.open; u; u = u->next) mark_obj((Obj *)u);
    mark_obj((Obj *)n->c.cur);
  }
  while (vm.gn > 0) blacken(vm.gray[--vm.gn]);
  for (Obj *o = vm.objs; o; o = o->next) if (o->t == O_CORO && !o->mark) { Coro *c = (Coro *)o; for (Upv *u = c->open; u; u = u->next) { u->closed = *u->loc; u->loc = &u->closed; } c->open = NULL; }
  Obj **p = &vm.objs;
  while (*p) {
    if ((*p)->mark) { (*p)->mark = false; p = &(*p)->next; }
    else { Obj *u = *p; *p = u->next; free_obj(u); }
  }
  vm.next_gc = vm.nobj * 2 < 4096 ? 4096 : vm.nobj * 2;
}

static int g_digits = 0;
static void fmt_num_raw(double n, Buf *b) {
  if (isnan(n)) { buf_fmt(b, "nan"); return; }
  if (isinf(n)) { buf_fmt(b, n < 0 ? "-inf" : "inf"); return; }
  if (n == 0) { buf_fmt(b, "0"); return; }
  if (n == floor(n) && fabs(n) < 1e15) { buf_fmt(b, "%.0f", n); return; }
  char t[48]; int p;
  for (p = 15; p <= 17; p++) { snprintf(t, sizeof t, "%.*e", p - 1, n); if (strtod(t, NULL) == n) break; }
  char dg[24]; int k = 0; const char *q = t; bool neg = false;
  if (*q == '-') { neg = true; q++; }
  for (; *q && *q != 'e'; q++) if (*q >= '0' && *q <= '9') dg[k++] = *q;
  while (k > 1 && dg[k - 1] == '0') k--;
  int e10 = (*q == 'e') ? atoi(q + 1) : 0, pt = e10 + 1;
  if (neg) buf_addc(b, '-');
  if (k <= pt && pt <= 21) { buf_add(b, dg, k); for (int i = k; i < pt; i++) buf_addc(b, '0'); }
  else if (0 < pt && pt <= 21) { buf_add(b, dg, pt); buf_addc(b, '.'); buf_add(b, dg + pt, k - pt); }
  else if (-6 < pt && pt <= 0) { buf_add(b, "0.", 2); for (int i = pt; i < 0; i++) buf_addc(b, '0'); buf_add(b, dg, k); }
  else {
    buf_addc(b, dg[0]);
    if (k > 1) { buf_addc(b, '.'); buf_add(b, dg + 1, k - 1); }
    buf_fmt(b, "e%c%d", pt - 1 < 0 ? '-' : '+', abs(pt - 1));
  }
}
static void fmt_num(double n, Buf *b) {
  if (!g_digits) { fmt_num_raw(n, b); return; }
  Buf t; buf_init(&t); fmt_num_raw(n, &t);
  for (int i = 0; i < t.n; i++) {
    char c = t.p[i];
    if (c >= '0' && c <= '9') { uint32_t cp = (g_digits == 1 ? 0x660 : 0x6F0) + (c - '0'); char u[4]; buf_add(b, u, u8_encode(cp, u)); }
    else if (c == '.' && g_digits == 1) buf_add(b, "\xD9\xAB", 2);
    else buf_addc(b, c);
  }
  free(t.p);
}

static const char *type_name(Value v) {
  switch (v.t) {
    case V_NIL: return "عدم"; case V_BOOL: return "منطقي"; case V_NUM: return "رقم"; case V_EMPTY: return "عدم";
    default: break;
  }
  switch (v.as.o->t) {
    case O_STR: return "نص"; case O_LIST: return "قائمة"; case O_MAP: return "قاموس";
    case O_FUN: case O_CLO: case O_NAT: case O_BOUND: return "دالة";
    case O_CORO: return "كوروتين"; case O_CLS: return "صنف"; case O_INST: return AS_INST(v)->cls->name->chars;
    default: return "؟";
  }
}
static bool call_method0(Value recv, Closure *m, Value *out);
static void repr(Value v, Buf *b, int depth, bool quote) {
  switch (v.t) {
    case V_NIL: case V_EMPTY: buf_fmt(b, "عدم"); return;
    case V_BOOL: buf_fmt(b, v.as.b ? "صح" : "غلط"); return;
    case V_NUM: fmt_num(v.as.n, b); return;
    default: break;
  }
  Obj *o = v.as.o;
  switch (o->t) {
    case O_STR: if (quote) { buf_addc(b, '"'); buf_add(b, ((Str *)o)->chars, ((Str *)o)->len); buf_addc(b, '"'); } else buf_add(b, ((Str *)o)->chars, ((Str *)o)->len); return;
    case O_LIST: {
      List *l = (List *)o; if (depth > 6) { buf_fmt(b, "[...]"); return; }
      buf_addc(b, '[');
      for (int i = 0; i < l->a.n; i++) { if (i) buf_fmt(b, "، "); repr(l->a.v[i], b, depth + 1, true); }
      buf_addc(b, ']'); return;
    }
    case O_MAP: {
      Map *m = (Map *)o; if (depth > 6) { buf_fmt(b, "{...}"); return; }
      buf_addc(b, '{'); bool first = true;
      for (int i = 0; i < m->d.n; i++) {
        if (m->d.e[i].k.t == V_EMPTY) continue;
        if (!first) buf_fmt(b, "، "); first = false;
        repr(m->d.e[i].k, b, depth + 1, true); buf_fmt(b, ": "); repr(m->d.e[i].v, b, depth + 1, true);
      }
      buf_addc(b, '}'); return;
    }
    case O_FUN: buf_fmt(b, "<دالة %s>", ((Fun *)o)->name ? ((Fun *)o)->name->chars : "مجهولة"); return;
    case O_CLO: buf_fmt(b, "<دالة %s>", ((Closure *)o)->f->name ? ((Closure *)o)->f->name->chars : "مجهولة"); return;
    case O_NAT: buf_fmt(b, "<دالة أصلية %s>", ((Native *)o)->name->chars); return;
    case O_BOUND: buf_fmt(b, "<دالة مرتبطة>"); return;
    case O_CORO: buf_fmt(b, "<كوروتين>"); return;
    case O_CLS: buf_fmt(b, "<صنف %s>", ((Class *)o)->name->chars); return;
    case O_INST: {
      Instance *in = (Instance *)o; Value m;
      if (dget(&in->cls->methods, OBJV(vm.tostr_str), &m) && IS_T(m, O_CLO) && vm.nest < NEST_MAX) {
        Value out;
        if (call_method0(v, AS_CLO(m), &out) && IS_T(out, O_STR)) { buf_add(b, AS_STR(out)->chars, AS_STR(out)->len); return; }
      }
      buf_fmt(b, "<كائن %s>", in->cls->name->chars); return;
    }
    default: buf_fmt(b, "<؟>"); return;
  }
}
