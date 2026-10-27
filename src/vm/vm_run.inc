static int run(int base) {
  static const void *dt[] = { OPLIST(X_LBL) };
  Frame *frame = &vm.frames[vm.fcount - 1]; uint8_t *ip = frame->ip; Value *sp = vm.sp;
  Value *kst = frame->clo->f->ch.consts.v; int *gcp = frame->clo->f->gc;
#define SAVE() (frame->ip = ip)
#define SYNC() (vm.sp = sp)
#define LOAD() (sp = vm.sp)
#define RELOAD() do { frame = &vm.frames[vm.fcount - 1]; ip = frame->ip; kst = frame->clo->f->ch.consts.v; gcp = frame->clo->f->gc; } while (0)
#define NEXT() goto *dt[*ip++]
#define CASE(o) L_##o:
#define READ8() (*ip++)
#define READ16() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
#define CONSTV() (kst[READ16()])
#define PUSHL(v) (*sp++ = (v))
#define POPL() (*--sp)
#define PK(n) (sp[-1 - (n)])
#define RT(...) do { SAVE(); SYNC(); rt_error(__VA_ARGS__); goto throw_exc; } while (0)
#define SLOW(call) do { SAVE(); SYNC(); if (call) goto throw_exc; LOAD(); } while (0)
  if (vm.pending_throw) { vm.pending_throw = false; goto throw_exc; }
  NEXT();

  CASE(OP_CONST) PUSHL(CONSTV()); NEXT();
  CASE(OP_NIL) PUSHL(NIL); NEXT();
  CASE(OP_TRUE) PUSHL(BOOLV(true)); NEXT();
  CASE(OP_FALSE) PUSHL(BOOLV(false)); NEXT();
  CASE(OP_POP) sp--; NEXT();
  CASE(OP_DUP) { Value t = PK(0); PUSHL(t); NEXT(); }
  CASE(OP_DUP2) { Value b = PK(0), a = PK(1); PUSHL(a); PUSHL(b); NEXT(); }

  CASE(OP_GET_LOCAL) PUSHL(frame->slots[READ8()]); NEXT();
  CASE(OP_SET_LOCAL) frame->slots[READ8()] = PK(0); NEXT();

  CASE(OP_Q_SETPOP) { frame->slots[ip[0]] = sp[-1]; sp--; ip += 2; NEXT(); }
  CASE(OP_Q_LL) { PUSHL(frame->slots[ip[0]]); PUSHL(frame->slots[ip[2]]); ip += 3; NEXT(); }
  CASE(OP_Q_LP) {
    Value r = frame->slots[ip[0]];
    if (IS_T(r, O_INST)) {
      int ci = (ip[2] << 8) | ip[3]; Str *name = AS_STR(kst[ci]); Dict *d = &AS_INST(r)->fields; int gi = gcp[ci];
      if (gi < d->n && d->e[gi].k.t == V_OBJ && d->e[gi].k.as.o == (Obj *)name) { PUSHL(d->e[gi].v); ip += 4; NEXT(); }
    }
    goto L_OP_GET_LOCAL;
  }
#define QLK(NAME, EXPR) \
  CASE(NAME) { Value a = frame->slots[ip[0]], b = kst[(ip[2] << 8) | ip[3]]; \
    if (IS_NUM(a) && IS_NUM(b)) { double x = a.as.n, y = b.as.n; (void)x; (void)y; PUSHL(EXPR); ip += 5; NEXT(); } \
    goto L_OP_GET_LOCAL; }
  QLK(OP_Q_LK_ADD, NUMV(x + y))
  QLK(OP_Q_LK_SUB, NUMV(x - y))
  QLK(OP_Q_LK_MUL, NUMV(x * y))
  QLK(OP_Q_LK_LT, BOOLV(x < y))
  QLK(OP_Q_LK_GT, BOOLV(x > y))
  QLK(OP_Q_LK_LE, BOOLV(x <= y))
  QLK(OP_Q_LK_GE, BOOLV(x >= y))
#define QLL(NAME, EXPR) \
  CASE(NAME) { Value a = frame->slots[ip[0]], b = frame->slots[ip[2]]; \
    if (IS_NUM(a) && IS_NUM(b)) { double x = a.as.n, y = b.as.n; PUSHL(EXPR); ip += 4; NEXT(); } \
    goto L_OP_GET_LOCAL; }
  QLL(OP_Q_LL_ADD, NUMV(x + y))
  QLL(OP_Q_LL_SUB, NUMV(x - y))
  QLL(OP_Q_LL_MUL, NUMV(x * y))
  CASE(OP_GET_GLOBAL) {
    int ci = READ16(); Str *name = AS_STR(kst[ci]); Dict *d = &frame->clo->globals->d; int gi = gcp[ci]; Value v;
    if (gi < d->n && d->e[gi].k.t == V_OBJ && d->e[gi].k.as.o == (Obj *)name) v = d->e[gi].v;
    else {
      int x = dfind(d, OBJV(name), name->hash);
      if (x >= 0) { gcp[ci] = x; v = d->e[x].v; }
      else if (!dget(&vm.builtins, OBJV(name), &v)) { char sg[300]; suggest(sg, sizeof sg, name, &frame->clo->globals->d, &vm.builtins, &vm.natmods); RT("المتغير '%s' غير معرّف%s", name->chars, sg); }
    }
    PUSHL(v); NEXT();
  }
  CASE(OP_DEF_GLOBAL) { Str *name = AS_STR(CONSTV()); dset(&frame->clo->globals->d, OBJV(name), PK(0)); sp--; NEXT(); }
  CASE(OP_SET_GLOBAL) {
    int ci = READ16(); Str *name = AS_STR(kst[ci]); Dict *d = &frame->clo->globals->d; int *gp = &gcp[ci]; int gi = *gp;
    if (gi < d->n && d->e[gi].k.t == V_OBJ && d->e[gi].k.as.o == (Obj *)name) { d->e[gi].v = PK(0); NEXT(); }
    int x = dfind(d, OBJV(name), name->hash);
    if (x < 0) { char sg[300]; suggest(sg, sizeof sg, name, &frame->clo->globals->d, &vm.builtins, NULL); RT("المتغير '%s' غير معرّف (استخدم 'خل' لتعريفه)%s", name->chars, sg); }
    *gp = x; d->e[x].v = PK(0); NEXT();
  }
  CASE(OP_GET_UPV) PUSHL(*frame->clo->upv[READ8()]->loc); NEXT();
  CASE(OP_SET_UPV) *frame->clo->upv[READ8()]->loc = PK(0); NEXT();

  CASE(OP_GET_PROP) {
    int ci = READ16(); Str *name = AS_STR(kst[ci]); Value out, r = PK(0);
    if (IS_T(r, O_INST)) {
      Dict *d = &AS_INST(r)->fields; int *gp = &gcp[ci]; int gi = *gp;
      if (gi < d->n && d->e[gi].k.t == V_OBJ && d->e[gi].k.as.o == (Obj *)name) { sp[-1] = d->e[gi].v; NEXT(); }
      int x = dfind(d, OBJV(name), name->hash);
      if (x >= 0) { *gp = x; sp[-1] = d->e[x].v; NEXT(); }
    }
    SAVE(); SYNC(); if (get_prop(r, name, &out)) goto throw_exc;
    sp[-1] = out; NEXT();
  }
  CASE(OP_SET_PROP) {
    int ci = READ16(); Str *name = AS_STR(kst[ci]); Value v = PK(0), o = PK(1);
    if (IS_T(o, O_INST)) {
      Dict *d = &AS_INST(o)->fields; int *gp = &gcp[ci]; int gi = *gp;
      if (CALLABLE_OBJ(v)) AS_INST(o)->fnf = true;
      if (gi < d->n && d->e[gi].k.t == V_OBJ && d->e[gi].k.as.o == (Obj *)name) d->e[gi].v = v;
      else { int x = dfind(d, OBJV(name), name->hash); if (x >= 0) { *gp = x; d->e[x].v = v; } else dset(d, OBJV(name), v); }
    }
    else if (IS_T(o, O_MAP)) { int mx = map_find_name(&AS_MAP(o)->d, name); if (mx >= 0) AS_MAP(o)->d.e[mx].v = v; else dset(&AS_MAP(o)->d, OBJV(name), v); }
    else if (IS_T(o, O_CLS)) dset(&AS_CLS(o)->statics, OBJV(name), v);
    else RT("لا يمكن إسناد خاصية على قيمة نوعها '%s'", type_name(o));
    sp[-2] = v; sp--; NEXT();
  }
  CASE(OP_GET_SUPER) {
    Str *name = AS_STR(CONSTV()); Class *sup = AS_CLS(POPL()); Value m;
    if (!dget(&sup->methods, OBJV(name), &m)) RT("الدالة '%s' غير معرفة في الصنف الأب", name->chars);
    sp[-1] = OBJV(new_bound(PK(0), m)); NEXT();
  }
  CASE(OP_GET_INDEX) {
    Value o = PK(1), ix = PK(0), out;
    if (IS_T(o, O_LIST) && IS_NUM(ix)) {
      List *l = AS_LIST(o); double d = ix.as.n; int i = (int)d;
      if (i == d && i >= 0 && i < l->a.n) { sp--; sp[-1] = l->a.v[i]; NEXT(); }
    }
    SAVE(); SYNC(); if (get_index(o, ix, &out)) goto throw_exc;
    sp -= 2; PUSHL(out); NEXT();
  }
  CASE(OP_SET_INDEX) {
    Value o = PK(2), ix = PK(1), v = PK(0);
    if (IS_T(o, O_LIST) && IS_NUM(ix)) {
      List *l = AS_LIST(o); double d = ix.as.n; int i = (int)d;
      if (i == d && i >= 0 && i < l->a.n) { l->a.v[i] = v; sp -= 2; sp[-1] = v; NEXT(); }
    }
    SAVE(); SYNC(); if (set_index(o, ix, v)) goto throw_exc;
    sp -= 3; PUSHL(v); NEXT();
  }
  CASE(OP_CHECK) {
    uint16_t mk = (uint16_t)READ16(); List *inf = AS_LIST(CONSTV());
    if (!type_ok(PK(0), mk, inf)) {
      RT("خطأ في النوع: %s يتوقع '%s' لكن وجدت '%s'", AS_STR(inf->a.v[1])->chars, AS_STR(inf->a.v[0])->chars, type_name(PK(0)));
    }
    NEXT();
  }
  CASE(OP_EQ) {
    Value b = sp[-1], a = sp[-2]; sp--;
    sp[-1] = BOOLV(IS_NUM(a) && IS_NUM(b) ? a.as.n == b.as.n : values_equal(a, b)); NEXT();
  }
#define CMP(OPN, NUMEXPR, INTEXPR) \
  CASE(OPN) { \
    if (IS_NUM(PK(0)) && IS_NUM(PK(1))) { double b = sp[-1].as.n, a = sp[-2].as.n; sp--; sp[-1] = BOOLV(NUMEXPR); NEXT(); } \
    SAVE(); SYNC(); int c = vm_cmp(sp[-2], sp[-1]); if (c == 2) goto throw_exc; \
    sp--; sp[-1] = BOOLV(INTEXPR); NEXT(); }
  CMP(OP_GT, a > b, c > 0) CMP(OP_LT, a < b, c < 0) CMP(OP_GE, a >= b, c >= 0) CMP(OP_LE, a <= b, c <= 0)
  CASE(OP_IN) { bool r; SAVE(); SYNC(); if (vm_in(PK(1), PK(0), &r)) goto throw_exc; sp -= 2; PUSHL(BOOLV(r)); NEXT(); }

  CASE(OP_ADD) {
    if (IS_NUM(PK(0)) && IS_NUM(PK(1))) { sp--; sp[-1].as.n += sp[0].as.n; NEXT(); }
    SAVE(); SYNC(); if (!vm_add()) goto throw_exc; LOAD(); NEXT();
  }
  CASE(OP_ADD_APPEND) {
    if (IS_NUM(PK(0)) && IS_NUM(PK(1))) { sp--; sp[-1].as.n += sp[0].as.n; NEXT(); }
    SAVE(); SYNC(); if (!vm_add_append()) goto throw_exc; LOAD(); NEXT();
  }
  CASE(OP_MUL) {
    if (IS_NUM(PK(0)) && IS_NUM(PK(1))) { sp--; sp[-1].as.n *= sp[0].as.n; NEXT(); }
    SAVE(); SYNC(); if (!vm_mul()) goto throw_exc; LOAD(); NEXT();
  }
  CASE(OP_SUB) {
    if (IS_NUM(PK(0)) && IS_NUM(PK(1))) { sp--; sp[-1].as.n -= sp[0].as.n; NEXT(); }
    RT("لا يمكن الطرح بين '%s' و '%s'", type_name(PK(1)), type_name(PK(0)));
  }
  CASE(OP_DIV) {
    if (IS_NUM(PK(0)) && IS_NUM(PK(1))) {
      if (PK(0).as.n == 0) RT("القسمة على صفر");
      sp--; sp[-1].as.n /= sp[0].as.n; NEXT();
    }
    RT("لا يمكن القسمة بين '%s' و '%s'", type_name(PK(1)), type_name(PK(0)));
  }
  CASE(OP_MOD) {
    if (IS_NUM(PK(0)) && IS_NUM(PK(1))) {
      double b = PK(0).as.n, a = PK(1).as.n; if (b == 0) RT("القسمة على صفر");
      double r = fmod(a, b); if (r != 0 && ((r < 0) != (b < 0))) r += b;
      sp--; sp[-1].as.n = r; NEXT();
    }
    RT("لا يمكن حساب الباقي بين '%s' و '%s'", type_name(PK(1)), type_name(PK(0)));
  }
  CASE(OP_POW) {
    if (IS_NUM(PK(0)) && IS_NUM(PK(1))) { sp--; sp[-1].as.n = sol_pow(sp[-1].as.n, sp[0].as.n); NEXT(); }
    RT("لا يمكن الرفع لأس بين '%s' و '%s'", type_name(PK(1)), type_name(PK(0)));
  }
  CASE(OP_IDIV) {
    if (IS_NUM(PK(0)) && IS_NUM(PK(1))) {
      if (PK(0).as.n == 0) RT("القسمة على صفر");
      sp--; sp[-1].as.n = floor(sp[-1].as.n / sp[0].as.n); NEXT();
    }
    RT("لا يمكن القسمة الصحيحة بين '%s' و '%s'", type_name(PK(1)), type_name(PK(0)));
  }
#define BITOP(OPN, EXPR) CASE(OPN) { \
    if (IS_NUM(PK(0)) && IS_NUM(PK(1))) { int64_t b = (int64_t)sp[-1].as.n, a = (int64_t)sp[-2].as.n; sp--; sp[-1].as.n = (double)(EXPR); NEXT(); } \
    RT("العمليات على البتات تتطلب أرقاما"); }
  BITOP(OP_BAND, a & b) BITOP(OP_BOR, a | b) BITOP(OP_BXOR, a ^ b) BITOP(OP_SHL, a << (b & 63)) BITOP(OP_SHR, a >> (b & 63))
  CASE(OP_LNEW) PUSHL(OBJV(new_list())); NEXT();
  CASE(OP_LPUSH) { Value v = POPL(); varr_push(&AS_LIST(PK(0))->a, v); NEXT(); }
  CASE(OP_LEXT) {
    Value v = POPL(); List *dst = AS_LIST(PK(0));
    if (IS_T(v, O_LIST)) { List *l = AS_LIST(v); for (int i = 0; i < l->a.n; i++) varr_push(&dst->a, l->a.v[i]); }
    else if (IS_T(v, O_STR)) { Str *st = AS_STR(v); for (int i = 0; i < st->len;) { int cs = u8_charsize(st->chars + i); varr_push(&dst->a, OBJV(intern(st->chars + i, cs))); i += cs; } }
    else if (IS_T(v, O_MAP)) { Map *mp = AS_MAP(v); for (int i = 0; i < mp->d.n; i++) if (mp->d.e[i].k.t != V_EMPTY) varr_push(&dst->a, mp->d.e[i].k); }
    else RT("لا يمكن نشر قيمة نوعها '%s'", type_name(v));
    NEXT();
  }
  CASE(OP_MEXT) {
    Value v = POPL(); Map *dst = AS_MAP(PK(0));
    if (IS_T(v, O_MAP)) { Map *mp = AS_MAP(v); for (int i = 0; i < mp->d.n; i++) if (mp->d.e[i].k.t != V_EMPTY) dset(&dst->d, mp->d.e[i].k, mp->d.e[i].v); }
    else if (IS_T(v, O_INST)) { Instance *in = AS_INST(v); for (int i = 0; i < in->fields.n; i++) if (in->fields.e[i].k.t != V_EMPTY) dset(&dst->d, in->fields.e[i].k, in->fields.e[i].v); }
    else RT("لا يمكن نشر قاموس من قيمة نوعها '%s'", type_name(v));
    NEXT();
  }
  CASE(OP_MSET) { Value val = POPL(), key = POPL(); dset(&AS_MAP(PK(0))->d, key, val); NEXT(); }
  CASE(OP_CALLV) {
    List *l = AS_LIST(POPL()); int n = l->a.n;
    if (n > 250 || sp + n > vm.stack_end - 600) RT("وسائط كثيرة جدا في الانتشار");
    for (int i = 0; i < n; i++) PUSHL(l->a.v[i]);
    SAVE(); SYNC(); if (vm.gc_pending) collect();
    if (!call_value(vm.sp[-1 - n], n)) goto throw_exc;
    RELOAD(); LOAD(); NEXT();
  }
  CASE(OP_INVOKEV) {
    Str *name = AS_STR(CONSTV()); List *l = AS_LIST(POPL()); int n = l->a.n;
    if (n > 250 || sp + n > vm.stack_end - 600) RT("وسائط كثيرة جدا في الانتشار");
    for (int i = 0; i < n; i++) PUSHL(l->a.v[i]);
    SAVE(); SYNC(); if (!invoke(name, n)) goto throw_exc;
    RELOAD(); LOAD(); NEXT();
  }
  CASE(OP_JNIL) { uint16_t off = READ16(); if (IS_NIL(PK(0))) ip += off; NEXT(); }
  CASE(OP_NOT) sp[-1] = BOOLV(falsey(sp[-1])); NEXT();
  CASE(OP_NEG) if (!IS_NUM(PK(0))) RT("لا يمكن السالب على '%s'", type_name(PK(0))); sp[-1].as.n = -sp[-1].as.n; NEXT();
  CASE(OP_CONCAT) { SAVE(); SYNC(); Str *r = concat_top2(); vm.sp -= 2; push(OBJV(r)); LOAD(); NEXT(); }
  CASE(OP_JUMP) { uint16_t off = READ16(); ip += off; NEXT(); }
  CASE(OP_JIF) { uint16_t off = READ16(); if (falsey(PK(0))) ip += off; NEXT(); }
  CASE(OP_JIT) { uint16_t off = READ16(); if (!falsey(PK(0))) ip += off; NEXT(); }
  CASE(OP_JNN) { uint16_t off = READ16(); if (!IS_NIL(PK(0))) ip += off; NEXT(); }
  CASE(OP_LOOP) {
    uint16_t off = READ16();
#if SOLAN_JIT
    if (g_jit && !vm.dbg_mode) {
      Fun *jf = frame->clo->f;
      if (!jf->lc) jf->lc = calloc((size_t)jf->ch.n + 1, sizeof(uint16_t));
      uint16_t *cnt = &jf->lc[ip - jf->ch.code], cv = *cnt;
      if (cv < JIT_TH) *cnt = cv + 1;
      else if (cv != JIT_OFF) {
        JitRegion *jr;
        if (cv == JIT_TH) { jr = jit_compile(jf, ip - off, ip, frame->clo, frame->slots, sp); *cnt = jr ? JIT_READY : JIT_OFF; }
        else jr = jit_find(jf, ip);
        if (jr) {
          JitCtx cx; cx.slots = frame->slots; cx.sp = sp; cx.clo = frame->clo; cx.ip = ip - off; cx.code = 0;
          jr->fn(&cx);
          sp = cx.sp; ip = (uint8_t *)cx.ip;
          if (cx.code != 0 && ++jr->deopts > JIT_MAX_DEOPT) *cnt = JIT_OFF;
          NEXT();
        }
      }
    }
#endif
    ip -= off; if (vm.gc_pending) { SAVE(); SYNC(); collect(); } NEXT();
  }
  CASE(OP_JNE_EMPTY) { uint8_t slot = READ8(); uint16_t off = READ16(); if (frame->slots[slot].t != V_EMPTY) ip += off; NEXT(); }
  CASE(OP_CALL) {
    int argc = READ8();
    if (vm.gc_pending) { SAVE(); SYNC(); collect(); }
    Value callee = sp[-1 - argc];
    if (IS_T(callee, O_CLO)) {
      Closure *c = AS_CLO(callee); Fun *f = c->f;
      if (!f->variadic && !f->kind && argc == f->arity && vm.fcount < vm.frames_max && sp < vm.stack_end - 600) {
#if SOLAN_JIT
        if (f->fjst != 2 && g_jit && !vm.dbg_mode && (f->fjst == 1 || (f->fjst == 0 && ++f->fjcnt >= FJ_TH && fj_compile(c)))) {
          double fjr;
          if (fj_run(c, sp - argc, argc, &fjr)) { sp -= argc; sp[-1] = NUMV(fjr); NEXT(); }
          if (f->fj && f->fj->bails > 6) f->fjst = 2;
        }
#endif
        SAVE(); Frame *fr = &vm.frames[vm.fcount++];
        fr->clo = c; fr->ip = f->ch.code; fr->slots = sp - argc - 1;
        frame = fr; ip = fr->ip; kst = f->ch.consts.v; gcp = f->gc; NEXT();
      }
    }
    SAVE(); SYNC();
    if (!call_value(callee, argc)) goto throw_exc;
    RELOAD(); LOAD(); NEXT();
  }
  CASE(OP_INVOKE) {
    int ci = READ16(); Str *name = AS_STR(kst[ci]); int argc = READ8();
    Value recv = sp[-1 - argc];
    if (IS_OBJ(recv) && !vm.gc_pending) {
      int *mp = &gcp[frame->clo->f->ch.consts.n + ci];
      if (recv.as.o->t == O_INST) {
        Instance *in = AS_INST(recv);
        if (!in->fnf || dfind(&in->fields, OBJV(name), name->hash) < 0) {
          Dict *md = &in->cls->methods; int mi = *mp; Value m;
          if (mi >= 0 && mi < md->n && md->e[mi].k.t == V_OBJ && md->e[mi].k.as.o == (Obj *)name) m = md->e[mi].v;
          else { int x = dfind(md, OBJV(name), name->hash); if (x >= 0) { *mp = x; m = md->e[x].v; } else m = NIL; }
          if (IS_T(m, O_CLO)) {
            Closure *c = AS_CLO(m); Fun *f = c->f;
            if (!f->variadic && !f->kind && argc == f->arity && vm.fcount < vm.frames_max && sp < vm.stack_end - 600) {
              SAVE(); Frame *fr = &vm.frames[vm.fcount++];
              fr->clo = c; fr->ip = f->ch.code; fr->slots = sp - argc - 1;
              frame = fr; ip = fr->ip; kst = f->ch.consts.v; gcp = f->gc; NEXT();
            }
          }
        }
      } else if (recv.as.o->t == O_LIST) {
        int mid = *mp;
        if (mid == 0) { Value idv; if (dget(&vm.mid, OBJV(name), &idv)) { mid = -((int)idv.as.n + 1); *mp = mid; } }
        if (mid == -(M_PUSH + 1) && argc == 1) { varr_push(&AS_LIST(recv)->a, sp[-1]); sp--; NEXT(); }
        if (mid == -(M_LEN + 1) && argc == 0) { sp[-1] = NUMV(AS_LIST(recv)->a.n); NEXT(); }
      }
    }
    SAVE(); SYNC(); if (vm.gc_pending) collect();
    if (!invoke(name, argc)) goto throw_exc;
    RELOAD(); LOAD(); NEXT();
  }
  CASE(OP_SUPER_INVOKE) {
    Str *name = AS_STR(CONSTV()); int argc = READ8(); SAVE(); Class *sup = AS_CLS(POPL()); SYNC();
    if (!invoke_from_class(sup, name, argc)) goto throw_exc;
    RELOAD(); LOAD(); NEXT();
  }
  CASE(OP_CLOSURE) {
    Fun *f = AS_FUN(CONSTV()); Closure *c = new_closure(f, frame->clo->globals); PUSHL(OBJV(c));
    for (int i = 0; i < f->upc; i++) {
      uint8_t isl = READ8(), idx = READ8();
      c->upv[i] = isl ? capture_upvalue(frame->slots + idx) : frame->clo->upv[idx];
    }
    NEXT();
  }
  CASE(OP_CLOSE_UPV) close_upvalues(sp - 1); sp--; NEXT();
  CASE(OP_RETURN) {
    Value result = POPL();
    while (vm.hcount > 0 && vm.handlers[vm.hcount - 1].fi >= vm.fcount - 1) vm.hcount--;
    if (vm.open) close_upvalues(frame->slots);
    vm.fcount--; sp = frame->slots; PUSHL(result);
    if (vm.fcount == base) { SYNC(); return 0; }
    RELOAD(); NEXT();
  }
  CASE(OP_CLASS) PUSHL(OBJV(new_class(AS_STR(CONSTV())))); NEXT();
  CASE(OP_INHERIT) {
    Value sup = PK(1); if (!IS_T(sup, O_CLS)) RT("الصنف الأب يجب أن يكون صنفا");
    Class *sub = AS_CLS(PK(0)), *s = AS_CLS(sup);
    for (int i = 0; i < s->methods.n; i++) if (s->methods.e[i].k.t != V_EMPTY) dset(&sub->methods, s->methods.e[i].k, s->methods.e[i].v);
    for (int i = 0; i < s->statics.n; i++) if (s->statics.e[i].k.t != V_EMPTY) dset(&sub->statics, s->statics.e[i].k, s->statics.e[i].v);
    sub->super = s; sp--; NEXT();
  }
  CASE(OP_METHOD) { Str *name = AS_STR(CONSTV()); dset(&AS_CLS(PK(1))->methods, OBJV(name), PK(0)); sp--; NEXT(); }
  CASE(OP_LIST) {
    int n = READ16(); List *l = new_list();
    if (n > 0) { l->a.v = xalloc(sizeof(Value) * (size_t)n); l->a.cap = n; }
    for (int i = 0; i < n; i++) varr_push(&l->a, sp[-n + i]);
    sp -= n; PUSHL(OBJV(l)); NEXT();
  }
  CASE(OP_MAP) {
    int n = READ16(); Map *m = new_map();
    for (int i = 0; i < n; i++) dset(&m->d, sp[-2 * n + 2 * i], sp[-2 * n + 2 * i + 1]);
    sp -= 2 * n; PUSHL(OBJV(m)); NEXT();
  }
  CASE(OP_ITERPREP) {
    int nv = READ8(); Value c = PK(2);
    if (IS_T(c, O_STR)) {
      Str *s = AS_STR(c); List *l = new_list();
      for (int i = 0; i < s->len;) { int cs = u8_charsize(s->chars + i); varr_push(&l->a, OBJV(intern(s->chars + i, cs))); i += cs; }
      sp[-3] = OBJV(l); sp[-1] = NUMV(nv == 2 ? 1 : 0);
    } else if (IS_T(c, O_MAP)) {
      Map *mp = AS_MAP(c); List *l = new_list();
      for (int i = 0; i < mp->d.n; i++) {
        if (mp->d.e[i].k.t == V_EMPTY) continue;
        if (nv == 2) { List *pr = new_list(); varr_push(&pr->a, mp->d.e[i].k); varr_push(&pr->a, mp->d.e[i].v); varr_push(&l->a, OBJV(pr)); }
        else varr_push(&l->a, mp->d.e[i].k);
      }
      sp[-3] = OBJV(l); sp[-1] = NUMV(nv == 2 ? 2 : 0);
    } else if (IS_T(c, O_LIST) || IS_NUM(c) || IS_T(c, O_CORO)) sp[-1] = NUMV(nv == 2 ? 1 : 0);
    else RT("لا يمكن التكرار على قيمة نوعها '%s'", type_name(c));
    NEXT();
  }
  CASE(OP_FOREACH) {
    uint8_t b = READ8(), nv = READ8(); uint16_t off = READ16(); Value *s = frame->slots + b;
    Value c = s[0]; int i = (int)s[1].as.n, mode = (int)s[2].as.n;
    if (IS_T(c, O_LIST)) {
      List *l = AS_LIST(c); if (i >= l->a.n) { ip += off; NEXT(); }
      Value x = l->a.v[i]; s[1] = NUMV(i + 1);
      if (mode == 2 && IS_T(x, O_LIST) && AS_LIST(x)->a.n == 2) { PUSHL(AS_LIST(x)->a.v[0]); PUSHL(AS_LIST(x)->a.v[1]); }
      else if (nv == 2) { PUSHL(NUMV(i)); PUSHL(x); } else PUSHL(x);
    } else if (IS_T(c, O_CORO)) {
      Value outv; SAVE(); SYNC(); int r = coro_resume(AS_CORO(c), NIL, false, &outv); if (r < 0) goto throw_exc; LOAD();
      if (r == 1) { ip += off; NEXT(); }
      s[1] = NUMV(i + 1); if (nv == 2) PUSHL(NUMV(i)); PUSHL(outv);
    } else {
      if (i >= c.as.n) { ip += off; NEXT(); }
      s[1] = NUMV(i + 1); PUSHL(NUMV(i)); if (nv == 2) PUSHL(NUMV(i));
    }
    NEXT();
  }
  CASE(OP_TRY) {
    uint16_t off = READ16(); if (vm.hcount >= vm.hmax) RT("حاول متداخلة أكثر من اللازم");
    vm.handlers[vm.hcount].fi = vm.fcount - 1; vm.handlers[vm.hcount].ip = ip + off; vm.handlers[vm.hcount].sp = sp; vm.hcount++; NEXT();
  }
  CASE(OP_ENDTRY) if (vm.hcount > 0) vm.hcount--; NEXT();
  CASE(OP_THROW) vm.exc = POPL(); SAVE(); SYNC(); goto throw_exc;
  CASE(OP_IMPORT) {
    Str *path = AS_STR(CONSTV()); SAVE(); SYNC(); Value m;
    if (!do_import(path, &m)) goto throw_exc;
    push(m); LOAD(); NEXT();
  }
  CASE(OP_STATIC) { Str *name = AS_STR(CONSTV()); dset(&AS_CLS(PK(1))->statics, OBJV(name), PK(0)); sp--; NEXT(); }
  CASE(OP_LINE) { int ln = READ16(); if (vm.dbg_mode) { SAVE(); SYNC(); debug_line(ln, frame); } NEXT(); }
  CASE(OP_YIELD) {
    if (!(vm.cur_co && base == 0)) RT("انتج خارج كوروتين (أو داخل دالة أصلية)");
    vm.yielded = PK(0); sp--; SAVE(); SYNC(); return 2;
  }
  CASE(OP_AWAIT) {
    Value v = PK(0);
    if (IS_T(v, O_INST) && AS_INST(v)->cls == vm.promise_cls) {
      Value dn, f2, val; Dict *fl = &AS_INST(v)->fields; dget(fl, KN("منجز"), &dn);
      if (!falsey(dn)) {
        dget(fl, KN("فشل"), &f2);
        if (!falsey(f2)) { dget(fl, KN("خطأ"), &vm.exc); SAVE(); SYNC(); goto throw_exc; }
        dget(fl, KN("قيمة"), &val); sp[-1] = val; NEXT();
      }
      if (vm.cur_co && base == 0) { vm.yielded = v; sp--; SAVE(); SYNC(); return 2; }
      SAVE(); SYNC(); Value out; if (!call_fn(vm.fn_block, 1, &v, &out)) goto throw_exc;
      LOAD(); sp[-1] = out; NEXT();
    }
    NEXT();
  }
  CASE(OP_SLICE) {
    Value out; SAVE(); SYNC(); if (do_slice(PK(2), PK(1), PK(0), &out)) goto throw_exc;
    sp -= 3; PUSHL(out); NEXT();
  }
throw_exc:
  if (vm.hcount == 0) capture_trace();
  if (vm.hcount > 0 && vm.handlers[vm.hcount - 1].fi >= base) {
    Handler h = vm.handlers[--vm.hcount];
    close_upvalues(h.sp); vm.fcount = h.fi + 1; vm.sp = h.sp; push(vm.exc); vm.frames[h.fi].ip = h.ip;
    RELOAD(); LOAD(); NEXT();
  }
  close_upvalues(vm.frames[base].slots); vm.fcount = base; return 1;
}
