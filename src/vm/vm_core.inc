static int map_find_name(Dict *d, Str *name) {
  int x = dfind(d, OBJV(name), name->hash); if (x >= 0) return x;
  bool hi = false; for (int q = 0; q < name->len; q++) if ((unsigned char)name->chars[q] >= 0x80) { hi = true; break; }
  if (!hi || d->live > 512) return -1;
  if (g_mfn_memo.d == d && g_mfn_memo.n == d->n && g_mfn_memo.live == d->live && g_mfn_memo.name == name) return -1;
  for (int i = 0; i < d->n; i++) {
    Value k = d->e[i].k; if (!IS_T(k, O_STR)) continue;
    Str *ks = AS_STR(k); if (ks == name || ks->len > 120) continue;
    char tmp[300]; int nl = norm_into(ks->chars, ks->len, tmp);
    if (nl == name->len && memcmp(tmp, name->chars, nl) == 0) return i;
  }
  g_mfn_memo.d = d; g_mfn_memo.n = d->n; g_mfn_memo.live = d->live; g_mfn_memo.name = name;
  return -1;
}

static bool type_ok(Value v, uint16_t mask, List *info) {
  switch (v.t) {
    case V_NIL: case V_EMPTY: return mask & TY_NIL;
    case V_BOOL: return mask & TY_BOOL;
    case V_NUM: if (mask & TY_NUM) return true; if (mask & TY_INT) { double d = v.as.n; return d == floor(d) && fabs(d) < 9007199254740992.0; } return false;
    default: break;
  }
  switch (v.as.o->t) {
    case O_STR: return mask & TY_STR;
    case O_LIST: return mask & TY_LIST;
    case O_MAP: return mask & TY_MAP;
    case O_FUN: case O_CLO: case O_NAT: case O_BOUND: return mask & TY_FUN;
    case O_CLS: return mask & TY_CLS;
    case O_CORO: return mask & TY_CORO;
    case O_INST:
      if (mask & TY_CLASSNAME) {
        for (Class *k = AS_INST(v)->cls; k; k = k->super)
          for (int i = 2; i < info->a.n; i++) if (IS_OBJ(info->a.v[i]) && (Obj *)k->name == info->a.v[i].as.o) return true;
      }
      return mask & TY_INST;
    default: return false;
  }
}

static int run(int base);

static Coro *new_coro(Closure *f, int argc, Value *slots);

static bool call_sync(int argc);
static int builtin_method(Value r, Str *nm, int argc, Value *a, Value *res);

static bool call_closure(Closure *c, int argc) {
  Fun *f = c->f; int fixed = f->variadic ? f->arity - 1 : f->arity;
  if (argc < f->minar || (!f->variadic && argc > f->arity)) {
    if (f->variadic) rt_error("الدالة '%s' تتوقع %d وسائط على الأقل لكن أُعطيت %d", f->name ? f->name->chars : "مجهولة", f->minar, argc);
    else if (f->minar == f->arity) rt_error("الدالة '%s' تتوقع %d وسائط لكن أُعطيت %d", f->name ? f->name->chars : "مجهولة", f->arity, argc);
    else rt_error("الدالة '%s' تتوقع من %d إلى %d وسائط لكن أُعطيت %d", f->name ? f->name->chars : "مجهولة", f->minar, f->arity, argc);
    return false;
  }
  if (f->kind && !vm.nowrap) {
    Coro *co = new_coro(c, argc, vm.sp - argc - 1);
    vm.sp -= argc + 1;
    if (f->kind == 2) { push(OBJV(co)); return true; }
    push(vm.fn_start); push(OBJV(co));
    return call_sync(1);
  }
  vm.nowrap = false;
  if (vm.fcount >= vm.frames_max || vm.sp > vm.stack_end - 600) { rt_error("تجاوز عمق الاستدعاء (استدعاء ذاتي لا نهائي؟)"); return false; }
  if (f->variadic) {
    List *rest = new_list();
    for (int i = fixed; i < argc; i++) varr_push(&rest->a, vm.sp[-argc + i]);
    if (argc > fixed) { vm.sp -= argc - fixed; argc = fixed; }
    for (; argc < fixed; argc++) push(EMPTYV);
    push(OBJV(rest));
  } else for (; argc < f->arity; argc++) push(EMPTYV);
  Frame *fr = &vm.frames[vm.fcount++];
  fr->clo = c; fr->ip = f->ch.code; fr->slots = vm.sp - f->arity - 1;
  return true;
}

static bool call_value(Value callee, int argc) {
  if (IS_OBJ(callee)) {
    switch (callee.as.o->t) {
      case O_BOUND: { Bound *b = AS_BOUND(callee); vm.sp[-argc - 1] = b->recv; return call_value(b->method, argc); }
      case O_CLS: {
        Class *k = AS_CLS(callee); vm.sp[-argc - 1] = OBJV(new_instance(k)); Value init;
        if (dget(&k->methods, OBJV(vm.init_str), &init)) return call_closure(AS_CLO(init), argc);
        if (argc != 0) { rt_error("الصنف '%s' لا يقبل وسائط (لا يوجد بناء)", k->name->chars); return false; }
        return true;
      }
      case O_CLO: return call_closure(AS_CLO(callee), argc);
      case O_NAT: {
        Native *n = AS_NAT(callee);
        if (argc < n->minar || (n->maxar >= 0 && argc > n->maxar)) { rt_error("عدد وسائط خاطئ للدالة '%s'", n->name->chars); return false; }
        Value res = NIL; g_cur_native = n; if (!n->fn(argc, vm.sp - argc, &res)) return false;
        vm.sp -= argc + 1; push(res); return true;
      }
      default: break;
    }
  }
  rt_error("لا يمكن استدعاء قيمة من نوع '%s'", type_name(callee)); return false;
}
static bool call_sync(int argc) {
  if (vm.nest >= NEST_MAX) { rt_error("تجاوز عمق الاستدعاءات المتداخلة"); return false; }
  int base = vm.fcount;
  if (!call_value(peek(argc), argc)) return false;
  if (vm.fcount > base) { vm.nest++; int r = run(base); vm.nest--; if (r != 0) return false; }
  return true;
}
static bool call_fn(Value f, int argc, Value *argv, Value *out) {
  push(f); for (int i = 0; i < argc; i++) push(argv[i]);
  if (!call_sync(argc)) return false;
  *out = pop(); return true;
}

#define CO_STACK 4096
#define CO_FRAMES 160
#define CO_HANDLERS 24
static Coro *new_coro(Closure *f, int argc, Value *slots) {
  Coro *c = (Coro *)newobj(sizeof(Coro), O_CORO);
  c->stack = xalloc(sizeof(Value) * CO_STACK); c->stack_end = c->stack + CO_STACK; c->sp = c->stack;
  c->frames = xalloc(sizeof(Frame) * CO_FRAMES); c->fcount = 0; c->frames_max = CO_FRAMES;
  c->handlers = xalloc(sizeof(Handler) * CO_HANDLERS); c->hcount = 0; c->hmax = CO_HANDLERS; c->open = NULL; c->state = 0; c->fn = f; c->args = NULL;
  List *l = new_list(); c->args = l; for (int i = 0; i <= argc; i++) varr_push(&l->a, slots[i]);
  return c;
}
static void ctx_save(Ctx *c) {
  c->stack = vm.stack; c->stack_end = vm.stack_end; c->sp = vm.sp; c->frames = vm.frames; c->fcount = vm.fcount; c->frames_max = vm.frames_max;
  c->handlers = vm.handlers; c->hcount = vm.hcount; c->hmax = vm.hmax; c->open = vm.open; c->cur = vm.cur_co;
}
static void ctx_load(const Ctx *c) {
  vm.stack = c->stack; vm.stack_end = c->stack_end; vm.sp = c->sp; vm.frames = c->frames; vm.fcount = c->fcount; vm.frames_max = c->frames_max;
  vm.handlers = c->handlers; vm.hcount = c->hcount; vm.hmax = c->hmax; vm.open = c->open; vm.cur_co = c->cur;
}

static int coro_resume(Coro *co, Value sent, bool isthrow, Value *out) {
  if (co->state == 3) { rt_error("الكوروتين انتهى"); return -1; }
  if (co->state == 2) { rt_error("الكوروتين يعمل حاليا"); return -1; }
  if (vm.nest >= NEST_MAX) { rt_error("تجاوز عمق الاستدعاءات المتداخلة"); return -1; }
  CtxNode node; ctx_save(&node.c); node.next = vm.ctxs; vm.ctxs = &node;
  Ctx cc; cc.stack = co->stack; cc.stack_end = co->stack_end; cc.sp = co->sp; cc.frames = co->frames; cc.fcount = co->fcount; cc.frames_max = co->frames_max;
  cc.handlers = co->handlers; cc.hcount = co->hcount; cc.hmax = co->hmax; cc.open = co->open; cc.cur = co; ctx_load(&cc);
  int prev = co->state; co->state = 2; int rc;
  if (prev == 0) {
    List *al = co->args; for (int i = 0; i < al->a.n; i++) push(al->a.v[i]);
    vm.nowrap = true;
    if (!call_closure(co->fn, al->a.n - 1)) rc = 1; else { vm.nest++; rc = run(0); vm.nest--; }
    vm.nowrap = false;
  } else {
    if (isthrow) { vm.exc = sent; vm.pending_throw = true; } else push(sent);
    vm.nest++; rc = run(0); vm.nest--;
  }
  int result;
  if (rc == 2) { co->state = 1; *out = vm.yielded; co->sp = vm.sp; co->fcount = vm.fcount; co->hcount = vm.hcount; co->open = vm.open; result = 0; }
  else if (rc == 0) { co->state = 3; *out = vm.sp[-1]; result = 1; }
  else { co->state = 3; result = -1; }
  ctx_load(&node.c); vm.ctxs = node.next; return result;
}
static bool call_method0(Value recv, Closure *m, Value *out) {
  if (vm.nest >= NEST_MAX) return false;
  Value *saved = vm.sp; int hc = vm.hcount; Value exc = vm.exc;
  push(recv); int base = vm.fcount;
  if (!call_closure(m, 0)) { vm.sp = saved; vm.exc = exc; return false; }
  if (vm.fcount == base) { *out = pop(); return true; }
  vm.nest++; int r = run(base); vm.nest--;
  if (r != 0) { vm.sp = saved; vm.hcount = hc; vm.exc = exc; return false; }
  *out = pop(); return true;
}
static bool invoke_from_class(Class *k, Str *name, int argc) {
  Value m;
  if (!dget(&k->methods, OBJV(name), &m)) { rt_error("الدالة '%s' غير معرفة في الصنف", name->chars); return false; }
  return call_closure(AS_CLO(m), argc);
}
static bool invoke(Str *name, int argc) {
  Value recv = peek(argc);
  if (IS_T(recv, O_CLS)) {
    Value f; if (dget(&AS_CLS(recv)->statics, OBJV(name), &f)) { vm.sp[-argc - 1] = f; return call_value(f, argc); }
    rt_error("الصنف '%s' لا يملك دالة ساكنة '%s'", AS_CLS(recv)->name->chars, name->chars); return false;
  }
  if (IS_T(recv, O_INST)) {
    Instance *in = AS_INST(recv); Value f;
    if (dget(&in->fields, OBJV(name), &f)) { vm.sp[-argc - 1] = f; return call_value(f, argc); }
    if (dget(&in->cls->methods, OBJV(name), &f)) return call_closure(AS_CLO(f), argc);
    rt_error("الكائن من صنف '%s' لا يملك '%s'", in->cls->name->chars, name->chars); return false;
  }
  if (IS_T(recv, O_MAP)) {
    Value f; int mx = map_find_name(&AS_MAP(recv)->d, name);
    if (mx >= 0 && (f = AS_MAP(recv)->d.e[mx].v, true) && IS_OBJ(f) && f.as.o->t != O_STR && f.as.o->t != O_LIST && f.as.o->t != O_MAP) {
      vm.sp[-argc - 1] = f; return call_value(f, argc);
    }
  }
  Value res; int r = builtin_method(recv, name, argc, vm.sp - argc, &res);
  if (r == 2) { char sg[300]; suggest(sg, sizeof sg, name, &vm.mid, NULL, NULL); rt_error("لا توجد دالة '%s' للنوع '%s'%s", name->chars, type_name(recv), sg); return false; }
  if (r == 1) return false;
  vm.sp -= argc + 1; push(res); return true;
}
static bool call_method_prop(Value recv, Closure *m, Value *out) {
  if (vm.nest >= NEST_MAX) { rt_error("تجاوز عمق الاستدعاءات المتداخلة"); return false; }
  push(recv); int base = vm.fcount; if (!call_closure(m, 0)) return false;
  if (vm.fcount == base) { *out = pop(); return true; }
  vm.nest++; int r = run(base); vm.nest--; if (r != 0) return false; *out = pop(); return true;
}

static int get_prop(Value recv, Str *name, Value *out) {
  if (IS_T(recv, O_CLS)) {
    if (dget(&AS_CLS(recv)->statics, OBJV(name), out)) return 0;
    rt_error("الصنف '%s' لا يملك عضوا ساكنا '%s'", AS_CLS(recv)->name->chars, name->chars); return 1;
  }
  if (IS_T(recv, O_INST)) {
    Instance *in = AS_INST(recv); Value m;
    if (dget(&in->fields, OBJV(name), out)) return 0;
    if (dget(&in->cls->methods, OBJV(name), &m)) {
      if (AS_CLO(m)->f->getter) return call_method_prop(recv, AS_CLO(m), out) ? 0 : 1;
      *out = OBJV(new_bound(recv, m)); return 0;
    }
    { char sg[300]; suggest(sg, sizeof sg, name, &in->fields, &in->cls->methods, NULL); rt_error("الكائن من صنف '%s' لا يملك '%s'%s", in->cls->name->chars, name->chars, sg); } return 1;
  }
  if (IS_T(recv, O_MAP)) {
    Map *m = AS_MAP(recv);
    int x = map_find_name(&m->d, name);
    if (x >= 0) { *out = m->d.e[x].v; return 0; }
    if (name == vm.s_len) { *out = NUMV(m->d.live); return 0; }
    if (m->module) { char sg[300]; suggest(sg, sizeof sg, name, &m->d, NULL, NULL); rt_error("الوحدة لا تحتوي على '%s'%s", name->chars, sg); return 1; }
    { char sg[300]; suggest(sg, sizeof sg, name, &m->d, NULL, NULL); rt_error("المفتاح '%s' غير موجود في القاموس%s", name->chars, sg); }
    return 1;
  }
  if (name == vm.s_len) {
    if (IS_T(recv, O_STR)) { *out = NUMV(u8_len(AS_STR(recv)->chars, AS_STR(recv)->len)); return 0; }
    if (IS_T(recv, O_LIST)) { *out = NUMV(AS_LIST(recv)->a.n); return 0; }
  }
  rt_error("لا يمكن قراءة '%s' من قيمة نوعها '%s'", name->chars, type_name(recv)); return 1;
}
static bool to_index(Value idx, int len, int *out) {
  if (!IS_NUM(idx)) return false;
  double d = idx.as.n; if (d != floor(d)) return false;
  if (d < 0) d += len;
  if (d < 0 || d >= len) return false;
  *out = (int)d; return true;
}
static int get_index(Value obj, Value idx, Value *out) {
  if (IS_T(obj, O_LIST)) {
    List *l = AS_LIST(obj); int i;
    if (!to_index(idx, l->a.n, &i)) { rt_error("فهرس القائمة غير صالح أو خارج النطاق (الطول %d)", l->a.n); return 1; }
    *out = l->a.v[i]; return 0;
  }
  if (IS_T(obj, O_STR)) {
    Str *s = AS_STR(obj); int n = u8_len(s->chars, s->len), i;
    if (!to_index(idx, n, &i)) { rt_error("فهرس النص غير صالح أو خارج النطاق (الطول %d)", n); return 1; }
    int o = u8_off(s->chars, s->len, i); *out = OBJV(intern(s->chars + o, u8_charsize(s->chars + o))); return 0;
  }
  if (IS_T(obj, O_MAP)) {
    if (dget(&AS_MAP(obj)->d, idx, out)) return 0;
    Str *ks = tostr(idx); rt_error("المفتاح '%s' غير موجود في القاموس", ks->chars); return 1;
  }
  if (IS_T(obj, O_INST)) {
    if (dget(&AS_INST(obj)->fields, idx, out)) return 0;
    Str *ks = tostr(idx); rt_error("الكائن من صنف '%s' لا يملك '%s'", AS_INST(obj)->cls->name->chars, ks->chars); return 1;
  }
  rt_error("لا يمكن الفهرسة على قيمة نوعها '%s'", type_name(obj)); return 1;
}
static int set_index(Value obj, Value idx, Value v) {
  if (IS_T(obj, O_LIST)) {
    List *l = AS_LIST(obj); int i;
    if (!to_index(idx, l->a.n, &i)) { rt_error("فهرس القائمة غير صالح أو خارج النطاق (الطول %d)", l->a.n); return 1; }
    l->a.v[i] = v; return 0;
  }
  if (IS_T(obj, O_MAP)) { dset(&AS_MAP(obj)->d, idx, v); return 0; }
  if (IS_T(obj, O_INST)) { if (CALLABLE_OBJ(v)) AS_INST(obj)->fnf = true; dset(&AS_INST(obj)->fields, idx, v); return 0; }
  rt_error("لا يمكن إسناد فهرس على قيمة نوعها '%s'", type_name(obj)); return 1;
}
static int fast_itoa(double d, char *o) {
  long long v = (long long)d; char t[24]; int n = 0, k = 0; bool neg = v < 0; unsigned long long u = neg ? 0ULL - (unsigned long long)v : (unsigned long long)v;
  do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
  if (neg) o[k++] = '-';
  while (n) o[k++] = t[--n];
  return k;
}
static bool vm_add(void);
static Str *concat_top2(void) {

  {
    Value va = vm.sp[-2], vb = vm.sp[-1]; char ta[24], tb[24]; const char *pa = NULL, *pb = NULL; int la = 0, lb = 0;
    if (IS_T(va, O_STR)) { pa = AS_STR(va)->chars; la = AS_STR(va)->len; }
    else if (!g_digits && IS_NUM(va) && va.as.n == floor(va.as.n) && fabs(va.as.n) < 1e15) { la = fast_itoa(va.as.n, ta); pa = ta; }
    if (IS_T(vb, O_STR)) { pb = AS_STR(vb)->chars; lb = AS_STR(vb)->len; }
    else if (!g_digits && IS_NUM(vb) && vb.as.n == floor(vb.as.n) && fabs(vb.as.n) < 1e15) { lb = fast_itoa(vb.as.n, tb); pb = tb; }
    if (pa && pb && la + lb <= 512) {
      char buf[520]; memcpy(buf, pa, la); memcpy(buf + la, pb, lb);
      return intern(buf, la + lb);
    }
  }
  Str *x = tostr(vm.sp[-2]); push(OBJV(x));
  Str *y = tostr(vm.sp[-2]); push(OBJV(y));
  if ((int64_t)x->len + y->len > (int64_t)1 << 30) { fprintf(stderr, "خطأ: النص أكبر من الحد المسموح\n"); exit(1); }
  int n = x->len + y->len;
  Str *r = intern_alloc(n);
  memcpy(r->chars, x->chars, x->len); memcpy(r->chars + x->len, y->chars, y->len); r->chars[n] = 0;
  r = intern_finish(r); vm.sp -= 2; return r;
}

static bool vm_add_append(void) {
  Value b = vm.sp[-1], a = vm.sp[-2];
  if (IS_T(a, O_STR) && AS_STR(a)->len >= SB_MIN_LEN && IS_T(b, O_STR)) {
    const char *pb = AS_STR(b)->chars; int lb = AS_STR(b)->len;
    if ((int64_t)AS_STR(a)->len + lb > (int64_t)1 << 30) { fprintf(stderr, "خطأ: النص أكبر من الحد المسموح\n"); exit(1); }
    Str *r = lb == 0 ? AS_STR(a) : str_extend(AS_STR(a), pb, lb); vm.sp -= 2; push(OBJV(r)); return true;
  }
  return vm_add();
}
static bool vm_add(void) {
  Value b = peek(0), a = peek(1);
  if (IS_NUM(a) && IS_NUM(b)) { vm.sp -= 2; push(NUMV(a.as.n + b.as.n)); return true; }
  if (IS_T(a, O_STR) && IS_T(b, O_STR)) { Str *r = concat_top2(); vm.sp -= 2; push(OBJV(r)); return true; }
  if (IS_T(a, O_STR) || IS_T(b, O_STR)) { rt_error("لا يمكن الجمع بين '%s' و '%s' (لا تحويل ضمني بين النص والأنواع الأخرى؛ استعمل ${} أو نص())", type_name(a), type_name(b)); return false; }
  if (IS_T(a, O_LIST) && IS_T(b, O_LIST)) {
    List *l = new_list(), *x = AS_LIST(a), *y = AS_LIST(b);
    for (int i = 0; i < x->a.n; i++) varr_push(&l->a, x->a.v[i]);
    for (int i = 0; i < y->a.n; i++) varr_push(&l->a, y->a.v[i]);
    vm.sp -= 2; push(OBJV(l)); return true;
  }
  rt_error("لا يمكن الجمع بين '%s' و '%s'", type_name(a), type_name(b)); return false;
}
static bool vm_mul(void) {
  Value b = peek(0), a = peek(1);
  if (IS_NUM(a) && IS_NUM(b)) { vm.sp -= 2; push(NUMV(a.as.n * b.as.n)); return true; }
  if (IS_T(a, O_STR) && IS_NUM(b) && b.as.n >= 0 && b.as.n < 1e8) {
    if (b.as.n * (double)AS_STR(a)->len > 1e9) { rt_error("نتيجة التكرار كبيرة جدا"); return false; }
    Str *s = AS_STR(a); int n = (int)b.as.n; Buf buf; buf_init(&buf);
    for (int i = 0; i < n; i++) buf_add(&buf, s->chars, s->len);
    Str *r = intern(buf_cstr(&buf), buf.n); free(buf.p); vm.sp -= 2; push(OBJV(r)); return true;
  }
  if (IS_T(a, O_LIST) && IS_NUM(b) && b.as.n >= 0 && b.as.n < 1e7) {
    if (b.as.n * (double)AS_LIST(a)->a.n > 1e8) { rt_error("نتيجة التكرار كبيرة جدا"); return false; }
    List *l = new_list(), *x = AS_LIST(a);
    for (int k = 0; k < (int)b.as.n; k++) for (int i = 0; i < x->a.n; i++) varr_push(&l->a, x->a.v[i]);
    vm.sp -= 2; push(OBJV(l)); return true;
  }
  rt_error("لا يمكن الضرب بين '%s' و '%s'", type_name(a), type_name(b)); return false;
}

static int vm_cmp(Value a, Value b) {
  if (IS_NUM(a) && IS_NUM(b)) return a.as.n < b.as.n ? -1 : a.as.n > b.as.n ? 1 : 0;
  if (IS_T(a, O_STR) && IS_T(b, O_STR)) {
    Str *x = AS_STR(a), *y = AS_STR(b); int m = x->len < y->len ? x->len : y->len; int c = memcmp(x->chars, y->chars, m);
    if (c) return c < 0 ? -1 : 1; return x->len < y->len ? -1 : x->len > y->len ? 1 : 0;
  }
  rt_error("لا يمكن المقارنة بين '%s' و '%s'", type_name(a), type_name(b)); return 2;
}
static int vm_in(Value x, Value coll, bool *res) {
  if (IS_T(coll, O_STR)) {
    if (!IS_T(x, O_STR)) { rt_error("البحث داخل النص يتطلب نصا"); return 1; }
    Str *s = AS_STR(coll), *t = AS_STR(x); *res = t->len == 0 || memmem(s->chars, s->len, t->chars, t->len) != NULL; return 0;
  }
  if (IS_T(coll, O_LIST)) {
    List *l = AS_LIST(coll); *res = false;
    for (int i = 0; i < l->a.n; i++) if (values_equal(l->a.v[i], x)) { *res = true; break; }
    return 0;
  }
  if (IS_T(coll, O_MAP)) { Value t; *res = dget(&AS_MAP(coll)->d, x, &t); return 0; }
  rt_error("العامل 'في' لا يعمل مع '%s'", type_name(coll)); return 1;
}
static bool do_import(Str *path, Value *out);

static void dbg_print_src(int ln) {
  if (!vm.dbg_src) return; const char *p = vm.dbg_src; for (int i = 1; i < ln && *p; i++) { const char *nl = strchr(p, '\n'); if (!nl) { p = ""; break; } p = nl + 1; }
  const char *e = strchr(p, '\n'); fprintf(stderr, "  %d| %.*s\n", ln, e ? (int)(e - p) : (int)strlen(p), p);
}
static bool dbg_lookup(Frame *fr, Str *name, Value *out) {
  Fun *f = fr->clo->f; int h = (int)(vm.sp - fr->slots);
  for (int i = f->ndbg - 1; i >= 0; i--) if (f->dbgname[i] == name && f->dbgslot[i] < h) { *out = fr->slots[f->dbgslot[i]]; return true; }
  return dget(&fr->clo->globals->d, OBJV(name), out) || dget(&vm.builtins, OBJV(name), out);
}

static void debug_line(int ln, Frame *fr) {
  const char *fname = fr->clo->f->name ? fr->clo->f->name->chars : "<الملف>";
  if (vm.dbg_mode == 1) { fprintf(stderr, "[سطر %d] %s\n", ln, fname); return; }
  bool stop = vm.dbg_step; for (int i = 0; i < vm.nbps; i++) if (vm.bps[i] == ln) stop = true;
  if (!stop) return;
  if (!vm.dbg_in) { vm.dbg_in = fopen(os_devtty(), "r"); if (!vm.dbg_in) vm.dbg_in = stdin; }
  fprintf(stderr, "\n(مصحح) في %s\n", fname); dbg_print_src(ln);
  for (;;) {
    fprintf(stderr, "(مصحح) > "); fflush(stderr); char line[256];
    if (!fgets(line, sizeof line, vm.dbg_in)) { vm.dbg_mode = 0; return; }
    size_t n = strlen(line); while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if (!n || !strcmp(line, "s") || !strcmp(line, "خ")) { vm.dbg_step = true; return; }
    if (!strcmp(line, "c") || !strcmp(line, "ن")) { vm.dbg_step = false; return; }
    if (!strcmp(line, "q") || !strcmp(line, "خروج")) { fflush(stdout); exit(0); }
    if ((line[0] == 'p' || !strncmp(line, "ط", 2)) && strchr(line, ' ')) {
      Value v; Str *nm = normstr(strchr(line, ' ') + 1);
      if (dbg_lookup(fr, nm, &v)) { Buf b; buf_init(&b); repr(v, &b, 0, true); fprintf(stderr, "%s\n", buf_cstr(&b)); free(b.p); } else fprintf(stderr, "غير معرّف\n");
      continue;
    }
    if (!strcmp(line, "l") || !strcmp(line, "ل")) {
      Fun *f = fr->clo->f; int h = (int)(vm.sp - fr->slots);
      for (int i = 0; i < f->ndbg; i++) if (f->dbgslot[i] < h && f->dbgname[i]->chars[0] != '$') { Buf b; buf_init(&b); repr(fr->slots[f->dbgslot[i]], &b, 0, true); fprintf(stderr, "  %s = %s\n", f->dbgname[i]->chars, buf_cstr(&b)); free(b.p); }
      continue;
    }
    if (!strcmp(line, "bt") || !strcmp(line, "ت")) {
      for (int i = vm.fcount - 1; i >= 0; i--) { Fun *f = vm.frames[i].clo->f; fprintf(stderr, "  #%d %s\n", vm.fcount - 1 - i, f->name ? f->name->chars : "<الملف>"); }
      continue;
    }
    if ((line[0] == 'b' || !strncmp(line, "ق", 2)) && strchr(line, ' ')) { if (vm.nbps < 64) vm.bps[vm.nbps++] = atoi(strchr(line, ' ') + 1); fprintf(stderr, "نقطة توقف %d\n", vm.bps[vm.nbps - 1]); continue; }
    if ((line[0] == 'd' || !strncmp(line, "ح", 2)) && strchr(line, ' ')) { int t = atoi(strchr(line, ' ') + 1), k = 0; for (int i = 0; i < vm.nbps; i++) if (vm.bps[i] != t) vm.bps[k++] = vm.bps[i]; vm.nbps = k; continue; }
    fprintf(stderr, "أوامر: خ/s خطوة | ن/c استمرار | ط/p اسم | ل/l متغيرات | ت/bt مكدس | ق/b سطر | ح/d سطر | خروج/q\n");
  }
}
static void slice_bounds_v(int len, Value s, Value e, int *x, int *y) {
  double a = IS_NUM(s) ? s.as.n : 0, b = IS_NUM(e) ? e.as.n : len;
  if (a < 0) a += len; if (b < 0) b += len; if (a < 0) a = 0; if (b > len) b = len; if (a > len) a = len; if (b < a) b = a;
  *x = (int)a; *y = (int)b;
}
static int do_slice(Value o, Value s, Value e, Value *out) {
  if ((!IS_NIL(s) && !IS_NUM(s)) || (!IS_NIL(e) && !IS_NUM(e))) { rt_error("حدود الشريحة يجب أن تكون أرقاما"); return 1; }
  int x, y;
  if (IS_T(o, O_LIST)) {
    List *l = AS_LIST(o), *r = new_list(); slice_bounds_v(l->a.n, s, e, &x, &y);
    for (int i = x; i < y; i++) varr_push(&r->a, l->a.v[i]);
    *out = OBJV(r); return 0;
  }
  if (IS_T(o, O_STR)) {
    Str *st = AS_STR(o); int n = u8_len(st->chars, st->len); slice_bounds_v(n, s, e, &x, &y);
    int bx = u8_off(st->chars, st->len, x), by = u8_off(st->chars, st->len, y);
    *out = OBJV(intern(st->chars + bx, by - bx)); return 0;
  }
  rt_error("لا يمكن اقتطاع شريحة من '%s'", type_name(o)); return 1;
}
