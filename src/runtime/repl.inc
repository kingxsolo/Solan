static char *get_line(const char *prompt) {
  if (isatty(0) && isatty(1)) return le_readline(prompt);
  fputs(prompt, stdout); fflush(stdout); char *line = NULL; size_t cap = 0; ssize_t n = getline(&line, &cap, stdin);
  if (n < 0) { free(line); return NULL; } while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0; return line;
}
static void repl(void) {
  Map *g = new_map(); pin((Obj *)g);
  printf("سولان %s — لغة برمجة عربية\nاكتب 'خروج' للخروج\n", SOLAN_VERSION);
  Buf acc; buf_init(&acc);
  for (;;) {
    char *line = get_line(acc.n ? "... " : (use_color(1) ? "\x1b[1;38;5;141m>>> \x1b[0m" : ">>> "));
    if (!line) break;
    if (isatty(0) && isatty(1) && !acc.n && *line && strncmp(line, "خروج", 8) && strncmp(line, "exit", 4)) le_history_add(line);
    buf_add(&acc, line, (int)strlen(line)); buf_addc(&acc, '\n'); free(line);
    if (!balanced(acc.p)) continue;
    char *t = acc.p; while (*t == ' ' || *t == '\n') t++;
    if (!strncmp(t, "خروج", 8) || !strncmp(t, "exit", 4)) break;
    if (*t == 0) { acc.n = 0; if (acc.p) acc.p[0] = 0; continue; }
    Buf ex; buf_init(&ex); buf_fmt(&ex, "ارجع (%s)", acc.p);
    Fun *fn = compile(ex.p, "<تفاعلي>", true); free(ex.p);
    if (!fn) fn = compile(acc.p, "<تفاعلي>", false);
    if (fn) {
      Closure *c = new_closure(fn, g); push(OBJV(c)); vm.exc = NIL;
      if (call_sync(0)) { Value r = pop(); if (!IS_NIL(r)) print_value_colored(r); }
      else { report_error(); reset_vm_state(); }
    }
    acc.n = 0; if (acc.p) acc.p[0] = 0;
  }
}
