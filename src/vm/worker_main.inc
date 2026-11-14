// =====================================================================
//  worker_main.inc — نقطة دخول العامل الفرعي (كل المنصات)
//  يُشغَّل: solan --عامل_فرعي <ملف_التجميد> <منفذ> <وضع>
//  يقرأ تجميد الإغلاق والوسائط من ملف، يذيبها، يستدعي الدالة، ويرسل الناتج
//  JSON (سطر واحد) عبر سوكت الأب عند توفره، وإلا فإلى stdout ثم يخرج.
//  يُضمَّن بعد worker_net.inc (يستخدم wv_thaw من worker_value.inc).
// =====================================================================
static void report_error(void);
static int run_source(const char *src, const char *name);
static void json_enc(Value v, Buf *b, int indent, int mode);
static void fd_setup(int fd);
static int worker_main(const char *freeze_path, int sock, int mode) {
  size_t fn = 0; char *data = read_file(freeze_path, &fn);
  if (!data) { fprintf(stderr, "تعذّرت قراءة ملف التجميد\n"); return 74; }
  // الصيغة: [طول الإغلاق][تجميد الإغلاق][طول الوسائط][تجميد الوسائط]
  WvIn r = {data, (int)fn, 0, false};
  uint32_t cl = wv_u32(&r);
  if (r.err || cl > (uint32_t)fn) { fprintf(stderr, "ملف تجميد تالف\n"); return 74; }
  const char *cdata = data + r.off;
  wv_get(&r, (int)cl);
  uint32_t an = wv_u32(&r);
  if (r.err || an > (uint32_t)fn) { fprintf(stderr, "ملف تجميد تالف\n"); return 74; }
  const char *adata = data + r.off;

  Map *g = new_map(); pin((Obj *)g);
  dset(&g->d, KN("وسائط"), OBJV(new_list()));
  dset(&g->d, KN("هو_الرئيسي"), BOOLV(false));
  dset(&g->d, KN("اسم_الملف"), mkstr("<عامل>", 6));

  Value clo;
  if (!wv_thaw(cdata, (int)cl, g, &clo) || !IS_T(clo, O_CLO)) {
    fprintf(stderr, "فشل إذابة إغلاق العامل\n");
    return 74;
  }
  List *args = new_list(); pin((Obj *)args);
  { WvIn ar = {adata, (int)an, 0, false};
    for (;;) { Value v = wv_decode(&ar, NULL, g); if (ar.err) break; varr_push(&args->a, v); if (ar.off >= ar.n) break; } }

  Value out = NIL;
  bool ok;
  Buf b; buf_init(&b);
  Map *rm = new_map(); push(OBJV(rm));
  if (mode == 1) {
    // وضع القناة: الدالة تستقبل مقبض السوكت وتدير البروتوكول بنفسها
    Value arg = NUMV(sock);
    push(clo); push(arg);
    ok = call_sync(1); if (ok) out = pop();
  } else if (args->a.n > 0) { push(clo); for (int i = 0; i < args->a.n; i++) push(args->a.v[i]); ok = call_sync(args->a.n); if (ok) out = pop(); }
  else { ok = call_fn(clo, 0, NULL, &out); }
  fflush(stdout);

  if (mode == 1) { vm.sp--; free(b.p); if (!ok) { report_error(); return 70; } return 0; }
  if (ok) dset(&rm->d, KX("قيمة"), out); else dset(&rm->d, KX("خطأ"), OBJV(tostr(vm.exc)));
  json_enc(OBJV(rm), &b, 0, 0);
  vm.sp--;
  buf_addc(&b, '\n');
  if (sock >= 0) {
    // أرسل الناتج عبر سوكت الأب (مع إعادة المحاولة عند الإرسال الجزئي)
    size_t off = 0;
    while (off < (size_t)b.n) {
      int w = send(sock, buf_cstr(&b) + off, (int)((size_t)b.n - off), 0);
      if (w <= 0) break;
      off += (size_t)w;
    }
    shutdown(sock, 1); // أبلغ الأب بانتهاء الإرسال (نصف مغلق)
  } else {
    fwrite(buf_cstr(&b), 1, (size_t)b.n, stdout);
    fflush(stdout);
  }
  free(b.p);
  if (!ok) { report_error(); return 70; }
  return 0;
}
