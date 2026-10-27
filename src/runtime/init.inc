static const char PRELUDE_SRC[] =
  "استورد \"حلقة\"\n"
  "استورد \"نظام\"\n"
  "استورد \"سوكت\"\n"
  "استورد \"جيسون\"\n"
  "استورد \"شبكة\"\n"
  "صنف وعد {\n"
  "  بناء() { هذا.منجز = غلط; هذا.قيمة = عدم; هذا.فشل = غلط; هذا.خطأ = عدم; هذا._رد = [] }\n"
  "  حل(ق) {\n"
  "    اذا هذا.منجز { ارجع هذا }\n"
  "    اذا هل_وعد(ق) {\n"
  "      خل ذا = هذا\n"
  "      ق._عند(دالة() { اذا ق.فشل { ذا.ارفض(ق.خطأ) } والا { ذا.حل(ق.قيمة) } })\n"
  "      ارجع هذا\n"
  "    }\n"
  "    هذا.منجز = صح; هذا.قيمة = ق; هذا._اطلق(); ارجع هذا\n"
  "  }\n"
  "  ارفض(خ) { اذا هذا.منجز { ارجع هذا } هذا.منجز = صح; هذا.فشل = صح; هذا.خطأ = خ; هذا._اطلق(); ارجع هذا }\n"
  "  _اطلق() { خل ر = هذا._رد; هذا._رد = []; لكل ف في ر { حلقة.بعد(0, ف) } }\n"
  "  _عند(ف) { اذا هذا.منجز { حلقة.بعد(0, ف) } والا { هذا._رد.اضف(ف) } }\n"
  "  ثم(ف) {\n"
  "    خل وع = وعد(); خل ذا = هذا\n"
  "    هذا._عند(دالة() { اذا ذا.فشل { وع.ارفض(ذا.خطأ) } والا { حاول { وع.حل(ف(ذا.قيمة)) } امسك (خ) { وع.ارفض(خ) } } })\n"
  "    ارجع وع\n"
  "  }\n"
  "  التقط(ف) {\n"
  "    خل وع = وعد(); خل ذا = هذا\n"
  "    هذا._عند(دالة() { اذا ذا.فشل { حاول { وع.حل(ف(ذا.خطأ)) } امسك (خ) { وع.ارفض(خ) } } والا { وع.حل(ذا.قيمة) } })\n"
  "    ارجع وع\n"
  "  }\n"
  "}\n"
  "دالة هل_وعد(س) { ارجع هل_فرع(س, وعد) }\n"
  "دالة _خطوة(م, وع, قيمة, فشل) {\n"
  "  خل ن = _استأنف(م, قيمة, فشل)\n"
  "  اذا ن[0] == 1 { وع.حل(ن[1]) }\n"
  "  والا اذا ن[0] == 2 { وع.ارفض(ن[1]) }\n"
  "  والا {\n"
  "    خل ي = ن[1]\n"
  "    اذا هل_وعد(ي) { ي._عند(دالة() { _خطوة(م, وع, ي.فشل ؟ ي.خطأ : ي.قيمة, ي.فشل) }) }\n"
  "    والا { حلقة.بعد(0, دالة() { _خطوة(م, وع, ي, غلط) }) }\n"
  "  }\n"
  "}\n"
  "دالة _ابدأ_مهمة(م) { خل وع = وعد(); _خطوة(م, وع, عدم, غلط); ارجع وع }\n"
  "دالة _انتظر_حظر(وع) {\n"
  "  طالما ليس وع.منجز { اذا ليس حلقة.خطوة() { ارمي \"انتظار وعد لن يكتمل: لا توجد أحداث معلقة\" } }\n"
  "  اذا وع.فشل { ارمي وع.خطأ }\n"
  "  ارجع وع.قيمة\n"
  "}\n"
  "دالة نم(ث) { خل وع = وعد(); حلقة.بعد(ث, دالة() { وع.حل(عدم) }); ارجع وع }\n"
  "دالة _ربط(ط, i, نتائج, بقي, وع) {\n"
  "  خل ف = ط\n"
  "  اذا ليس هل_وعد(ط) { ف = وعد().حل(ط) }\n"
  "  ف._عند(دالة() {\n"
  "    اذا ف.فشل { وع.ارفض(ف.خطأ) }\n"
  "    والا { نتائج[i] = ف.قيمة; بقي[0] -= 1; اذا بقي[0] == 0 { وع.حل(نتائج) } }\n"
  "  })\n"
  "}\n"
  "دالة كل_الوعود(قائمة) {\n"
  "  خل وع = وعد(); خل عدد = قائمة.طول\n"
  "  اذا عدد == 0 { ارجع وع.حل([]) }\n"
  "  خل نتائج = قائمة.انسخ(); خل بقي = [عدد]\n"
  "  لكل i في مدى(عدد) { _ربط(قائمة[i], i, نتائج, بقي, وع) }\n"
  "  ارجع وع\n"
  "}\n"
  "دالة سباق(قائمة) {\n"
  "  خل وع = وعد()\n"
  "  لكل ط في قائمة {\n"
  "    خل ف = ط\n"
  "    اذا ليس هل_وعد(ط) { ف = وعد().حل(ط) }\n"
  "    ف._عند(دالة() { اذا ف.فشل { وع.ارفض(ف.خطأ) } والا { وع.حل(ف.قيمة) } })\n"
  "  }\n"
  "  ارجع وع\n"
  "}\n"
  "دالة شغل(ف) { خل ر = ف(); اذا هل_وعد(ر) { ارجع انتظر ر } ارجع ر }\n"
  "دالة عملية(ف) {\n"
  "  خل وع = وعد(); خل ر = نظام.فرع(ف); خل بيانات = \"\"\n"
  "  حلقة.راقب(ر.مقبض, دالة(م) {\n"
  "    خل ق = سوكت.استقبل(م, 65536)\n"
  "    اذا ق == عدم {\n"
  "      حلقة.الغ_مراقبة(م); سوكت.اغلق(م); نظام.انتظر_عملية(ر.pid)\n"
  "      حاول { خل ن = جيسون.فك(بيانات); اذا ن.احصل(\"خطأ\") { وع.ارفض(ن[\"خطأ\"]) } والا { وع.حل(ن.احصل(\"قيمة\")) } }\n"
  "      امسك (خ) { وع.ارفض(\"فشلت العملية الفرعية\") }\n"
  "    } والا اذا ق { بيانات += ق }\n"
  "  })\n"
  "  ارجع وع\n"
  "}\n"
  "دالة جلب_غير_متزامن(رابط, خيارات = عدم) { ارجع عملية(دالة() => شبكة.جلب(رابط, خيارات)) }\n"
  "غير_متزامن دالة _خدمة(ف, عنوان, معالج) {\n"
  "  خل ط = شبكة.اقرا_طلب(ف, عنوان)\n"
  "  اذا ط == عدم { سوكت.اغلق(ف); ارجع عدم }\n"
  "  حاول { خل ر = انتظر معالج(ط); شبكة.اكتب_رد(ف, ر) }\n"
  "  امسك (خ) { اطبع(\"خطأ في المعالج:\", خ); شبكة.اكتب_رد(ف, {\"حالة\": 500, \"جسم\": \"خطأ داخلي\"}) }\n"
  "  سوكت.اغلق(ف)\n"
  "}\n"
  "دالة خادم_حلقة(منفذ, معالج, خيارات = عدم) {\n"
  "  خل خ = سوكت.استمع(منفذ, خيارات)\n"
  "  حلقة.راقب(خ, دالة(ف) { خل ع = سوكت.اقبل(ف); اذا ع { _خدمة(ع[0], ع[1], معالج) } })\n"
  "  ارجع خ\n"
  "}\n"
  "شبكة.خادم_حلقة = خادم_حلقة\n";
static void report_error(void);
static void load_prelude(void) {
  Fun *fn = compile(PRELUDE_SRC, "<prelude>", false);
  if (!fn) { fprintf(stderr, "سولان: فشل تحميل المقدمة\n"); exit(70); }
  Map *g = new_map(); pin((Obj *)g); Closure *c = new_closure(fn, g); push(OBJV(c)); vm.exc = NIL;
  if (!call_sync(0)) { report_error(); exit(70); }
  vm.sp--;
  for (int i = 0; i < g->d.n; i++) { Value k = g->d.e[i].k; if (IS_T(k, O_STR) && AS_STR(k)->chars[0] != '$') dset(&vm.builtins, k, g->d.e[i].v); }
  Value v;
  if (dget(&g->d, KN("وعد"), &v)) vm.promise_cls = AS_CLS(v);
  if (dget(&g->d, KN("_انتظر_حظر"), &v)) vm.fn_block = v;
  if (dget(&g->d, KN("_ابدأ_مهمة"), &v)) vm.fn_start = v;
}
static void vm_init(void) {
  for (int i = 0; i < g_nkroots; i++) *g_kroots[i] = NULL;
  g_nkroots = 0; g_mfn_memo.d = NULL; g_rxst = NULL; g_rxsc = 0;
  jit_init();
  vm.stack = xalloc(sizeof(Value) * STACK_MAX); vm.stack_end = vm.stack + STACK_MAX; vm.frames = xalloc(sizeof(Frame) * FRAMES_MAX); vm.frames_max = FRAMES_MAX;
  vm.handlers = xalloc(sizeof(Handler) * 512); vm.hmax = 512; vm.yielded = NIL; vm.fn_block = NIL; vm.fn_start = NIL;
  vm.sp = vm.stack; vm.next_gc = 4096; vm.sbcap = 16384; vm.sb = xalloc(sizeof(Str *) * vm.sbcap);
  for (int i = 0; i < vm.sbcap; i++) vm.sb[i] = NULL;
  vm.exc = NIL; vm.pins = new_list();
  vm.init_str = normstr("بناء"); vm.tostr_str = normstr("الى_نص"); vm.s_len = normstr("طول"); vm.s_this = normstr("هذا"); vm.s_super = normstr("الاب"); vm.s_from = normstr("من"); vm.s_case = normstr("حالة"); vm.s_default = normstr("افتراضي"); vm.s_static = normstr("ساكن"); vm.s_prop = normstr("خاصية"); vm.s_enum = normstr("تعداد");
  init_keywords();
  for (int i = 0; i < M_COUNT; i++) dset(&vm.mid, OBJV(normstr(method_names[i])), NUMV(i));
  init_natives();
  for (int i = 0; STDLIB[i].name; i++) dset(&vm.stdlibd, OBJV(normstr(STDLIB[i].name)), NUMV(i));
  load_prelude();
  apply_aliases();
}
static void reset_vm_state(void) { vm.sp = vm.stack; vm.fcount = 0; vm.hcount = 0; vm.open = NULL; vm.nest = 0; }

static bool run_fun(Fun *fn, Map *globals) {
  Closure *c = new_closure(fn, globals); push(OBJV(c));
  vm.exc = NIL;
  bool ok = call_sync(0);
  if (ok) vm.sp--;
  return ok;
}
static void report_error(void) {
  Str *s = tostr(vm.exc); bool col = use_color(2);
  fprintf(stderr, "%sخطأ:%s %s\n", col ? "\x1b[1;38;5;203m" : "", col ? "\x1b[0m" : "", s->chars);
  if (vm.trace) fputs(vm.trace, stderr);
  if (vm.tr_line > 0) print_snippet_file(vm.tr_file, vm.tr_line);
}

static int run_source(const char *src, const char *name) {
  Fun *fn = compile(src, name, false); g_debug_compile = false; if (!fn) return 65;
  Map *g = new_map(); pin((Obj *)g);
  Value args = g_args; dset(&g->d, KN("وسائط"), args); dset(&g->d, KN("هو_الرئيسي"), BOOLV(true)); dset(&g->d, KN("اسم_الملف"), mkstr(name, (int)strlen(name)));
  if (!run_fun(fn, g)) { fflush(stdout); report_error(); return 70; }
  return 0;
}
static bool balanced(const char *s) {
  int d = 0; char q = 0;
  for (; *s; s++) {
    if (q) { if (*s == '\\' && s[1]) s++; else if (*s == q) q = 0; continue; }
    if (*s == '"' || *s == '\'') q = *s; else if (*s == '#') { while (*s && *s != '\n') s++; if (!*s) break; }
    else if (*s == '(' || *s == '[' || *s == '{') d++; else if (*s == ')' || *s == ']' || *s == '}') d--;
  }
  return d <= 0 && !q;
}
