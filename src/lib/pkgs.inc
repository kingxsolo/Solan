static bool pk_dir_exists(const char *p) { sstat_t st; return sstat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static bool pk_probe_write(const char *dir) {
  char f[PATH_MAX + 64]; snprintf(f, sizeof f, "%s/.solan-probe-%ld", dir, (long)rand());
  FILE *fp = fopen(f, "wb"); if (!fp) return false;
  fclose(fp); remove(f); return true;
}
static bool pk_can_create(const char *p) {
  char d[PATH_MAX]; snprintf(d, sizeof d, "%s", p);
  for (int i = 0; i < 64; i++) {
    if (pk_dir_exists(d)) return os_can_write_dir(d) && pk_probe_write(d);
    char *sl = strrchr(d, '/'); if (!sl || sl == d) return false;
    *sl = 0;
  }
  return false;
}
static void pk_abs(const char *in, char *out, size_t sz) {
  char rp[PATH_MAX];
  if (os_is_abs(in)) { snprintf(out, sz, "%s", in); return; }
  if (realpath(in, rp)) { snprintf(out, sz, "%s", rp); return; }
  char cwd[PATH_MAX]; if (getcwd(cwd, sizeof cwd)) snprintf(out, sz, "%s/%s", cwd, in); else snprintf(out, sz, "%s", in);
}
static bool pk_home_dir(const char *project_root, char *out, size_t sz, const char **why) {
  const char *e = getenv("SOLAN_HOME");
  if (e && *e) { pk_abs(e, out, sz); *why = "SOLAN_HOME"; return true; }
  char uh[PATH_MAX];
#if SOLAN_WIN
  const char *la = getenv("LOCALAPPDATA");
  if (la && *la) { snprintf(out, sz, "%s/Solan", la); for (char *c = out; *c; c++) if (*c == '\\') *c = '/'; if (pk_can_create(out)) { *why = "LOCALAPPDATA"; return true; } }
#endif
  if (os_user_home(uh, sizeof uh)) { char ua[PATH_MAX]; pk_abs(uh, ua, sizeof ua); snprintf(out, sz, "%s/.solan", ua); if (pk_can_create(out)) { *why = "home"; return true; } }
  if (project_root && *project_root) { snprintf(out, sz, "%s/.solan", project_root); if (pk_can_create(out)) { *why = "project"; return true; } }
  char tag[64]; os_user_tag(tag, sizeof tag);
  snprintf(out, sz, "%s/solan-%s", os_tmpdir(), tag); *why = "tmp";
  return true;
}
static char g_pk_root[PATH_MAX];
static bool g_pk_init, g_pk_has, g_pk_tried_install;
static Value g_pk_man, g_pk_lock;

static Value pk_json_file(const char *path) {
  size_t n; char *s = read_file(path, &n); if (!s) return NIL;
  JP j = {s, s + n, false}; Value v = jval(&j, 0); jws(&j); free(s);
  if (j.err || j.p != j.end || !IS_T(v, O_MAP)) return NIL;
  pin(v.as.o); return v;
}
static bool pk_find_root(void) {
  if (g_pk_init) return g_pk_has;
  g_pk_init = true;
  for (int pass = 0; pass < 2 && !g_pk_has; pass++) {
    char d[PATH_MAX], rp[PATH_MAX];
    if (pass == 0) { if (!vm.script_dir || !*vm.script_dir) continue; snprintf(d, sizeof d, "%s", vm.script_dir); }
    else if (!getcwd(d, sizeof d)) continue;
    if (realpath(d, rp)) snprintf(d, sizeof d, "%s", rp);
    for (int up = 0; up < 40; up++) {
      char f[PATH_MAX + 16]; snprintf(f, sizeof f, "%s/solan.json", d);
      if (file_exists(f)) { snprintf(g_pk_root, sizeof g_pk_root, "%s", d); g_pk_has = true; break; }
      char *sl = strrchr(d, '/'); if (!sl || sl == d) break; *sl = 0;
    }
  }
  return g_pk_has;
}
static void pk_load(void) {
  char f[PATH_MAX + 16];
  snprintf(f, sizeof f, "%s/solan.json", g_pk_root); g_pk_man = pk_json_file(f);
  snprintf(f, sizeof f, "%s/solan.lock", g_pk_root); g_pk_lock = pk_json_file(f);
}
static bool pk_map_get(Value m, const char *key, Value *out) {
  if (!IS_T(m, O_MAP)) return false;
  return mget(&AS_MAP(m)->d, OBJV(normstr(key)), out);
}
static bool pk_deps_of(Value man, Value *out) {
  if (pk_map_get(man, "الاعتماديات", out) && IS_T(*out, O_MAP)) return true;
  if (pk_map_get(man, "dependencies", out) && IS_T(*out, O_MAP)) return true;
  return false;
}
static bool pk_is_dir(const char *p) { return pk_dir_exists(p); }
static bool pk_pkg_dir(const char *id, const char *localpath, char *out, size_t sz) {
  if (localpath && *localpath) {
    if (localpath[0] == '/') snprintf(out, sz, "%s", localpath); else snprintf(out, sz, "%s/%s", g_pk_root, localpath);
    return pk_is_dir(out);
  }
  char idp[600]; snprintf(idp, sizeof idp, "%s", id);
  for (char *c = idp; *c; c++) if (*c == '@') *c = '/';
  char bases[4][PATH_MAX]; int nb = 0; const char *why;
  pk_home_dir(g_pk_has ? g_pk_root : NULL, bases[nb++], PATH_MAX, &why);
  const char *e = getenv("SOLAN_HOME"); char uh[PATH_MAX];
  if (!(e && *e)) {
    if (os_user_home(uh, sizeof uh)) snprintf(bases[nb++], PATH_MAX, "%s/.solan", uh);
    if (g_pk_has) snprintf(bases[nb++], PATH_MAX, "%s/.solan", g_pk_root);
  }
  for (int i = 0; i < nb; i++) {
    snprintf(out, sz, "%s/store/%s", bases[i], idp);
    if (pk_is_dir(out)) return true;
  }
  return false;
}
static bool pk_try_file_in(const char *dir, const char *rel, char *out, size_t sz) {
  static const char *exts[] = {"", ".سو", ".an", ".solan", "/رئيسي.سو", "/رئيسي.an", "/main.an", "/main.solan", NULL};
  for (int e = 0; exts[e]; e++) {
    snprintf(out, sz, "%s/%s%s", dir, rel, exts[e]);
    if (file_exists(out)) return true;
  }
  return false;
}
static bool pk_entry(const char *dir, const char *sub, char *out, size_t sz) {
  if (sub && *sub) return pk_try_file_in(dir, sub, out, sz);
  char mf[PATH_MAX + 16]; snprintf(mf, sizeof mf, "%s/solan.json", dir);
  Value m = pk_json_file(mf), v;
  if (IS_T(m, O_MAP)) {
    const char *keys[] = {"المدخل", "entry", "main", NULL};
    for (int i = 0; keys[i]; i++) if (pk_map_get(m, keys[i], &v) && IS_T(v, O_STR)) {
      const char *r = AS_STR(v)->chars; if (strstr(r, "..") || r[0] == '/') continue;
      snprintf(out, sz, "%s/%s", dir, r); if (file_exists(out)) return true;
    }
  }
  return pk_try_file_in(dir, "رئيسي.سو", out, sz) || pk_try_file_in(dir, "main", out, sz) || pk_try_file_in(dir, "src/رئيسي.سو", out, sz);
}
static bool pk_ctx_of(const char *importer, char *ctxid, size_t sz) {
  ctxid[0] = 0;
  if (!importer || !*importer || !IS_T(g_pk_lock, O_MAP)) return false;
  char ip[PATH_MAX]; if (!realpath(importer, ip)) snprintf(ip, sizeof ip, "%s", importer);
  Value pk; if (!pk_map_get(g_pk_lock, "حزم", &pk) || !IS_T(pk, O_MAP)) return false;
  Dict *d = &AS_MAP(pk)->d; size_t best = 0;
  for (int i = 0; i < d->n; i++) {
    if (d->e[i].k.t == V_EMPTY || !IS_T(d->e[i].k, O_STR)) continue;
    const char *id = AS_STR(d->e[i].k)->chars; Value lp = NIL; char dir[PATH_MAX];
    if (IS_T(d->e[i].v, O_MAP)) pk_map_get(d->e[i].v, "مسار", &lp);
    if (!pk_pkg_dir(id, IS_T(lp, O_STR) ? AS_STR(lp)->chars : NULL, dir, sizeof dir)) continue;
    char rd[PATH_MAX]; if (realpath(dir, rd)) snprintf(dir, sizeof dir, "%s", rd);
    size_t L = strlen(dir);
    if (!strncmp(ip, dir, L) && ip[L] == '/' && L > best) { best = L; snprintf(ctxid, sz, "%s", id); }
  }
  return best > 0;
}
static bool pk_auto_install(void) {
  if (g_pk_tried_install) return false;
  g_pk_tried_install = true;
  if (!perm_has(PM_EXEC) || !perm_has(PM_NET)) return false;
  char self[PATH_MAX]; if (!os_self_exe(self, sizeof self)) snprintf(self, sizeof self, "solan");
  fflush(stdout); fprintf(stderr, "solan: تثبيت اعتماديات المشروع تلقائيا...\n"); fflush(stderr);
  char *argv[] = {self, (char *)"install", (char *)"--صامت", (char *)"--محلي", NULL};
  int code = os_run(argv, g_pk_root, NULL, NULL);
  if (code != 0) return false;
  pk_load(); return true;
}
static bool pkg_resolve(const char *name, Str *importer, char *out, size_t sz) {
  if (!name || !*name || name[0] == '.' || name[0] == '/' || !pk_find_root()) return false;
  static bool loaded; if (!loaded) { loaded = true; pk_load(); }
  char dep[300]; const char *sub = ""; const char *sl = strchr(name, '/');
  if (sl) { size_t L = (size_t)(sl - name); if (L >= sizeof dep) return false; memcpy(dep, name, L); dep[L] = 0; sub = sl + 1; }
  else { if (strlen(name) >= sizeof dep) return false; snprintf(dep, sizeof dep, "%s", name); }
  for (int attempt = 0; attempt < 2; attempt++) {
    char ctx[600]; bool inpkg = pk_ctx_of(importer ? importer->chars : NULL, ctx, sizeof ctx);
    Value deps = NIL, v, rootdeps = NIL; bool stale = false;
    if (IS_T(g_pk_lock, O_MAP)) {
      if (inpkg) {
        Value pk, ent; if (pk_map_get(g_pk_lock, "حزم", &pk) && pk_map_get(pk, ctx, &ent)) pk_map_get(ent, "اعتماديات", &deps);
      } else pk_map_get(g_pk_lock, "جذر", &deps);
    }
    if (!inpkg) {
      if (IS_T(g_pk_man, O_MAP)) pk_deps_of(g_pk_man, &rootdeps);
      Value want, have, specs;
      if (IS_T(rootdeps, O_MAP) && pk_map_get(rootdeps, dep, &want) && IS_T(want, O_STR)) {
        if (!IS_T(g_pk_lock, O_MAP) || !pk_map_get(g_pk_lock, "مواصفات", &specs) || !pk_map_get(specs, dep, &have) || !IS_T(have, O_STR) || strcmp(AS_STR(have)->chars, AS_STR(want)->chars) != 0) stale = true;
      }
    }
    if (!stale && IS_T(deps, O_MAP) && pk_map_get(deps, dep, &v) && IS_T(v, O_STR)) {
      const char *id = AS_STR(v)->chars; Value pk, ent, lp = NIL; char dir[PATH_MAX];
      if (pk_map_get(g_pk_lock, "حزم", &pk) && pk_map_get(pk, id, &ent)) pk_map_get(ent, "مسار", &lp);
      if (pk_pkg_dir(id, IS_T(lp, O_STR) ? AS_STR(lp)->chars : NULL, dir, sizeof dir) && pk_entry(dir, sub, out, sz)) return true;
      if (inpkg) return false;
    }
    if (inpkg) return false;
    bool declared = IS_T(rootdeps, O_MAP) && ({ Value t; pk_map_get(rootdeps, dep, &t); });
    if (attempt == 0 && declared && pk_auto_install()) continue;
    break;
  }
  return false;
}
