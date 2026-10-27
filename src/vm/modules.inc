static bool file_exists(const char *p) { sstat_t st; return sstat(p, &st) == 0 && S_ISREG(st.st_mode); }
static char *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  char *b = xalloc(n + 1); size_t r = fread(b, 1, n, f); b[r] = 0; fclose(f); if (len) *len = r; return b;
}
static bool resolve_module(const char *name, char *out, size_t sz) {
  static const char *exts[] = {"", ".سو", ".an", ".solan", "/رئيسي.سو", "/رئيسي.an", "/main.an", "/main.solan", ".so", ".dylib", ".dll", NULL};
  char dirs[10][PATH_MAX]; int nd = 0;
  bool rel = os_is_abs(name) || (name[0] == '.' && (name[1] == '/' || (name[1] == '.' && name[2] == '/')));
  if (os_is_abs(name)) dirs[nd++][0] = 0;
  else {
    if (vm.script_dir) snprintf(dirs[nd++], PATH_MAX, "%s/", vm.script_dir);
    snprintf(dirs[nd++], PATH_MAX, "./");
    if (!rel) {
      if (vm.script_dir) snprintf(dirs[nd++], PATH_MAX, "%s/solan_modules/", vm.script_dir);
      snprintf(dirs[nd++], PATH_MAX, "./solan_modules/");
      const char *sp = getenv("SOLAN_PATH");
      if (sp && *sp) { char tmp[PATH_MAX]; snprintf(tmp, sizeof tmp, "%s", sp); for (char *t = strtok(tmp, SOLAN_WIN ? ";" : ":"); t && nd < 8; t = strtok(NULL, ":")) snprintf(dirs[nd++], PATH_MAX, "%s/", t); }
      const char *h = getenv("HOME"); if (h && nd < 9) snprintf(dirs[nd++], PATH_MAX, "%s/.solan/modules/", h);
      const char *pf = getenv("PREFIX"); if (pf && nd < 10) snprintf(dirs[nd++], PATH_MAX, "%s/lib/solan/", pf);
    }
  }
  for (int d = 0; d < nd; d++)
    for (int e = 0; exts[e]; e++) {
      snprintf(out, sz, "%s%s%s", dirs[d], name, exts[e]);
      if (file_exists(out)) return true;
    }
  return false;
}
static bool load_native_module(const char *file, Value *out);
static bool pkg_resolve(const char *name, Str *importer, char *out, size_t sz);
static bool try_relative_to_importer(const char *name, Str *importer, char *out, size_t sz) {
  if (!importer || !(name[0] == '.' && (name[1] == '/' || (name[1] == '.' && name[2] == '/')))) return false;
  char ip[PATH_MAX]; if (!realpath(importer->chars, ip)) return false;
  char *sl = strrchr(ip, '/'); if (!sl) return false; *sl = 0;
  static const char *exts[] = {"", ".سو", ".an", ".solan", "/رئيسي.سو", "/رئيسي.an", "/main.an", "/main.solan", NULL};
  for (int e = 0; exts[e]; e++) { snprintf(out, sz, "%s/%s%s", ip, name, exts[e]); if (file_exists(out)) return true; }
  return false;
}
static bool do_import(Str *path, Value *out) {
  Value v; Str *key = normstr(path->chars);
  if (dget(&vm.natmods, OBJV(key), &v)) { *out = v; return true; }
  if (dget(&vm.stdlibd, OBJV(key), &v)) {
    int idx = (int)v.as.n; char ck[300]; snprintf(ck, sizeof ck, "<stdlib>:%s", STDLIB[idx].name); Str *cks = intern_c(ck);
    if (dget(&vm.modcache, OBJV(cks), &v)) { *out = v; return true; }
    char fname[300]; snprintf(fname, sizeof fname, "<مكتبة:%s>", STDLIB[idx].name);
    Fun *fn = compile(STDLIB[idx].src, fname, true); if (!fn) { vm.exc = OBJV(intern_c(P.errmsg)); return false; }
    Map *mod = new_map(); mod->module = true; dset(&vm.modcache, OBJV(cks), OBJV(mod));
    dset(&mod->d, KN("هو_الرئيسي"), BOOLV(false)); dset(&mod->d, KN("اسم_الملف"), mkstr(fname, (int)strlen(fname)));
    Closure *c = new_closure(fn, mod); push(OBJV(c));
    if (!call_sync(0)) { ddel(&vm.modcache, OBJV(cks), NULL); return false; }
    vm.sp--; *out = OBJV(mod); return true;
  }
  char full[PATH_MAX];
  Str *importer = vm.fcount > 0 ? vm.frames[vm.fcount - 1].clo->f->file : NULL;
  if (!try_relative_to_importer(path->chars, importer, full, sizeof full) && !pkg_resolve(path->chars, importer, full, sizeof full) && !resolve_module(path->chars, full, sizeof full)) { rt_error("المكتبة '%s' غير موجودة", path->chars); return false; }
  char rp[PATH_MAX]; if (!realpath(full, rp)) snprintf(rp, sizeof rp, "%s", full);
  Str *ck = intern_c(rp);
  if (dget(&vm.modcache, OBJV(ck), &v)) { *out = v; return true; }
  { size_t L = strlen(rp); if ((L > 3 && !strcmp(rp + L - 3, ".so")) || (L > 6 && !strcmp(rp + L - 6, ".dylib"))) {
      if (!perm_has(PM_NATIVE)) { rt_error("الصلاحية غير ممنوحة: اصلي (تحميل مكتبة أصلية)"); return false; }
      if (!load_native_module(rp, out)) return false; dset(&vm.modcache, OBJV(ck), *out); return true; } }
  char *src = read_file(rp, NULL); if (!src) { rt_error("تعذّرت قراءة المكتبة '%s'", rp); return false; }
  Fun *fn = compile(src, rp, true); free(src);
  if (!fn) { vm.exc = OBJV(intern_c(P.errmsg)); return false; }
  Map *mod = new_map(); mod->module = true; dset(&vm.modcache, OBJV(ck), OBJV(mod));
  dset(&mod->d, KN("هو_الرئيسي"), BOOLV(false)); dset(&mod->d, KN("اسم_الملف"), mkstr(rp, (int)strlen(rp)));
  Closure *c = new_closure(fn, mod); push(OBJV(c));
  if (!call_sync(0)) { ddel(&vm.modcache, OBJV(ck), NULL); return false; }
  vm.sp--; *out = OBJV(mod); return true;
}
