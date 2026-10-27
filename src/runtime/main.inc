static int run_pkg_cmd(const char *call) {
  char code[1800]; snprintf(code, sizeof code, "استورد \"حزم\"\nاستورد \"نظام\"\nحاول { حزم.%s } امسك (خ) { اطبع(\"خطأ:\", خ); نظام.اخرج(1) }\n", call);
  List *al = new_list(); g_args = OBJV(al); pin((Obj *)al);
  return run_source(code, "<حزم>");
}
#define HASPFX(a, lit) (!strncmp((a), lit, sizeof(lit) - 1))
static bool g_perm_explicit = false;
static int perm_from_word(const char *w) {
  struct { const char *n; int b; } t[] = {{"قراءة", PM_READ}, {"read", PM_READ}, {"كتابة", PM_WRITE}, {"write", PM_WRITE}, {"شبكة", PM_NET}, {"net", PM_NET},
    {"تنفيذ", PM_EXEC}, {"exec", PM_EXEC}, {"بيئة", PM_ENV}, {"env", PM_ENV}, {"اصلي", PM_NATIVE}, {"native", PM_NATIVE}, {"الكل", PM_ALL}, {"all", PM_ALL}, {NULL, 0}};
  for (int i = 0; t[i].n; i++) if (!strcmp(w, t[i].n)) return t[i].b;
  return -1;
}
static bool parse_perm_flag(const char *arg) {
  char buf[2048]; snprintf(buf, sizeof buf, "%s", arg);
  char *eq = strchr(buf, '='); char *val = eq ? eq + 1 : (char *)""; if (eq) *eq = 0;
  bool deny = HASPFX(buf, "--امنع") || HASPFX(buf, "--deny");
  const char *dash = strchr(buf + 2, '-'); int scope = -1;
  if (dash && !deny) { scope = perm_from_word(dash + 1); if (scope < 0) { fprintf(stderr, "صلاحية غير معروفة: %s\n", dash + 1); return false; } }
  if (!g_perm_explicit && !deny) { g_perm = 0; g_perm_explicit = true; }
  if (scope == PM_READ || scope == PM_WRITE) {
    g_perm |= scope; int k = scope == PM_READ ? 0 : 1;
    for (char *t = strtok(val, ","); t && g_npdirs[k] < 16; t = strtok(NULL, ",")) { char rp[PATH_MAX]; g_pdirs[k][g_npdirs[k]++] = strdup(realpath(t, rp) ? rp : t); }
    return true;
  }
  if (scope == PM_NET) {
    g_perm |= PM_NET; for (char *t = strtok(val, ","); t && g_nphosts < 32; t = strtok(NULL, ",")) g_phosts[g_nphosts++] = strdup(t);
    return true;
  }
  if (scope >= 0) { g_perm |= scope; return true; }
  for (char *t = strtok(val, ","); t; t = strtok(NULL, ",")) {
    int b = perm_from_word(t); if (b < 0) { fprintf(stderr, "صلاحية غير معروفة: %s\n", t); return false; }
    if (deny) g_perm &= ~b; else g_perm |= b;
  }
  return true;
}

#ifndef SOLAN_LIBRARY
static void limit_time_handler(int sig) { (void)sig; static const char m[] = "\nخطأ: تجاوز الحد الزمني\n"; ssize_t w = write(2, m, sizeof m - 1); (void)w; _exit(124); }
static bool pk_q(char *dst, size_t sz, const char *s) {
  size_t n = strlen(dst);
  if (n + strlen(s) * 2 + 4 >= sz) return false;
  dst[n++] = '"';
  for (const char *c = s; *c; c++) {
    unsigned char u = (unsigned char)*c;
    if (u < 32 || u == 127) return false;
    if (*c == '"' || *c == '\\') dst[n++] = '\\';
    dst[n++] = *c;
  }
  dst[n++] = '"'; dst[n] = 0;
  return true;
}
int main(int argc, char **argv) {
  g_argv0 = argc > 0 ? argv[0] : NULL;
  os_console_init(); os_args_utf8(&argc, &argv);
  setvbuf(stdout, NULL, _IOLBF, 0); os_ignore_sigpipe();
  mallopt(M_MMAP_THRESHOLD, 256 << 20); mallopt(M_TRIM_THRESHOLD, 512 << 20);
  vm_init();
  { const char *se = getenv("SOLAN_SAFE"); if (se && se[0] && se[0] != '0') { g_safe = true; g_perm = PM_READ | PM_ENV; } }
  int ai = 1;
  while (ai < argc) {
    if (!strcmp(argv[ai], "--تتبع") || !strcmp(argv[ai], "--trace")) { vm.dbg_mode = 1; g_debug_compile = true; ai++; }
    else if (!strcmp(argv[ai], "--تصحيح") || !strcmp(argv[ai], "--debug")) { vm.dbg_mode = 2; vm.dbg_step = true; g_debug_compile = true; ai++; }
    else if (!strcmp(argv[ai], "--آمن") || !strcmp(argv[ai], "--safe")) { g_safe = true; g_perm = PM_READ | PM_ENV; ai++; }
    else if (HASPFX(argv[ai], "--اسمح") || HASPFX(argv[ai], "--allow") || HASPFX(argv[ai], "--امنع") || HASPFX(argv[ai], "--deny")) { if (!parse_perm_flag(argv[ai])) return 64; ai++; }
    else if ((!strcmp(argv[ai], "--حد_وقت") || !strcmp(argv[ai], "--timeout")) && ai + 1 < argc) { int sec = atoi(argv[ai + 1]); if (sec > 0) os_set_timeout((unsigned)sec, limit_time_handler); ai += 2; }
    else if ((!strcmp(argv[ai], "--حد_ذاكرة") || !strcmp(argv[ai], "--max-mem")) && ai + 1 < argc) { long mb = atol(argv[ai + 1]); if (mb > 0) os_set_memlimit(mb); ai += 2; }
    else if (!strcmp(argv[ai], "--بلا_لون") || !strcmp(argv[ai], "--no-color")) { g_color_off = true; ai++; }
    else if ((!strcmp(argv[ai], "--نقطة") || !strcmp(argv[ai], "--break")) && ai + 1 < argc) { vm.dbg_mode = 2; g_debug_compile = true; if (vm.nbps < 64) vm.bps[vm.nbps++] = atoi(argv[ai + 1]); ai += 2; }
    else break;
  }
  int rest = argc - ai; char **av = argv + ai;
  if (rest >= 2 && (!strcmp(av[0], "--لون") || !strcmp(av[0], "--color") || !strcmp(av[0], "--html"))) {
    size_t hn; char *hs = read_file(av[1], &hn); if (!hs) { fprintf(stderr, "تعذّر فتح الملف\n"); return 66; }
    highlight_to(stdout, hs, (int)hn, !strcmp(av[0], "--html")); free(hs); return 0;
  }
  if (rest >= 1 && (!strcmp(av[0], "-v") || !strcmp(av[0], "--version"))) { printf("solan %s\n", SOLAN_VERSION); return 0; }
  if (rest >= 1 && (!strcmp(av[0], "-h") || !strcmp(av[0], "--help"))) { usage(); return 0; }
  if (rest >= 1 && !strcmp(av[0], "lsp")) { List *al = new_list(); g_args = OBJV(al); pin((Obj *)al); return run_source("استورد \"خادم_اللغة\"\nخادم_اللغة.شغل()\n", "<lsp>"); }
  if (rest >= 1) {
    const char *c0 = av[0];
#define PKIS(en, ar) (!strcmp(c0, en) || !strcmp(c0, ar))
    int cmd = 0;
    if (PKIS("init", "بدء")) cmd = 1; else if (PKIS("add", "اضف")) cmd = 2; else if (PKIS("install", "ثبت")) cmd = 3;
    else if (PKIS("remove", "ازل") || !strcmp(c0, "احذف")) cmd = 4; else if (PKIS("list", "قائمة") || !strcmp(c0, "اعرض")) cmd = 5;
    else if (PKIS("update", "حدث")) cmd = 6; else if (PKIS("verify", "تحقق")) cmd = 7; else if (PKIS("outdated", "قديمة")) cmd = 8;
    else if (PKIS("why", "لماذا")) cmd = 9; else if (PKIS("prune", "تنظيف")) cmd = 10; else if (PKIS("cache", "كاش")) cmd = 11; else if (PKIS("publish", "انشر")) cmd = 12;
    if (cmd) {
      bool frozen = false, quiet = false, major = false, tree = false, store = false, glob = false, local = false, dry = false, push = false, lib = false;
      const char *pos[16]; int np = 0; const char *asname = NULL;
      for (int i = 1; i < rest; i++) {
        const char *x = av[i];
        if (!strcmp(x, "--frozen") || !strcmp(x, "--مجمد")) frozen = true;
        else if (!strcmp(x, "--quiet") || !strcmp(x, "--صامت")) quiet = true;
        else if (!strcmp(x, "--major") || !strcmp(x, "--رئيسي")) major = true;
        else if (!strcmp(x, "--tree") || !strcmp(x, "--شجرة")) tree = true;
        else if (!strcmp(x, "--store") || !strcmp(x, "--المخزن")) store = true;
        else if (!strcmp(x, "--global") || !strcmp(x, "--عام")) glob = true;
        else if (!strcmp(x, "--local") || !strcmp(x, "--محلي")) local = true;
        else if (!strcmp(x, "--dry-run") || !strcmp(x, "--تجربة")) dry = true;
        else if (!strcmp(x, "--push") || !strcmp(x, "--ادفع")) push = true;
        else if (!strcmp(x, "--lib") || !strcmp(x, "--مكتبة")) lib = true;
        else if ((!strcmp(x, "--as") || !strcmp(x, "--باسم")) && i + 1 < rest) asname = av[++i];
        else if (!strncmp(x, "--", 2)) { fprintf(stderr, "خيار غير معروف: %s\n", x); return 1; }
        else if (np < 16) pos[np++] = x;
        else { fprintf(stderr, "عدد الوسائط كبير جدا\n"); return 1; }
      }
      bool legacy_ok = (cmd == 3 && np >= 1) || (cmd == 4 && np >= 1) || cmd == 5;
      bool in_proj = false;
      if (cmd != 1 && !glob) {
        char cwd0[PATH_MAX], d[PATH_MAX];
        if (getcwd(cwd0, sizeof cwd0)) {
          snprintf(d, sizeof d, "%s", cwd0);
          for (int up = 0; up < 40; up++) {
            char f[PATH_MAX + 16]; snprintf(f, sizeof f, "%s/solan.json", d);
            if (access(f, F_OK) == 0) { in_proj = true; if (strcmp(d, cwd0) != 0 && chdir(d) != 0) { fprintf(stderr, "تعذّر الانتقال إلى جذر المشروع\n"); return 1; } break; }
            char *sl = strrchr(d, '/'); if (!sl || sl == d) break; *sl = 0;
          }
        }
      }
      if (!in_proj && !legacy_ok && cmd != 1 && cmd != 10 && cmd != 11) { fprintf(stderr, "لا يوجد solan.json في هذا المجلد أو مجلداته العليا (شغّل: solan init)\n"); return 1; }
      char code[8192]; code[0] = 0; bool okq = true;
      for (int i = 0; i < np; i++) for (const char *c = pos[i]; *c; c++) if (*c == '$') okq = false;
      if (asname) for (const char *c = asname; *c; c++) if (*c == '$') okq = false;
      if (!okq) { fprintf(stderr, "الوسائط لا يجوز أن تحتوي على '$'\n"); return 1; }
#define PKAPP(...) do { size_t _n = strlen(code); snprintf(code + _n, sizeof code - _n, __VA_ARGS__); } while (0)
#define PKQ(str) do { if (!pk_q(code, sizeof code, (str))) { fprintf(stderr, "قيمة غير صالحة\n"); return 1; } } while (0)
      const char *B[2] = {"غلط", "صح"};
      switch (cmd) {
        case 1: PKAPP("بدء(%s)", B[lib]); return run_pkg_cmd(code);
        case 2:
          if (np < 1) { fprintf(stderr, "الاستخدام: solan add [اسم=]مصدر[@نطاق] [--as اسم]\n"); return 1; }
          for (int i = 0; i < np; i++) { PKAPP("اضف("); PKQ(pos[i]); PKAPP(", "); if (asname && np == 1) PKQ(asname); else PKAPP("عدم"); PKAPP(")\n"); }
          return run_pkg_cmd(code);
        case 3:
          if (np == 0) { PKAPP("ثبت_الكل(%s, %s, %s)", B[frozen], B[quiet], B[local]); return run_pkg_cmd(code); }
          if (!in_proj) return cmd_install(pos[0]);
          for (int i = 0; i < np; i++) { PKAPP("اضف("); PKQ(pos[i]); PKAPP(", "); if (asname && np == 1) PKQ(asname); else PKAPP("عدم"); PKAPP(")\n"); }
          return run_pkg_cmd(code);
        case 4:
          if (np < 1) { fprintf(stderr, "الاستخدام: solan remove اسم\n"); return 1; }
          if (!in_proj) return cmd_remove(pos[0]);
          for (int i = 0; i < np; i++) { PKAPP("احذف("); PKQ(pos[i]); PKAPP(")\n"); }
          return run_pkg_cmd(code);
        case 5:
          if (!in_proj && !store) return cmd_list();
          PKAPP("اعرض(%s, %s)", B[tree], B[store || !in_proj]); return run_pkg_cmd(code);
        case 6: PKAPP("حدّث("); if (np) PKQ(pos[0]); else PKAPP("عدم"); PKAPP(", %s)", B[major]); return run_pkg_cmd(code);
        case 7: return run_pkg_cmd("تحقق()");
        case 8: return run_pkg_cmd("قديمة()");
        case 9: if (np < 1) { fprintf(stderr, "الاستخدام: solan why اسم\n"); return 1; } PKAPP("لماذا("); PKQ(pos[0]); PKAPP(")"); return run_pkg_cmd(code);
        case 10: PKAPP("نظف_المتجر(%s)", B[dry]); return run_pkg_cmd(code);
        case 11:
          if (np >= 1 && (!strcmp(pos[0], "clean") || !strcmp(pos[0], "نظف"))) return run_pkg_cmd("نظف_الكاش()");
          fprintf(stderr, "الاستخدام: solan cache clean\n"); return 1;
        case 12: PKAPP("انشر(%s, %s)", B[dry], B[push]); return run_pkg_cmd(code);
      }
    }
  }
  if (rest >= 2 && (!strcmp(av[0], "fmt") || !strcmp(av[0], "نسق"))) {
    bool w = false, canon = false;
    for (int k = 2; k < rest; k++) { if (!strcmp(av[k], "-w") || !strcmp(av[k], "--write")) w = true; else if (!strcmp(av[k], "--قياسي") || !strcmp(av[k], "--canonical")) canon = true; }
    return canon ? cmd_canon(av[1], w) : cmd_fmt(av[1], w);
  }
  List *al = new_list(); g_args = OBJV(al); pin((Obj *)al);
  int first = (rest >= 2 && !strcmp(av[0], "-e")) ? 2 : 1;
  for (int i = first; i < rest; i++) varr_push(&al->a, mkstr(av[i], (int)strlen(av[i])));
  if (rest >= 2 && !strcmp(av[0], "--ops")) {
    g_debug_compile = false; char *cs = read_file(av[1], NULL); if (!cs) { fprintf(stderr, "تعذّر فتح الملف\n"); return 66; }
    Fun *cf = compile(cs, av[1], false); free(cs); if (!cf) return 65; disasm_fun(cf, 0); return 0;
  }
  if (rest >= 2 && !strcmp(av[0], "--check")) {
    g_debug_compile = false; char *cs = read_file(av[1], NULL); if (!cs) { fprintf(stderr, "تعذّر فتح الملف\n"); return 66; }
    Fun *cf = compile(cs, av[1], false); free(cs); if (cf) { printf("سليم: %s\n", av[1]); return 0; } return 65;
  }
  if (rest >= 2 && !strcmp(av[0], "-e")) return run_source(av[1], "<-e>");
  if (rest < 1) { repl(); return 0; }
  size_t n; char *src = read_file(av[0], &n);
  if (!src) { fprintf(stderr, "سولان: تعذّر فتح الملف '%s'\n", av[0]); return 66; }
  vm.dbg_src = src;
  char rp[PATH_MAX]; if (realpath(av[0], rp)) { char *sl = strrchr(rp, '/'); if (sl) { *sl = 0; vm.script_dir = strdup(rp); } }
  int rc = run_source(src, av[0]); fflush(stdout); return rc;
}
#endif
