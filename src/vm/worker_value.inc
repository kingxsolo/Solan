// =====================================================================
//  تسلسل القيم وإرسالها بين العمليات — دعم العمال على كل المنصات
//  worker_value.inc: مُسلسِل ثنائي للقيم (نص/رقم/منطقي/عدم/قائمة/قاموس/
//  إغلاق مع bytecode وقاموس globals) — دلالات مكافئة لـ fork.
//  الوسوم: n عدم، T/F منطقي، d رقم، s نص، L قائمة، M قاموس(معرّف)،
//  m مرجع قاموس سابق، g وحدة أصلية باسمها، C إغلاق، R مرجع إغلاق سابق،
//  u قيمة upvalue.
//  يُضمَّن بعد vm_run.inc وقبل natives_core.inc.
// =====================================================================

static void wv_put32(Buf *b, uint32_t v) { buf_add(b, (char *)&v, 4); }

static bool wv_encode(Value v, Buf *b, Dict *seen_clo, Dict *seen_map);
static bool wv_closure(Value v, Buf *b, Dict *seen_clo, Dict *seen_map);

static void wv_encode_bytes(Buf *b, const char *s, int n) {
  wv_put32(b, (uint32_t)n);
  buf_add(b, s, n);
}

// هل هذا القاموس وحدة معروفة (أصلية/قياسية/مخبأة)؟ إن نعم، سجّل اسمها
static bool wv_module_name(Map *m, Buf *b) {
  for (int i = 0; i < vm.natmods.n; i++) {
    Ent *e = &vm.natmods.e[i];
    if (e->v.t == V_OBJ && e->v.as.o == (Obj *)m) {
      buf_addc(b, 'g'); wv_encode_bytes(b, AS_STR(e->k)->chars, AS_STR(e->k)->len); return true;
    }
  }
  for (int i = 0; i < vm.stdlibd.n; i++) {
    Ent *e = &vm.stdlibd.e[i];
    if (e->v.t == V_OBJ && e->v.as.o == (Obj *)m) {
      buf_addc(b, 'g'); wv_encode_bytes(b, AS_STR(e->k)->chars, AS_STR(e->k)->len); return true;
    }
  }
  for (int i = 0; i < vm.modcache.n; i++) {
    Ent *e = &vm.modcache.e[i];
    if (e->v.t == V_OBJ && e->v.as.o == (Obj *)m) {
      // مفاتيح المخزن قد تحمل بادئة "<stdlib>:" — نشطفها لأن do_import يأخذ الاسم الخام
      const char *kn = AS_STR(e->k)->chars;
      const char *pfx = "<stdlib>:";
      size_t pl = strlen(pfx);
      if (!strncmp(kn, pfx, pl)) kn += pl;
      buf_addc(b, 'g'); wv_encode_bytes(b, kn, (int)strlen(kn)); return true;
    }
  }
  return false;
}

static bool wv_encode(Value v, Buf *b, Dict *seen_clo, Dict *seen_map) {
  switch (v.t) {
    case V_NIL: buf_addc(b, 'n'); return true;
    case V_BOOL: buf_addc(b, v.as.b ? 'T' : 'F'); return true;
    case V_NUM: buf_addc(b, 'd'); { uint64_t u; double d = v.as.n; memcpy(&u, &d, 8); buf_add(b, (char *)&u, 8); } return true;
    default: break;
  }
  if (v.t != V_OBJ) return false;
  Obj *o = v.as.o;
  switch (o->t) {
    case O_STR: { Str *s = (Str *)o; buf_addc(b, 's'); wv_encode_bytes(b, s->chars, s->len); return true; }
    case O_LIST: {
      List *l = (List *)o; buf_addc(b, 'L'); wv_put32(b, (uint32_t)l->a.n);
      for (int i = 0; i < l->a.n; i++) if (!wv_encode(l->a.v[i], b, seen_clo, seen_map)) return false;
      return true;
    }
    case O_MAP: {
      Map *m = (Map *)o;
      if (wv_module_name(m, b)) return true; // وحدة أصلية/قياسية: مرجع باسمها
      // قاموس عام: تعقّب الهوية لدعم المراجع الدائرية
      { Value mv = OBJV(o); Value idv; if (dget(seen_map, mv, &idv)) { buf_addc(b, 'm'); wv_put32(b, (uint32_t)idv.as.n); return true; }
        dset(seen_map, mv, NUMV(seen_map->live)); }
      Value idv2; dget(seen_map, OBJV(o), &idv2);
      buf_addc(b, 'M'); wv_put32(b, (uint32_t)idv2.as.n);
      wv_put32(b, (uint32_t)m->d.n);
      for (int i = 0; i < m->d.n; i++) {
        if (m->d.e[i].k.t == V_EMPTY) continue;
        if (!wv_encode(m->d.e[i].k, b, seen_clo, seen_map) || !wv_encode(m->d.e[i].v, b, seen_clo, seen_map)) return false;
      }
      return true;
    }
    case O_CLO: return wv_closure(v, b, seen_clo, seen_map);
    case O_FUN: {
      // دالة خام (ثابت داخل bytecode) — تُغلَّف لاحقاً بـ OP_CLOSURE وقت التشغيل
      Fun *f = (Fun *)o;
      buf_addc(b, 'P');
      wv_put32(b, (uint32_t)f->arity); wv_put32(b, (uint32_t)f->minar);
      wv_put32(b, (uint32_t)f->upc); wv_put32(b, (uint32_t)f->kind);
      wv_put32(b, (uint32_t)(f->variadic ? 1 : 0)); wv_put32(b, (uint32_t)(f->getter ? 1 : 0));
      wv_encode_bytes(b, f->name ? f->name->chars : "", f->name ? f->name->len : 0);
      wv_put32(b, (uint32_t)f->ch.n); buf_add(b, (const char *)f->ch.code, f->ch.n);
      wv_put32(b, (uint32_t)f->ch.consts.n);
      for (int i = 0; i < f->ch.consts.n; i++) if (!wv_encode(f->ch.consts.v[i], b, seen_clo, seen_map)) return false;
      return true;
    }
    default: return false; // كائنات المستخدم/الكوروتينات/الأصناف لا تُسلسل
  }
}

// ------------------------- تجميد الإغلاق -------------------------
// يجمد إغلاقاً: bytecode كامل + الثوابت + upvalues + قاموس globals كامل.
// هذا يعطي الطرف الآخر بيئة مطابقة للقطة fork لحظة التجميد.
static bool wv_closure(Value v, Buf *b, Dict *seen_clo, Dict *seen_map) {
  Closure *c = AS_CLO(v);
  { Value cv = OBJV((Obj *)c); Value idv; if (dget(seen_clo, cv, &idv)) { buf_addc(b, 'R'); wv_put32(b, (uint32_t)idv.as.n); return true; }
    dset(seen_clo, cv, NUMV(seen_clo->live)); }
  Value idv2; dget(seen_clo, OBJV((Obj *)c), &idv2);
  int myid = (int)idv2.as.n;
  buf_addc(b, 'C'); wv_put32(b, (uint32_t)myid);
  Fun *f = c->f;
  wv_put32(b, (uint32_t)f->arity); wv_put32(b, (uint32_t)f->minar);
  wv_put32(b, (uint32_t)f->upc); wv_put32(b, (uint32_t)f->kind);
  wv_put32(b, (uint32_t)(f->variadic ? 1 : 0)); wv_put32(b, (uint32_t)(f->getter ? 1 : 0));
  wv_encode_bytes(b, f->name ? f->name->chars : "", f->name ? f->name->len : 0);
  wv_put32(b, (uint32_t)f->ch.n); buf_add(b, (const char *)f->ch.code, f->ch.n);
  wv_put32(b, (uint32_t)f->ch.consts.n);
  for (int i = 0; i < f->ch.consts.n; i++) if (!wv_encode(f->ch.consts.v[i], b, seen_clo, seen_map)) return false;
  // upvalues: لكل upvalue قيمته — المفتوح نأخذ منه لقطة فورية (دلالات fork)
  for (int i = 0; i < c->upc; i++) {
    Upv *u = c->upv[i];
    if (!u) return false;
    buf_addc(b, 'u');
    if (!wv_encode(u->loc != &u->closed ? *u->loc : u->closed, b, seen_clo, seen_map)) return false;
  }
  // قاموس globals كامل (يحمل دوال الوحدة والمراجع الأصلية)
  if (!wv_encode(OBJV((Obj *)c->globals), b, seen_clo, seen_map)) return false;
  return true;
}

// ------------------------- إذابة (فك) -------------------------
typedef struct { const char *p; int n, off; bool err; } WvIn;
static bool wv_get(WvIn *r, int n) { if (r->err || r->off + n > r->n) { r->err = true; return false; } r->off += n; return true; }
static uint8_t wv_u8(WvIn *r) { if (!wv_get(r, 1)) return 0; return (uint8_t)r->p[r->off - 1]; }
static uint32_t wv_u32(WvIn *r) { if (!wv_get(r, 4)) return 0; uint32_t v; memcpy(&v, r->p + r->off - 4, 4); return v; }
static Value wv_dec_val(WvIn *r, Dict *made, Map *globals);
static Value wv_decode(WvIn *r, Dict *made, Map *globals) {
  if (r->err) return NIL;
  if (!made) { Dict tmp; memset(&tmp, 0, sizeof tmp); Value v = wv_dec_val(r, &tmp, globals); dfree(&tmp); return v; }
  return wv_dec_val(r, made, globals);
}

static bool do_import(Str *path, Value *out);
static Value wv_dec_val(WvIn *r, Dict *made, Map *globals) {
  if (r->err) return NIL;
  char tag = (char)wv_u8(r);
  switch (tag) {
    case 'n': return NIL;
    case 'T': return BOOLV(true);
    case 'F': return BOOLV(false);
    case 'd': { uint64_t u; if (!wv_get(r, 8)) return NIL; memcpy(&u, r->p + r->off - 8, 8); double d; memcpy(&d, &u, 8); return NUMV(d); }
    case 's': { uint32_t n = wv_u32(r); if (r->err || n > (uint32_t)r->n) { r->err = true; return NIL; }
                Value v = OBJV(intern(r->p + r->off, (int)n)); wv_get(r, (int)n); return v; }
    case 'g': { uint32_t n = wv_u32(r); if (r->err || n > 1024u || n > (uint32_t)r->n) { r->err = true; return NIL; }
                char nm[1100]; memcpy(nm, r->p + r->off, n); nm[n] = 0; wv_get(r, (int)n);
                Value mod; Str *p = intern_c(nm);
                if (!do_import(p, &mod)) { r->err = true; return NIL; }
                return mod; }
    case 'L': { uint32_t n = wv_u32(r); if (r->err || n > 10000000u) { r->err = true; return NIL; }
                List *l = new_list(); Value lv = OBJV((Obj *)l); pin((Obj *)l);
                for (uint32_t i = 0; i < n; i++) { Value x = wv_dec_val(r, made, globals); if (r->err) return NIL; varr_push(&l->a, x); }
                return lv; }
    case 'm': { uint32_t id = wv_u32(r); Value key = NUMV(id); Value out;
                if (r->err || !dget(made, key, &out) || !IS_T(out, O_MAP)) { r->err = true; return NIL; } return out; }
    case 'M': {
      uint32_t id = wv_u32(r);
      uint32_t n = wv_u32(r);
      if (r->err || n > 10000000u) { r->err = true; return NIL; }
      Map *m = new_map(); Value mv = OBJV((Obj *)m); pin((Obj *)m);
      dset(made, NUMV(id), mv);
      for (uint32_t i = 0; i < n; i++) {
        Value k = wv_dec_val(r, made, globals);
        Value x = wv_dec_val(r, made, globals);
        if (r->err) return NIL;
        dset(&m->d, k, x);
      }
      return mv;
    }
    case 'P': {
      // دالة خام: نفس بنية الإغلاق بدون upvalues ولا globals
      Fun *f = new_fun();
      f->arity = (int)wv_u32(r); f->minar = (int)wv_u32(r);
      f->upc = (int)wv_u32(r); f->kind = (int)wv_u32(r);
      f->variadic = wv_u32(r) != 0; f->getter = wv_u32(r) != 0;
      uint32_t nl = wv_u32(r);
      if (!wv_get(r, (int)nl)) return NIL;
      char *nmb = (char *)xalloc((size_t)nl + 1); memcpy(nmb, r->p + r->off - nl, nl); nmb[nl] = 0;
      f->name = intern_c(nmb); free(nmb);
      uint32_t cn = wv_u32(r);
      if (r->err || cn > 10000000u) { r->err = true; return NIL; }
      f->ch.code = (uint8_t *)xalloc(cn ? cn : 1);
      if (!wv_get(r, (int)cn)) return NIL;
      memcpy(f->ch.code, r->p + r->off - cn, cn);
      f->ch.n = (int)cn; f->ch.cap = (int)cn;
      f->ch.lines = (int *)xalloc(sizeof(int) * ((size_t)cn + 1));
      for (uint32_t i = 0; i < cn; i++) f->ch.lines[i] = 0;
      int kcn = (int)wv_u32(r);
      if (r->err || kcn > 10000000u) { r->err = true; return NIL; }
      if (kcn) f->ch.consts.v = (Value *)xalloc(sizeof(Value) * (size_t)kcn);
      f->ch.consts.cap = kcn;
      for (int i = 0; i < kcn; i++) f->ch.consts.v[i] = wv_dec_val(r, made, globals);
      if (r->err) return NIL;
      f->gc = (int *)xalloc(sizeof(int) * (2 * kcn + 1));
      for (int i = 0; i < 2 * kcn + 1; i++) f->gc[i] = 0;
      return OBJV((Obj *)f);
    }
    case 'R': { uint32_t id = wv_u32(r); Value key = NUMV(id); Value out; if (r->err || !dget(made, key, &out)) { r->err = true; return NIL; } return out; }
    case 'C': {
      uint32_t id = wv_u32(r);
      Fun *f = new_fun();
      f->arity = (int)wv_u32(r); f->minar = (int)wv_u32(r);
      f->upc = (int)wv_u32(r); f->kind = (int)wv_u32(r);
      f->variadic = wv_u32(r) != 0; f->getter = wv_u32(r) != 0;
      uint32_t nl = wv_u32(r);
      if (!wv_get(r, (int)nl)) return NIL;
      char *nmb = (char *)xalloc((size_t)nl + 1); memcpy(nmb, r->p + r->off - nl, nl); nmb[nl] = 0;
      f->name = intern_c(nmb); free(nmb);
      uint32_t cn = wv_u32(r);
      if (r->err || cn > 10000000u) { r->err = true; return NIL; }
      f->ch.code = (uint8_t *)xalloc(cn ? cn : 1);
      if (!wv_get(r, (int)cn)) return NIL;
      memcpy(f->ch.code, r->p + r->off - cn, cn);
      f->ch.n = (int)cn; f->ch.cap = (int)cn;
      f->ch.lines = (int *)xalloc(sizeof(int) * ((size_t)cn + 1));
      for (uint32_t i = 0; i < cn; i++) f->ch.lines[i] = 0;
      Closure *c = new_closure(f, globals);
      Value cv = OBJV((Obj *)c); pin((Obj *)c);
      dset(made, NUMV(id), cv);
      int kcn = (int)wv_u32(r);
      if (r->err || kcn > 10000000u) { r->err = true; return NIL; }
      if (kcn) f->ch.consts.v = (Value *)xalloc(sizeof(Value) * (size_t)kcn);
      f->ch.consts.cap = kcn;
      for (int i = 0; i < kcn; i++) f->ch.consts.v[i] = wv_dec_val(r, made, globals);
      if (r->err) return NIL;
      f->gc = (int *)xalloc(sizeof(int) * (2 * kcn + 1));
      for (int i = 0; i < 2 * kcn + 1; i++) f->gc[i] = 0;
      // مهم: bytecode المتجمد جاء من الأم بعد quicken_fun — نعيد quicken هنا لضبط
      // أنماط القفزات المتوقعة (متسقة مع end_compiler في الأم).
      quicken_fun(f);
      for (int i = 0; i < c->upc; i++) {
        char ut = (char)wv_u8(r); if (ut != 'u') { r->err = true; return NIL; }
        Value uv = wv_dec_val(r, made, globals); if (r->err) return NIL;
        Upv *u = new_upv(NULL); u->closed = uv; u->loc = &u->closed; c->upv[i] = u;
      }
      // globals الحقيقي (قد يحمل مراجع دائرية لهذا الإغلاق نفسه)
      Value gv = wv_dec_val(r, made, globals);
      if (r->err) return NIL;
      if (IS_T(gv, O_MAP)) c->globals = AS_MAP(gv);
      return cv;
    }
    default: r->err = true; return NIL;
  }
}

// فك تغليف كامل: ينتظر قيمة واحدة من دفق، ويعيد false عند فشل
static bool wv_thaw(const char *data, int n, Map *globals, Value *out) {
  WvIn r = {data, n, 0, false};
  Dict made; memset(&made, 0, sizeof made);
  Value v = wv_decode(&r, &made, globals);
  dfree(&made);
  if (r.err || r.off != n) return false;
  *out = v; return true;
}

// ------------------------- واجهة الطرفين -------------------------
static bool wv_freeze(Value v, Buf *b) {
  Dict seen_clo, seen_map;
  memset(&seen_clo, 0, sizeof seen_clo); memset(&seen_map, 0, sizeof seen_map);
  bool ok = wv_encode(v, b, &seen_clo, &seen_map);
  dfree(&seen_clo); dfree(&seen_map);
  return ok;
}
