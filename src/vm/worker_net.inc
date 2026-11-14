// =====================================================================
//  worker_net.inc — دوال شبكة العمال المحلية (loopback TCP) على ويندوز
//  يُضمَّن بعد methods.inc وقبل natives_core.inc، قبل كل مستخدمي هذه
//  الدوال (regex.inc و worker_entry.inc).
//  تعتمد worker_value.inc (مُسلسل القيم) المضمَّن قبله.
// =====================================================================
#if !SOLAN_FORK
static bool w_freeze_file(const char *path, Value clo) {
  Buf b; buf_init(&b);
  bool ok = wv_freeze(clo, &b);
  if (ok) {
    // الصيغة المتفق عليها مع worker_main: [طول الإغلاق][تجميد الإغلاق][طول الوسائط=0]
    Buf all; buf_init(&all);
    wv_put32(&all, (uint32_t)b.n); buf_add(&all, buf_cstr(&b), b.n);
    wv_put32(&all, 0);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(buf_cstr(&all), 1, (size_t)all.n, f); fclose(f); } else ok = false;
    free(all.p);
  }
  free(b.p);
  return ok;
}
static int w_listen_local(int *outfd, unsigned short *outport) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
  sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK); sa.sin_port = 0;
  if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0 || listen(fd, 1) < 0) { close(fd); return -1; }
  socklen_t sl = sizeof sa; getsockname(fd, (struct sockaddr *)&sa, &sl);
  *outport = ntohs(sa.sin_port); *outfd = fd;
  return 0;
}
static int w_accept_local(int lfd, int tmo_ms) {
  struct pollfd p = {lfd, POLLIN, 0};
  int pr = poll(&p, 1, tmo_ms);
  if (pr <= 0) return -1;
  return accept(lfd, NULL, NULL);
}
static int w_dial_local(unsigned short port, int tmo_ms) {
  w_wsa(); // تأكد من تهيئة Winsock في العملية الفرعية
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
  sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK); sa.sin_port = htons(port);
  // سوكت غير حاجب + select — لا تعليق حتى لو كان المنفذ غرقاناً
  unsigned long nb = 1; ioctlsocket((SOCKET)fd, FIONBIO, &nb);
  int rc = connect(fd, (struct sockaddr *)&sa, sizeof sa);
  if (rc != 0) {
    int ce = WSAGetLastError();
    if (ce != WSAEWOULDBLOCK) { closesocket((SOCKET)fd); return -1; }
    fd_set ws; FD_ZERO(&ws); FD_SET((SOCKET)fd, &ws);
    struct timeval tv = {tmo_ms / 1000, (tmo_ms % 1000) * 1000};
    int sel = select((int)fd + 1, NULL, &ws, NULL, &tv);
    if (sel <= 0) { closesocket((SOCKET)fd); return -1; }
    int soerr = 0; socklen_t sl = sizeof soerr;
    getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&soerr, &sl);
    if (soerr != 0) { closesocket((SOCKET)fd); return -1; }
  }
  // أعد الحاجب
  unsigned long b = 0; ioctlsocket((SOCKET)fd, FIONBIO, &b);
  return fd;
}
static int w_spawn_argv(char **av, HANDLE *outph, DWORD *outpid) {
  size_t n = 0, cap = 0; char *cl = NULL;
  for (int i = 0; av[i]; i++) w_append_arg(&cl, &n, &cap, av[i]);
  if (!cl) return -1;
  wchar_t *wcl = w_utf16(cl); free(cl);
  STARTUPINFOW si; memset(&si, 0, sizeof si); si.cb = sizeof si;
  PROCESS_INFORMATION pi; memset(&pi, 0, sizeof pi);
  fflush(stdout); fflush(stderr);
  // وراثة المقابض القياسية حتى ترى رسائل الخطأ الفرعية stderr الأب عند تشخيصها
  // (لا تظهر في الاستخدام العادي بلا نافذة، لكنها تصل عند إعادة التوجيه)
  STARTUPINFOW si2 = si; si2.dwFlags |= STARTF_USESTDHANDLES;
  si2.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si2.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si2.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  BOOL ok = CreateProcessW(NULL, wcl, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si2, &pi);
  free(wcl);
  if (!ok) return -1;
  CloseHandle(pi.hThread);
  if (outph) *outph = pi.hProcess; else CloseHandle(pi.hProcess);
  if (outpid) *outpid = pi.dwProcessId;
  return 0;
}
static void w_tmpname(char *out, size_t sz, const char *tag) {
  static int seq = 0;
  snprintf(out, sz, "%s/solan_wk_%s_%ld_%d.tmp", os_tmpdir(), tag, (long)ex_now_ms(), seq++);
}
#define W_MAXCH 64
static HANDLE w_chproc[W_MAXCH]; static DWORD w_chpid[W_MAXCH]; static int w_nch;
static void w_track_proc(HANDLE h, DWORD pid) { if (w_nch < W_MAXCH) { w_chproc[w_nch] = h; w_chpid[w_nch] = pid; w_nch++; } }
static HANDLE w_proc_by_pid(DWORD pid) { for (int i = 0; i < w_nch; i++) if (w_chpid[i] == pid) return w_chproc[i]; return NULL; }
static void w_untrack_pid(DWORD pid) {
  for (int i = 0; i < w_nch; i++) if (w_chpid[i] == pid) {
    CloseHandle(w_chproc[i]);
    w_chproc[i] = w_chproc[w_nch - 1]; w_chpid[i] = w_chpid[w_nch - 1]; w_nch--;
    return;
  }
}
#endif // !SOLAN_FORK
