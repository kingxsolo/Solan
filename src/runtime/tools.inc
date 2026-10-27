static void format_source(const char *src, Buf *out) {
  int depth = 0; char instr = 0; bool inblock = false; bool prevblank = true; const char *p = src;
  while (*p) {
    const char *nl = strchr(p, '\n'); int len = nl ? (int)(nl - p) : (int)strlen(p);
    char *line = xalloc(len + 1); memcpy(line, p, len); line[len] = 0; int ll = len; while (ll > 0 && (line[ll - 1] == '\r' || line[ll - 1] == ' ' || line[ll - 1] == '\t')) line[--ll] = 0;
    if (instr || inblock) { buf_add(out, line, ll); buf_addc(out, '\n'); prevblank = false; }
    else {
      char *t = line; while (*t == ' ' || *t == '\t') t++;
      if (!*t) { if (!prevblank) buf_addc(out, '\n'); prevblank = true; }
      else {
        int lead = 0; for (const char *q = t; *q == '}' || *q == ']' || *q == ')'; q++) lead++;
        int ind = depth - lead; if (ind < 0) ind = 0; for (int k = 0; k < ind * 2; k++) buf_addc(out, ' ');
        buf_add(out, t, (int)strlen(t)); buf_addc(out, '\n'); prevblank = false;
      }
    }
    for (const char *q = line; *q; q++) {
      if (inblock) { if (q[0] == '*' && q[1] == '/') { inblock = false; q++; } continue; }
      if (instr) { if (*q == '\\' && q[1]) q++; else if (instr == 'g' ? ((unsigned char)q[0] == 0xC2 && (unsigned char)q[1] == 0xBB) : *q == instr) { if (instr == 'g') q++; instr = 0; } continue; }
      if (*q == '"' || *q == '\'') { instr = *q; continue; }
      if ((unsigned char)q[0] == 0xC2 && (unsigned char)q[1] == 0xAB) { instr = 'g'; q++; continue; }
      if (q[0] == '/' && q[1] == '/') break;
      if (q[0] == '/' && q[1] == '*') { inblock = true; q++; continue; }
      if (*q == '{' || *q == '[' || *q == '(') depth++; else if (*q == '}' || *q == ']' || *q == ')') { if (depth > 0) depth--; }
    }
    free(line); if (!nl) break; p = nl + 1;
  }
}
static int cmd_fmt(const char *path, bool write) {
  char *src = read_file(path, NULL); if (!src) { fprintf(stderr, "تعذّر فتح الملف\n"); return 66; }
  Buf out; buf_init(&out); format_source(src, &out); free(src);
  while (out.n > 1 && out.p[out.n - 1] == '\n' && out.p[out.n - 2] == '\n') out.p[--out.n] = 0;
  if (write) { FILE *f = fopen(path, "wb"); if (!f) { fprintf(stderr, "تعذّرت الكتابة\n"); return 74; } fwrite(buf_cstr(&out), 1, out.n, f); fclose(f); printf("تم تنسيق %s\n", path); }
  else fwrite(buf_cstr(&out), 1, out.n, stdout);
  free(out.p); return 0;
}

static int cmd_canon(const char *path, bool write) {
  char *src = read_file(path, NULL); if (!src) { fprintf(stderr, "تعذّر فتح الملف\n"); return 66; }
  memset(&P.sc, 0, sizeof P.sc); P.sc.src = P.sc.cur = src; P.sc.line = 1; P.file = path; P.quiet = true;
  if (setjmp(P.jb)) { fprintf(stderr, "%s\n", P.errmsg); free(src); return 65; }
  Buf out; buf_init(&out); const char *last = src; int changed = 0; TT prev = T_EOF;
  for (;;) {
    Token t = scan_token(); if (t.t == T_EOF) break;
    if (t.s && t.t != T_IDENT && kw_canon[t.t] && prev != T_DOT && prev != T_QDOT) {
      if (t.s != normstr(kw_canon[t.t])) {
        buf_add(&out, last, (int)(t.start - last)); buf_add(&out, kw_canon[t.t], (int)strlen(kw_canon[t.t]));
        last = t.start + t.len; changed++;
      }
    }
    prev = t.t;
  }
  buf_add(&out, last, (int)strlen(last));
  if (write) { FILE *f = fopen(path, "wb"); if (!f) { fprintf(stderr, "تعذّرت الكتابة\n"); return 74; } fwrite(buf_cstr(&out), 1, out.n, f); fclose(f); printf("%s: استُبدل %d مرادف بالكلمة القياسية\n", path, changed); }
  else fwrite(buf_cstr(&out), 1, out.n, stdout);
  free(out.p); free(src); return 0;
}
static bool safe_name(const char *n) {
  if (!*n || !strcmp(n, "..") || !strcmp(n, ".") || strlen(n) > 200) return false;
  for (const char *c = n; *c; c++) if (*c == '/' || *c == '\\' || *c == '\'' || *c == ';' || (unsigned char)*c < 32) return false;
  return true;
}
static int run_argv(char *const argv[]) {
  fflush(stdout); fflush(stderr);
#if SOLAN_WIN
  return (int)_spawnvp(_P_WAIT, argv[0], (const char *const *)argv) == 0 ? 0 : 1;
#else
  pid_t pid = fork();
  if (pid < 0) return 1;
  if (pid == 0) { execvp(argv[0], argv); _exit(127); }
  int st = 0; if (waitpid(pid, &st, 0) < 0) return 1;
  return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
#endif
}
static int cmd_install(const char *src0) {
  const char *home = getenv("HOME"); char dir[PATH_MAX], dest[PATH_MAX + 300], src[1024], tag[128] = "";
  if (strlen(src0) >= sizeof src) { fprintf(stderr, "المصدر طويل جدا\n"); return 1; }
  snprintf(src, sizeof src, "%s", src0); snprintf(dir, sizeof dir, "%s/.solan/modules", home ? home : ".");
  { char p1[PATH_MAX]; snprintf(p1, sizeof p1, "%s/.solan", home ? home : "."); mkdir(p1, 0755); mkdir(dir, 0755); }
  sstat_t st; bool local = sstat(src, &st) == 0;
  char *hash = strchr(src, '#'); if (hash) { snprintf(tag, sizeof tag, "%s", hash + 1); *hash = 0; }
  bool url = strstr(src, "://") || !strncmp(src, "git@", 4);
  if (!local && !url && strchr(src, '/') && src[0] != '.' && src[0] != '/') {
    char *at = strchr(src, '@'); if (at) { snprintf(tag, sizeof tag, "%s", at + 1); *at = 0; }
    for (const char *c = src; *c; c++) if (!isalnum((unsigned char)*c) && *c != '/' && *c != '-' && *c != '_' && *c != '.') { fprintf(stderr, "مصدر غير صالح\n"); return 1; }
    char full[1100]; snprintf(full, sizeof full, "https://github.com/%s.git", src); snprintf(src, sizeof src, "%s", full); url = true;
  }
  for (char *c = tag; *c; c++) if (!isalnum((unsigned char)*c) && *c != '.' && *c != '-' && *c != '_' && *c != '/') { fprintf(stderr, "وسم غير صالح\n"); return 1; }
  if (tag[0] == '-' || src[0] == '-') { fprintf(stderr, "قيمة غير صالحة\n"); return 1; }
  const char *b = strrchr(src, '/'); b = b ? b + 1 : src; char name[256]; snprintf(name, sizeof name, "%s", b);
  size_t l = strlen(name); if (l > 4 && !strcmp(name + l - 4, ".git")) name[l - 4] = 0;
  if (!safe_name(name)) { fprintf(stderr, "اسم مكتبة غير صالح\n"); return 1; }
  snprintf(dest, sizeof dest, "%s/%s", dir, name);
  int r;
  if (url) {
    char *rm[] = {"rm", "-rf", "--", dest, NULL}; run_argv(rm);
    if (tag[0]) { char *g[] = {"git", "clone", "--depth", "1", "--branch", tag, "--", src, dest, NULL}; r = run_argv(g); }
    else { char *g[] = {"git", "clone", "--depth", "1", "--", src, dest, NULL}; r = run_argv(g); }
  } else if (local && S_ISDIR(st.st_mode)) {
    char *rm[] = {"rm", "-rf", "--", dest, NULL}; run_argv(rm);
    char *cp[] = {"cp", "-r", "--", src, dest, NULL}; r = run_argv(cp);
  } else if (local) { char *cp[] = {"cp", "--", src, dir, NULL}; r = run_argv(cp); }
  else { fprintf(stderr, "المصدر غير موجود: %s\n", src); return 1; }
  if (r == 0) printf("تم تثبيت '%s' في %s\n", name, dir);
  return r == 0 ? 0 : 1;
}
static int cmd_list(void) {
  const char *home = getenv("HOME"); char dir[PATH_MAX]; snprintf(dir, sizeof dir, "%s/.solan/modules", home ? home : ".");
  DIR *d = opendir(dir); if (!d) { printf("لا توجد مكتبات مثبتة\n"); return 0; } struct dirent *e; int n = 0;
  while ((e = readdir(d))) { if (e->d_name[0] == '.') continue; printf("%s\n", e->d_name); n++; } closedir(d); if (!n) printf("لا توجد مكتبات مثبتة\n"); return 0;
}
static int cmd_remove(const char *name) {
  if (!safe_name(name)) { fprintf(stderr, "اسم غير صالح\n"); return 1; }
  const char *home = getenv("HOME"); char dest[PATH_MAX + 300]; snprintf(dest, sizeof dest, "%s/.solan/modules/%s", home ? home : ".", name);
  char *rm[] = {"rm", "-rf", "--", dest, NULL}; int r = run_argv(rm);
  if (r == 0) printf("تمت إزالة '%s'\n", name); return r == 0 ? 0 : 1;
}
static void usage(void) {
  puts("سولان " SOLAN_VERSION " — لغة برمجة عربية\n\n"
       "الاستخدام:\n"
       "  solan ملف.سو أو ملف.an [وسائط...]   تشغيل ملف\n"
       "  solan                        الوضع التفاعلي (مع تاريخ الأوامر إن توفر readline)\n"
       "  solan -e \"كود\"               تشغيل كود مباشر\n"
       "  solan --check ملف.سو         فحص الصياغة بدون تشغيل\n"
       "  solan fmt ملف.سو [-w]        تنسيق الكود (-w لكتابته في الملف)\n"
       "  solan --لون ملف.سو           عرض الكود ملوّنا في الطرفية (--html لتصدير صفحة)\n"
       "  solan --تتبع ملف.سو          طباعة كل سطر ينفَّذ\n"
       "  solan --تصحيح ملف.سو [--نقطة N]   مصحح تفاعلي خطوة بخطوة\n"
       "  solan install <مصدر>         تثبيت مكتبة: مجلد | رابط git[#وسم] | مستخدم/مستودع[@وسم]\n"
       "  solan list | remove <اسم>    عرض/إزالة المكتبات المثبتة\n"
       "  solan init | add <مصدر>      مشروع: إنشاء solan.json / إضافة اعتمادية (مع solan.lock)\n"
       "  solan install [--frozen]     تثبيت اعتماديات المشروع (القفل يثبّت الإصدارات والبصمات)\n"
       "  solan verify                 التحقق من سلامة solan_modules مقابل القفل\n"
       "  solan lsp                    خادم اللغة (LSP) عبر stdio للمحررات\n"
       "  --اسمح=قراءة,كتابة,شبكة,تنفيذ,بيئة,اصلي   --امنع=...   --اسمح-قراءة=/مسار   --اسمح-شبكة=مضيف\n"
       "  solan -v                     الإصدار");
}
